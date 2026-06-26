#include "VoiceOverTranslationClient.h"

#include "BackendText.h"
#include "FileOperations.h"
#include "ProcessRunner.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace {

bool IsRegularFile(const std::filesystem::path& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::is_regular_file(path, ec);
}

std::wstring LowerCopy(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

std::wstring SafeLanguageForFile(std::wstring language) {
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
    return out.empty() ? L"ru" : out;
}

std::wstring NormalizedMode(const std::wstring& mode) {
    return mode == L"mixed" ? L"mixed" : L"separate";
}

std::wstring NormalizedSubtitlesFormat(const std::wstring& format) {
    const std::wstring lower = LowerCopy(format);
    if (lower == L"vtt" || lower == L"json") {
        return lower;
    }
    return L"srt";
}

std::wstring VoiceOverSuffix(const std::wstring& language, const std::wstring& mode) {
    const std::wstring lang = SafeLanguageForFile(language);
    return NormalizedMode(mode) == L"mixed"
        ? L".vot-mixed." + lang
        : L".vot." + lang;
}

std::wstring OutputVideoExtensionFor(const std::filesystem::path& mediaPath) {
    return LowerCopy(mediaPath.extension().wstring()) == L".mp4" ? L".mp4" : L".mkv";
}

std::filesystem::path PathWithSuffix(
    const std::filesystem::path& parent,
    const std::wstring& stem,
    const std::wstring& suffix,
    const std::wstring& extension
) {
    return parent / (stem + suffix + extension);
}

std::wstring LanguageMetadataCode(const std::wstring& language) {
    const std::wstring lang = SafeLanguageForFile(language);
    if (lang == L"ru" || lang == L"rus") {
        return L"rus";
    }
    if (lang == L"en" || lang == L"eng") {
        return L"eng";
    }
    return lang;
}

std::wstring LanguageTitle(const std::wstring& language) {
    const std::wstring lang = SafeLanguageForFile(language);
    if (lang == L"ru" || lang == L"rus") {
        return L"Russian";
    }
    if (lang == L"en" || lang == L"eng") {
        return L"English";
    }
    return lang;
}

std::wstring VolumeValue(int originalVolumePercent) {
    const double value = static_cast<double>(std::clamp(originalVolumePercent, 0, 100)) / 100.0;
    std::wostringstream stream;
    stream << std::fixed << std::setprecision(2) << value;
    std::wstring text = stream.str();
    while (text.size() > 1 && text.back() == L'0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == L'.') {
        text.pop_back();
    }
    return text.empty() ? L"0" : text;
}

std::wstring ProcessErrorSummary(const ProcessRunResult& result, const std::wstring& fallback) {
    std::wstring summary = LastNonEmptyLine(result.stderrText);
    if (!summary.empty()) {
        return summary;
    }
    summary = LastNonEmptyLine(result.stdoutText);
    if (!summary.empty()) {
        return summary;
    }
    return fallback;
}

VoiceOverTranslationResult Failed(std::wstring errorText) {
    VoiceOverTranslationResult result;
    result.errorText = std::move(errorText);
    return result;
}

VotSubtitlesResult FailedSubtitles(std::wstring errorText) {
    VotSubtitlesResult result;
    result.errorText = std::move(errorText);
    return result;
}

VoiceOverTranslationResult Canceled(std::vector<std::filesystem::path> cleanupPaths) {
    std::error_code ec;
    for (const std::filesystem::path& path : cleanupPaths) {
        std::filesystem::remove(path, ec);
        ec.clear();
    }

    VoiceOverTranslationResult result;
    result.canceled = true;
    result.errorText = L"Отменено";
    return result;
}

VotSubtitlesResult CanceledSubtitles(std::vector<std::filesystem::path> cleanupPaths) {
    std::error_code ec;
    for (const std::filesystem::path& path : cleanupPaths) {
        std::filesystem::remove(path, ec);
        ec.clear();
    }

    VotSubtitlesResult result;
    result.canceled = true;
    result.errorText = L"Canceled";
    return result;
}

void EmitVoiceOverProgress(
    const VoiceOverTranslationCallbacks& callbacks,
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

VoiceOverTranslationPaths BuildVoiceOverPaths(
    const std::filesystem::path& mediaPath,
    const std::filesystem::path& tempDirectory,
    const std::wstring& language,
    const std::wstring& mode
) {
    const std::wstring stem = mediaPath.stem().wstring();
    const std::wstring audioSuffix = VoiceOverSuffix(language, L"separate");
    const std::wstring videoSuffix = VoiceOverSuffix(language, mode);

    VoiceOverTranslationPaths paths;
    paths.tempAudioPath = PathWithSuffix(tempDirectory, stem, audioSuffix, L".mp3");
    paths.finalAudioPath = PathWithSuffix(mediaPath.parent_path(), stem, audioSuffix, L".mp3");
    paths.finalVideoPath = PathWithSuffix(mediaPath.parent_path(), stem, videoSuffix, OutputVideoExtensionFor(mediaPath));
    return paths;
}

std::filesystem::path BuildVotSubtitlesPath(
    const std::filesystem::path& mediaPath,
    const std::wstring& language,
    const std::wstring& format
) {
    const std::wstring suffix = L".vot-subtitles." + SafeLanguageForFile(language);
    return PathWithSuffix(mediaPath.parent_path(), mediaPath.stem().wstring(), suffix, L"." + NormalizedSubtitlesFormat(format));
}

std::vector<std::wstring> BuildVotCliArguments(
    const VoiceOverTranslationRequest& request,
    const std::filesystem::path& outputAudioPath
) {
    const std::wstring language = SafeLanguageForFile(request.language);
    return {
        L"translate",
        L"--url",
        request.youtubeUrl,
        L"--target-lang",
        language,
        L"--output",
        outputAudioPath.wstring(),
        L"--force"
    };
}

std::vector<std::wstring> BuildVotSubtitlesArguments(
    const VotSubtitlesRequest& request,
    const std::filesystem::path& outputPath
) {
    return {
        L"subtitles",
        L"--url",
        request.youtubeUrl,
        L"--target-lang",
        SafeLanguageForFile(request.language),
        L"--format",
        NormalizedSubtitlesFormat(request.format),
        L"--output",
        outputPath.wstring(),
        L"--force"
    };
}

VotHelperResult ParseVotHelperResult(const std::wstring& stdoutText) {
    VotHelperResult result;
    const std::wstring text = TrimWhitespace(stdoutText);
    if (text.empty()) {
        result.errorText = L"VOT helper did not return JSON";
        return result;
    }

    try {
        const nlohmann::json json = nlohmann::json::parse(WideToUtf8(text));
        if (!json.is_object()) {
            result.errorText = L"VOT helper returned invalid JSON";
            return result;
        }

        result.parsed = true;
        result.ok = json.value("ok", false);
        if (const auto operation = json.find("operation"); operation != json.end() && operation->is_string()) {
            result.operation = Utf8ToWide(operation->get<std::string>());
        }

        if (const auto data = json.find("data"); data != json.end() && data->is_object()) {
            if (const auto state = data->find("state"); state != data->end() && state->is_string()) {
                result.state = Utf8ToWide(state->get<std::string>());
            }
            if (const auto output = data->find("output"); output != data->end() && output->is_object()) {
                if (const auto path = output->find("path"); path != output->end() && path->is_string()) {
                    result.outputPath = PathFromUtf8(path->get<std::string>());
                }
            }
        }

        if (!result.ok) {
            if (const auto error = json.find("error"); error != json.end() && error->is_object()) {
                if (const auto message = error->find("message"); message != error->end() && message->is_string()) {
                    result.errorText = Utf8ToWide(message->get<std::string>());
                }
            }
            if (result.errorText.empty()) {
                result.errorText = L"VOT helper returned an error";
            }
        }
    } catch (...) {
        result = VotHelperResult{};
        result.errorText = L"VOT helper returned invalid JSON";
    }
    return result;
}

VoiceOverProcessInvocation BuildVotCliInvocation(
    const VoiceOverTranslationRequest& request,
    const std::filesystem::path& outputAudioPath
) {
    VoiceOverProcessInvocation invocation;
    invocation.executable = request.votCliPath;
    invocation.arguments = BuildVotCliArguments(request, outputAudioPath);
    return invocation;
}

std::vector<std::wstring> BuildVoiceOverMuxArguments(
    const std::filesystem::path& mediaPath,
    const std::filesystem::path& translationAudioPath,
    const std::filesystem::path& outputVideoPath,
    const std::wstring& language
) {
    return {
        L"-y",
        L"-i",
        mediaPath.wstring(),
        L"-i",
        translationAudioPath.wstring(),
        L"-map",
        L"0:v",
        L"-map",
        L"0:a?",
        L"-map",
        L"1:a",
        L"-c:v",
        L"copy",
        L"-c:a",
        L"aac",
        L"-metadata:s:a:1",
        L"language=" + LanguageMetadataCode(language),
        L"-metadata:s:a:1",
        L"title=VOT " + LanguageTitle(language),
        outputVideoPath.wstring()
    };
}

std::vector<std::wstring> BuildVoiceOverMixArguments(
    const std::filesystem::path& mediaPath,
    const std::filesystem::path& translationAudioPath,
    const std::filesystem::path& outputVideoPath,
    int originalVolumePercent
) {
    return {
        L"-y",
        L"-i",
        mediaPath.wstring(),
        L"-i",
        translationAudioPath.wstring(),
        L"-filter_complex",
        L"[0:a]volume=" + VolumeValue(originalVolumePercent) + L"[orig];[orig][1:a]amix=inputs=2:duration=first[a]",
        L"-map",
        L"0:v",
        L"-map",
        L"[a]",
        L"-c:v",
        L"copy",
        L"-c:a",
        L"aac",
        outputVideoPath.wstring()
    };
}

VoiceOverTranslationResult VoiceOverTranslationClient::Translate(
    const VoiceOverTranslationRequest& request,
    const VoiceOverTranslationCallbacks& callbacks,
    HANDLE cancelEvent
) {
    if (!IsRegularFile(request.mediaPath)) {
        return Failed(L"Файл для перевода не найден");
    }
    if (!IsRegularFile(request.ffmpegExePath)) {
        return Failed(L"FFmpeg не найден");
    }
    if (!IsRegularFile(request.votCliPath)) {
        return Failed(L"VOT helper не найден");
    }
    if (request.youtubeUrl.empty()) {
        return Failed(L"Ссылка на видео недоступна");
    }

    std::error_code ec;
    std::filesystem::create_directories(request.tempDirectory, ec);
    if (ec) {
        return Failed(L"Не удалось создать папку для временной озвучки");
    }

    const VoiceOverTranslationPaths paths = BuildVoiceOverPaths(
        request.mediaPath,
        request.tempDirectory,
        request.language,
        request.mode
    );
    std::filesystem::remove(paths.tempAudioPath, ec);
    ec.clear();

    EmitVoiceOverProgress(callbacks, 5.0, L"Получение перевода");

    ProcessRunOptions vot;
    const VoiceOverProcessInvocation votInvocation = BuildVotCliInvocation(request, paths.tempAudioPath);
    vot.executable = votInvocation.executable;
    vot.arguments = votInvocation.arguments;
    vot.timeoutMs = INFINITE;
    vot.cancelEvent = cancelEvent;
    const auto handleVotLine = [&callbacks](const std::wstring&) {
        EmitVoiceOverProgress(callbacks, 20.0, L"Получение перевода");
    };
    vot.onStdoutLine = handleVotLine;
    vot.onStderrLine = handleVotLine;

    const ProcessRunResult translated = ProcessRunner::Run(vot);
    if (translated.canceled) {
        return Canceled({paths.tempAudioPath});
    }
    if (translated.exitCode != 0) {
        std::filesystem::remove(paths.tempAudioPath, ec);
        return Failed(L"VOT helper завершился с ошибкой: " + ProcessErrorSummary(translated, L"неизвестная ошибка"));
    }
    const VotHelperResult helperResult = ParseVotHelperResult(translated.stdoutText);
    if (!helperResult.parsed) {
        std::filesystem::remove(paths.tempAudioPath, ec);
        return Failed(helperResult.errorText);
    }
    if (!helperResult.ok) {
        std::filesystem::remove(paths.tempAudioPath, ec);
        return Failed(helperResult.errorText);
    }
    if (helperResult.state == L"pending") {
        std::filesystem::remove(paths.tempAudioPath, ec);
        return Failed(L"VOT helper result is pending; try again later");
    }

    const std::filesystem::path translatedAudio = helperResult.outputPath.empty()
        ? paths.tempAudioPath
        : helperResult.outputPath;
    if (!MoveFileReplacing(translatedAudio, paths.finalAudioPath)) {
        std::filesystem::remove(paths.tempAudioPath, ec);
        return Failed(L"VOT helper не создал mp3 с переводом");
    }

    EmitVoiceOverProgress(callbacks, 70.0, L"Сборка видео");

    std::filesystem::remove(paths.finalVideoPath, ec);
    ec.clear();

    ProcessRunOptions ffmpeg;
    ffmpeg.executable = request.ffmpegExePath;
    ffmpeg.arguments = NormalizedMode(request.mode) == L"mixed"
        ? BuildVoiceOverMixArguments(request.mediaPath, paths.finalAudioPath, paths.finalVideoPath, request.originalVolumePercent)
        : BuildVoiceOverMuxArguments(request.mediaPath, paths.finalAudioPath, paths.finalVideoPath, request.language);
    ffmpeg.timeoutMs = INFINITE;
    ffmpeg.cancelEvent = cancelEvent;

    const ProcessRunResult muxed = ProcessRunner::Run(ffmpeg);
    if (muxed.canceled) {
        return Canceled({paths.finalVideoPath});
    }
    if (muxed.exitCode != 0) {
        std::filesystem::remove(paths.finalVideoPath, ec);
        return Failed(L"FFmpeg не собрал видео с переводом: " + ProcessErrorSummary(muxed, L"неизвестная ошибка"));
    }
    if (!IsRegularFile(paths.finalVideoPath)) {
        return Failed(L"FFmpeg не создал видео с переводом");
    }

    EmitVoiceOverProgress(callbacks, 99.0, L"Сохранение перевода");

    VoiceOverTranslationResult result;
    result.success = true;
    result.audioPath = paths.finalAudioPath;
    result.videoPath = paths.finalVideoPath;
    return result;
}

VotSubtitlesResult VotSubtitlesClient::Export(
    const VotSubtitlesRequest& request,
    const VoiceOverTranslationCallbacks& callbacks,
    HANDLE cancelEvent
) {
    if (!IsRegularFile(request.mediaPath)) {
        return FailedSubtitles(L"Media file for subtitles was not found");
    }
    if (!IsRegularFile(request.votCliPath)) {
        return FailedSubtitles(L"VOT helper was not found");
    }
    if (request.youtubeUrl.empty()) {
        return FailedSubtitles(L"Video URL is unavailable");
    }

    const std::filesystem::path outputPath = BuildVotSubtitlesPath(request.mediaPath, request.language, request.format);
    std::error_code ec;
    std::filesystem::create_directories(outputPath.parent_path(), ec);
    std::filesystem::remove(outputPath, ec);
    ec.clear();

    EmitVoiceOverProgress(callbacks, 5.0, L"Getting VOT subtitles");

    ProcessRunOptions options;
    options.executable = request.votCliPath;
    options.arguments = BuildVotSubtitlesArguments(request, outputPath);
    options.timeoutMs = INFINITE;
    options.cancelEvent = cancelEvent;
    const auto handleLine = [&callbacks](const std::wstring&) {
        EmitVoiceOverProgress(callbacks, 30.0, L"Getting VOT subtitles");
    };
    options.onStdoutLine = handleLine;
    options.onStderrLine = handleLine;

    const ProcessRunResult process = ProcessRunner::Run(options);
    if (process.canceled) {
        return CanceledSubtitles({outputPath});
    }
    if (process.exitCode != 0) {
        std::filesystem::remove(outputPath, ec);
        return FailedSubtitles(L"VOT helper failed: " + ProcessErrorSummary(process, L"unknown error"));
    }

    const VotHelperResult helperResult = ParseVotHelperResult(process.stdoutText);
    if (!helperResult.parsed) {
        std::filesystem::remove(outputPath, ec);
        return FailedSubtitles(helperResult.errorText);
    }
    if (!helperResult.ok) {
        std::filesystem::remove(outputPath, ec);
        return FailedSubtitles(helperResult.errorText);
    }
    if (helperResult.state == L"pending") {
        std::filesystem::remove(outputPath, ec);
        return FailedSubtitles(L"VOT subtitles are pending; try again later");
    }

    const std::filesystem::path producedPath = helperResult.outputPath.empty() ? outputPath : helperResult.outputPath;
    if (producedPath != outputPath && !MoveFileReplacing(producedPath, outputPath)) {
        std::filesystem::remove(outputPath, ec);
        return FailedSubtitles(L"VOT helper did not create subtitles");
    }
    if (!IsRegularFile(outputPath)) {
        return FailedSubtitles(L"VOT helper did not create subtitles");
    }

    EmitVoiceOverProgress(callbacks, 100.0, L"VOT subtitles saved");

    VotSubtitlesResult result;
    result.success = true;
    result.subtitlesPath = outputPath;
    return result;
}
