#include "Config.h"

#include "BackendText.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <ranges>
#include <string_view>
#include <system_error>

namespace {

std::string PathToJsonString(const std::filesystem::path& path) {
    return PathToUtf8(path);
}

std::filesystem::path PathFromJsonString(const nlohmann::json& json, const char* key, const std::filesystem::path& fallback = {}) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_string()) {
        return fallback;
    }
    return PathFromUtf8(it->get<std::string>());
}

std::wstring WStringFromJson(const nlohmann::json& json, const char* key, const std::wstring& fallback = L"") {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_string()) {
        return fallback;
    }
    return Utf8ToWide(it->get<std::string>());
}

int IntFromJson(const nlohmann::json& json, const char* key, int fallback) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_number_integer()) {
        return fallback;
    }
    return it->get<int>();
}

bool BoolFromJson(const nlohmann::json& json, const char* key, bool fallback) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_boolean()) {
        return fallback;
    }
    return it->get<bool>();
}

std::wstring LowerAscii(std::wstring value) {
    for (wchar_t& ch : value) {
        if (ch >= L'A' && ch <= L'Z') {
            ch = static_cast<wchar_t>(ch - L'A' + L'a');
        }
    }
    return value;
}

TranscriptionEngine TranscriptionEngineFromJson(
    const nlohmann::json& json,
    const char* key,
    TranscriptionEngine fallback
) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_string()) {
        return fallback;
    }
    return TranscriptionEngineFromConfigValue(Utf8ToWide(it->get<std::string>()));
}

WhisperBackend WhisperBackendFromJson(const nlohmann::json& json, const char* key, WhisperBackend fallback) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_string()) {
        return fallback;
    }
    return WhisperBackendFromConfigValue(Utf8ToWide(it->get<std::string>()));
}

VoiceOverFfmpegMode VoiceOverFfmpegModeFromJson(
    const nlohmann::json& json,
    const char* key,
    VoiceOverFfmpegMode fallback
) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_string()) {
        return fallback;
    }
    return VoiceOverFfmpegModeFromConfigValue(Utf8ToWide(it->get<std::string>()));
}

SubtitleFfmpegMode SubtitleFfmpegModeFromJson(
    const nlohmann::json& json,
    const char* key,
    SubtitleFfmpegMode fallback
) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_string()) {
        return fallback;
    }
    return SubtitleFfmpegModeFromConfigValue(Utf8ToWide(it->get<std::string>()));
}

std::wstring NormalizeVoiceOverLanguage(const std::wstring& value) {
    const std::wstring normalized = LowerAscii(value);
    static constexpr std::array<std::wstring_view, 9> kLanguages = {
        L"ru",
        L"en",
        L"de",
        L"es",
        L"it",
        L"pt",
        L"ja",
        L"ko",
        L"zh"
    };
    if (std::ranges::find(kLanguages, normalized) != kLanguages.end()) {
        return normalized;
    }
    return L"ru";
}

std::wstring NormalizeWhisperLanguage(const std::wstring& value) {
    const std::wstring normalized = LowerAscii(value);
    static constexpr std::array<std::wstring_view, 10> kLanguages = {
        L"auto",
        L"ru",
        L"en",
        L"de",
        L"es",
        L"it",
        L"pt",
        L"ja",
        L"ko",
        L"zh"
    };
    if (std::ranges::find(kLanguages, normalized) != kLanguages.end()) {
        return normalized;
    }
    return L"auto";
}

std::filesystem::path DefaultDownloadDir() {
    wchar_t* profile = nullptr;
    size_t profileLength = 0;
    if (_wdupenv_s(&profile, &profileLength, L"USERPROFILE") == 0 && profile && profileLength > 0) {
        std::filesystem::path result = std::filesystem::path(profile) / L"Downloads";
        free(profile);
        return result;
    }
    free(profile);
    return std::filesystem::current_path() / L"Downloads";
}

} // namespace

