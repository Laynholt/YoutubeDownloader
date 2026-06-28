#include "VoiceOverTranslationClient.h"

#include "PostProcessingFileOps.h"
#include "ProcessRunner.h"

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

std::wstring LanguageMetadataCode(const std::wstring& language) {
    const std::wstring lang = SafeLanguageForFile(language);
    if (lang == L"ru" || lang == L"rus") {
        return L"rus";
    }
    if (lang == L"en" || lang == L"eng") {
        return L"eng";
    }
    if (lang == L"de" || lang == L"ger" || lang == L"deu") {
        return L"deu";
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
    if (lang == L"de" || lang == L"ger" || lang == L"deu") {
        return L"German";
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

std::filesystem::path PathWithSuffix(
    const std::filesystem::path& parent,
    const std::wstring& stem,
    const std::wstring& suffix,
    const std::wstring& extension
) {
    return parent / (stem + suffix + extension);
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
    const std::wstring& language
) {
    const std::wstring stem = mediaPath.stem().wstring();
    const std::wstring suffix = L".vot." + SafeLanguageForFile(language);
    const std::wstring extension = mediaPath.extension().wstring().empty()
        ? L".mp4"
        : mediaPath.extension().wstring();

    VoiceOverTranslationPaths paths;
    paths.tempAudioPath = PathWithSuffix(tempDirectory, stem, suffix, L".mp3");
    paths.finalAudioPath = PathWithSuffix(mediaPath.parent_path(), stem, suffix, L".mp3");
    paths.tempVideoPath = PathWithSuffix(tempDirectory, stem, suffix, extension);
    paths.finalVideoPath = mediaPath;
    return paths;
}

std::vector<std::wstring> BuildVotHelperTranslateArguments(
    const VoiceOverTranslationRequest& request,
    const std::filesystem::path& outputAudioPath
) {
    std::vector<std::wstring> args = {
        L"translate",
        L"--url",
        request.youtubeUrl,
        L"--source-lang",
        request.sourceLanguage.empty() ? L"auto" : request.sourceLanguage,
        L"--target-lang",
        SafeLanguageForFile(request.targetLanguage),
        L"--timeout",
        std::to_wstring(std::max(1, request.timeoutSeconds)),
        L"--output",
        outputAudioPath.wstring(),
        L"--force"
    };
    if (request.livelyVoice) {
        args.push_back(L"--lively-voice");
    }
    return args;
}

std::vector<std::wstring> BuildVoiceOverAudioTrackArguments(
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
    if (!IsRegularFile(request.votExePath)) {
        return Failed(L"vot-helper.exe не найден");
    }
    if (request.youtubeUrl.empty()) {
        return Failed(L"Ссылка на видео недоступна");
    }
    if (request.ffmpegMode != VoiceOverFfmpegMode::Off && !IsRegularFile(request.ffmpegExePath)) {
        return Failed(L"FFmpeg не найден");
    }

    std::error_code ec;
    std::filesystem::create_directories(request.tempDirectory, ec);
    if (ec) {
        return Failed(L"Не удалось создать папку для временной озвучки");
    }

    const VoiceOverTranslationPaths paths = BuildVoiceOverPaths(
        request.mediaPath,
        request.tempDirectory,
        request.targetLanguage
    );
    std::filesystem::remove(paths.tempAudioPath, ec);
    ec.clear();

    EmitVoiceOverProgress(callbacks, 5.0, L"Получение перевода");

    ProcessRunOptions vot;
    vot.executable = request.votExePath;
    vot.arguments = BuildVotHelperTranslateArguments(request, paths.tempAudioPath);
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
        return Failed(L"vot-helper.exe завершился с ошибкой: " + ProcessErrorSummary(translated, L"неизвестная ошибка"));
    }
    if (!CommitPostProcessingSidecarFile(paths.tempAudioPath, paths.finalAudioPath)) {
        std::filesystem::remove(paths.tempAudioPath, ec);
        return Failed(L"vot-helper.exe не создал mp3 с переводом");
    }

    if (request.ffmpegMode == VoiceOverFfmpegMode::Off) {
        VoiceOverTranslationResult result;
        result.success = true;
        result.audioPath = paths.finalAudioPath;
        return result;
    }

    EmitVoiceOverProgress(callbacks, 70.0, L"Встраивание перевода в видео");

    std::filesystem::remove(paths.tempVideoPath, ec);
    ec.clear();

    ProcessRunOptions ffmpeg;
    ffmpeg.executable = request.ffmpegExePath;
    ffmpeg.arguments = request.ffmpegMode == VoiceOverFfmpegMode::Mix
        ? BuildVoiceOverMixArguments(request.mediaPath, paths.finalAudioPath, paths.tempVideoPath, request.originalVolumePercent)
        : BuildVoiceOverAudioTrackArguments(request.mediaPath, paths.finalAudioPath, paths.tempVideoPath, request.targetLanguage);
    ffmpeg.timeoutMs = INFINITE;
    ffmpeg.cancelEvent = cancelEvent;

    const ProcessRunResult embedded = ProcessRunner::Run(ffmpeg);
    if (embedded.canceled) {
        return Canceled({paths.tempVideoPath});
    }
    if (embedded.exitCode != 0) {
        std::filesystem::remove(paths.tempVideoPath, ec);
        return Failed(L"FFmpeg не встроил перевод в видео: " + ProcessErrorSummary(embedded, L"неизвестная ошибка"));
    }
    if (!ReplaceOriginalMediaWithPostProcessedFile(paths.tempVideoPath, paths.finalVideoPath)) {
        std::filesystem::remove(paths.tempVideoPath, ec);
        return Failed(L"Не удалось заменить исходное видео версией с переводом");
    }

    EmitVoiceOverProgress(callbacks, 99.0, L"Сохранение перевода");

    VoiceOverTranslationResult result;
    result.success = true;
    result.audioPath = paths.finalAudioPath;
    result.videoPath = paths.finalVideoPath;
    return result;
}
