#include "UiActions.h"

#include <algorithm>
#include <cwctype>
#include <system_error>

namespace {

std::wstring Lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

bool IsTranscriptTextPath(const std::filesystem::path& path) {
    return Lowercase(path.extension().wstring()) == L".txt";
}

} // namespace

DownloadAttemptAction ResolveDownloadAttempt(bool ytDlpReady) {
    return ytDlpReady ? DownloadAttemptAction::Enqueue : DownloadAttemptAction::ShowYtDlpNotReady;
}

WhisperUtilityStatusText BuildWhisperUtilityStatusText(
    bool executableAvailable,
    const std::filesystem::path& executablePath,
    bool modelAvailable,
    const std::filesystem::path& modelPath
) {
    WhisperUtilityStatusText text;
    text.executableText = executableAvailable && !executablePath.empty()
        ? L"Найден: " + executablePath.wstring()
        : L"Не найден";
    text.modelText = modelAvailable && !modelPath.empty()
        ? L"Модель: " + modelPath.wstring()
        : L"Модель не скачана";
    return text;
}

std::filesystem::path FindTranscriptTextPath(const std::vector<std::filesystem::path>& outputFiles) {
    std::error_code ec;
    for (auto it = outputFiles.rbegin(); it != outputFiles.rend(); ++it) {
        if (!IsTranscriptTextPath(*it)) {
            continue;
        }
        if (std::filesystem::is_regular_file(*it, ec)) {
            return *it;
        }
        ec.clear();
    }
    return {};
}

void PasteReplacingEditText(HWND editControl) {
    if (!IsWindow(editControl)) {
        return;
    }

    SetFocus(editControl);
    SendMessageW(editControl, EM_SETSEL, 0, -1);
    SendMessageW(editControl, WM_PASTE, 0, 0);
}

void RestoreModalOwner(HWND owner, bool ownerWasEnabled) {
    if (!ownerWasEnabled || !IsWindow(owner)) {
        return;
    }

    EnableWindow(owner, TRUE);
    if (IsIconic(owner)) {
        ShowWindow(owner, SW_RESTORE);
    }
    SetActiveWindow(owner);
}
