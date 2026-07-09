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
    std::wstring version;
    std::wstring message;
};

namespace FfmpegManager {
FfmpegStatus Resolve(const AppPaths& paths, const AppConfig& config);
FfmpegStatus ResolveUserPath(const std::filesystem::path& path);
std::wstring ExecutableVersion(const std::filesystem::path& executable, HANDLE cancelEvent = nullptr);
std::filesystem::path FindExtractedBinDir(const std::filesystem::path& extractedRoot);
std::wstring EssentialsDownloadUrl();
FfmpegStatus InstallEssentials(
        const AppPaths& paths,
        const std::function<void(std::uint64_t downloaded, std::uint64_t total, const std::wstring& status)>& onProgress = {},
        HANDLE cancelEvent = nullptr
);
}

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

namespace WhisperManager {
const char* WindowsCpuAssetName();
const char* WindowsCudaAssetName();
const char* BackendAssetName(WhisperBackend backend);
std::vector<WhisperModelInfo> ModelCatalog();
std::filesystem::path ModelPath(const AppPaths& paths, const WhisperModelInfo& model);
std::filesystem::path BackendInstallDir(const AppPaths& paths, WhisperBackend backend);
std::filesystem::path BackendExecutablePath(const AppPaths& paths, WhisperBackend backend);
std::filesystem::path FindExecutableDir(const std::filesystem::path& extractedRoot);
std::wstring ExecutableVersion(const std::filesystem::path& executable, HANDLE cancelEvent = nullptr);
bool SelfTestExecutable(const std::filesystem::path& executable, HANDLE cancelEvent = nullptr);
ToolInstallStatus ResolveBackend(const AppPaths& paths, WhisperBackend backend);
ToolInstallStatus Resolve(const AppPaths& paths, const AppConfig& config);
ReleaseAssetInfo CheckLatestRelease(WhisperBackend backend = WhisperBackend::Cpu, HANDLE cancelEvent = nullptr);
ToolInstallStatus Install(
        const AppPaths& paths,
        WhisperBackend backend,
        const std::function<void(std::uint64_t downloaded, std::uint64_t total, const std::wstring& status)>& onProgress = {},
        HANDLE cancelEvent = nullptr
);
ToolInstallStatus Install(
        const AppPaths& paths,
        const std::function<void(std::uint64_t downloaded, std::uint64_t total, const std::wstring& status)>& onProgress = {},
        HANDLE cancelEvent = nullptr
);
std::filesystem::path DownloadModel(
        const AppPaths& paths,
        const WhisperModelInfo& model,
        const std::function<void(std::uint64_t downloaded, std::uint64_t total, const std::wstring& status)>& onProgress = {},
        HANDLE cancelEvent = nullptr
);
}

WhisperBackend SelectWhisperInstallBackend(WhisperBackend configuredBackend, bool cudaAvailable);
bool IsWhisperCudaCandidateAvailable();

struct VotExeStatus {
    bool available = false;
    std::filesystem::path executable;
    std::wstring version;
    std::wstring message;
};

namespace VotExeManager {
const char* WindowsZipAssetName();
const char* Sha256SumsAssetName();
ReleaseAssetInfo CheckLatestRelease(HANDLE cancelEvent = nullptr);
ReleaseAssetInfo CheckLatestSha256Sums(HANDLE cancelEvent = nullptr);
VotExeStatus Resolve(const AppPaths& paths, const AppConfig& config);
VotExeStatus ResolveUserPath(const std::filesystem::path& path);
std::filesystem::path FindExecutable(const std::filesystem::path& root);
std::vector<std::filesystem::path> FindExecutables(const std::filesystem::path& root);
std::wstring Sha256ForFile(const std::string& sumsText, const std::string& fileName);
std::wstring ExecutableVersion(const std::filesystem::path& executable, HANDLE cancelEvent = nullptr);
bool SelfTestExecutable(const std::filesystem::path& executable, HANDLE cancelEvent = nullptr);
VotExeStatus Install(
        const AppPaths& paths,
        const std::function<void(std::uint64_t downloaded, std::uint64_t total, const std::wstring& status)>& onProgress = {},
        HANDLE cancelEvent = nullptr
);
}

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

namespace AppUpdateService {
const char* ExeAssetName();
const char* Sha256SumsAssetName();
ReleaseAssetInfo CheckLatestRelease(HANDLE cancelEvent = nullptr);
ReleaseAssetInfo CheckLatestSha256Sums(HANDLE cancelEvent = nullptr);
void EnsureLocalSha256Sums(const AppPaths& paths);
std::filesystem::path DownloadUpdateExe(
        const AppPaths& paths,
        const ReleaseAssetInfo& release,
        const std::function<void(std::uint64_t downloaded, std::uint64_t total)>& onProgress = {},
        HANDLE cancelEvent = nullptr
);
void StartDownloadedUpdate(
        const AppPaths& paths,
        const std::filesystem::path& downloadedExe
);
}
