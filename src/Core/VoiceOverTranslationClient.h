#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "Config.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct VoiceOverTranslationRequest {
    std::filesystem::path mediaPath;
    std::filesystem::path tempDirectory;
    std::filesystem::path ffmpegExePath;
    std::filesystem::path votExePath;
    std::wstring youtubeUrl;
    std::wstring sourceLanguage = L"auto";
    std::wstring targetLanguage = L"ru";
    int timeoutSeconds = 900;
    bool livelyVoice = false;
    VoiceOverFfmpegMode ffmpegMode = VoiceOverFfmpegMode::Off;
    int originalVolumePercent = 25;
};

struct VoiceOverTranslationCallbacks {
    std::function<void(const std::wstring&)> onStatus;
    std::function<void(double percent, const std::wstring& status)> onProgress;
};

struct VoiceOverTranslationResult {
    bool success = false;
    bool canceled = false;
    std::filesystem::path audioPath;
    std::filesystem::path videoPath;
    std::wstring errorText;
};

struct VoiceOverTranslationPaths {
    std::filesystem::path tempAudioPath;
    std::filesystem::path finalAudioPath;
    std::filesystem::path tempVideoPath;
    std::filesystem::path finalVideoPath;
};

VoiceOverTranslationPaths BuildVoiceOverPaths(
    const std::filesystem::path& mediaPath,
    const std::filesystem::path& tempDirectory,
    const std::wstring& language
);
std::vector<std::wstring> BuildVotHelperTranslateArguments(
    const VoiceOverTranslationRequest& request,
    const std::filesystem::path& outputAudioPath
);
std::vector<std::wstring> BuildVoiceOverAudioTrackArguments(
    const std::filesystem::path& mediaPath,
    const std::filesystem::path& translationAudioPath,
    const std::filesystem::path& outputVideoPath,
    const std::wstring& language
);
std::vector<std::wstring> BuildVoiceOverMixArguments(
    const std::filesystem::path& mediaPath,
    const std::filesystem::path& translationAudioPath,
    const std::filesystem::path& outputVideoPath,
    int originalVolumePercent
);

class VoiceOverTranslationClient {
public:
    static VoiceOverTranslationResult Translate(
        const VoiceOverTranslationRequest& request,
        const VoiceOverTranslationCallbacks& callbacks = {},
        HANDLE cancelEvent = nullptr
    );
};
