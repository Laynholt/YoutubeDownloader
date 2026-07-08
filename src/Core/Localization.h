#pragma once

#include "AppPaths.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct UiLanguage {
    std::wstring id;
    std::wstring name;
    std::filesystem::path path;
};

class Localization {
public:
    Localization();

    static std::vector<UiLanguage> AvailableLanguages(const AppPaths& paths);
    static Localization Load(const AppPaths& paths, const std::wstring& languageId);
    static void SetActive(Localization localization);
    static std::wstring UiText(std::wstring_view text);

    const std::wstring& currentLanguageId() const;
    std::wstring Text(std::wstring_view key) const;
    std::wstring Format(std::wstring_view key, const std::unordered_map<std::wstring, std::wstring>& values) const;

private:
    std::wstring m_currentLanguageId = L"ru";
    std::unordered_map<std::wstring, std::wstring> m_strings;
};
