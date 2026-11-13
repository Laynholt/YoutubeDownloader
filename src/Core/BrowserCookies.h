#pragma once

#include "AppPaths.h"
#include <windows.h>
#include <filesystem>
#include <functional>
#include <string>
class Logger;

void LogCookieFileLoaded(Logger* logger, const std::filesystem::path& file,
    const std::wstring& source, const std::wstring& browser = {});
void LogBrowserCookieExtraction(Logger* logger, const std::wstring& line);

bool SupportsBrowserLogin(const std::wstring& browser);
std::filesystem::path ManagedBrowserCookiesPath(
    const std::filesystem::path& ytDlpExe, const std::wstring& browser);
void ConnectBrowserCookies(const AppPaths& paths, const std::wstring& browser,
    const std::filesystem::path& browserExe, HANDLE cancelEvent, Logger* logger = nullptr,
    const std::function<void(const std::wstring&)>& onStatus = {});

// The managed session file is encrypted with DPAPI for the current Windows user.
std::string LoadManagedCookies(const std::filesystem::path& file);
void SaveManagedCookies(const std::filesystem::path& file, const std::string& text);
// Encrypts a plaintext session left by older versions, then reports whether a saved session exists.
bool HasManagedBrowserCookies(const std::filesystem::path& ytDlpExe, const std::wstring& browser);

// Plaintext copy of the saved session for one yt-dlp run. It exists on disk only while this
// object lives; cookies yt-dlp updates are encrypted back into the session file afterwards.
class ManagedCookieLease {
public:
    ManagedCookieLease(const std::wstring& source, const std::wstring& browser,
        const std::filesystem::path& ytDlpExe, Logger* logger = nullptr);
    ~ManagedCookieLease();
    ManagedCookieLease(const ManagedCookieLease&) = delete;
    ManagedCookieLease& operator=(const ManagedCookieLease&) = delete;
    const std::filesystem::path& path() const { return m_path; }

private:
    std::filesystem::path m_store;
    std::filesystem::path m_path;
    std::string m_text;
};
