#include "Localization.h"

#include "BackendText.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <set>
#include <vector>

namespace {

std::wstring NormalizeLanguageId(const std::wstring& value) {
    std::wstring out;
    out.reserve(value.size());
    for (wchar_t ch : value) {
        if (ch >= L'A' && ch <= L'Z') {
            out.push_back(static_cast<wchar_t>(ch - L'A' + L'a'));
        } else if ((ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') || ch == L'_' || ch == L'-') {
            out.push_back(ch);
        } else {
            return {};
        }
    }
    return out.size() <= 32 ? out : std::wstring{};
}

std::unordered_map<std::wstring, std::wstring> RussianStrings() {
    return {
        {L"app.title", L"YouTube Downloader"},
        {L"settings.language.title", L"Язык интерфейса"},
        {L"settings.language.description", L"Выбор языка приложения."},
        {L"settings.language.restart", L"Язык применится после перезапуска."},
        {L"settings.language.russian", L"Русский"}
    };
}

bool HasCyrillic(std::wstring_view text) {
    return std::any_of(text.begin(), text.end(), [](wchar_t ch) {
        return (ch >= L'А' && ch <= L'я') || ch == L'Ё' || ch == L'ё';
    });
}

void ReplaceAll(std::wstring& text, const std::wstring& from, const std::wstring& to) {
    if (from.empty() || from == to) {
        return;
    }

    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::wstring::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

bool ReadLanguageFile(const std::filesystem::path& path, UiLanguage* language, std::unordered_map<std::wstring, std::wstring>* strings) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }

    try {
        const nlohmann::json root = nlohmann::json::parse(in, nullptr, true, true);
        if (!root.is_object()) {
            return false;
        }
        if (root.contains("version") && (!root["version"].is_number_integer() || root["version"].get<int>() != 1)) {
            return false;
        }
        if (!root.contains("id") || !root["id"].is_string() ||
            !root.contains("name") || !root["name"].is_string() ||
            !root.contains("strings") || !root["strings"].is_object()) {
            return false;
        }

        const std::wstring id = NormalizeLanguageId(Utf8ToWide(root["id"].get<std::string>()));
        const std::wstring name = Utf8ToWide(root["name"].get<std::string>());
        if (id.empty() || name.empty() || id == L"ru") {
            return false;
        }

        std::unordered_map<std::wstring, std::wstring> loaded;
        for (const auto& item : root["strings"].items()) {
            if (!item.value().is_string()) {
                return false;
            }
            loaded[Utf8ToWide(item.key())] = Utf8ToWide(item.value().get<std::string>());
        }

        if (language) {
            language->id = id;
            language->name = name;
            language->path = path;
        }
        if (strings) {
            *strings = std::move(loaded);
        }
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

std::mutex g_activeLocalizationMutex;
Localization g_activeLocalization;

std::vector<UiLanguage> Localization::AvailableLanguages(const AppPaths& paths) {
    std::vector<UiLanguage> languages = {{L"ru", L"Русский", {}}};
    std::set<std::wstring> seen = {L"ru"};

    std::error_code ec;
    if (!std::filesystem::is_directory(paths.languagesDir(), ec)) {
        return languages;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(paths.languagesDir(), ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec) || entry.path().extension() != L".json") {
            continue;
        }
        UiLanguage language;
        if (!ReadLanguageFile(entry.path(), &language, nullptr) || seen.contains(language.id)) {
            continue;
        }
        seen.insert(language.id);
        languages.push_back(std::move(language));
    }

    return languages;
}

Localization Localization::Load(const AppPaths& paths, const std::wstring& languageId) {
    Localization localization;
    localization.m_strings = RussianStrings();

    const std::wstring wanted = NormalizeLanguageId(languageId.empty() ? L"ru" : languageId);
    if (wanted.empty() || wanted == L"ru") {
        return localization;
    }

    for (const UiLanguage& language : AvailableLanguages(paths)) {
        if (language.id != wanted || language.path.empty()) {
            continue;
        }

        std::unordered_map<std::wstring, std::wstring> external;
        if (!ReadLanguageFile(language.path, nullptr, &external)) {
            return localization;
        }
        for (auto& [key, value] : external) {
            localization.m_strings[std::move(key)] = std::move(value);
        }
        localization.m_currentLanguageId = wanted;
        return localization;
    }

    return localization;
}

const std::wstring& Localization::currentLanguageId() const {
    return m_currentLanguageId;
}

void Localization::SetActive(Localization localization) {
    std::lock_guard lock(g_activeLocalizationMutex);
    g_activeLocalization = std::move(localization);
}

std::wstring Localization::UiText(std::wstring_view text) {
    std::lock_guard lock(g_activeLocalizationMutex);
    return g_activeLocalization.Text(text);
}

std::wstring Localization::Text(std::wstring_view key) const {
    const auto it = m_strings.find(std::wstring(key));
    if (it != m_strings.end()) {
        return it->second;
    }
    if (!HasCyrillic(key)) {
        return std::wstring(key);
    }

    std::wstring translated(key);
    std::vector<const std::pair<const std::wstring, std::wstring>*> fragments;
    fragments.reserve(m_strings.size());
    for (const auto& item : m_strings) {
        if (item.first.size() >= 4 && HasCyrillic(item.first)) {
            fragments.push_back(&item);
        }
    }
    std::sort(fragments.begin(), fragments.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->first.size() > rhs->first.size();
    });
    for (const auto* item : fragments) {
        ReplaceAll(translated, item->first, item->second);
    }
    return translated;
}

std::wstring Localization::Format(std::wstring_view key, const std::unordered_map<std::wstring, std::wstring>& values) const {
    std::wstring text = Text(key);
    for (const auto& [name, value] : values) {
        const std::wstring needle = L"{" + name + L"}";
        size_t pos = 0;
        while ((pos = text.find(needle, pos)) != std::wstring::npos) {
            text.replace(pos, needle.size(), value);
            pos += value.size();
        }
    }
    return text;
}
