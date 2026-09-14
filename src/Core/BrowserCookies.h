#pragma once

#include "AppPaths.h"
#include <windows.h>
#include <filesystem>
#include <string>
class Logger;

void LogCookieFileLoaded(Logger* logger, const std::filesystem::path& file,
    const std::wstring& source, const std::wstring& browser = {});
void LogBrowserCookieExtraction(Logger* logger, const std::wstring& line);

bool SupportsBrowserLogin(const std::wstring& browser);
std::filesystem::path ManagedBrowserCookiesPath(
    const std::filesystem::path& ytDlpExe, const std::wstring& browser);
void ConnectBrowserCookies(const AppPaths& paths, const std::wstring& browser,
    const std::filesystem::path& browserExe, HANDLE cancelEvent, Logger* logger = nullptr);
