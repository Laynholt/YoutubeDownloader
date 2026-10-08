#include "BackendText.h"
#include "Localization.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <vector>

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (size <= 0) {
        throw std::runtime_error("failed to convert wide string to UTF-8");
    }

    std::string out(static_cast<size_t>(size), '\0');
    const int written = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.c_str(),
        static_cast<int>(value.size()),
        out.data(),
        size,
        nullptr,
        nullptr
    );
    if (written != size) {
        throw std::runtime_error("failed to write UTF-8 string");
    }
    return out;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0
    );
    if (size <= 0) {
        throw std::runtime_error("failed to convert UTF-8 string to wide");
    }

    std::wstring out(static_cast<size_t>(size), L'\0');
    const int written = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.c_str(),
        static_cast<int>(value.size()),
        out.data(),
        size
    );
    if (written != size) {
        throw std::runtime_error("failed to write wide string");
    }
    return out;
}

std::string PathToUtf8(const std::filesystem::path& path) {
    return WideToUtf8(path.wstring());
}

std::filesystem::path PathFromUtf8(const std::string& value) {
    return std::filesystem::path(Utf8ToWide(value));
}

std::wstring FormatBytes(std::uint64_t bytes) {
    if (bytes == 0) {
        return {};
    }

    constexpr double kKiB = 1024.0;
    constexpr double kMiB = kKiB * 1024.0;
    constexpr double kGiB = kMiB * 1024.0;
    double value = static_cast<double>(bytes);
    const wchar_t* unit = L"B";
    if (value >= kGiB) {
        value /= kGiB;
        unit = L"GB";
    } else if (value >= kMiB) {
        value /= kMiB;
        unit = L"MB";
    } else if (value >= kKiB) {
        value /= kKiB;
        unit = L"KB";
    }

    std::wostringstream out;
    out.setf(std::ios::fixed);
    out.precision((value >= 10.0 || bytes < 1024) ? 0 : 1);
    out << value << L" " << unit;
    return out.str();
}

std::wstring FormatProgressBytes(std::uint64_t downloaded, std::uint64_t total) {
    if (total > 0) {
        const std::wstring downloadedText = downloaded > 0 ? FormatBytes(downloaded) : L"0 B";
        return downloadedText + L" / " + FormatBytes(total);
    }
    return FormatBytes(downloaded);
}

std::wstring FormatDuration(std::uint64_t seconds) {
    if (seconds == 0) {
        return {};
    }

    constexpr std::uint64_t kMinute = 60;
    constexpr std::uint64_t kHour = 60 * kMinute;
    constexpr std::uint64_t kDay = 24 * kHour;

    const std::uint64_t days = seconds / kDay;
    seconds %= kDay;
    const std::uint64_t hours = seconds / kHour;
    seconds %= kHour;
    const std::uint64_t minutes = seconds / kMinute;
    seconds %= kMinute;

    std::vector<std::wstring> parts;
    if (days > 0) {
        parts.push_back(std::to_wstring(days) + L"backend.d");
        if (hours > 0) {
            parts.push_back(std::to_wstring(hours) + L"backend.h");
        } else if (minutes > 0) {
            parts.push_back(std::to_wstring(minutes) + L"backend.min");
        }
    } else if (hours > 0) {
        parts.push_back(std::to_wstring(hours) + L"backend.h");
        if (minutes > 0) {
            parts.push_back(std::to_wstring(minutes) + L"backend.min");
        }
    } else if (minutes > 0) {
        parts.push_back(std::to_wstring(minutes) + L"backend.min");
        if (seconds > 0) {
            parts.push_back(std::to_wstring(seconds) + L"backend.s");
        }
    } else {
        parts.push_back(std::to_wstring(seconds) + L"backend.s");
    }

    std::wstring result;
    for (const std::wstring& part : parts) {
        if (!result.empty()) {
            result += L" ";
        }
        result += part;
    }
    return result;
}

int CalculateProgressPercent(std::uint64_t downloaded, std::uint64_t total) {
    if (total == 0) {
        return 0;
    }
    const double percent = (static_cast<double>(downloaded) * 100.0) / static_cast<double>(total);
    return std::clamp(static_cast<int>(percent), 0, 100);
}

namespace {

bool ContainsAny(std::wstring_view text, std::initializer_list<std::wstring_view> fragments) {
    return std::any_of(fragments.begin(), fragments.end(), [text](auto fragment) {
        return text.find(fragment) != std::wstring_view::npos;
    });
}

bool ContainsErrorCode(
    std::wstring_view text,
    std::initializer_list<std::wstring_view> labels,
    std::initializer_list<int> codes
) {
    for (auto label : labels) {
        size_t offset = 0;
        while ((offset = text.find(label, offset)) != std::wstring_view::npos) {
            offset += label.size();
            const auto start = text.find_first_not_of(L" :=\t", offset);
            if (start == std::wstring_view::npos) { break; }
            const auto end = text.find_first_not_of(L"0123456789", start);
            const auto number = text.substr(start, end == std::wstring_view::npos ? end : end - start);
            for (int code : codes) {
                if (number == std::to_wstring(code)) { return true; }
            }
        }
    }
    return false;
}

} // namespace

