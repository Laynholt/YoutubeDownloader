#include "BrowserCookies.h"
#include "Config.h"
#include "ProcessRunner.h"
#include "Logger.h"
#include <algorithm>
#include <sstream>
#include <fstream>
#include <stdexcept>

void LogCookieFileLoaded(Logger* logger, const std::filesystem::path& file,
    const std::wstring& source, const std::wstring& browser) {
    if (!logger) { return; }
    std::ifstream input(file, std::ios::binary);
    std::string line;
    std::uint64_t count = 0;
    bool header = false;
    while (std::getline(input, line)) {
        if (line.starts_with("# Netscape HTTP Cookie File") || line.starts_with("# HTTP Cookie File")) { header = true; }
        if (line.empty() || (line.front() == '#' && !line.starts_with("#HttpOnly_"))) { continue; }
        if (std::count(line.begin(), line.end(), '\t') == 6) { ++count; }
    }
    const auto description = L"source=" + source + (browser.empty() ? L"" : L" browser=" + browser);
    if (header && count > 0 && !input.bad()) {
        logger->Info(L"Cookies loaded successfully: " + description + L" count=" + std::to_wstring(count));
    } else {
        logger->Error(L"Cookies file is unreadable, empty or invalid: " + description);
    }
}

void LogBrowserCookieExtraction(Logger* logger, const std::wstring& line) {
    if (!logger || !line.starts_with(L"Extracted ")) { return; }
    std::wistringstream input(line);
    std::wstring extracted, count, cookies, from, browser;
    input >> extracted >> count >> cookies >> from >> browser;
    browser = NormalizeCookieBrowser(browser);
    if (count.empty() || count.find_first_not_of(L"0123456789") != std::wstring::npos ||
        cookies != L"cookies" || from != L"from" || browser.empty()) { return; }
    logger->Info((count == L"0" ? L"No cookies extracted: " : L"Cookies loaded successfully: ") +
        std::wstring(L"source=browser browser=") + browser + L" count=" + count);
}

bool SupportsBrowserLogin(const std::wstring& browser) {
    return browser == L"edge" || browser == L"chrome" || browser == L"brave" ||
        browser == L"vivaldi" || browser == L"opera";
}

std::filesystem::path ManagedBrowserCookiesPath(
    const std::filesystem::path& ytDlpExe, const std::wstring& browser) {
    if (ytDlpExe.empty() || !SupportsBrowserLogin(browser)) {
        return {};
    }
    return AppPaths(ytDlpExe.parent_path().parent_path().parent_path()).stuffDir() /
        L"cookies" / (browser + L".txt");
}

void ConnectBrowserCookies(const AppPaths& paths, const std::wstring& browser,
    const std::filesystem::path& browserExe, HANDLE cancelEvent, Logger* logger) {
    if (!SupportsBrowserLogin(browser)) {
        throw std::runtime_error("dialog.cookies_browser_required");
    }
    wchar_t systemDirectory[MAX_PATH] = {};
    if (!GetSystemDirectoryW(systemDirectory, MAX_PATH)) {
        throw std::runtime_error("dialog.cookies_login_failed");
    }
    const auto cookieFile = ManagedBrowserCookiesPath(paths.ytDlpExePath(), browser);
    const auto scriptPath = paths.stuffDir() / L"browser-login.ps1";
    const auto resource = FindResourceW(nullptr, L"YTD_BROWSER_LOGIN", RT_RCDATA);
    const auto resourceData = resource ? LoadResource(nullptr, resource) : nullptr;
    const auto data = resourceData ? LockResource(resourceData) : nullptr;
    const auto size = resource ? SizeofResource(nullptr, resource) : 0;
    if (!data || size == 0) {
        throw std::runtime_error("dialog.cookies_login_failed");
    }
    std::filesystem::create_directories(paths.stuffDir());
    std::ofstream script(scriptPath, std::ios::binary | std::ios::trunc);
    script.write(static_cast<const char*>(data), size);
    script.close();
    if (!script) {
        throw std::runtime_error("dialog.cookies_login_failed");
    }
    ProcessRunOptions options;
    options.executable = std::filesystem::path(systemDirectory) / L"WindowsPowerShell/v1.0/powershell.exe";
    options.arguments = {L"-NoProfile", L"-NonInteractive", L"-ExecutionPolicy", L"Bypass", L"-File",
        scriptPath.wstring(), L"-BrowserExe", browserExe.wstring(),
        L"-Profile", (cookieFile.parent_path() / (browser + L"-profile")).wstring(),
        L"-OutputFile", cookieFile.wstring()};
    options.cancelEvent = cancelEvent;
    options.timeoutMs = 11 * 60 * 1000;
    const auto result = ProcessRunner::Run(options);
    if (result.canceled) {
        throw std::runtime_error("dialog.cookies_login_canceled");
    }
    if (result.exitCode != 0) {
        if (result.stderrText.find(L"dialog.cookies_login_closed") != std::wstring::npos) {
            throw std::runtime_error("dialog.cookies_login_closed");
        }
        if (result.timedOut || result.stderrText.find(L"dialog.cookies_login_timeout") != std::wstring::npos) {
            throw std::runtime_error("dialog.cookies_login_timeout");
        }
        throw std::runtime_error("dialog.cookies_login_failed");
    }
    LogCookieFileLoaded(logger, cookieFile, L"browser-login", browser);
}
