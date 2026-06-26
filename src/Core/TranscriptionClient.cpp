#include "TranscriptionClient.h"

#include "BackendText.h"
#include "FileOperations.h"
#include "ProcessRunner.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
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

std::wstring ProcessErrorText(const ProcessRunResult& result, const std::wstring& fallback) {
    return BuildProcessErrorSummary(result.stderrText, result.stdoutText, fallback);
}

TranscriptionResult Failed(std::wstring errorText) {
    TranscriptionResult result;
    result.errorText = std::move(errorText);
    return result;
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

TranscriptionResult TranscriptionClient::Transcribe(
    const TranscriptionRequest& request,
    const TranscriptionCallbacks& callbacks,
    HANDLE cancelEvent
) {
    if (!IsRegularFile(request.mediaPath)) {
        return Failed(L"Файл для распознавания не найден");
    }
    if (!IsRegularFile(request.ffmpegExePath)) {
        return Failed(L"FFmpeg не найден");
    }
    if (!IsRegularFile(request.whisperExePath)) {
        return Failed(L"whisper-cli.exe не найден");
    }
    if (!IsRegularFile(request.whisperModelPath)) {
        return Failed(L"Модель Whisper не найдена");
    }

    std::error_code ec;
    std::filesystem::create_directories(request.tempDirectory, ec);
    if (ec) {
        return Failed(L"Не удалось создать папку для временного аудио");
    }

    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const TranscriptionPaths paths = BuildTranscriptionPaths(request.mediaPath, request.tempDirectory, ticks);

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

    const bool textMoved = MoveFileReplacing(paths.tempTextPath, paths.finalTextPath);
    const bool srtMoved = MoveFileReplacing(paths.tempSrtPath, paths.finalSrtPath);
    if (!textMoved && !srtMoved) {
        return Failed(L"Whisper не создал файл расшифровки");
    }

    EmitTranscriptionProgress(callbacks, 99.0, L"Сохранение расшифровки");

    TranscriptionResult result;
    result.success = true;
    result.textPath = textMoved ? paths.finalTextPath : std::filesystem::path{};
    result.srtPath = srtMoved ? paths.finalSrtPath : std::filesystem::path{};
    return result;
}
