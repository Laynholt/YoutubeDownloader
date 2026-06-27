#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "AppPaths.h"
#include "Config.h"
#include "DialogWindows.h"
#include "DownloadQueue.h"
#include "Logger.h"
#include "ToolManagers.h"
#include "YtDlpClient.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

enum class QueueTaskAction;

class Application {
public:
    Application();
    ~Application();

    bool Initialize(HINSTANCE instance, int showCommand);
    int Run();
    void Shutdown();

private:
    struct ToolCheckResult {
        bool ready = false;
        ToolInstallStatus status;
        ReleaseAssetInfo latestRelease;
        std::wstring latestCheckAt;
        std::wstring message;
    };

    struct PreviewFetchResult {
        unsigned long requestId = 0;
        std::wstring url;
        bool ok = false;
        VideoPreview preview;
        std::wstring error;
    };

    struct AppUpdateCheckResult {
        bool ok = false;
        ReleaseAssetInfo release;
        std::wstring error;
    };

    struct PostProcessingProgressResult {
        int taskId = 0;
        int action = 0;
        double percent = 0.0;
        bool indeterminate = false;
        std::wstring status;
    };

    struct PostProcessingCompleteResult {
        int taskId = 0;
        int action = 0;
        bool success = false;
        bool canceled = false;
        std::wstring status;
        std::wstring error;
    };

    struct PendingPostProcessingOperation {
        int taskId = 0;
        int action = 0;
        DownloadTaskSnapshot task;
        std::filesystem::path mediaPath;
        AppPaths paths;
        AppConfig config;
        FfmpegStatus ffmpeg;
    };

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK ButtonWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    bool HandleMainWindowShortcut(const MSG& message);
    void RelayTooltipMessage(const MSG& message);
    bool RegisterButtonClass();
    void CreateControls();
    HWND CreateButton(const wchar_t* text, int id, bool primary, bool onPanel);
    void LayoutControls(int width, int height);
    void DrawControlFrames(HDC dc);
    void DrawPreviewContent(HDC dc, const RECT& previewRect);
    void DrawQueueContent(HDC dc, const RECT& queueRect);
    bool HandleQueueClick(POINT point);
    bool HandleQueueContextMenu(POINT point);
    bool UpdateQueueHover(POINT point);
    void ClearQueueHover();
    bool ScrollQueue(int wheelDelta, POINT point);
    void SetControlFonts();
    void SetStatus(const std::wstring& text);
    void SetTransientStatus(const std::wstring& text);
    void RestoreStatusText();
    void InitializeBackend();
    void LoadDownloadQueue();
    void SaveDownloadQueue(bool forShutdown = false);
    void StartToolCheck();
    void StartAppUpdateCheck();
    void StartPreviewFetch();
    void StartPreviewLoadingText();
    void StopPreviewLoadingText();
    void UpdatePreviewLoadingText();
    void EnqueueCurrentUrl();
    bool ShowAndSaveSettings(SettingsInitialSection initialSection = SettingsInitialSection::Downloads);
    void StartPostProcessing(int taskId, int action);
    void StartPostProcessingWorker(PendingPostProcessingOperation operation);
    void StartNextQueuedPostProcessing();
    bool HasPostProcessingOperationForTask(int taskId) const;
    QueueTaskAction PostProcessingActionForTask(int taskId) const;
    bool CancelPostProcessing(int taskId);
    void RefreshQueueText();
    std::wstring GetWindowTextString(HWND control) const;

    HINSTANCE m_instance = nullptr;
    HWND m_window = nullptr;
    bool m_buttonClassRegistered = false;
    HWND m_urlEdit = nullptr;
    HWND m_pasteButton = nullptr;
    HWND m_previewTitle = nullptr;
    HWND m_folderEdit = nullptr;
    HWND m_chooseFolderButton = nullptr;
    HWND m_downloadButton = nullptr;
    HWND m_clearButton = nullptr;
    HWND m_clearFinishedButton = nullptr;
    HWND m_openFolderButton = nullptr;
    HWND m_logsButton = nullptr;
    HWND m_settingsButton = nullptr;
    HWND m_statusLabel = nullptr;
    HWND m_queueLabel = nullptr;
    HWND m_queuePlaceholder = nullptr;
    RECT m_urlFrameRect = {};
    RECT m_folderFrameRect = {};

    HFONT m_font = nullptr;
    HFONT m_boldFont = nullptr;
    HBRUSH m_backgroundBrush = nullptr;
    HBRUSH m_panelBrush = nullptr;
    HWND m_tooltip = nullptr;
    ULONG_PTR m_gdiplusToken = 0;

    std::unique_ptr<AppPaths> m_paths;
    AppConfig m_config;
    std::unique_ptr<Logger> m_logger;
    FfmpegStatus m_ffmpeg;
    ToolInstallStatus m_ytDlpStatus;
    std::unique_ptr<DownloadQueue> m_downloadQueue;
    std::jthread m_toolCheckWorker;
    std::jthread m_appUpdateWorker;
    std::jthread m_previewWorker;
    std::jthread m_postProcessingWorker;
    std::mutex m_asyncResultMutex;
    std::optional<ToolCheckResult> m_toolCheckResult;
    std::optional<AppUpdateCheckResult> m_appUpdateCheckResult;
    std::optional<PreviewFetchResult> m_previewFetchResult;
    std::optional<PostProcessingProgressResult> m_postProcessingProgressResult;
    std::optional<PostProcessingCompleteResult> m_postProcessingCompleteResult;
    std::mutex m_previewMutex;
    VideoPreview m_preview;
    std::atomic<unsigned long> m_previewRequestId = 0;
    std::uint64_t m_lastRenderedQueueRevision = static_cast<std::uint64_t>(-1);
    std::uint64_t m_lastSavedQueueRevision = static_cast<std::uint64_t>(-1);
    bool m_queuePlaceholderVisible = true;
    bool m_ytDlpReady = false;
    bool m_transientStatusActive = false;
    bool m_previewLoading = false;
    bool m_queueMouseTracking = false;
    bool m_shutdownStarted = false;
    bool m_comInitialized = false;
    int m_previewLoadingDots = 3;
    int m_hotQueueTaskId = 0;
    int m_hotQueueAction = 0;
    int m_queueScrollOffset = 0;
    int m_postProcessingTaskId = 0;
    int m_postProcessingAction = 0;
    std::deque<PendingPostProcessingOperation> m_postProcessingQueue;
    double m_postProcessingPercent = 0.0;
    bool m_postProcessingIndeterminate = false;
    std::wstring m_postProcessingStatus;
    DWORD m_postProcessingStartedTick = 0;
};
