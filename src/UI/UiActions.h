#pragma once

#include <windows.h>

#include "Config.h"
#include "TranscriptionClient.h"
#include "VoiceOverTranslationClient.h"

#include <filesystem>
#include <string>
#include <vector>

enum class DownloadAttemptAction { Enqueue, ShowYtDlpNotReady, ShowPreviewLoading };

enum class QueueTaskAction {
    None,
    Transcribe,
    Translate,
    Retry,
    Clear,
    CancelPostProcessing
};

enum class ToolReadinessIssue {
    MissingFfmpegForWhisper,
    MissingWhisperExe,
    MissingWhisperModel,
    MissingWhisperSetup,
    MissingWhisperCuda,
    MissingVotExe
};

enum class WhisperCudaReadinessAction {
    UseResolvedBackend,
    FallbackToCpu,
    BlockCuda
};

struct ToolReadinessDialogContent {
    std::wstring title;
    std::wstring message;
    std::wstring openToolsText;
    std::wstring cancelText;
};

struct QueueTaskActionInput {
    bool completed = false;
    bool failedOrCanceled = false;
    bool hasOutputFile = false;
    bool hasSourceUrl = false;
    bool postProcessingBusy = false;
};

struct QueueTaskActionItem {
    QueueTaskAction action = QueueTaskAction::None;
    std::wstring text;
};

enum EditContextMenuCommand : UINT {
    IdEditMenuUndo = 1,
    IdEditMenuCut = 2,
    IdEditMenuCopy = 3,
    IdEditMenuPaste = 4,
    IdEditMenuDelete = 5,
    IdEditMenuSelectAll = 6
};

struct EditContextMenuItem {
    UINT id = 0;
    std::wstring text;
    bool separator = false;
    bool enabled = true;
};

DownloadAttemptAction ResolveDownloadAttempt(bool ytDlpReady, bool previewLoading);
std::vector<QueueTaskActionItem> BuildQueueTaskActions(const QueueTaskActionInput& input);
ToolReadinessDialogContent BuildToolReadinessDialogContent(ToolReadinessIssue issue);
std::wstring VoiceOverFfmpegModeDisplayText(VoiceOverFfmpegMode mode);
std::wstring SubtitleFfmpegModeDisplayText(SubtitleFfmpegMode mode);
std::wstring OpenDownloadFolderButtonText();
std::wstring TranslationSettingsCollapsedIcon();
std::wstring ToolSetupButtonText();
std::wstring WhisperBackendStatusText(WhisperBackend configuredBackend, WhisperBackend resolvedBackend, bool cudaAvailable);
std::wstring WhisperInstallButtonText(WhisperBackend configuredBackend, bool cudaAvailable, bool backendInstalled);
std::wstring FfmpegGatedOptionTooltip(const std::wstring& actionText);
std::wstring LocalizedToolErrorText(const std::string& message);
std::wstring PostProcessingQueueStatusText(QueueTaskAction action);
bool ShouldBlockWhisperCudaBackend(
    WhisperBackend configuredBackend,
    WhisperBackend resolvedBackend,
    bool cudaRuntimeAvailable
);
WhisperCudaReadinessAction ResolveWhisperCudaReadinessAction(
    WhisperBackend configuredBackend,
    WhisperBackend resolvedBackend,
    bool cudaSelfTestPassed,
    bool cpuBackendInstalled,
    bool modelReady
);
bool ShouldRetryWhisperCudaFailureWithCpu(
    WhisperBackend configuredBackend,
    WhisperBackend resolvedBackend,
    bool transcriptionSucceeded,
    bool transcriptionCanceled,
    bool cpuBackendInstalled,
    bool cpuSelfTestPassed,
    bool modelReady
);
std::vector<std::filesystem::path> BuildTranscriptionAffectedFiles(
    const TranscriptionPaths& paths,
    SubtitleFfmpegMode subtitleMode
);
std::vector<std::filesystem::path> BuildVoiceOverAffectedFiles(
    const VoiceOverTranslationPaths& paths,
    VoiceOverFfmpegMode ffmpegMode
);
std::vector<std::filesystem::path> FindUnapprovedAffectedFiles(
    const std::vector<std::filesystem::path>& currentAffectedFiles,
    const std::vector<std::filesystem::path>& approvedAffectedFiles
);
std::vector<EditContextMenuItem> BuildEditContextMenuItems(
    bool canUndo,
    bool hasSelection,
    bool canPaste,
    bool hasText
);
int EditContextMenuHeight(const std::vector<EditContextMenuItem>& items);
UINT HitTestEditContextMenuItem(const std::vector<EditContextMenuItem>& items, int y);
void PasteReplacingEditText(HWND editControl);
void CopyTextToClipboard(HWND owner, const std::wstring& text);
void RestoreModalOwner(HWND owner, bool ownerWasEnabled);
