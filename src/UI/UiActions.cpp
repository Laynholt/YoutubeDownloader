#include "UiActions.h"

#include <algorithm>
#include <cstring>
#include <system_error>

namespace {

constexpr int kEditMenuOuterPadding = 2;
constexpr int kEditMenuItemHeight = 34;
constexpr int kEditMenuSeparatorHeight = 10;

int EditMenuItemHeight(const EditContextMenuItem& item) {
    return item.separator ? kEditMenuSeparatorHeight : kEditMenuItemHeight;
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

std::vector<QueueTaskActionItem> BuildQueueTaskActions(const QueueTaskActionInput& input) {
    if (input.postProcessingBusy) {
        return {
            {QueueTaskAction::CancelPostProcessing, L"Отменить"}
        };
    }

    if (input.completed) {
        std::vector<QueueTaskActionItem> actions;
        if (input.hasSourceUrl) {
            actions.push_back({QueueTaskAction::Transcribe, L"Транскрибировать"});
            actions.push_back({QueueTaskAction::Translate, L"Перевести"});
        }
        actions.push_back({QueueTaskAction::Clear, L"Закрыть"});
        return actions;
    }

    if (input.failedOrCanceled) {
        return {
            {QueueTaskAction::Retry, L"Повторить"},
            {QueueTaskAction::Clear, L"Очистить"}
        };
    }

    return {};
}

ToolReadinessDialogContent BuildToolReadinessDialogContent(ToolReadinessIssue issue) {
    ToolReadinessDialogContent content;
    content.title = L"Инструмент не готов";
    content.openToolsText = L"Открыть Инструменты";
    content.cancelText = L"Отмена";

    switch (issue) {
    case ToolReadinessIssue::MissingFfmpegForWhisper:
        content.message =
            L"Для транскрибации через Whisper требуется FFmpeg: приложению нужно извлечь аудиодорожку перед запуском whisper-cli.exe.\n\n"
            L"Откройте раздел Инструменты, чтобы установить FFmpeg или выбрать папку с ffmpeg.exe.";
        break;
    case ToolReadinessIssue::MissingWhisperExe:
        content.message =
            L"Не найден whisper-cli.exe для локальной транскрибации.\n\n"
            L"Откройте раздел Инструменты, чтобы выбрать папку Whisper.cpp или установить инструмент.";
        break;
    case ToolReadinessIssue::MissingWhisperModel:
        content.message =
            L"Не найдена выбранная модель Whisper. Без модели whisper-cli.exe не сможет распознать аудио.\n\n"
            L"Откройте раздел Инструменты, чтобы скачать или выбрать модель Whisper.";
        break;
    case ToolReadinessIssue::MissingWhisperSetup:
        content.message =
            L"Для транскрибации через Whisper нужно подготовить несколько компонентов: FFmpeg для извлечения аудио, "
            L"whisper-cli.exe для распознавания и модель Whisper.\n\n"
            L"Откройте раздел Инструменты, чтобы установить или выбрать недостающие файлы.";
        break;
    case ToolReadinessIssue::MissingWhisperCuda:
        content.message =
            L"В настройках выбран CUDA-backend Whisper, но CUDA недоступна или приложение нашло только CPU-версию whisper-cli.exe.\n\n"
            L"Откройте раздел Инструменты, чтобы установить CUDA-версию, выбрать CPU-режим или скачать подходящий Whisper заново.";
        break;
    case ToolReadinessIssue::MissingVotExe:
        content.message =
            L"Не найден vot-helper.exe для Voice Over Translation.\n\n"
            L"Откройте раздел Инструменты, чтобы выбрать папку VOT или установить инструмент.";
        break;
    }

    return content;
}

std::wstring VoiceOverFfmpegModeDisplayText(VoiceOverFfmpegMode mode) {
    switch (mode) {
    case VoiceOverFfmpegMode::AudioTrack:
        return L"Аудиодорожка";
    case VoiceOverFfmpegMode::Mix:
        return L"Смешать";
    case VoiceOverFfmpegMode::Off:
    default:
        return L"Выкл";
    }
}

std::wstring SubtitleFfmpegModeDisplayText(SubtitleFfmpegMode mode) {
    switch (mode) {
    case SubtitleFfmpegMode::SubtitleTrack:
        return L"Дорожка субтитров";
    case SubtitleFfmpegMode::BurnIn:
        return L"Вшить в видео";
    case SubtitleFfmpegMode::Off:
    default:
        return L"Выкл";
    }
}

std::wstring OpenDownloadFolderButtonText() {
    return L"Открыть папку";
}

std::wstring TranslationSettingsCollapsedIcon() {
    return L"♫";
}

std::wstring ToolSetupButtonText() {
    return L"Настроить";
}

std::wstring WhisperBackendStatusText(WhisperBackend configuredBackend, WhisperBackend resolvedBackend, bool cudaAvailable) {
    if (configuredBackend == WhisperBackend::Cuda && (!cudaAvailable || resolvedBackend != WhisperBackend::Cuda)) {
        return L"CUDA нет";
    }
    if (resolvedBackend == WhisperBackend::Cuda) {
        return L"CUDA";
    }
    if (resolvedBackend == WhisperBackend::Custom) {
        return L"Выбран";
    }
    return L"CPU";
}

std::wstring WhisperInstallButtonText(WhisperBackend configuredBackend, bool cudaAvailable, bool backendInstalled) {
    (void)configuredBackend;
    const bool installCuda = cudaAvailable;
    if (installCuda) {
        return backendInstalled ? L"Переустановить CUDA" : L"Установить CUDA";
    }
    return backendInstalled ? L"Переустановить CPU" : L"Установить CPU";
}

std::wstring FfmpegGatedOptionTooltip(const std::wstring& actionText) {
    return actionText + L"\nТребуется FFmpeg; без него эта опция недоступна.";
}

std::wstring LocalizedToolErrorText(const std::string& message) {
    if (message == "operation canceled") {
        return L"Операция отменена";
    }
    if (message == "media file is no longer available") {
        return L"Исходный медиафайл больше недоступен";
    }
    if (message == "FFmpeg is no longer available") {
        return L"FFmpeg больше недоступен";
    }
    if (message == "whisper-cli.exe is no longer available") {
        return L"whisper-cli.exe больше недоступен";
    }
    if (message == "Whisper model is no longer available") {
        return L"Модель Whisper больше недоступна";
    }
    if (message == "vot-helper.exe is no longer available") {
        return L"vot-helper.exe больше недоступен";
    }
    if (message == "installed VOT helper self-test failed") {
        return L"VOT helper установлен, но не прошёл проверку запуска";
    }
    if (message == "installed whisper.cpp self-test failed") {
        return L"Whisper.cpp установлен, но не прошёл проверку запуска";
    }
    if (message == "post-processing output conflict is no longer approved") {
        return L"Появился новый конфликт вывода. Повторите операцию и подтвердите перезапись.";
    }
    return std::wstring(message.begin(), message.end());
}

std::wstring PostProcessingQueueStatusText(QueueTaskAction action) {
    switch (action) {
    case QueueTaskAction::Transcribe:
        return L"Ожидает транскрибации...";
    case QueueTaskAction::Translate:
        return L"Ожидает перевода...";
    default:
        return L"Ожидает обработки...";
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
    bool cpuBackendInstalled,
    bool modelReady
) {
    if (configuredBackend != WhisperBackend::Cuda) {
        return WhisperCudaReadinessAction::UseResolvedBackend;
    }
    if (resolvedBackend == WhisperBackend::Cuda && cudaSelfTestPassed) {
        return WhisperCudaReadinessAction::UseResolvedBackend;
    }
    if (cpuBackendInstalled && modelReady) {
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
        {IdEditMenuUndo, L"Отменить", false, canUndo},
        {0, L"", true, false},
        {IdEditMenuCut, L"Вырезать", false, hasSelection},
        {IdEditMenuCopy, L"Копировать", false, hasSelection},
        {IdEditMenuPaste, L"Вставить", false, canPaste},
        {IdEditMenuDelete, L"Удалить", false, hasSelection},
        {0, L"", true, false},
        {IdEditMenuSelectAll, L"Выделить всё", false, hasText}
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
