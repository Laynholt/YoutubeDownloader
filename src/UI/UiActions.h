#pragma once

#include <windows.h>

#include "Config.h"
#include "TranscriptionClient.h"
#include "VoiceOverTranslationClient.h"

#include <cstdint>
#include <filesystem>
#include <set>
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

enum class ProgressTaskKind {
    CookieLogin,
    FfmpegInstall,
    WhisperInstall,
    WhisperModelDownload,
    VotInstall,
    AppUpdate
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
bool ShouldStartPreviewFetchForText(const std::wstring& text);
struct LogSelection {
    std::set<int> rows;
    int anchor = -1;
    int focused = -1;
};
void SelectLogRow(LogSelection& selection, int row, int count, bool control, bool shift, bool contextMenu = false);
std::wstring SelectedLogText(const std::vector<std::wstring>& lines, const LogSelection& selection);
double PingPongProgressPhase(std::uint64_t elapsedMs, std::uint64_t periodMs);
struct DownloadTaskSnapshot;
struct DownloadStatistics {
    std::uint64_t totalBytes = 0;
    std::uint64_t speedBytesPerSecond = 0;
    std::uint64_t etaSeconds = 0;
    double smoothedTotal = 0;
    double smoothedSpeed = 0;
    double smoothedEta = 0;
    std::uint64_t lastTick = 0;
    std::wstring track;
    bool active = false;
};
void UpdateDownloadStatistics(DownloadStatistics& statistics, const DownloadTaskSnapshot& task, std::uint64_t nowMs);
struct DownloadProgressAnimation {
    double percent = 0.0;
    std::uint64_t lastTick = 0;
    std::wstring mediaKind;
    bool downloading = false;
    DownloadStatistics statistics;
};
double UpdateDownloadProgressAnimation(
    DownloadProgressAnimation& animation, const DownloadTaskSnapshot& task, std::uint64_t nowMs);
std::vector<QueueTaskActionItem> BuildQueueTaskActions(const QueueTaskActionInput& input);
ToolReadinessDialogContent BuildToolReadinessDialogContent(ToolReadinessIssue issue);
std::wstring VoiceOverFfmpegModeDisplayText(VoiceOverFfmpegMode mode);
std::wstring SubtitleFfmpegModeDisplayText(SubtitleFfmpegMode mode);
std::wstring OpenDownloadFolderButtonText();
std::wstring TranslationSettingsCollapsedIcon();
std::wstring ToolSetupButtonText();
std::wstring WhisperBackendStatusText(WhisperBackend configuredBackend, WhisperBackend resolvedBackend, bool cudaAvailable);
std::wstring WhisperInstallButtonText(WhisperBackend configuredBackend, bool cudaAvailable, bool backendInstalled);
bool IsWhisperInstallTargetInstalled(WhisperBackend installBackend, bool cpuInstalled, bool cudaInstalled);
std::wstring FfmpegGatedOptionTooltip(const std::wstring& actionText);
std::wstring LocalizedToolErrorText(const std::string& message);
std::wstring ProgressTaskFailureMessage(ProgressTaskKind kind);
std::wstring ProgressTaskUnknownErrorMessage(ProgressTaskKind kind);
std::wstring ProgressTaskSuccessMessage(ProgressTaskKind kind, bool whisperModelReady);
std::wstring ProgressDoneButtonText(ProgressTaskKind kind, bool success, bool whisperModelReady);
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
    bool cpuSelfTestPassed,
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
bool ShouldFallbackWhisperCudaInstallToCpu(
    WhisperBackend installBackend,
    const std::string& installError,
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
SubtitleFfmpegMode EffectiveSubtitleFfmpegModeForMedia(
    SubtitleFfmpegMode mode,
    const std::wstring& mediaKind,
    const std::filesystem::path& mediaPath,
    const std::wstring& quality
);
VoiceOverFfmpegMode EffectiveVoiceOverFfmpegModeForMedia(
    VoiceOverFfmpegMode mode,
    const std::wstring& mediaKind,
    const std::filesystem::path& mediaPath,
    const std::wstring& quality
);
bool ShouldBlockVoiceOverTranslationForDuration(std::uint64_t durationSeconds);
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
bool HasOwnWindowVisibleStyle(HWND window);
RECT CenteredMainInputEditRect(const RECT& frameRect);
int ClampSettingsScrollOffset(int scrollY, int contentHeight, int viewportHeight);
int SettingsScrollOffsetAfterWheel(
    int scrollY,
    int contentHeight,
    int viewportHeight,
    int wheelDelta
);
