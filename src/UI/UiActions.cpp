#include "UiActions.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cwctype>
#include <system_error>

namespace {

constexpr int kEditMenuOuterPadding = 2;
constexpr int kEditMenuItemHeight = 34;
constexpr int kEditMenuSeparatorHeight = 10;

int EditMenuItemHeight(const EditContextMenuItem& item) {
    return item.separator ? kEditMenuSeparatorHeight : kEditMenuItemHeight;
}

std::wstring LowerCopy(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

bool IsAudioOnlyMedia(const std::wstring& mediaKind, const std::filesystem::path& mediaPath, const std::wstring& quality) {
    if (quality == L"audio") {
        return true;
    }
    const std::wstring extension = LowerCopy(mediaPath.extension().wstring());
    static constexpr std::array<std::wstring_view, 8> kVideoExtensions = {
        L".mp4",
        L".mkv",
        L".webm",
        L".mov",
        L".avi",
        L".m4v",
        L".mpg",
        L".mpeg"
    };
    if (std::ranges::find(kVideoExtensions, extension) != kVideoExtensions.end()) {
        return false;
    }
    static constexpr std::array<std::wstring_view, 10> kAudioExtensions = {
        L".mp3",
        L".m4a",
        L".opus",
        L".ogg",
        L".wav",
        L".flac",
        L".aac",
        L".wma",
        L".weba",
        L".mp2"
    };
    return std::ranges::find(kAudioExtensions, extension) != kAudioExtensions.end() || mediaKind == L"audio";
}

} // namespace

DownloadAttemptAction ResolveDownloadAttempt(bool ytDlpReady, bool previewLoading) {
    if (!ytDlpReady) {
        return DownloadAttemptAction::ShowYtDlpNotReady;
    }
    if (previewLoading) {
        return DownloadAttemptAction::ShowPreviewLoading;
    }
    return DownloadAttemptAction::Enqueue;
}

bool ShouldStartPreviewFetchForText(const std::wstring& text) {
    if (text.size() < 8) {
        return false;
    }
    return text.find_first_of(L"\r\n") == std::wstring::npos;
}

double PingPongProgressPhase(std::uint64_t elapsedMs, std::uint64_t periodMs) {
    if (periodMs == 0) {
        return 0.0;
    }
    const std::uint64_t position = elapsedMs % periodMs;
    const double half = static_cast<double>(periodMs) / 2.0;
    if (static_cast<double>(position) <= half) {
        return static_cast<double>(position) / half;
    }
    return (static_cast<double>(periodMs - position) / half);
}

std::vector<QueueTaskActionItem> BuildQueueTaskActions(const QueueTaskActionInput& input) {
    if (input.postProcessingBusy) {
        return {
            {QueueTaskAction::CancelPostProcessing, L"app.cancel"}
        };
    }

    if (input.completed) {
        std::vector<QueueTaskActionItem> actions;
        if (input.hasSourceUrl) {
            actions.push_back({QueueTaskAction::Transcribe, L"actions.transcribe"});
            actions.push_back({QueueTaskAction::Translate, L"actions.translate"});
        }
        actions.push_back({QueueTaskAction::Clear, L"app.close"});
        return actions;
    }

    if (input.failedOrCanceled) {
        return {
            {QueueTaskAction::Retry, L"actions.retry"},
            {QueueTaskAction::Clear, L"actions.clear"}
        };
    }

    return {};
}

ToolReadinessDialogContent BuildToolReadinessDialogContent(ToolReadinessIssue issue) {
    ToolReadinessDialogContent content;
    content.title = L"actions.tool_is_not_ready";
    content.openToolsText = L"dialog.open_tools";
    content.cancelText = L"dialog.cancel";

    switch (issue) {
    case ToolReadinessIssue::MissingFfmpegForWhisper:
        content.message =
            L"actions.whisper_transcription_requires_ffmpeg_the_application_mu"
            L"actions.open_tools_to_install_ffmpeg_or_choose_the_folder_with_f";
        break;
    case ToolReadinessIssue::MissingWhisperExe:
        content.message =
            L"actions.whisper_cli_exe_for_local_transcription_was_not_found"
            L"actions.open_tools_to_choose_the_whisper_cpp_folder_or_install_t";
        break;
    case ToolReadinessIssue::MissingWhisperModel:
        content.message =
            L"actions.the_selected_whisper_model_was_not_found_without_a_model"
            L"actions.open_tools_to_download_or_choose_a_whisper_model";
        break;
    case ToolReadinessIssue::MissingWhisperSetup:
        content.message =
            L"actions.whisper_transcription_requires_several_components_ffmpeg"
            L"actions.whisper_cli_exe_for_recognition_and_the_whisper_model"
            L"actions.open_tools_to_install_or_choose_the_missing_files";
        break;
    case ToolReadinessIssue::MissingWhisperCuda:
        content.message =
            L"actions.the_cuda_whisper_backend_is_selected_in_settings_but_cud"
            L"actions.open_tools_to_install_the_cuda_version_choose_cpu_mode_o";
        break;
    case ToolReadinessIssue::MissingVotExe:
        content.message =
            L"actions.vot_helper_exe_for_voice_over_translation_was_not_found"
            L"actions.open_tools_to_choose_the_vot_folder_or_install_the_tool";
        break;
    }

    return content;
}

std::wstring VoiceOverFfmpegModeDisplayText(VoiceOverFfmpegMode mode) {
    switch (mode) {
    case VoiceOverFfmpegMode::AudioTrack:
        return L"actions.audio_track";
    case VoiceOverFfmpegMode::Mix:
        return L"actions.mix";
    case VoiceOverFfmpegMode::Off:
    default:
        return L"dialog.off";
    }
}

std::wstring SubtitleFfmpegModeDisplayText(SubtitleFfmpegMode mode) {
    switch (mode) {
    case SubtitleFfmpegMode::SubtitleTrack:
        return L"actions.subtitle_track";
    case SubtitleFfmpegMode::BurnIn:
        return L"actions.burn_into_video";
    case SubtitleFfmpegMode::Off:
    default:
        return L"dialog.off";
    }
}

std::wstring OpenDownloadFolderButtonText() {
    return L"actions.open_folder";
}

std::wstring TranslationSettingsCollapsedIcon() {
    return L"♫";
}

std::wstring ToolSetupButtonText() {
    return L"actions.configure";
}

std::wstring WhisperBackendStatusText(WhisperBackend configuredBackend, WhisperBackend resolvedBackend, bool cudaAvailable) {
    if (configuredBackend == WhisperBackend::Cuda && (!cudaAvailable || resolvedBackend != WhisperBackend::Cuda)) {
        return L"actions.no_cuda";
    }
    if (resolvedBackend == WhisperBackend::Cuda) {
        return L"CUDA";
    }
    if (resolvedBackend == WhisperBackend::Custom) {
        return L"actions.selected";
    }
    return L"CPU";
}

std::wstring WhisperInstallButtonText(WhisperBackend configuredBackend, bool cudaAvailable, bool backendInstalled) {
    (void)configuredBackend;
    const bool installCuda = cudaAvailable;
    if (installCuda) {
        return backendInstalled ? L"actions.reinstall_cuda" : L"actions.install_cuda";
    }
    return backendInstalled ? L"actions.reinstall_cpu" : L"actions.install_cpu";
}

bool IsWhisperInstallTargetInstalled(WhisperBackend installBackend, bool cpuInstalled, bool cudaInstalled) {
    return installBackend == WhisperBackend::Cuda ? cudaInstalled : cpuInstalled;
}

std::wstring FfmpegGatedOptionTooltip(const std::wstring& actionText) {
    return actionText;
}

std::wstring LocalizedToolErrorText(const std::string& message) {
    if (message == "operation canceled") {
        return L"app.operation_canceled";
    }
    if (message == "media file is no longer available") {
        return L"actions.source_media_file_is_no_longer_available";
    }
    if (message == "FFmpeg is no longer available") {
        return L"actions.ffmpeg_is_no_longer_available";
    }
    if (message == "whisper-cli.exe is no longer available") {
        return L"actions.whisper_cli_exe_is_no_longer_available";
    }
    if (message == "Whisper model is no longer available") {
        return L"actions.whisper_model_is_no_longer_available";
    }
    if (message == "vot-helper.exe is no longer available") {
        return L"actions.vot_helper_exe_is_no_longer_available";
    }
    if (message == "installed VOT helper self-test failed") {
        return L"actions.vot_helper_is_installed_but_failed_the_launch_check";
    }
    if (message == "installed whisper.cpp self-test failed") {
        return L"actions.whisper_cpp_is_installed_but_failed_the_launch_check";
    }
    if (message == "post-processing output conflict is no longer approved") {
        return L"actions.a_new_output_conflict_appeared_repeat_the_operation_and";
    }
    return std::wstring(message.begin(), message.end());
}

std::wstring ProgressTaskFailureMessage(ProgressTaskKind kind) {
    switch (kind) {
    case ProgressTaskKind::AppUpdate:
        return L"dialog.failed_to_update_the_application";
    case ProgressTaskKind::WhisperModelDownload:
        return L"dialog.failed_to_download_the_whisper_model";
    case ProgressTaskKind::VotInstall:
        return L"dialog.failed_to_install_vot_helper";
    case ProgressTaskKind::WhisperInstall:
        return L"dialog.failed_to_install_whisper_cpp";
    case ProgressTaskKind::FfmpegInstall:
    default:
        return L"dialog.failed_to_install_ffmpeg";
    }
}

std::wstring ProgressTaskUnknownErrorMessage(ProgressTaskKind kind) {
    switch (kind) {
    case ProgressTaskKind::AppUpdate:
        return L"dialog.unknown_application_update_error";
    case ProgressTaskKind::WhisperModelDownload:
        return L"dialog.unknown_whisper_model_download_error";
    case ProgressTaskKind::VotInstall:
        return L"dialog.unknown_vot_helper_installation_error";
    case ProgressTaskKind::WhisperInstall:
        return L"dialog.unknown_whisper_cpp_installation_error";
    case ProgressTaskKind::FfmpegInstall:
    default:
        return L"dialog.unknown_ffmpeg_installation_error";
    }
}

std::wstring ProgressTaskSuccessMessage(ProgressTaskKind kind, bool whisperModelReady) {
    switch (kind) {
    case ProgressTaskKind::AppUpdate:
        return L"dialog.update_downloaded_the_application_will_close_and_restart";
    case ProgressTaskKind::WhisperInstall:
        return whisperModelReady
            ? L"dialog.whisper_cpp_installed"
            : L"dialog.whisper_cpp_installed_now_download_a_model_the_model_win";
    case ProgressTaskKind::WhisperModelDownload:
        return L"dialog.whisper_model_downloaded";
    case ProgressTaskKind::VotInstall:
        return L"dialog.vot_helper_installed";
    case ProgressTaskKind::FfmpegInstall:
    default:
        return L"dialog.ffmpeg_installed";
    }
}

std::wstring ProgressDoneButtonText(ProgressTaskKind kind, bool success, bool whisperModelReady) {
    return success && kind == ProgressTaskKind::WhisperInstall && !whisperModelReady
        ? L"dialog.models"
        : L"OK";
}

std::wstring PostProcessingQueueStatusText(QueueTaskAction action) {
    switch (action) {
    case QueueTaskAction::Transcribe:
        return L"actions.waiting_for_transcription";
    case QueueTaskAction::Translate:
        return L"actions.waiting_for_translation";
    default:
        return L"actions.waiting_for_processing";
    }
}

bool ShouldBlockWhisperCudaBackend(
    WhisperBackend configuredBackend,
    WhisperBackend resolvedBackend,
    bool cudaRuntimeAvailable
) {
    return configuredBackend == WhisperBackend::Cuda &&
        (!cudaRuntimeAvailable || resolvedBackend != WhisperBackend::Cuda);
}

WhisperCudaReadinessAction ResolveWhisperCudaReadinessAction(
    WhisperBackend configuredBackend,
    WhisperBackend resolvedBackend,
    bool cudaSelfTestPassed,
    bool cpuSelfTestPassed,
    bool modelReady
) {
    if (configuredBackend != WhisperBackend::Cuda) {
        return WhisperCudaReadinessAction::UseResolvedBackend;
    }
    if (resolvedBackend == WhisperBackend::Cuda && cudaSelfTestPassed) {
        return WhisperCudaReadinessAction::UseResolvedBackend;
    }
    if (cpuSelfTestPassed && modelReady) {
        return WhisperCudaReadinessAction::FallbackToCpu;
    }
    return WhisperCudaReadinessAction::BlockCuda;
}

bool ShouldRetryWhisperCudaFailureWithCpu(
    WhisperBackend configuredBackend,
    WhisperBackend resolvedBackend,
    bool transcriptionSucceeded,
    bool transcriptionCanceled,
    bool cpuBackendInstalled,
    bool cpuSelfTestPassed,
    bool modelReady
) {
    return configuredBackend == WhisperBackend::Cuda &&
        resolvedBackend == WhisperBackend::Cuda &&
        !transcriptionSucceeded &&
        !transcriptionCanceled &&
        cpuBackendInstalled &&
        cpuSelfTestPassed &&
        modelReady;
}

bool ShouldFallbackWhisperCudaInstallToCpu(
    WhisperBackend installBackend,
    const std::string& installError,
    bool cpuBackendInstalled,
    bool cpuSelfTestPassed,
    bool modelReady
) {
    return installBackend == WhisperBackend::Cuda &&
        installError == "installed whisper.cpp self-test failed" &&
        cpuBackendInstalled &&
        cpuSelfTestPassed &&
        modelReady;
}

std::vector<std::filesystem::path> BuildTranscriptionAffectedFiles(
    const TranscriptionPaths& paths,
    SubtitleFfmpegMode subtitleMode
) {
    std::vector<std::filesystem::path> affected;
    std::error_code ec;
    if (!paths.finalTextPath.empty() && std::filesystem::exists(paths.finalTextPath, ec)) {
        affected.push_back(paths.finalTextPath);
    }
    ec.clear();
    if (!paths.finalSrtPath.empty() && std::filesystem::exists(paths.finalSrtPath, ec)) {
        affected.push_back(paths.finalSrtPath);
    }
    if (subtitleMode != SubtitleFfmpegMode::Off && !paths.finalVideoPath.empty()) {
        affected.push_back(paths.finalVideoPath);
    }
    return affected;
}

std::vector<std::filesystem::path> BuildVoiceOverAffectedFiles(
    const VoiceOverTranslationPaths& paths,
    VoiceOverFfmpegMode ffmpegMode
) {
    std::vector<std::filesystem::path> affected;
    std::error_code ec;
    if (!paths.finalAudioPath.empty() && std::filesystem::exists(paths.finalAudioPath, ec)) {
        affected.push_back(paths.finalAudioPath);
    }
    if (ffmpegMode != VoiceOverFfmpegMode::Off && !paths.finalVideoPath.empty()) {
        affected.push_back(paths.finalVideoPath);
    }
    return affected;
}

SubtitleFfmpegMode EffectiveSubtitleFfmpegModeForMedia(
    SubtitleFfmpegMode mode,
    const std::wstring& mediaKind,
    const std::filesystem::path& mediaPath,
    const std::wstring& quality
) {
    return IsAudioOnlyMedia(mediaKind, mediaPath, quality) ? SubtitleFfmpegMode::Off : mode;
}

VoiceOverFfmpegMode EffectiveVoiceOverFfmpegModeForMedia(
    VoiceOverFfmpegMode mode,
    const std::wstring& mediaKind,
    const std::filesystem::path& mediaPath,
    const std::wstring& quality
) {
    return IsAudioOnlyMedia(mediaKind, mediaPath, quality) ? VoiceOverFfmpegMode::Off : mode;
}

bool ShouldBlockVoiceOverTranslationForDuration(std::uint64_t durationSeconds) {
    return durationSeconds > 14400;
}

std::vector<std::filesystem::path> FindUnapprovedAffectedFiles(
    const std::vector<std::filesystem::path>& currentAffectedFiles,
    const std::vector<std::filesystem::path>& approvedAffectedFiles
) {
    std::vector<std::filesystem::path> unapproved;
    for (const std::filesystem::path& current : currentAffectedFiles) {
        const auto approved = std::find(
            approvedAffectedFiles.begin(),
            approvedAffectedFiles.end(),
            current
        );
        if (approved == approvedAffectedFiles.end()) {
            unapproved.push_back(current);
        }
    }
    return unapproved;
}

std::vector<EditContextMenuItem> BuildEditContextMenuItems(
    bool canUndo,
    bool hasSelection,
    bool canPaste,
    bool hasText
) {
    return {
        {IdEditMenuUndo, L"app.cancel", false, canUndo},
        {0, L"", true, false},
        {IdEditMenuCut, L"actions.cut", false, hasSelection},
        {IdEditMenuCopy, L"dialog.copy_2", false, hasSelection},
        {IdEditMenuPaste, L"app.paste", false, canPaste},
        {IdEditMenuDelete, L"app.delete", false, hasSelection},
        {0, L"", true, false},
        {IdEditMenuSelectAll, L"actions.select_all", false, hasText}
    };
}

int EditContextMenuHeight(const std::vector<EditContextMenuItem>& items) {
    int height = kEditMenuOuterPadding * 2;
    for (const EditContextMenuItem& item : items) {
        height += EditMenuItemHeight(item);
    }
    return height;
}

UINT HitTestEditContextMenuItem(const std::vector<EditContextMenuItem>& items, int y) {
    int top = kEditMenuOuterPadding;
    for (const EditContextMenuItem& item : items) {
        const int bottom = top + EditMenuItemHeight(item);
        if (y >= top && y < bottom) {
            return (!item.separator && item.enabled) ? item.id : 0;
        }
        top = bottom;
    }
    return 0;
}

void PasteReplacingEditText(HWND editControl) {
    if (!IsWindow(editControl)) {
        return;
    }

    SetFocus(editControl);
    SendMessageW(editControl, EM_SETSEL, 0, -1);
    SendMessageW(editControl, WM_PASTE, 0, 0);
}

void CopyTextToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) {
        return;
    }
    EmptyClipboard();
    const SIZE_T byteCount = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, byteCount);
    if (memory) {
        void* target = GlobalLock(memory);
        if (target) {
            std::memcpy(target, text.c_str(), byteCount);
            GlobalUnlock(memory);
            SetClipboardData(CF_UNICODETEXT, memory);
            memory = nullptr;
        }
    }
    if (memory) {
        GlobalFree(memory);
    }
    CloseClipboard();
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
