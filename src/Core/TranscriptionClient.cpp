#include "TranscriptionClient.h"

#include "BackendText.h"
#include "PostProcessingFileOps.h"
#include "ProcessRunner.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace {

bool IsRegularFile(const std::filesystem::path& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::is_regular_file(path, ec);
}

std::filesystem::path PathWithExtension(std::filesystem::path base, const wchar_t* extension) {
    base += extension;
    return base;
}

std::wstring TrimWhitespace(const std::wstring& text) {
    size_t begin = 0;
    while (begin < text.size() && std::iswspace(text[begin])) {
        ++begin;
    }

    size_t end = text.size();
    while (end > begin && std::iswspace(text[end - 1])) {
        --end;
    }

    return text.substr(begin, end - begin);
}

std::wstring LastNonEmptyLine(const std::wstring& text) {
    size_t end = text.size();
    while (end > 0) {
        const size_t newline = text.rfind(L'\n', end - 1);
        const size_t begin = newline == std::wstring::npos ? 0 : newline + 1;
        const std::wstring line = TrimWhitespace(text.substr(begin, end - begin));
        if (!line.empty()) {
            return line;
        }
        if (newline == std::wstring::npos) {
            break;
        }
        end = newline;
    }
    return {};
}

TranscriptionResult Failed(std::wstring errorText) {
    TranscriptionResult result;
    result.errorText = std::move(errorText);
    return result;
}

std::wstring ProcessErrorText(const ProcessRunResult& result, const std::wstring& fallback) {
    return BuildProcessErrorSummary(result.stderrText, result.stdoutText, fallback);
}

std::wstring SafeLanguageForArgument(const std::wstring& language, const std::wstring& fallback) {
    std::wstring out;
    out.reserve(language.size());
    for (wchar_t ch : language) {
        if ((ch >= L'0' && ch <= L'9') ||
            (ch >= L'a' && ch <= L'z') ||
            (ch >= L'A' && ch <= L'Z') ||
            ch == L'-' ||
            ch == L'_') {
            out.push_back(static_cast<wchar_t>(std::towlower(ch)));
        }
    }
    return out.empty() ? fallback : out;
}

std::wstring TranscriptionEngineSuffix(TranscriptionEngine engine) {
    return engine == TranscriptionEngine::Vot ? L"vot" : L"whisper";
}

std::wstring SubtitleLanguageTitle(const std::wstring& language) {
    const std::wstring lang = SafeLanguageForArgument(language, L"auto");
    if (lang == L"ru" || lang == L"rus") {
        return L"Russian";
    }
    if (lang == L"en" || lang == L"eng") {
        return L"English";
    }
    if (lang == L"de" || lang == L"ger" || lang == L"deu") {
        return L"German";
    }
    if (lang == L"auto") {
        return L"Auto";
    }
    return lang;
}

std::wstring SubtitleLanguageCode(const std::wstring& language) {
    const std::wstring lang = SafeLanguageForArgument(language, L"und");
    if (lang == L"ru" || lang == L"rus") {
        return L"rus";
    }
    if (lang == L"en" || lang == L"eng") {
        return L"eng";
    }
    if (lang == L"de" || lang == L"ger" || lang == L"deu") {
        return L"deu";
    }
    if (lang == L"auto") {
        return L"und";
    }
    return lang;
}

std::wstring SubtitleTrackTitle(TranscriptionEngine engine, const std::wstring& language) {
    return (engine == TranscriptionEngine::Vot ? L"VOT " : L"Whisper ") + SubtitleLanguageTitle(language);
}