std::wstring ErrorSummary(std::wstring_view diagnostic) {
    std::wstring text(diagnostic);
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) {
        return ch >= L'A' && ch <= L'Z' ? static_cast<wchar_t>(ch - L'A' + L'a') : ch;
    });
    const auto systemCode = [&](std::initializer_list<int> codes) {
        return ContainsErrorCode(text, {L"win32 error", L"winerror", L"winsock error", L"winsocket error"}, codes);
    };
    const auto errnoCode = [&](std::initializer_list<int> codes) {
        return ContainsErrorCode(text, {L"errno"}, codes);
    };
    const auto httpCode = [&](std::initializer_list<int> codes) {
        return ContainsErrorCode(text, {L"http error", L"http status", L"http request failed with status", L"http"}, codes);
    };

    // ponytail: external tools return text; match known diagnostics until they expose structured errors.
    const wchar_t* key = nullptr;
    if (systemCode({12007, 11001, 11002, 11003, 11004}) || errnoCode({11001, 11002, 11003, 11004}) ||
        ContainsAny(text, {L"getaddrinfo failed", L"name or service not known", L"name resolution", L"could not resolve host", L"enotfound", L"eai_again"})) {
        key = L"errors.dns";
    } else if (systemCode({12002, 10060}) || errnoCode({110, 10060}) || httpCode({408, 504}) ||
        ContainsAny(text, {L"timed out", L"timeout", L"etimedout"})) {
        key = L"errors.timeout";
    } else if (systemCode({12030, 10052, 10053, 10054}) || errnoCode({32, 104, 10052, 10053, 10054}) ||
        ContainsAny(text, {L"connection reset", L"connection aborted", L"remote end closed", L"broken pipe", L"econnreset", L"econnaborted"})) {
        key = L"errors.connection_lost";
    } else if (systemCode({12029, 10050, 10051, 10061, 10064, 10065}) || errnoCode({101, 111, 113, 10050, 10051, 10061, 10064, 10065}) ||
        ContainsAny(text, {L"network is unreachable", L"network is down", L"connection refused", L"failed to connect", L"econnrefused", L"enetunreach", L"ehostunreach"})) {
        key = L"errors.connection";
    } else if (systemCode({12037, 12038, 12045, 12057, 12157, 12169, 12170, 12175, 12179}) ||
        ContainsAny(text, {L"certificate verify failed", L"certificate_verify_failed", L"ssl error", L"tls handshake", L"unable to verify", L"self-signed certificate"})) {
        key = L"errors.tls";
    } else if (httpCode({401}) || ContainsAny(text, {L"sign in", L"login required", L"authentication required", L"cookies are no longer valid"})) {
        key = L"errors.authentication";
    } else if (httpCode({407})) {
        key = L"errors.proxy_authentication";
    } else if (httpCode({403})) {
        key = L"errors.forbidden";
    } else if (httpCode({404, 410})) {
        key = L"errors.not_found";
    } else if (httpCode({429}) || ContainsAny(text, {L"too many requests", L"rate limit"})) {
        key = L"errors.rate_limit";
    } else if (httpCode({500, 501, 502, 503, 505, 506, 507, 508, 510, 511})) {
        key = L"errors.server";
    } else if (httpCode({400, 405, 406, 409, 422})) {
        key = L"errors.request";
    } else if (ContainsAny(text, {L"video unavailable", L"private video", L"video is private", L"has been removed", L"not available in your country", L"geo-restricted", L"members-only"})) {
        key = L"errors.media_unavailable";
    } else if (ContainsAny(text, {L"requested format is not available", L"no video formats found"})) {
        key = L"errors.format_unavailable";
    } else if (systemCode({112}) || errnoCode({28}) || ContainsAny(text, {L"no space left", L"disk full", L"not enough space", L"enospc"})) {
        key = L"errors.disk_full";
    } else if (systemCode({5, 32, 33}) || errnoCode({1, 13}) || ContainsAny(text, {L"permission denied", L"access is denied", L"being used by another process", L"eacces", L"eperm"})) {
        key = L"errors.file_access";
    } else if (ContainsAny(text, {L"failed to create process", L"failed to resume process", L"process executable is empty", L"failed to assign process", L"failed to create process pipes", L"failed to create process job", L"failed to configure process job"})) {
        key = L"errors.process_start";
    } else if (systemCode({2, 3}) || errnoCode({2}) || ContainsAny(text, {L"no such file or directory", L"file not found", L"enoent"})) {
        key = L"errors.file_missing";
    } else if (systemCode({12005, 12006}) || ContainsAny(text, {L"invalid url", L"unsupported url", L"url host is missing"})) {
        key = L"errors.invalid_url";
    } else if (ContainsAny(text, {L"out of memory", L"not enough memory", L"cuda out of memory"})) {
        key = L"errors.memory";
    } else if (errnoCode({5}) || ContainsAny(text, {L"failed to write", L"failed to open", L"failed to create download directory", L"failed to create config directory", L"failed to create queue store directory", L"failed to replace", L"failed to flush", L"failed to close downloaded"})) {
        key = L"errors.file_io";
    } else if (ContainsAny(text, {L"parse error", L"parse_error", L"invalid json", L"invalid data found", L"invalid server response", L"returned no preview metadata"})) {
        key = L"errors.invalid_data";
    }
    if (key) { return Localization::UiText(key); }
    const auto translated = Localization::UiText(diagnostic);
    return translated != diagnostic ? translated : Localization::UiText(L"errors.operation_failed");
}

std::wstring ErrorDetails(std::wstring_view diagnostic) {
    const auto summary = ErrorSummary(diagnostic);
    if (diagnostic.starts_with(summary)) { return std::wstring(diagnostic); }
    if (diagnostic.empty() || summary == Localization::UiText(diagnostic)) { return summary; }
    return summary + L" (" + std::wstring(diagnostic) + L")";
}
