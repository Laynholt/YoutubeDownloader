#include "BrowserCookies.h"
#include "Config.h"
#include "ProcessRunner.h"
#include "Logger.h"
#include <wincrypt.h>
#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <regex>
#include <sstream>
#include <fstream>
#include <stdexcept>

namespace {

// Must match the entropy used by stuff/browser-login.ps1.
constexpr char kCookieEntropy[] = "YoutubeDownloader cookies v1";

std::mutex g_cookieMutex;
struct CachedCookies {
    std::filesystem::file_time_type stamp;
    std::string text;
};
// Decrypted sessions stay in memory while the app runs.
std::map<std::filesystem::path, CachedCookies> g_cookieCache;

std::string Dpapi(const std::string& input, bool protect) {
    DATA_BLOB in{static_cast<DWORD>(input.size()), reinterpret_cast<BYTE*>(const_cast<char*>(input.data()))};
    DATA_BLOB entropy{sizeof(kCookieEntropy) - 1, reinterpret_cast<BYTE*>(const_cast<char*>(kCookieEntropy))};
    DATA_BLOB out{};
    const BOOL ok = protect
        ? CryptProtectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)
        : CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
    if (!ok) {
        throw std::runtime_error(protect ? "cookie encryption failed" : "cookie decryption failed");
    }
    std::string result(reinterpret_cast<const char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}

std::string ReadBinaryFile(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) { throw std::runtime_error("cookie file is unreadable"); }
    std::ostringstream data;
    data << input.rdbuf();
    return data.str();
}

void WriteBinaryFile(const std::filesystem::path& file, const std::string& data) {
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    output.close();
    if (!output) { throw std::runtime_error("cookie file is not writable"); }
}

void LogCookieText(Logger* logger, const std::string& text, bool readable,
    const std::wstring& source, const std::wstring& browser) {
    if (!logger) { return; }
    std::istringstream input(text);
    std::string line;
    std::uint64_t count = 0;
    bool header = false;
    while (std::getline(input, line)) {
        if (line.starts_with("# Netscape HTTP Cookie File") || line.starts_with("# HTTP Cookie File")) { header = true; }
        if (line.empty() || (line.front() == '#' && !line.starts_with("#HttpOnly_"))) { continue; }
        if (std::count(line.begin(), line.end(), '\t') == 6) { ++count; }
    }
    const auto description = L"source=" + source + (browser.empty() ? L"" : L" browser=" + browser);
    if (readable && header && count > 0) {
        logger->Info(L"Cookies loaded successfully: " + description + L" count=" + std::to_wstring(count));
    } else {
        logger->Error(L"Cookies file is unreadable, empty or invalid: " + description);
    }
}

} // namespace

void LogCookieFileLoaded(Logger* logger, const std::filesystem::path& file,
    const std::wstring& source, const std::wstring& browser) {
    if (!logger) { return; }
    std::string text;
    bool readable = true;
    try { text = ReadBinaryFile(file); } catch (...) { readable = false; }
    LogCookieText(logger, text, readable, source, browser);
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
        L"cookies" / (browser + L".cookies");
}

std::string LoadManagedCookies(const std::filesystem::path& file) {
    std::lock_guard lock(g_cookieMutex);
    const auto stamp = std::filesystem::last_write_time(file);
    const auto cached = g_cookieCache.find(file);
    if (cached != g_cookieCache.end() && cached->second.stamp == stamp) {
        return cached->second.text;
    }
    std::string text = Dpapi(ReadBinaryFile(file), false);
    g_cookieCache[file] = {stamp, text};
    return text;
}

void SaveManagedCookies(const std::filesystem::path& file, const std::string& text) {
    std::lock_guard lock(g_cookieMutex);
    const auto temporary = std::filesystem::path(file.wstring() + L".tmp");
    WriteBinaryFile(temporary, Dpapi(text, true));
    std::filesystem::rename(temporary, file);
    g_cookieCache[file] = {std::filesystem::last_write_time(file), text};
}

bool HasManagedBrowserCookies(const std::filesystem::path& ytDlpExe, const std::wstring& browser) {
    const auto store = ManagedBrowserCookiesPath(ytDlpExe, browser);
    if (store.empty()) { return false; }
    std::error_code error;
    auto legacy = store;
    legacy.replace_extension(L".txt");
    if (std::filesystem::is_regular_file(legacy, error)) {
        try {
            if (!std::filesystem::is_regular_file(store, error)) {
                SaveManagedCookies(store, ReadBinaryFile(legacy));
            }
            std::filesystem::remove(legacy, error);
        } catch (...) {}
    }
    return std::filesystem::is_regular_file(store, error);
}

ManagedCookieLease::ManagedCookieLease(const std::wstring& source, const std::wstring& browser,
    const std::filesystem::path& ytDlpExe, Logger* logger) {
    const std::wstring normalized = NormalizeCookieBrowser(browser);
    if (NormalizeCookieSource(source) != L"browser" || !HasManagedBrowserCookies(ytDlpExe, normalized)) {
        return;
    }
    m_store = ManagedBrowserCookiesPath(ytDlpExe, normalized);
    const auto directory = m_store.parent_path();
    const std::wstring pid = std::to_wstring(GetCurrentProcessId());
    // Remove plaintext copies that a crashed earlier run of the app may have left behind.
    std::error_code error;
    const std::wregex leftover(LR"(^[a-z]+-(\d+)-\d+\.txt$)");
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        std::wsmatch match;
        const std::wstring name = entry.path().filename().wstring();
        if (std::regex_match(name, match, leftover) && match[1].str() != pid) {
            std::filesystem::remove(entry.path(), error);
        }
    }
    static std::atomic<unsigned> counter = 0;
    try {
        m_text = LoadManagedCookies(m_store);
        m_path = directory / (normalized + L"-" + pid + L"-" + std::to_wstring(++counter) + L".txt");
        WriteBinaryFile(m_path, m_text);
    } catch (...) {
        if (!m_path.empty()) { std::filesystem::remove(m_path, error); }
        m_path.clear();
        if (logger) {
            logger->Error(L"Saved cookies could not be decrypted: source=cache browser=" + normalized);
        }
    }
}

ManagedCookieLease::~ManagedCookieLease() {
    if (m_path.empty()) { return; }
    try {
        // yt-dlp writes refreshed cookies back to its cookie file; keep them, encrypted.
        const std::string updated = ReadBinaryFile(m_path);
        if (updated != m_text && updated.starts_with("# Netscape HTTP Cookie File")) {
            SaveManagedCookies(m_store, updated);
        }
    } catch (...) {}
    std::error_code error;
    std::filesystem::remove(m_path, error);
}

void ConnectBrowserCookies(const AppPaths& paths, const std::wstring& browser,
    const std::filesystem::path& browserExe, HANDLE cancelEvent, Logger* logger,
    const std::function<void(const std::wstring&)>& onStatus) {
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
    options.onStdoutLine = [&onStatus](const std::wstring& line) {
        if (onStatus && line.starts_with(L"dialog.cookies_")) {
            onStatus(line.substr(0, line.find_last_not_of(L" \r\n") + 1));
        }
    };
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
    std::string text;
    bool readable = true;
    try { text = LoadManagedCookies(cookieFile); } catch (...) { readable = false; }
    LogCookieText(logger, text, readable, L"browser-login", browser);
}