std::wstring SuggestedVotSubtitleSourceLanguage(
    const ProcessRunResult& result,
    const std::wstring& targetLanguage
) {
    const std::wstring diagnostic = result.stdoutText + L"\n" + result.stderrText;
    if (diagnostic.find(L"Subtitle track selection is ambiguous") == std::wstring::npos) {
        return {};
    }

    const std::wstring safeTarget = SafeLanguageForArgument(targetLanguage, L"ru");
    std::wstring firstLanguage;

    try {
        const std::wstring& jsonText = result.stdoutText;
        const size_t jsonBegin = jsonText.find(L'{');
        const size_t jsonEnd = jsonText.rfind(L'}');
        if (jsonBegin == std::wstring::npos || jsonEnd == std::wstring::npos || jsonEnd < jsonBegin) {
            return {};
        }
        const nlohmann::json json = nlohmann::json::parse(
            WideToUtf8(jsonText.substr(jsonBegin, jsonEnd - jsonBegin + 1)),
            nullptr,
            false
        );
        if (json.is_discarded()) {
            return {};
        }
        const auto tracks = json.find("error") != json.end()
            ? json["error"].value("details", nlohmann::json::object()).value("availableTracks", nlohmann::json::array())
            : nlohmann::json::array();
        if (!tracks.is_array()) {
            return {};
        }
        for (const nlohmann::json& track : tracks) {
            if (!track.is_object()) {
                continue;
            }
            const std::wstring language = SafeLanguageForArgument(
                Utf8ToWide(track.value("language", std::string{})),
                L""
            );
            if (!language.empty() && firstLanguage.empty()) {
                firstLanguage = language;
            }
            const std::wstring translated = SafeLanguageForArgument(
                Utf8ToWide(track.value("translatedLanguage", std::string{})),
                L""
            );
            if (!language.empty() && translated == safeTarget) {
                return language;
            }
        }
    } catch (...) {
        return {};
    }

    return firstLanguage;
}

std::wstring SubtitleCodecForOutput(const std::filesystem::path& outputVideoPath) {
    std::wstring extension = outputVideoPath.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    if (extension == L".mkv") {
        return L"srt";
    }
    if (extension == L".webm") {
        return L"webvtt";
    }
    return L"mov_text";
}

std::wstring AudioCodecForSubtitleBurnInOutput(const std::filesystem::path& outputVideoPath) {
    std::wstring extension = outputVideoPath.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return extension == L".webm" ? L"libopus" : L"copy";
}

bool WriteUtf8TextFile(const std::filesystem::path& path, const std::wstring& text) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return false;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    const std::string utf8 = WideToUtf8(text);
    out.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    return static_cast<bool>(out);
}

std::wstring ReadUtf8TextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream stream;
    stream << in.rdbuf();
    return Utf8ToWide(stream.str());
}

