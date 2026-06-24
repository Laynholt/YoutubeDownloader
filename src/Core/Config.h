#pragma once

#include "AppPaths.h"

#include <filesystem>
#include <string>

struct AppConfig {
    std::filesystem::path downloadDir;
    std::filesystem::path cookiesPath;
    std::filesystem::path ffmpegPath;
    std::filesystem::path whisperPath;
    std::filesystem::path whisperModelPath;
    std::wstring quality = L"max";
    std::wstring container = L"auto";
    std::wstring whisperLanguage = L"auto";
    int maxParallelDownloads = 3;
    bool autoUpdateApp = true;
    bool transcribeAfterDownload = false;
    std::wstring lastYtDlpCheckAt;
    std::wstring lastYtDlpVersion;
};

class ConfigStore {
public:
    static AppConfig Load(const AppPaths& paths);
    static void Save(const AppPaths& paths, const AppConfig& config);

private:
    static AppConfig Defaults();
};