std::wstring TranscriptionEngineToConfigValue(TranscriptionEngine engine) {
    return engine == TranscriptionEngine::Vot ? L"vot" : L"whisper";
}

TranscriptionEngine TranscriptionEngineFromConfigValue(const std::wstring& value) {
    return LowerAscii(value) == L"vot" ? TranscriptionEngine::Vot : TranscriptionEngine::Whisper;
}

std::wstring WhisperBackendToConfigValue(WhisperBackend backend) {
    switch (backend) {
    case WhisperBackend::Cpu:
        return L"cpu";
    case WhisperBackend::Cuda:
        return L"cuda";
    case WhisperBackend::Custom:
        return L"custom";
    case WhisperBackend::Auto:
    default:
        return L"auto";
    }
}

WhisperBackend WhisperBackendFromConfigValue(const std::wstring& value) {
    const std::wstring normalized = LowerAscii(value);
    if (normalized == L"cpu") {
        return WhisperBackend::Cpu;
    }
    if (normalized == L"cuda") {
        return WhisperBackend::Cuda;
    }
    if (normalized == L"custom") {
        return WhisperBackend::Custom;
    }
    return WhisperBackend::Auto;
}

std::wstring VoiceOverFfmpegModeToConfigValue(VoiceOverFfmpegMode mode) {
    switch (mode) {
    case VoiceOverFfmpegMode::AudioTrack:
        return L"audio_track";
    case VoiceOverFfmpegMode::Mix:
        return L"mix";
    case VoiceOverFfmpegMode::Off:
    default:
        return L"off";
    }
}

VoiceOverFfmpegMode VoiceOverFfmpegModeFromConfigValue(const std::wstring& value) {
    const std::wstring normalized = LowerAscii(value);
    if (normalized == L"audio_track") {
        return VoiceOverFfmpegMode::AudioTrack;
    }
    if (normalized == L"mix") {
        return VoiceOverFfmpegMode::Mix;
    }
    return VoiceOverFfmpegMode::Off;
}

std::wstring SubtitleFfmpegModeToConfigValue(SubtitleFfmpegMode mode) {
    switch (mode) {
    case SubtitleFfmpegMode::SubtitleTrack:
        return L"subtitle_track";
    case SubtitleFfmpegMode::BurnIn:
        return L"burn_in";
    case SubtitleFfmpegMode::Off:
    default:
        return L"off";
    }
}

SubtitleFfmpegMode SubtitleFfmpegModeFromConfigValue(const std::wstring& value) {
    const std::wstring normalized = LowerAscii(value);
    if (normalized == L"subtitle_track") {
        return SubtitleFfmpegMode::SubtitleTrack;
    }
    if (normalized == L"burn_in") {
        return SubtitleFfmpegMode::BurnIn;
    }
    return SubtitleFfmpegMode::Off;
}

AppConfig ConfigStore::Defaults() {
    AppConfig config;
    config.downloadDir = DefaultDownloadDir();
    return config;
}