std::wstring EscapeSubtitleFilterPath(std::wstring value) {
    std::replace(value.begin(), value.end(), L'\\', L'/');
    std::wstring escaped;
    escaped.reserve(value.size() + 8);
    for (wchar_t ch : value) {
        if (ch == L':' || ch == L'\'' || ch == L',' || ch == L'[' || ch == L']') {
            escaped.push_back(L'\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

void EmitTranscriptionProgress(
    const TranscriptionCallbacks& callbacks,
    double percent,
    const std::wstring& status
) {
    if (callbacks.onProgress) {
        callbacks.onProgress(std::clamp(percent, 0.0, 100.0), status);
    }
    if (callbacks.onStatus) {
        callbacks.onStatus(status);
    }
}

} // namespace

std::filesystem::path TranscriptOutputBaseFor(
    const std::filesystem::path& mediaPath,
    TranscriptionEngine engine,
    const std::wstring& language
) {
    const std::wstring suffix =
        L"." + TranscriptionEngineSuffix(engine) + L"." + SafeLanguageForArgument(language, L"auto");
    return mediaPath.parent_path() / (mediaPath.stem().wstring() + suffix);
}

TranscriptionPaths BuildTranscriptionPaths(
    const std::filesystem::path& mediaPath,
    const std::filesystem::path& tempDirectory,
    long long nonce,
    TranscriptionEngine engine,
    const std::wstring& language
) {
    const std::wstring nonceText = std::to_wstring(nonce);
    TranscriptionPaths paths;
    paths.wavPath = tempDirectory / (L"audio-" + nonceText + L".wav");
    paths.whisperOutputBase = tempDirectory / (L"transcript-" + nonceText);
    paths.tempTextPath = PathWithExtension(paths.whisperOutputBase, L".txt");
    paths.tempSrtPath = PathWithExtension(paths.whisperOutputBase, L".srt");

    const std::filesystem::path finalBase = TranscriptOutputBaseFor(mediaPath, engine, language);
    paths.finalTextPath = PathWithExtension(finalBase, L".txt");
    paths.finalSrtPath = PathWithExtension(finalBase, L".srt");
    paths.tempVideoPath = tempDirectory / (L"transcript-" + nonceText + mediaPath.extension().wstring());
    if (paths.tempVideoPath.extension().empty()) {
        paths.tempVideoPath += L".mp4";
    }
    paths.finalVideoPath = mediaPath;
    return paths;
}

std::vector<std::wstring> BuildFfmpegAudioExtractionArguments(
    const std::filesystem::path& mediaPath,
    const std::filesystem::path& wavPath
) {
    return {
        L"-y",
        L"-i",
        mediaPath.wstring(),
        L"-vn",
        L"-ac",
        L"1",
        L"-ar",
        L"16000",
        L"-c:a",
        L"pcm_s16le",
        wavPath.wstring()
    };
}

std::vector<std::wstring> BuildWhisperArguments(
    const TranscriptionRequest& request,
    const std::filesystem::path& wavPath,
    const std::filesystem::path& outputBase
) {
    std::vector<std::wstring> args = {
        L"-m",
        request.whisperModelPath.wstring(),
        L"-f",
        wavPath.wstring(),
        L"-otxt",
        L"-osrt",
        L"-of",
        outputBase.wstring()
    };

    if (!request.language.empty()) {
        args.push_back(L"-l");
        args.push_back(request.language);
    }
    return args;
}

std::vector<std::wstring> BuildVotHelperSubtitlesArgumentsForSource(
    const TranscriptionRequest& request,
    const std::filesystem::path& outputSrtPath,
    const std::wstring& sourceLanguage
) {
    return {
        L"subtitles",
        L"--url",
        request.youtubeUrl,
        L"--source-lang",
        SafeLanguageForArgument(sourceLanguage.empty() ? L"auto" : sourceLanguage, L"auto"),
        L"--target-lang",
        SafeLanguageForArgument(request.votTargetLanguage, L"ru"),
        L"--format",
        L"srt",
        L"--output",
        outputSrtPath.wstring(),
        L"--force"
    };
}

std::vector<std::wstring> BuildVotHelperSubtitlesArguments(
    const TranscriptionRequest& request,
    const std::filesystem::path& outputSrtPath
) {
    return BuildVotHelperSubtitlesArgumentsForSource(
        request,
        outputSrtPath,
        request.language.empty() ? L"auto" : request.language
    );
}

std::vector<std::wstring> BuildSubtitleTrackArguments(
    const std::filesystem::path& mediaPath,
    const std::filesystem::path& srtPath,
    const std::filesystem::path& outputVideoPath,
    TranscriptionEngine engine,
    const std::wstring& language
) {
    return {
        L"-y",
        L"-i",
        mediaPath.wstring(),
        L"-i",
        srtPath.wstring(),
        L"-map",
        L"0:v?",
        L"-map",
        L"0:a?",
        L"-map",
        L"1:0",
        L"-c",
        L"copy",
        L"-c:s",
        SubtitleCodecForOutput(outputVideoPath),
        L"-metadata:s:s:0",
        L"title=" + SubtitleTrackTitle(engine, language),
        L"-metadata:s:s:0",
        L"language=" + SubtitleLanguageCode(language),
        outputVideoPath.wstring()
    };
}

std::vector<std::wstring> BuildSubtitleBurnInArguments(
    const std::filesystem::path& mediaPath,
    const std::filesystem::path& srtPath,
    const std::filesystem::path& outputVideoPath
) {
    return {
        L"-y",
        L"-i",
        mediaPath.wstring(),
        L"-vf",
        L"subtitles='" + EscapeSubtitleFilterPath(srtPath.wstring()) + L"'",
        L"-c:a",
        AudioCodecForSubtitleBurnInOutput(outputVideoPath),
        outputVideoPath.wstring()
    };
}

std::optional<double> ParseWhisperProgressPercent(const std::wstring& line) {
    const size_t percentPos = line.find(L'%');
    if (percentPos == std::wstring::npos) {
        return std::nullopt;
    }

    size_t end = percentPos;
    while (end > 0 && std::iswspace(line[end - 1])) {
        --end;
    }

    size_t begin = end;
    while (begin > 0) {
        const wchar_t ch = line[begin - 1];
        if (!std::iswdigit(ch) && ch != L'.' && ch != L',') {
            break;
        }
        --begin;
    }
    if (begin == end) {
        return std::nullopt;
    }

    std::wstring number = line.substr(begin, end - begin);
    std::replace(number.begin(), number.end(), L',', L'.');
    try {
        return std::clamp(std::stod(number), 0.0, 100.0);
    } catch (...) {
        return std::nullopt;
    }
}

std::wstring PlainTextFromSrtContent(const std::wstring& srtContent) {
    std::wistringstream stream(srtContent);
    std::wstring line;
    std::wstring text;
    bool atCueStart = true;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        if (!line.empty() && line.front() == L'\xfeff') {
            line.erase(line.begin());
        }
        const std::wstring trimmed = TrimWhitespace(line);
        if (trimmed.empty()) {
            atCueStart = true;
            continue;
        }
        if (trimmed.find(L"-->") != std::wstring::npos) {
            atCueStart = false;
            continue;
        }
        const bool cueNumber = std::all_of(trimmed.begin(), trimmed.end(), [](wchar_t ch) {
            return std::iswdigit(ch) != 0;
        });
        if (atCueStart && cueNumber) {
            atCueStart = false;
            continue;
        }
        if (!text.empty()) {
            text += L"\n";
        }
        text += trimmed;
        atCueStart = false;
    }
    return text;
}

std::wstring BuildProcessErrorSummary(
    const std::wstring& stderrText,
    const std::wstring& stdoutText,
    const std::wstring& fallback
) {
    std::wstring summary = LastNonEmptyLine(stderrText);
    if (!summary.empty()) {
        return summary;
    }

    summary = LastNonEmptyLine(stdoutText);
    if (!summary.empty()) {
        return summary;
    }

    return fallback;
}

TranscriptionResult TranscriptionClient::Transcribe(
    const TranscriptionRequest& request,
    const TranscriptionCallbacks& callbacks,
    HANDLE cancelEvent
) {
    if (!IsRegularFile(request.mediaPath)) {
        return Failed(L"transcription.file_for_recognition_not_found");
    }
    if (request.engine == TranscriptionEngine::Whisper) {
        if (!IsRegularFile(request.ffmpegExePath)) {
            return Failed(L"dialog.ffmpeg_not_found");
        }
        if (!IsRegularFile(request.whisperExePath)) {
            return Failed(L"transcription.whisper_cli_exe_not_found");
        }
        if (!IsRegularFile(request.whisperModelPath)) {
            return Failed(L"dialog.whisper_model_not_found");
        }
    } else {
        if (!IsRegularFile(request.votExePath)) {
            return Failed(L"transcription.vot_helper_exe_not_found");
        }
        if (request.youtubeUrl.empty()) {
            return Failed(L"transcription.video_url_is_unavailable");
        }
        if (request.subtitleMode != SubtitleFfmpegMode::Off && !IsRegularFile(request.ffmpegExePath)) {
            return Failed(L"dialog.ffmpeg_not_found");
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(request.tempDirectory, ec);
    if (ec) {
        return Failed(L"transcription.failed_to_create_the_temporary_audio_folder");
    }

    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::wstring outputLanguage = request.engine == TranscriptionEngine::Vot
        ? request.votTargetLanguage
        : request.language;
    const TranscriptionPaths paths = BuildTranscriptionPaths(
        request.mediaPath,
        request.tempDirectory,
        ticks,
        request.engine,
        outputLanguage
    );

    bool textMoved = false;
    bool srtMoved = false;

    if (request.engine == TranscriptionEngine::Whisper) {
        EmitTranscriptionProgress(callbacks, 5.0, L"transcription.extracting_audio_for_recognition");

        ProcessRunOptions ffmpeg;
        ffmpeg.executable = request.ffmpegExePath;
        ffmpeg.arguments = BuildFfmpegAudioExtractionArguments(request.mediaPath, paths.wavPath);
        ffmpeg.timeoutMs = INFINITE;
        ffmpeg.cancelEvent = cancelEvent;

        const ProcessRunResult extracted = ProcessRunner::Run(ffmpeg);
        if (extracted.canceled) {
            std::filesystem::remove(paths.wavPath, ec);
            TranscriptionResult result;
            result.canceled = true;
            result.errorText = L"app.canceled";
            return result;
        }
        if (extracted.exitCode != 0) {
            std::filesystem::remove(paths.wavPath, ec);
            return Failed(L"transcription.failed_to_extract_audio" + ProcessErrorText(extracted, L"transcription.ffmpeg_exited_with_an_error"));
        }

        EmitTranscriptionProgress(callbacks, 20.0, L"transcription.recognizing_speech");

        ProcessRunOptions whisper;
        whisper.executable = request.whisperExePath;
        whisper.arguments = BuildWhisperArguments(request, paths.wavPath, paths.whisperOutputBase);
        whisper.timeoutMs = INFINITE;
        whisper.cancelEvent = cancelEvent;
        const auto handleWhisperLine = [&callbacks](const std::wstring& line) {
            const std::optional<double> progress = ParseWhisperProgressPercent(line);
            if (progress) {
                EmitTranscriptionProgress(callbacks, 20.0 + (*progress * 0.78), L"transcription.recognizing_speech");
            }
        };
        whisper.onStdoutLine = handleWhisperLine;
        whisper.onStderrLine = handleWhisperLine;

        const ProcessRunResult recognized = ProcessRunner::Run(whisper);
        std::filesystem::remove(paths.wavPath, ec);

        if (recognized.canceled) {
            TranscriptionResult result;
            result.canceled = true;
            result.errorText = L"app.canceled";
            return result;
        }
        if (recognized.exitCode != 0) {
            return Failed(L"transcription.whisper_exited_with_an_error" + ProcessErrorText(recognized, L"transcription.text"));
        }

        textMoved = CommitPostProcessingSidecarFile(paths.tempTextPath, paths.finalTextPath);
        srtMoved = CommitPostProcessingSidecarFile(paths.tempSrtPath, paths.finalSrtPath);
        if (!textMoved || !srtMoved) {
            return Failed(L"transcription.whisper_did_not_save_txt_and_srt_transcript_files");
        }
    } else {
        EmitTranscriptionProgress(callbacks, 10.0, L"transcription.getting_vot_subtitles");

        std::filesystem::remove(paths.tempSrtPath, ec);
        ec.clear();
        std::filesystem::remove(paths.tempTextPath, ec);
        ec.clear();

        ProcessRunOptions vot;
        vot.executable = request.votExePath;
        vot.arguments = BuildVotHelperSubtitlesArguments(request, paths.tempSrtPath);
        vot.timeoutMs = INFINITE;
        vot.cancelEvent = cancelEvent;
        const auto handleVotLine = [&callbacks](const std::wstring&) {
            EmitTranscriptionProgress(callbacks, 35.0, L"transcription.getting_vot_subtitles");
        };
        vot.onStdoutLine = handleVotLine;
        vot.onStderrLine = handleVotLine;

        ProcessRunResult subtitles = ProcessRunner::Run(vot);
        const std::wstring requestedSourceLanguage =
            SafeLanguageForArgument(request.language.empty() ? L"auto" : request.language, L"auto");
        if (!subtitles.canceled && subtitles.exitCode != 0 && requestedSourceLanguage == L"auto") {
            const std::wstring retrySourceLanguage =
                SuggestedVotSubtitleSourceLanguage(subtitles, request.votTargetLanguage);
            if (!retrySourceLanguage.empty() && retrySourceLanguage != L"auto") {
                EmitTranscriptionProgress(callbacks, 45.0, L"transcription.refining_vot_subtitle_language");
                std::filesystem::remove(paths.tempSrtPath, ec);
                ec.clear();
                vot.arguments = BuildVotHelperSubtitlesArgumentsForSource(
                    request,
                    paths.tempSrtPath,
                    retrySourceLanguage
                );
                subtitles = ProcessRunner::Run(vot);
            }
        }
        if (subtitles.canceled) {
            std::filesystem::remove(paths.tempSrtPath, ec);
            TranscriptionResult result;
            result.canceled = true;
            result.errorText = L"app.canceled";
            return result;
        }
        if (subtitles.exitCode != 0) {
            std::filesystem::remove(paths.tempSrtPath, ec);
            return Failed(L"transcription.vot_helper_exe_did_not_get_subtitles" + ProcessErrorText(subtitles, L"transcription.text"));
        }
        if (!IsRegularFile(paths.tempSrtPath)) {
            return Failed(L"transcription.vot_helper_exe_did_not_create_an_srt_file");
        }

        EmitTranscriptionProgress(callbacks, 70.0, L"transcription.saving_txt_and_srt");
        const std::wstring plainText = PlainTextFromSrtContent(ReadUtf8TextFile(paths.tempSrtPath));
        if (!WriteUtf8TextFile(paths.tempTextPath, plainText)) {
            std::filesystem::remove(paths.tempSrtPath, ec);
            return Failed(L"transcription.failed_to_create_txt_from_vot_subtitles");
        }

        textMoved = CommitPostProcessingSidecarFile(paths.tempTextPath, paths.finalTextPath);
        srtMoved = CommitPostProcessingSidecarFile(paths.tempSrtPath, paths.finalSrtPath);
        if (!textMoved || !srtMoved) {
            return Failed(L"transcription.failed_to_save_vot_txt_and_srt_files");
        }
    }

    std::filesystem::path embeddedVideoPath;
    if (request.subtitleMode != SubtitleFfmpegMode::Off && srtMoved) {
        EmitTranscriptionProgress(callbacks, 90.0, L"transcription.embedding_subtitles_into_video");

        std::filesystem::remove(paths.tempVideoPath, ec);
        ec.clear();

        ProcessRunOptions embedFfmpeg;
        embedFfmpeg.executable = request.ffmpegExePath;
        embedFfmpeg.arguments = request.subtitleMode == SubtitleFfmpegMode::BurnIn
            ? BuildSubtitleBurnInArguments(request.mediaPath, paths.finalSrtPath, paths.tempVideoPath)
            : BuildSubtitleTrackArguments(
                request.mediaPath,
                paths.finalSrtPath,
                paths.tempVideoPath,
                request.engine,
                outputLanguage
            );
        embedFfmpeg.timeoutMs = INFINITE;
        embedFfmpeg.cancelEvent = cancelEvent;

        const ProcessRunResult embedded = ProcessRunner::Run(embedFfmpeg);
        if (embedded.canceled) {
            TranscriptionResult result;
            result.canceled = true;
            result.errorText = L"app.canceled";
            std::filesystem::remove(paths.tempVideoPath, ec);
            return result;
        }
        if (embedded.exitCode != 0) {
            std::filesystem::remove(paths.tempVideoPath, ec);
            return Failed(L"transcription.ffmpeg_did_not_embed_subtitles_into_video" + ProcessErrorText(embedded, L"transcription.text"));
        }
        if (!ReplaceOriginalMediaWithPostProcessedFile(paths.tempVideoPath, paths.finalVideoPath)) {
            std::filesystem::remove(paths.tempVideoPath, ec);
            return Failed(L"transcription.failed_to_replace_the_source_video_with_the_subtitled_ve");
        }
        embeddedVideoPath = paths.finalVideoPath;
    }

    EmitTranscriptionProgress(callbacks, 99.0, L"transcription.saving_transcript");

    TranscriptionResult result;
    result.success = true;
    result.textPath = textMoved ? paths.finalTextPath : std::filesystem::path{};
    result.srtPath = srtMoved ? paths.finalSrtPath : std::filesystem::path{};
    result.videoPath = embeddedVideoPath;
    return result;
}
