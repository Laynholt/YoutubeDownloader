#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "Config.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct TranscriptionRequest {
    TranscriptionEngine engine = TranscriptionEngine::Whisper;
    std::filesystem::path mediaPath;
    std::filesystem::path tempDirectory;
    std::filesystem::path ffmpegExePath;
    std::filesystem::path whisperExePath;
    std::filesystem::path whisperModelPath;
    std::filesystem::path votExePath;
    std::wstring youtubeUrl;
    std::wstring language = L"auto";
    std::wstring votTargetLanguage = L"ru";
    SubtitleFfmpegMode subtitleMode = SubtitleFfmpegMode::Off;
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
    std::filesystem::path videoPath;
    std::wstring errorText;
};

struct TranscriptionPaths {
    std::filesystem::path wavPath;
    std::filesystem::path whisperOutputBase;
    std::filesystem::path tempTextPath;
    std::filesystem::path tempSrtPath;
    std::filesystem::path finalTextPath;
    std::filesystem::path finalSrtPath;
    std::filesystem::path tempVideoPath;
    std::filesystem::path finalVideoPath;
};

std::filesystem::path TranscriptOutputBaseFor(
    const std::filesystem::path& mediaPath,
    TranscriptionEngine engine = TranscriptionEngine::Whisper,
    const std::wstring& language = L"auto"
);
TranscriptionPaths BuildTranscriptionPaths(
    const std::filesystem::path& mediaPath,
    const std::filesystem::path& tempDirectory,
    long long nonce,
    TranscriptionEngine engine = TranscriptionEngine::Whisper,
    const std::wstring& language = L"auto"
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
std::vector<std::wstring> BuildVotHelperSubtitlesArguments(
    const TranscriptionRequest& request,
    const std::filesystem::path& outputSrtPath
);
std::vector<std::wstring> BuildSubtitleTrackArguments(
    const std::filesystem::path& mediaPath,
    const std::filesystem::path& srtPath,
    const std::filesystem::path& outputVideoPath,
    TranscriptionEngine engine = TranscriptionEngine::Whisper,
    const std::wstring& language = L"auto"
);
std::vector<std::wstring> BuildSubtitleBurnInArguments(
    const std::filesystem::path& mediaPath,
    const std::filesystem::path& srtPath,
    const std::filesystem::path& outputVideoPath
);
std::optional<double> ParseWhisperProgressPercent(const std::wstring& line);
std::wstring PlainTextFromSrtContent(const std::wstring& srtContent);
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
