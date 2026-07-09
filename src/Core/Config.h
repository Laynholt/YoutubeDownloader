#pragma once

#include "AppPaths.h"

#include <filesystem>
#include <string>

enum class TranscriptionEngine {
    Whisper,
    Vot
};

enum class WhisperBackend {
    Auto,
    Cpu,
    Cuda,
    Custom
};

enum class VoiceOverFfmpegMode {
    Off,
    AudioTrack,
    Mix
};

enum class SubtitleFfmpegMode {
    Off,
    SubtitleTrack,
    BurnIn
};

std::wstring TranscriptionEngineToConfigValue(TranscriptionEngine engine);
TranscriptionEngine TranscriptionEngineFromConfigValue(const std::wstring& value);
std::wstring WhisperBackendToConfigValue(WhisperBackend backend);
WhisperBackend WhisperBackendFromConfigValue(const std::wstring& value);
std::wstring VoiceOverFfmpegModeToConfigValue(VoiceOverFfmpegMode mode);
VoiceOverFfmpegMode VoiceOverFfmpegModeFromConfigValue(const std::wstring& value);
std::wstring SubtitleFfmpegModeToConfigValue(SubtitleFfmpegMode mode);
SubtitleFfmpegMode SubtitleFfmpegModeFromConfigValue(const std::wstring& value);

struct AppConfig {
    std::filesystem::path downloadDir;
    std::filesystem::path cookiesPath;
    std::filesystem::path ffmpegPath;
    std::wstring ffmpegVersion;
    std::filesystem::path whisperPath;
    std::wstring whisperVersion;
    std::filesystem::path whisperModelPath;
    std::filesystem::path votExePath;
    std::wstring votExeVersion;
    std::wstring quality = L"max";
    std::wstring container = L"auto";
    std::wstring uiLanguage = L"ru";
    TranscriptionEngine transcriptionEngine = TranscriptionEngine::Whisper;
    WhisperBackend whisperBackend = WhisperBackend::Auto;
    std::wstring whisperLanguage = L"auto";
    std::wstring votSubtitleLanguage = L"ru";
    std::wstring voiceOverLanguage = L"ru";
    int voiceOverOriginalVolumePercent = 25;
    VoiceOverFfmpegMode voiceOverFfmpegMode = VoiceOverFfmpegMode::Off;
    SubtitleFfmpegMode subtitleFfmpegMode = SubtitleFfmpegMode::Off;
    int maxParallelDownloads = 3;
    bool autoUpdateApp = true;
    std::wstring lastYtDlpCheckAt;
    std::wstring lastYtDlpVersion;
};

namespace ConfigStore {
AppConfig Defaults();
AppConfig Load(const AppPaths& paths);
void Save(const AppPaths& paths, const AppConfig& config);
}
