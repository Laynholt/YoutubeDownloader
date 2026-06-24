#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct TranscriptionRequest {
    std::filesystem::path mediaPath;
    std::filesystem::path tempDirectory;
    std::filesystem::path ffmpegExePath;
    std::filesystem::path whisperExePath;
    std::filesystem::path whisperModelPath;
    std::wstring language = L"auto";
};

struct TranscriptionCallbacks {
    std::function<void(const std::wstring&)> onStatus;
    std::function<void(double percent, const std::wstring& status)> onProgress;
};

struct TranscriptionResult {
    bool success = false;
    bool canceled = false;
    std::filesystem::path textPath;
    std::filesystem::path srtPath;
    std::wstring errorText;
};

struct TranscriptionPaths {
    std::filesystem::path wavPath;
    std::filesystem::path whisperOutputBase;
    std::filesystem::path tempTextPath;
    std::filesystem::path tempSrtPath;
    std::filesystem::path finalTextPath;
    std::filesystem::path finalSrtPath;
};

std::filesystem::path TranscriptOutputBaseFor(const std::filesystem::path& mediaPath);
TranscriptionPaths BuildTranscriptionPaths(
    const std::filesystem::path& mediaPath,
    const std::filesystem::path& tempDirectory,
    long long nonce
);
std::vector<std::wstring> BuildFfmpegAudioExtractionArguments(
    const std::filesystem::path& mediaPath,
    const std::filesystem::path& wavPath
);
std::vector<std::wstring> BuildWhisperArguments(
    const TranscriptionRequest& request,
    const std::filesystem::path& wavPath,
    const std::filesystem::path& outputBase
);
std::optional<double> ParseWhisperProgressPercent(const std::wstring& line);
std::wstring BuildProcessErrorSummary(
    const std::wstring& stderrText,
    const std::wstring& stdoutText,
    const std::wstring& fallback
);

class TranscriptionClient {
public:
    static TranscriptionResult Transcribe(
        const TranscriptionRequest& request,
        const TranscriptionCallbacks& callbacks = {},
        HANDLE cancelEvent = nullptr
    );
};