AppConfig ConfigStore::Load(const AppPaths& paths) {
    AppConfig config = Defaults();

    std::ifstream in(paths.configPath(), std::ios::binary);
    if (!in) {
        Save(paths, config);
        return config;
    }

    try {
        const nlohmann::json json = nlohmann::json::parse(in, nullptr, true, true);
        if (!json.is_object()) {
            return config;
        }

        config.downloadDir = PathFromJsonString(json, "download_dir", config.downloadDir);
        config.cookiesPath = PathFromJsonString(json, "cookies_path", config.cookiesPath);
        config.ffmpegPath = PathFromJsonString(json, "ffmpeg_path", config.ffmpegPath);
        config.whisperPath = PathFromJsonString(json, "whisper_path", config.whisperPath);
        config.whisperModelPath = PathFromJsonString(json, "whisper_model_path", config.whisperModelPath);
        config.votExePath = PathFromJsonString(json, "vot_exe_path", PathFromJsonString(json, "vot_cli_path", config.votExePath));
        config.quality = WStringFromJson(json, "quality", config.quality);
        config.container = WStringFromJson(json, "container", config.container);
        config.transcriptionEngine = TranscriptionEngineFromJson(json, "transcription_engine", config.transcriptionEngine);
        config.whisperBackend = WhisperBackendFromJson(json, "whisper_backend", config.whisperBackend);
        config.whisperLanguage = NormalizeWhisperLanguage(WStringFromJson(json, "whisper_language", config.whisperLanguage));
        config.voiceOverLanguage = NormalizeVoiceOverLanguage(WStringFromJson(json, "voice_over_language", config.voiceOverLanguage));
        config.voiceOverFfmpegMode = VoiceOverFfmpegModeFromJson(json, "voice_over_ffmpeg_mode", config.voiceOverFfmpegMode);
        config.subtitleFfmpegMode = SubtitleFfmpegModeFromJson(json, "subtitle_ffmpeg_mode", config.subtitleFfmpegMode);
        config.maxParallelDownloads = IntFromJson(json, "max_parallel_downloads", config.maxParallelDownloads);
        config.maxParallelDownloads = std::clamp(config.maxParallelDownloads, 3, 10);
        config.autoUpdateApp = BoolFromJson(json, "auto_update_app", config.autoUpdateApp);
        config.lastYtDlpCheckAt = WStringFromJson(json, "last_ytdlp_check_at", config.lastYtDlpCheckAt);
        config.lastYtDlpVersion = WStringFromJson(json, "last_ytdlp_version", config.lastYtDlpVersion);
    } catch (...) {
        return Defaults();
    }

    return config;
}

void ConfigStore::Save(const AppPaths& paths, const AppConfig& config) {
    std::error_code ec;
    std::filesystem::create_directories(paths.stuffDir(), ec);
    if (ec) {
        throw std::runtime_error("failed to create config directory");
    }

    nlohmann::json json;
    json["download_dir"] = PathToJsonString(config.downloadDir);
    json["cookies_path"] = PathToJsonString(config.cookiesPath);
    json["ffmpeg_path"] = PathToJsonString(config.ffmpegPath);
    json["whisper_path"] = PathToJsonString(config.whisperPath);
    json["whisper_model_path"] = PathToJsonString(config.whisperModelPath);
    json["vot_exe_path"] = PathToJsonString(config.votExePath);
    json["quality"] = WideToUtf8(config.quality);
    json["container"] = WideToUtf8(config.container);
    json["transcription_engine"] = WideToUtf8(TranscriptionEngineToConfigValue(config.transcriptionEngine));
    json["whisper_backend"] = WideToUtf8(WhisperBackendToConfigValue(config.whisperBackend));
    json["whisper_language"] = WideToUtf8(NormalizeWhisperLanguage(config.whisperLanguage));
    json["voice_over_language"] = WideToUtf8(NormalizeVoiceOverLanguage(config.voiceOverLanguage));
    json["voice_over_ffmpeg_mode"] = WideToUtf8(VoiceOverFfmpegModeToConfigValue(config.voiceOverFfmpegMode));
    json["subtitle_ffmpeg_mode"] = WideToUtf8(SubtitleFfmpegModeToConfigValue(config.subtitleFfmpegMode));
    json["max_parallel_downloads"] = config.maxParallelDownloads;
    json["auto_update_app"] = config.autoUpdateApp;
    json["last_ytdlp_check_at"] = WideToUtf8(config.lastYtDlpCheckAt);
    json["last_ytdlp_version"] = WideToUtf8(config.lastYtDlpVersion);

    const std::filesystem::path tmpPath = paths.configPath().wstring() + L".tmp";
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("failed to open temporary config file");
        }
        out << json.dump(2);
        out << "\n";
    }

    std::filesystem::rename(tmpPath, paths.configPath(), ec);
    if (ec) {
        std::filesystem::remove(paths.configPath(), ec);
        ec.clear();
        std::filesystem::rename(tmpPath, paths.configPath(), ec);
    }
    if (ec) {
        throw std::runtime_error("failed to replace config file");
    }
}
