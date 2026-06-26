#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "AppPaths.h"
#include "Config.h"

#include <filesystem>
#include <functional>
#include <cstdint>
#include <string>
#include <vector>

struct ReleaseAssetInfo {
    bool found = false;
    std::wstring version;
    std::wstring downloadUrl;
};

ReleaseAssetInfo ParseGitHubReleaseAsset(const std::string& releaseJson, const std::string& assetName);

enum class FfmpegSource {
    Missing,
    ConfiguredPath,
    LocalTools,
    Path
};

struct FfmpegStatus {
    bool available = false;
    FfmpegSource source = FfmpegSource::Missing;
    std::filesystem::path ffmpegExe;
    std::wstring message;
};

class FfmpegManager {
public:
    static FfmpegStatus Resolve(const AppPaths& paths, const AppConfig& config);
    static FfmpegStatus ResolveUserPath(const std::filesystem::path& path);
    static std::filesystem::path FindExtractedBinDir(const std::filesystem::path& extractedRoot);
    static std::wstring EssentialsDownloadUrl();
    static FfmpegStatus InstallEssentials(
        const AppPaths& paths,
        const std::function<void(std::uint64_t downloaded, std::uint64_t total, const std::wstring& status)>& onProgress = {},
        HANDLE cancelEvent = nullptr
    );
};

struct ToolInstallStatus {
    bool installed = false;
    std::filesystem::path executable;
    std::wstring version;
    WhisperBackend whisperBackend = WhisperBackend::Auto;
};

struct WhisperModelInfo {
    std::wstring id;
    std::wstring name;
    std::wstring fileName;
    std::wstring downloadUrl;
    std::wstring tags;
    std::wstring description;
    std::uint64_t sizeBytes = 0;
    bool recommended = false;
    bool bestQuality = false;
};

class WhisperManager {
public:
    static const char* WindowsCpuAssetName();
    static const char* WindowsCudaAssetName();
    static const char* BackendAssetName(WhisperBackend backend);
    static std::vector<WhisperModelInfo> ModelCatalog();
    static std::filesystem::path ModelPath(const AppPaths& paths, const WhisperModelInfo& model);
    static std::filesystem::path BackendInstallDir(const AppPaths& paths, WhisperBackend backend);
    static std::filesystem::path BackendExecutablePath(const AppPaths& paths, WhisperBackend backend);
    static std::filesystem::path FindExecutableDir(const std::filesystem::path& extractedRoot);
    static ToolInstallStatus ResolveBackend(const AppPaths& paths, WhisperBackend backend);
    static ToolInstallStatus Resolve(const AppPaths& paths, const AppConfig& config);
    static ReleaseAssetInfo CheckLatestRelease(WhisperBackend backend = WhisperBackend::Cpu, HANDLE cancelEvent = nullptr);
    static ToolInstallStatus Install(
        const AppPaths& paths,
        WhisperBackend backend,
        const std::function<void(std::uint64_t downloaded, std::uint64_t total, const std::wstring& status)>& onProgress = {},
        HANDLE cancelEvent = nullptr
    );
    static ToolInstallStatus Install(
        const AppPaths& paths,
        const std::function<void(std::uint64_t downloaded, std::uint64_t total, const std::wstring& status)>& onProgress = {},
        HANDLE cancelEvent = nullptr
    );
    static std::filesystem::path DownloadModel(
        const AppPaths& paths,
        const WhisperModelInfo& model,
        const std::function<void(std::uint64_t downloaded, std::uint64_t total, const std::wstring& status)>& onProgress = {},
        HANDLE cancelEvent = nullptr
    );
};

struct VotExeStatus {
    bool available = false;
    std::filesystem::path executable;
    std::wstring message;
};

class VotExeManager {
public:
    static const char* WindowsZipAssetName();
    static const char* Sha256SumsAssetName();
    static ReleaseAssetInfo CheckLatestRelease(HANDLE cancelEvent = nullptr);
    static ReleaseAssetInfo CheckLatestSha256Sums(HANDLE cancelEvent = nullptr);
    static VotExeStatus Resolve(const AppPaths& paths, const AppConfig& config);
    static VotExeStatus ResolveUserPath(const std::filesystem::path& path);
    static std::filesystem::path FindExecutable(const std::filesystem::path& root);
    static std::wstring Sha256ForFile(const std::string& sumsText, const std::string& fileName);
    static VotExeStatus Install(
        const AppPaths& paths,
        const std::function<void(std::uint64_t downloaded, std::uint64_t total, const std::wstring& status)>& onProgress = {},
        HANDLE cancelEvent = nullptr
    );
};

bool ShouldInstallYtDlpUpdate(const ToolInstallStatus& current, const ReleaseAssetInfo& latest);
bool ValidateYtDlpExecutableVersion(const std::filesystem::path& executable, const std::wstring& expectedVersion);
bool ShouldInstallAppUpdate(const ReleaseAssetInfo& latest);
std::wstring BuildAppUpdatePromptMessage(const ReleaseAssetInfo& release);

class YtDlpManager {
public:
    explicit YtDlpManager(AppPaths paths);

    ToolInstallStatus Status() const;
    ReleaseAssetInfo CheckLatestRelease(HANDLE cancelEvent = nullptr) const;
    ToolInstallStatus InstallOrUpdate(HANDLE cancelEvent = nullptr) const;
    ToolInstallStatus InstallOrUpdate(const ReleaseAssetInfo& release, HANDLE cancelEvent = nullptr) const;

private:
    AppPaths m_paths;
};

class AppUpdateService {
public:
    static ReleaseAssetInfo CheckLatestRelease(HANDLE cancelEvent = nullptr);
    static std::filesystem::path DownloadUpdateExe(
        const AppPaths& paths,
        const ReleaseAssetInfo& release,
        const std::function<void(std::uint64_t downloaded, std::uint64_t total)>& onProgress = {},
        HANDLE cancelEvent = nullptr
    );
    static void StartDownloadedUpdate(
        const AppPaths& paths,
        const std::filesystem::path& downloadedExe
    );
};
