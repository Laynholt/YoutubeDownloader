#pragma once

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

enum class DownloadAttemptAction { Enqueue, ShowYtDlpNotReady };

struct WhisperUtilityStatusText {
    std::wstring executableText;
    std::wstring modelText;
};

DownloadAttemptAction ResolveDownloadAttempt(bool ytDlpReady);
WhisperUtilityStatusText BuildWhisperUtilityStatusText(
    bool executableAvailable,
    const std::filesystem::path& executablePath,
    bool modelAvailable,
    const std::filesystem::path& modelPath
);
std::filesystem::path FindTranscriptTextPath(const std::vector<std::filesystem::path>& outputFiles);
void PasteReplacingEditText(HWND editControl);
void RestoreModalOwner(HWND owner, bool ownerWasEnabled);
