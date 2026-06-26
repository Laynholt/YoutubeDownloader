#include "TranscriptionClient.h"

#include "BackendText.h"
#include "ProcessRunner.h"

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

bool MoveTranscriptFile(const std::filesystem::path& source, const std::filesystem::path& destination) {
    if (!IsRegularFile(source)) {
        return false;
    }

    std::error_code ec;
    std::filesystem::remove(destination, ec);
    ec.clear();
    std::filesystem::rename(source, destination, ec);
    if (!ec && IsRegularFile(destination)) {
        return true;
    }

    ec.clear();
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec || !IsRegularFile(destination)) {
        return false;
    }
    ec.clear();
    std::filesystem::remove(source, ec);
    return true;
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

bool ReplaceOriginalMedia(const std::filesystem::path& replacement, const std::filesystem::path& original) {
    if (!IsRegularFile(replacement)) {
        return false;
    }

    std::error_code ec;
    if (!IsRegularFile(original)) {
        std::filesystem::rename(replacement, original, ec);
        return !ec && IsRegularFile(original);
    }

    const BOOL replaced = ReplaceFileW(
        original.c_str(),
        replacement.c_str(),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (replaced) {
        return true;
    }

    std::filesystem::remove(original, ec);
    ec.clear();
    std::filesystem::rename(replacement, original, ec);
    return !ec && IsRegularFile(original);
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

std::filesystem::path TranscriptOutputBaseFor(const std::filesystem::path& mediaPath) {
    return mediaPath.parent_path() / mediaPath.stem();
}

TranscriptionPaths BuildTranscriptionPaths(
    const std::filesystem::path& mediaPath,
    const std::filesystem::path& tempDirectory,
    long long nonce
) {
    const std::wstring nonceText = std::to_wstring(nonce);
    TranscriptionPaths paths;
    paths.wavPath = tempDirectory / (L"audio-" + nonceText + L".wav");
    paths.whisperOutputBase = tempDirectory / (L"transcript-" + nonceText);
    paths.tempTextPath = PathWithExtension(paths.whisperOutputBase, L".txt");
    paths.tempSrtPath = PathWithExtension(paths.whisperOutputBase, L".srt");

    const std::filesystem::path finalBase = TranscriptOutputBaseFor(mediaPath);
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

std::vector<std::wstring> BuildVotHelperSubtitlesArguments(
    const TranscriptionRequest& request,
    const std::filesystem::path& outputSrtPath
) {
    return {
        L"subtitles",
        L"--url",
        request.youtubeUrl,
        L"--source-lang",
        SafeLanguageForArgument(request.language.empty() ? L"auto" : request.language, L"auto"),
        L"--target-lang",
        SafeLanguageForArgument(request.votTargetLanguage, L"ru"),
        L"--format",
        L"srt",
        L"--output",
        outputSrtPath.wstring(),
        L"--force"
    };
}

std::vector<std::wstring> BuildSubtitleTrackArguments(
    const std::filesystem::path& mediaPath,
    const std::filesystem::path& srtPath,
    const std::filesystem::path& outputVideoPath
) {
    return {
        L"-y",
        L"-i",
        mediaPath.wstring(),
        L"-i",
        srtPath.wstring(),
        L"-map",
        L"0",
        L"-map",
        L"1:0",
        L"-c",
        L"copy",
        L"-c:s",
        L"mov_text",
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
        L"copy",
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
        return Failed(L"Файл для распознавания не найден");
    }
    if (request.engine == TranscriptionEngine::Whisper) {
        if (!IsRegularFile(request.ffmpegExePath)) {
            return Failed(L"FFmpeg не найден");
        }
        if (!IsRegularFile(request.whisperExePath)) {
            return Failed(L"whisper-cli.exe не найден");
        }
        if (!IsRegularFile(request.whisperModelPath)) {
            return Failed(L"Модель Whisper не найдена");
        }
    } else {
        if (!IsRegularFile(request.votExePath)) {
            return Failed(L"vot-helper.exe не найден");
        }
        if (request.youtubeUrl.empty()) {
            return Failed(L"Ссылка на видео недоступна");
        }
        if (request.subtitleMode != SubtitleFfmpegMode::Off && !IsRegularFile(request.ffmpegExePath)) {
            return Failed(L"FFmpeg не найден");
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(request.tempDirectory, ec);
    if (ec) {
        return Failed(L"Не удалось создать папку для временного аудио");
    }

    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const TranscriptionPaths paths = BuildTranscriptionPaths(request.mediaPath, request.tempDirectory, ticks);

    bool textMoved = false;
    bool srtMoved = false;

    if (request.engine == TranscriptionEngine::Whisper) {
        EmitTranscriptionProgress(callbacks, 5.0, L"Извлечение аудио для распознавания");

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
            result.errorText = L"Отменено";
            return result;
        }
        if (extracted.exitCode != 0) {
            std::filesystem::remove(paths.wavPath, ec);
            return Failed(L"Не удалось извлечь аудио: " + ProcessErrorText(extracted, L"FFmpeg завершился с ошибкой"));
        }

        EmitTranscriptionProgress(callbacks, 20.0, L"Распознавание речи");

        ProcessRunOptions whisper;
        whisper.executable = request.whisperExePath;
        whisper.arguments = BuildWhisperArguments(request, paths.wavPath, paths.whisperOutputBase);
        whisper.timeoutMs = INFINITE;
        whisper.cancelEvent = cancelEvent;
        const auto handleWhisperLine = [&callbacks](const std::wstring& line) {
            const std::optional<double> progress = ParseWhisperProgressPercent(line);
            if (progress) {
                EmitTranscriptionProgress(callbacks, 20.0 + (*progress * 0.78), L"Распознавание речи");
            }
        };
        whisper.onStdoutLine = handleWhisperLine;
        whisper.onStderrLine = handleWhisperLine;

        const ProcessRunResult recognized = ProcessRunner::Run(whisper);
        std::filesystem::remove(paths.wavPath, ec);

        if (recognized.canceled) {
            TranscriptionResult result;
            result.canceled = true;
            result.errorText = L"Отменено";
            return result;
        }
        if (recognized.exitCode != 0) {
            return Failed(L"Whisper завершился с ошибкой: " + ProcessErrorText(recognized, L"неизвестная ошибка"));
        }

        textMoved = MoveTranscriptFile(paths.tempTextPath, paths.finalTextPath);
        srtMoved = MoveTranscriptFile(paths.tempSrtPath, paths.finalSrtPath);
        if (!textMoved && !srtMoved) {
            return Failed(L"Whisper не создал файл расшифровки");
        }
    } else {
        EmitTranscriptionProgress(callbacks, 10.0, L"Получение субтитров VOT");

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
            EmitTranscriptionProgress(callbacks, 35.0, L"Получение субтитров VOT");
        };
        vot.onStdoutLine = handleVotLine;
        vot.onStderrLine = handleVotLine;

        const ProcessRunResult subtitles = ProcessRunner::Run(vot);
        if (subtitles.canceled) {
            std::filesystem::remove(paths.tempSrtPath, ec);
            TranscriptionResult result;
            result.canceled = true;
            result.errorText = L"Отменено";
            return result;
        }
        if (subtitles.exitCode != 0) {
            std::filesystem::remove(paths.tempSrtPath, ec);
            return Failed(L"vot-helper.exe не получил субтитры: " + ProcessErrorText(subtitles, L"неизвестная ошибка"));
        }
        if (!IsRegularFile(paths.tempSrtPath)) {
            return Failed(L"vot-helper.exe не создал SRT-файл");
        }

        EmitTranscriptionProgress(callbacks, 70.0, L"Сохранение TXT и SRT");
        const std::wstring plainText = PlainTextFromSrtContent(ReadUtf8TextFile(paths.tempSrtPath));
        if (!WriteUtf8TextFile(paths.tempTextPath, plainText)) {
            std::filesystem::remove(paths.tempSrtPath, ec);
            return Failed(L"Не удалось создать TXT из субтитров VOT");
        }

        textMoved = MoveTranscriptFile(paths.tempTextPath, paths.finalTextPath);
        srtMoved = MoveTranscriptFile(paths.tempSrtPath, paths.finalSrtPath);
        if (!textMoved && !srtMoved) {
            return Failed(L"Не удалось сохранить субтитры VOT");
        }
    }

    std::filesystem::path embeddedVideoPath;
    if (request.subtitleMode != SubtitleFfmpegMode::Off && srtMoved) {
        EmitTranscriptionProgress(callbacks, 90.0, L"Встраивание субтитров в видео");

        std::filesystem::remove(paths.tempVideoPath, ec);
        ec.clear();

        ProcessRunOptions embedFfmpeg;
        embedFfmpeg.executable = request.ffmpegExePath;
        embedFfmpeg.arguments = request.subtitleMode == SubtitleFfmpegMode::BurnIn
            ? BuildSubtitleBurnInArguments(request.mediaPath, paths.finalSrtPath, paths.tempVideoPath)
            : BuildSubtitleTrackArguments(request.mediaPath, paths.finalSrtPath, paths.tempVideoPath);
        embedFfmpeg.timeoutMs = INFINITE;
        embedFfmpeg.cancelEvent = cancelEvent;

        const ProcessRunResult embedded = ProcessRunner::Run(embedFfmpeg);
        if (embedded.canceled) {
            TranscriptionResult result;
            result.canceled = true;
            result.errorText = L"Отменено";
            std::filesystem::remove(paths.tempVideoPath, ec);
            return result;
        }
        if (embedded.exitCode != 0) {
            std::filesystem::remove(paths.tempVideoPath, ec);
            return Failed(L"FFmpeg не встроил субтитры в видео: " + ProcessErrorText(embedded, L"неизвестная ошибка"));
        }
        if (!ReplaceOriginalMedia(paths.tempVideoPath, paths.finalVideoPath)) {
            std::filesystem::remove(paths.tempVideoPath, ec);
            return Failed(L"Не удалось заменить исходное видео версией с субтитрами");
        }
        embeddedVideoPath = paths.finalVideoPath;
    }

    EmitTranscriptionProgress(callbacks, 99.0, L"Сохранение расшифровки");

    TranscriptionResult result;
    result.success = true;
    result.textPath = textMoved ? paths.finalTextPath : std::filesystem::path{};
    result.srtPath = srtMoved ? paths.finalSrtPath : std::filesystem::path{};
    result.videoPath = embeddedVideoPath;
    return result;
}
