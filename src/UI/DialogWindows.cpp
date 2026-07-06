#include "DialogWindows.h"

#include "AppVersion.h"
#include "BackendText.h"
#include "ToolManagers.h"
#include "UiActions.h"
#include "UiRenderer.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

bool ShowFfmpegInstallProgress(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config);
bool ShowWhisperDialog(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config);
bool ShowVotDialog(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config);
bool ShowWhisperModelDialog(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config);
bool ShowWhisperInstallProgress(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config);
bool ShowWhisperModelDownloadProgress(
    HWND owner,
    HINSTANCE instance,
    const AppPaths& paths,
    AppConfig& config,
    const WhisperModelInfo& model
);
bool ShowVotInstallProgress(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config);
bool ShowAppUpdateProgress(HWND owner, HINSTANCE instance, const AppPaths& paths, const ReleaseAssetInfo& release);

namespace {

constexpr const wchar_t* kDialogClassName = L"YoutubeDownloaderDialogWindow";
constexpr const wchar_t* kDialogButtonClassName = L"YoutubeDownloaderDialogButton";
constexpr const wchar_t* kScrollTextClassName = L"YoutubeDownloaderScrollText";
constexpr const wchar_t* kLogViewClassName = L"YoutubeDownloaderLogView";
constexpr const wchar_t* kLogCopyMenuClassName = L"YoutubeDownloaderLogCopyMenu";
constexpr const wchar_t* kSettingsComboMenuClassName = L"YoutubeDownloaderSettingsComboMenu";

constexpr COLORREF kBackgroundColor = RGB(20, 20, 22);
constexpr COLORREF kPanelColor = RGB(28, 28, 31);
constexpr COLORREF kInputColor = RGB(25, 25, 28);
constexpr COLORREF kTextColor = RGB(242, 242, 242);
constexpr COLORREF kMutedTextColor = RGB(172, 172, 178);
constexpr int kDialogPanelInset = 12;
constexpr int kDialogButtonInset = 16;
constexpr int kDialogButtonGap = 12;
constexpr int kDialogButtonHeight = 34;
constexpr int kScrollTextTopPadding = 12;
constexpr int kScrollTextBottomPadding = 12;
constexpr int kScrollTextClipTopPadding = 8;
constexpr int kScrollTextClipBottomPadding = 8;
constexpr int kSettingsDialogWidth = 980;
constexpr int kSettingsDialogHeight = 760;
constexpr int kSettingsMinWidth = 760;
constexpr int kSettingsMinHeight = 600;
constexpr int kSettingsSidebarExpandedWidth = 214;
constexpr int kSettingsSidebarCollapsedWidth = 70;
constexpr int kSettingsSidebarCollapseWidth = 820;
constexpr int kSettingsSidebarGap = 18;
constexpr int kSettingsContentTop = 86;
constexpr int kSettingsCardGap = 14;
constexpr int kSettingsCardPadding = 18;
constexpr int kSettingsChoiceCardHeight = 112;
constexpr int kSettingsBehaviorCardHeight = 158;
constexpr int kSettingsWorkflowCardHeight = 128;
constexpr int kSettingsWorkflowLanguageCardHeight = kSettingsChoiceCardHeight;
constexpr int kSettingsToolCardHeight = 116;
constexpr int kSettingsAboutCardHeight = 172;
constexpr int kSettingsCardControlTop = 66;
constexpr int kSettingsBottomButtonWidth = 128;
constexpr UINT kProgressUpdateMessage = WM_APP + 40;
constexpr UINT kProgressDoneMessage = WM_APP + 41;

enum class DialogType {
    Info,
    Error,
    Confirmation,
    AffectedFiles,
    Settings,
    About,
    Logs,
    Ffmpeg,
    Whisper,
    Vot,
    VotCandidate,
    WhisperModel,
    Progress
};

enum class ProgressMode {
    FfmpegInstall,
    WhisperInstall,
    WhisperModelDownload,
    VotInstall,
    AppUpdate
};

enum class SettingsSection {
    Downloads,
    Transcription,
    Translation,
    Tools,
    About
};

enum DialogCommand {
    IdOk = 1,
    IdCancel = 2,
    IdCopy = 10,
    IdAbout = 11,
    IdFfmpeg = 12,
    IdInstall = 13,
    IdSkip = 15,
    IdCheckUpdates = 16,
    IdChooseFolder = 17,
    IdAutoUpdate = 120,
    IdParallelMinus = 121,
    IdParallelPlus = 122,
    IdSettingsNavDownloads = 123,
    IdSettingsNavTranscription = 124,
    IdSettingsNavTranslation = 125,
    IdSettingsNavTools = 126,
    IdSettingsNavAbout = 127,
    IdSettingsToggleSidebar = 128,
    IdTranscriptionOpenTools = 129,
    IdTranscriptionWhisper = 130,
    IdTranscriptionVot = 131,
    IdVoiceModeOff = 132,
    IdVoiceModeTrack = 133,
    IdVoiceModeMix = 134,
    IdSubtitleModeOff = 135,
    IdSubtitleModeTrack = 136,
    IdSubtitleModeBurn = 137,
    IdVoiceLanguageEdit = 141,
    IdChooseWhisperFolder = 142,
    IdChooseVotFolder = 143,
    IdTranslationOpenTools = 144,
    IdYtDlpDetails = 145,
    IdFfmpegDetails = 146,
    IdWhisperDetails = 147,
    IdVotDetails = 148,
    IdWhisperDownloadModel = 149,
    IdWhisperModelDownloadSelected = 150,
    IdVotCandidateSelect = 151,
    IdChooseVotExecutable = 152,
    IdVotSubtitleLanguageEdit = 153,
    IdVoiceVolumeMinus = 154,
    IdVoiceVolumePlus = 155,
    IdWhisperModelBase = 300,
    IdVotCandidateBase = 500
};

struct ButtonState {
    int commandId = 0;
    bool primary = false;
    bool onCard = true;
    bool enabled = true;
    bool hot = false;
    bool pressed = false;
    std::wstring text;
};

struct ScrollTextState {
    std::wstring text;
    int scrollY = 0;
    int contentHeight = 0;
    bool draggingThumb = false;
    int dragStartY = 0;
    int dragStartScrollY = 0;
};

struct LogLineLayout {
    RECT rect = {};
};

struct LogViewState {
    std::wstring text;
    std::vector<std::wstring> lines;
    std::vector<LogLineLayout> layouts;
    int scrollY = 0;
    int contentHeight = 0;
    int selectedLine = -1;
    bool draggingThumb = false;
    int dragStartY = 0;
    int dragStartScrollY = 0;
};

struct LogCopyMenuState {
    HWND owner = nullptr;
    std::wstring text;
    bool hot = false;
};

enum class SettingsLanguageTarget {
    VotSubtitle,
    VoiceOver
};

struct SettingsComboMenuState {
    HWND owner = nullptr;
    SettingsLanguageTarget target = SettingsLanguageTarget::VotSubtitle;
    std::vector<std::wstring> values;
    int hotIndex = -1;
};

struct ProgressUpdate {
    std::uint64_t downloaded = 0;
    std::uint64_t total = 0;
    std::wstring status;
};

struct DialogState {
    DialogType type = DialogType::Info;
    HINSTANCE instance = nullptr;
    HWND owner = nullptr;
    HWND window = nullptr;
    HWND scrollText = nullptr;
    HWND logView = nullptr;
    std::wstring title;
    std::wstring subtitle;
    std::wstring message;
    std::wstring primaryButtonText;
    std::wstring cancelButtonText;
    const AppPaths* paths = nullptr;
    AppConfig* config = nullptr;
    AppConfig workingConfig;
    SettingsSection settingsSection = SettingsSection::Downloads;
    bool settingsSidebarCollapsed = false;
    bool ytDlpDetailsExpanded = false;
    bool ffmpegDetailsExpanded = false;
    bool whisperDetailsExpanded = false;
    bool votDetailsExpanded = false;
    bool* savedResult = nullptr;
    std::uint64_t progressDownloaded = 0;
    std::uint64_t progressTotal = 0;
    HWND tooltip = nullptr;
    std::vector<HWND> tooltips;
    std::deque<std::wstring> tooltipTexts;
    HANDLE cancelEvent = nullptr;
    bool progressDone = false;
    bool progressSuccess = false;
    ProgressMode progressMode = ProgressMode::FfmpegInstall;
    ReleaseAssetInfo release;
    std::jthread worker;
    std::mutex progressMutex;
    std::optional<ProgressUpdate> pendingProgress;
    std::optional<std::wstring> progressError;
    std::optional<std::wstring> progressSuccessMessage;
    std::vector<WhisperModelInfo> whisperModels;
    int selectedWhisperModelIndex = 0;
    std::optional<WhisperModelInfo> progressWhisperModel;
    std::vector<std::filesystem::path> votExecutableCandidates;
    int selectedVotExecutableIndex = 0;
    std::filesystem::path* selectedVotExecutableResult = nullptr;
};

void ShowModal(DialogState* state, int width, int height);

SettingsSection ToSettingsSection(SettingsInitialSection section) {
    switch (section) {
    case SettingsInitialSection::Transcription:
        return SettingsSection::Transcription;
    case SettingsInitialSection::Translation:
        return SettingsSection::Translation;
    case SettingsInitialSection::Tools:
        return SettingsSection::Tools;
    case SettingsInitialSection::About:
        return SettingsSection::About;
    case SettingsInitialSection::Downloads:
    default:
        return SettingsSection::Downloads;
    }
}

bool IsSettingsSidebarCollapsed(const DialogState* state, int width) {
    return width < kSettingsSidebarCollapseWidth || (state && state->settingsSidebarCollapsed);
}

int SettingsSidebarWidth(const DialogState* state, int width) {
    return IsSettingsSidebarCollapsed(state, width)
        ? kSettingsSidebarCollapsedWidth
        : kSettingsSidebarExpandedWidth;
}

RECT SettingsPanelRect(int width, int height) {
    return {kDialogPanelInset, kDialogPanelInset, width - kDialogPanelInset, height - kDialogPanelInset};
}

RECT SettingsSidebarRect(const DialogState* state, int width, int height) {
    const RECT panel = SettingsPanelRect(width, height);
    const int left = panel.left + kDialogButtonInset;
    const int top = panel.top + kDialogButtonInset;
    return {
        left,
        top,
        left + SettingsSidebarWidth(state, width),
        panel.bottom - kDialogButtonInset
    };
}

RECT SettingsContentRect(const DialogState* state, int width, int height) {
    const RECT panel = SettingsPanelRect(width, height);
    const RECT sidebar = SettingsSidebarRect(state, width, height);
    return {
        sidebar.right + kSettingsSidebarGap,
        panel.top + kDialogButtonInset,
        panel.right - kDialogButtonInset,
        panel.bottom - kDialogButtonInset - kDialogButtonHeight - 18
    };
}

int SettingsBottomButtonY(int height) {
    const RECT panel = SettingsPanelRect(0, height);
    return panel.bottom - kDialogButtonInset - kDialogButtonHeight;
}

RECT SettingsStackCardRect(const DialogState* state, int width, int height, int index, int cardHeight) {
    const RECT content = SettingsContentRect(state, width, height);
    const int top = content.top + kSettingsContentTop + index * (cardHeight + kSettingsCardGap);
    return {content.left, top, content.right, top + cardHeight};
}

RECT SettingsCardBelow(const RECT& previous, int cardHeight) {
    return {previous.left, previous.bottom + kSettingsCardGap, previous.right, previous.bottom + kSettingsCardGap + cardHeight};
}

RECT SettingsTranscriptionEngineCardRect(const DialogState* state, int width, int height) {
    return SettingsStackCardRect(state, width, height, 0, kSettingsWorkflowCardHeight);
}

RECT SettingsTranscriptionSubtitleCardRect(const DialogState* state, int width, int height) {
    return SettingsCardBelow(SettingsTranscriptionEngineCardRect(state, width, height), kSettingsWorkflowLanguageCardHeight);
}

RECT SettingsTranscriptionLanguageCardRect(const DialogState* state, int width, int height) {
    return SettingsCardBelow(SettingsTranscriptionSubtitleCardRect(state, width, height), kSettingsWorkflowLanguageCardHeight);
}

RECT SettingsTranscriptionToolsCardRect(const DialogState* state, int width, int height) {
    return SettingsCardBelow(SettingsTranscriptionLanguageCardRect(state, width, height), 92);
}

RECT SettingsTranslationWorkflowCardRect(const DialogState* state, int width, int height) {
    return SettingsStackCardRect(state, width, height, 0, kSettingsBehaviorCardHeight);
}

RECT SettingsTranslationToolsCardRect(const DialogState* state, int width, int height) {
    return SettingsCardBelow(SettingsTranslationWorkflowCardRect(state, width, height), 92);
}

int SettingsToolCardHeight(const DialogState* state, int index) {
    if (index == 0) {
        return state && state->ytDlpDetailsExpanded ? 124 : kSettingsToolCardHeight;
    }
    if (index == 1) {
        return state && state->ffmpegDetailsExpanded ? 124 : kSettingsToolCardHeight;
    }
    if (index == 2) {
        return state && state->whisperDetailsExpanded ? 146 : kSettingsToolCardHeight;
    }
    return state && state->votDetailsExpanded ? 124 : kSettingsToolCardHeight;
}

RECT SettingsToolCardRect(const DialogState* state, int width, int height, int index) {
    const RECT content = SettingsContentRect(state, width, height);
    int top = content.top + kSettingsContentTop;
    for (int i = 0; i < index; ++i) {
        top += SettingsToolCardHeight(state, i) + kSettingsCardGap;
    }
    const int cardHeight = SettingsToolCardHeight(state, index);
    return {content.left, top, content.right, top + cardHeight};
}

RECT SettingsParallelValueRect(const DialogState* state, int width, int height) {
    const RECT card = SettingsStackCardRect(state, width, height, 2, kSettingsBehaviorCardHeight);
    const int valueRight = card.right - kSettingsCardPadding - 46 - kDialogButtonGap;
    return {valueRight - 54, card.top + kSettingsCardControlTop, valueRight, card.top + kSettingsCardControlTop + kDialogButtonHeight};
}

bool IsFfmpegReady(const DialogState* state) {
    if (!state || !state->config) {
        return false;
    }
    if (state->paths) {
        return FfmpegManager::Resolve(*state->paths, state->workingConfig).available;
    }
    return !state->workingConfig.ffmpegPath.empty() &&
        FfmpegManager::ResolveUserPath(state->workingConfig.ffmpegPath).available;
}

void EnableDarkTitleBar(HWND window) {
    BOOL enabled = TRUE;
    constexpr DWORD kDwmUseImmersiveDarkMode = 20;
    if (FAILED(DwmSetWindowAttribute(window, kDwmUseImmersiveDarkMode, &enabled, sizeof(enabled)))) {
        constexpr DWORD kDwmUseImmersiveDarkModeBefore20H1 = 19;
        DwmSetWindowAttribute(window, kDwmUseImmersiveDarkModeBefore20H1, &enabled, sizeof(enabled));
    }
}

HFONT CreateUiFont(int height = -16, int weight = FW_NORMAL) {
    LOGFONTW font = {};
    font.lfHeight = height;
    font.lfWeight = weight;
    wcscpy_s(font.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&font);
}

void DrawTextBlock(HDC dc, const std::wstring& text, RECT rect, COLORREF color, HFONT font, UINT format) {
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(dc, font));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &rect, format | DT_NOPREFIX);
    SelectObject(dc, oldFont);
}

void AddRoundedRect(Gdiplus::GraphicsPath& path, const RECT& rect, int radius) {
    const int diameter = radius * 2;
    path.AddArc(rect.left, rect.top, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rect.right - diameter, rect.top, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(rect.right - diameter, rect.bottom - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(rect.left, rect.bottom - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

void PaintBuffered(HWND window, const std::function<void(HDC, const RECT&)>& paintContent) {
    PAINTSTRUCT paint = {};
    HDC screenDc = BeginPaint(window, &paint);

    RECT client = {};
    GetClientRect(window, &client);
    const int width = std::max(1, static_cast<int>(client.right - client.left));
    const int height = std::max(1, static_cast<int>(client.bottom - client.top));

    HDC bufferDc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap = CreateCompatibleBitmap(screenDc, width, height);
    HGDIOBJ oldBitmap = SelectObject(bufferDc, bitmap);
    HBRUSH background = CreateSolidBrush(kPanelColor);
    FillRect(bufferDc, &client, background);
    DeleteObject(background);

    paintContent(bufferDc, client);
    BitBlt(screenDc, 0, 0, width, height, bufferDc, 0, 0, SRCCOPY);

    SelectObject(bufferDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(bufferDc);
    EndPaint(window, &paint);
}

std::optional<std::filesystem::path> PickFolder(HWND owner, const wchar_t* title) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    }
    dialog->SetTitle(title);

    std::optional<std::filesystem::path> result;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = std::filesystem::path(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

std::optional<std::filesystem::path> PickFfmpegFolder(HWND owner) {
    return PickFolder(owner, L"Выберите папку FFmpeg или папку bin");
}

std::optional<std::filesystem::path> PickVotExecutableFile(HWND owner) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
    }
    dialog->SetTitle(L"Выберите vot-helper.exe");
    const COMDLG_FILTERSPEC filters[] = {
        {L"VOT helper", L"vot-helper.exe"},
        {L"EXE", L"*.exe"}
    };
    dialog->SetFileTypes(2, filters);
    dialog->SetFileName(L"vot-helper.exe");

    std::optional<std::filesystem::path> result;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = std::filesystem::path(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

std::wstring GetChildText(HWND parent, int id) {
    HWND control = GetDlgItem(parent, id);
    if (!control) {
        return {};
    }
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    if (length > 0) {
        GetWindowTextW(control, text.data(), length + 1);
    }
    text.resize(static_cast<size_t>(length));
    return text;
}

HWND CreateSettingsEdit(DialogState* state, int id, const std::wstring& text) {
    HWND edit = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        text.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0,
        0,
        10,
        10,
        state->window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        state->instance,
        nullptr
    );
    if (edit) {
        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }
    return edit;
}

std::vector<std::wstring> VotSubtitleLanguageOptions() {
    return {L"ru", L"en"};
}

std::vector<std::wstring> VoiceLanguageOptions() {
    return {L"ru", L"en"};
}

std::wstring SettingsLanguageButtonText(const std::wstring& value) {
    return (value.empty() ? L"auto" : value) + L"  ▾";
}

void SetControlsVisible(HWND parent, std::initializer_list<int> ids, bool visible) {
    for (int id : ids) {
        HWND control = GetDlgItem(parent, id);
        if (control) {
            ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
        }
    }
}

UINT TooltipInfoSize();
bool IsTooltipRelayMessage(UINT message);
void RelayDialogTooltipMessage(const DialogState* state, const MSG& message);

HWND CreateTooltip(HWND parent, HWND tool, const wchar_t* text) {
    HWND tooltip = CreateWindowExW(
        WS_EX_TOPMOST,
        TOOLTIPS_CLASSW,
        nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        parent,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );
    if (!tooltip) {
        return nullptr;
    }

    SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, 360);
    SendMessageW(tooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 500);
    SendMessageW(tooltip, TTM_SETDELAYTIME, TTDT_RESHOW, 100);
    SendMessageW(tooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 10000);
    SendMessageW(tooltip, TTM_SETTIPBKCOLOR, RGB(35, 35, 38), 0);
    SendMessageW(tooltip, TTM_SETTIPTEXTCOLOR, kTextColor, 0);
    SendMessageW(tooltip, TTM_ACTIVATE, TRUE, 0);
    SetWindowPos(tooltip, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    TOOLINFOW info = {};
    info.cbSize = TooltipInfoSize();
    info.uFlags = TTF_IDISHWND | TTF_TRANSPARENT;
    info.hwnd = parent;
    info.uId = reinterpret_cast<UINT_PTR>(tool);
    info.lpszText = const_cast<LPWSTR>(text);
    SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    return tooltip;
}

UINT TooltipInfoSize() {
#ifdef TTTOOLINFOW_V2_SIZE
    return TTTOOLINFOW_V2_SIZE;
#else
    return sizeof(TOOLINFOW);
#endif
}

bool IsTooltipRelayMessage(UINT message) {
    switch (message) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        return true;
    default:
        return false;
    }
}

void RelayDialogTooltipMessage(const DialogState* state, const MSG& message) {
    if (!state || !IsTooltipRelayMessage(message.message)) {
        return;
    }

    MSG relayMessage = message;
    if (state->tooltip) {
        SendMessageW(state->tooltip, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&relayMessage));
    }
    for (HWND tooltip : state->tooltips) {
        if (tooltip) {
            relayMessage = message;
            SendMessageW(tooltip, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&relayMessage));
        }
    }
}

void AddDialogTooltip(DialogState* state, HWND tool, const wchar_t* text) {
    if (!state || !tool || !text) {
        return;
    }
    HWND tooltip = CreateTooltip(state->window, tool, text);
    if (tooltip) {
        state->tooltips.push_back(tooltip);
    }
}

void AddDialogTooltip(DialogState* state, HWND tool, std::wstring text) {
    if (!state || !tool) {
        return;
    }
    state->tooltipTexts.push_back(std::move(text));
    AddDialogTooltip(state, tool, state->tooltipTexts.back().c_str());
}

bool ApplySelectedFfmpegPath(HWND owner, HINSTANCE instance, AppConfig& config, const std::filesystem::path& path) {
    const FfmpegStatus status = FfmpegManager::ResolveUserPath(path);
    if (!status.available) {
        ShowErrorDialog(owner, instance, L"FFmpeg не найден", status.message);
        return false;
    }
    config.ffmpegPath = status.ffmpegExe;
    return true;
}

bool ApplySelectedWhisperPath(HWND owner, HINSTANCE instance, AppConfig& config, const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path executable = path / L"whisper-cli.exe";
    if (!std::filesystem::is_regular_file(executable, ec)) {
        ec.clear();
        const std::filesystem::path discoveredDir = WhisperManager::FindExecutableDir(path);
        executable = discoveredDir.empty() ? std::filesystem::path{} : discoveredDir / L"whisper-cli.exe";
    }
    if (executable.empty() || !std::filesystem::is_regular_file(executable, ec)) {
        ShowErrorDialog(owner, instance, L"Whisper не найден", L"В выбранной папке не найден whisper-cli.exe.");
        return false;
    }
    if (!WhisperManager::SelfTestExecutable(executable)) {
        ShowErrorDialog(
            owner,
            instance,
            L"Whisper не запускается",
            L"Выбранный whisper-cli.exe найден, но не прошёл проверку запуска. Выберите другую папку или установите Whisper.cpp заново."
        );
        return false;
    }
    config.whisperPath = executable;
    config.whisperBackend = WhisperBackend::Custom;
    return true;
}

std::optional<std::filesystem::path> ShowVotExecutableChoiceDialog(
    HWND owner,
    HINSTANCE instance,
    std::vector<std::filesystem::path> candidates
) {
    if (candidates.empty()) {
        return std::nullopt;
    }
    if (candidates.size() == 1) {
        return candidates.front();
    }

    auto* state = new DialogState{};
    state->type = DialogType::VotCandidate;
    state->instance = instance;
    state->owner = owner;
    state->title = L"Выберите VOT helper";
    state->votExecutableCandidates = std::move(candidates);
    state->selectedVotExecutableIndex = 0;
    bool selected = false;
    std::filesystem::path selectedPath;
    state->savedResult = &selected;
    state->selectedVotExecutableResult = &selectedPath;
    const int dialogHeight = std::min(
        620,
        230 + static_cast<int>(state->votExecutableCandidates.size()) * 46
    );
    ShowModal(state, 760, std::max(360, dialogHeight));
    return selected ? std::optional<std::filesystem::path>{selectedPath} : std::nullopt;
}

bool ApplySelectedVotPath(HWND owner, HINSTANCE instance, AppConfig& config, const std::filesystem::path& path) {
    const std::vector<std::filesystem::path> candidates = VotExeManager::FindExecutables(path);
    const std::optional<std::filesystem::path> selected = ShowVotExecutableChoiceDialog(owner, instance, candidates);
    if (!selected) {
        if (candidates.empty()) {
            ShowErrorDialog(owner, instance, L"VOT helper не найден", L"В выбранном пути не найден vot-helper.exe");
        }
        return false;
    }
    if (!VotExeManager::SelfTestExecutable(*selected)) {
        ShowErrorDialog(
            owner,
            instance,
            L"VOT helper не запускается",
            L"Выбранный vot-helper.exe найден, но не прошёл проверку запуска. Выберите другой файл или установите VOT helper заново."
        );
        return false;
    }
    config.votExePath = *selected;
    return true;
}

FfmpegStatus ResolveDialogFfmpegStatus(const DialogState* state) {
    if (!state || !state->config) {
        return {};
    }
    if (state->paths) {
        return FfmpegManager::Resolve(*state->paths, *state->config);
    }
    if (!state->config->ffmpegPath.empty()) {
        return FfmpegManager::ResolveUserPath(state->config->ffmpegPath);
    }
    return {};
}

std::wstring FfmpegDialogTitle(const FfmpegStatus& status) {
    return status.available ? L"FFmpeg указан" : L"FFmpeg не найден";
}

std::wstring FfmpegDialogMessage(const FfmpegStatus& status) {
    if (status.available) {
        return L"FFmpeg найден и будет использоваться для объединения видео/аудио дорожек и переконвертации.\n\nПуть:\n" +
            status.ffmpegExe.wstring();
    }
    return L"FFmpeg не найден. Без него приложение сможет скачивать только готовые единые файлы без переконвертации и объединения отдельных видео/аудио дорожек.";
}

std::wstring WhisperDialogTitle(const ToolInstallStatus& status, bool modelReady) {
    if (status.installed && modelReady) {
        return L"Whisper.cpp готов";
    }
    if (status.installed) {
        return L"Нужна модель Whisper";
    }
    return L"Whisper.cpp не найден";
}

std::wstring WhisperDialogMessage(const ToolInstallStatus& status, const std::filesystem::path& modelPath, bool modelReady) {
    std::wstring message = status.installed
        ? L"whisper-cli.exe найден и может использоваться для локальной транскрибации."
        : L"Whisper.cpp нужен для локальной транскрибации без VOT.";
    message += L"\n\nwhisper-cli.exe:\n";
    message += status.executable.empty() ? L"-" : status.executable.wstring();
    message += L"\n\nМодель:\n";
    message += modelReady ? modelPath.wstring() : L"Не найдена. Можно скачать рекомендуемую модель.";
    return message;
}

std::wstring VotDialogTitle(const VotExeStatus& status) {
    return status.available ? L"VOT helper готов" : L"VOT helper не найден";
}

std::wstring VotDialogMessage(const VotExeStatus& status) {
    if (status.available) {
        return L"vot-helper.exe найден и будет использоваться для перевода и VOT-транскрибации.\n\nПуть:\n" +
            status.executable.wstring();
    }
    return L"vot-helper.exe нужен для Voice Over Translation и VOT-транскрибации.\n\nМожно установить его автоматически или выбрать папку с готовым EXE.";
}

std::filesystem::path ResolveDialogWhisperModelPath(const DialogState* state) {
    if (!state || !state->config) {
        return {};
    }
    if (!state->config->whisperModelPath.empty()) {
        return state->config->whisperModelPath;
    }
    return state->paths ? state->paths->localWhisperModelPath() : std::filesystem::path{};
}

bool IsRegularFile(const std::filesystem::path& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::is_regular_file(path, ec);
}

std::wstring FormatModelSize(std::uint64_t bytes) {
    if (bytes == 0) {
        return L"";
    }
    const double mib = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mib >= 1024.0) {
        const double gib = mib / 1024.0;
        wchar_t buffer[32] = {};
        swprintf_s(buffer, L"%.1f GiB", gib);
        return buffer;
    }
    return std::to_wstring(static_cast<int>(mib + 0.5)) + L" MiB";
}

const WhisperModelInfo* RecommendedWhisperModel(const std::vector<WhisperModelInfo>& catalog) {
    auto it = std::find_if(catalog.begin(), catalog.end(), [](const WhisperModelInfo& model) {
        return model.recommended;
    });
    return it != catalog.end() ? &*it : (catalog.empty() ? nullptr : &catalog.front());
}

std::wstring WhisperModelButtonText(const WhisperModelInfo& model) {
    std::wstring text = model.name;
    if (model.recommended) {
        text += L" · рекоменд.";
    } else if (model.bestQuality) {
        text += L" · лучшее качество";
    }
    const std::wstring size = FormatModelSize(model.sizeBytes);
    if (!size.empty()) {
        text += L" · " + size;
    }
    return text;
}

std::wstring BuildAffectedFilesMessage(const std::vector<std::filesystem::path>& affectedFiles) {
    std::wstring message = L"Будут перезаписаны или изменены следующие файлы:\n\n";
    for (const std::filesystem::path& path : affectedFiles) {
        message += L"- ";
        message += path.wstring();
        message += L"\n";
    }
    return message;
}

void RegisterDialogClasses(HINSTANCE instance);
LRESULT CALLBACK DialogWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK DialogButtonProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LogViewProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LogCopyMenuProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK SettingsComboMenuProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
HWND CreateLogView(HWND parent, HINSTANCE instance, const std::wstring& text);
LRESULT CALLBACK ScrollTextProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

HWND CreateDarkButton(HWND parent, HINSTANCE instance, const wchar_t* text, int id, bool primary, bool onCard = true) {
    auto* state = new ButtonState{};
    state->commandId = id;
    state->primary = primary;
    state->onCard = onCard;
    state->text = text;

    HWND button = CreateWindowExW(
        0,
        kDialogButtonClassName,
        text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0,
        0,
        10,
        10,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        instance,
        state
    );
    if (!button) {
        delete state;
    }
    return button;
}

ButtonState* GetButtonState(HWND button) {
    return reinterpret_cast<ButtonState*>(GetWindowLongPtrW(button, GWLP_USERDATA));
}

void SetDarkButtonState(HWND parent, int id, bool primary, const std::wstring& text = L"") {
    HWND button = GetDlgItem(parent, id);
    if (!button) {
        return;
    }
    ButtonState* state = GetButtonState(button);
    if (!state) {
        return;
    }
    const bool textChanged = !text.empty() && state->text != text;
    if (state->primary == primary && !textChanged) {
        return;
    }
    state->primary = primary;
    if (textChanged) {
        state->text = text;
    }
    InvalidateRect(button, nullptr, TRUE);
}

void SetDarkButtonEnabled(HWND parent, int id, bool enabled) {
    HWND button = GetDlgItem(parent, id);
    if (!button) {
        return;
    }
    ButtonState* state = GetButtonState(button);
    if (!state) {
        return;
    }
    state->enabled = enabled;
    if (!enabled) {
        state->pressed = false;
        state->hot = false;
    }
    InvalidateRect(button, nullptr, TRUE);
}

void RefreshWhisperModelButtons(DialogState* state) {
    if (!state || !state->window) {
        return;
    }
    for (size_t i = 0; i < state->whisperModels.size(); ++i) {
        SetDarkButtonState(
            state->window,
            IdWhisperModelBase + static_cast<int>(i),
            static_cast<int>(i) == state->selectedWhisperModelIndex,
            WhisperModelButtonText(state->whisperModels[i])
        );
    }
}

void RefreshVotCandidateButtons(DialogState* state) {
    if (!state || !state->window) {
        return;
    }
    for (size_t i = 0; i < state->votExecutableCandidates.size(); ++i) {
        SetDarkButtonState(
            state->window,
            IdVotCandidateBase + static_cast<int>(i),
            static_cast<int>(i) == state->selectedVotExecutableIndex,
            state->votExecutableCandidates[i].wstring()
        );
    }
}

void RefreshSettingsButtons(DialogState* state) {
    if (!state || !state->window) {
        return;
    }

    RECT client = {};
    GetClientRect(state->window, &client);
    const bool collapsed = IsSettingsSidebarCollapsed(state, client.right);

    SetDarkButtonState(state->window, IdSettingsToggleSidebar, false, collapsed ? L">" : L"<");
    SetDarkButtonState(state->window, IdSettingsNavDownloads, state->settingsSection == SettingsSection::Downloads, collapsed ? L"⬇" : L"Загрузки");
    SetDarkButtonState(state->window, IdSettingsNavTranscription, state->settingsSection == SettingsSection::Transcription, collapsed ? L"✎" : L"Транскрибация");
    SetDarkButtonState(state->window, IdSettingsNavTranslation, state->settingsSection == SettingsSection::Translation, collapsed ? TranslationSettingsCollapsedIcon() : L"Перевод");
    SetDarkButtonState(state->window, IdSettingsNavTools, state->settingsSection == SettingsSection::Tools, collapsed ? L"⚙" : L"Инструменты");
    SetDarkButtonState(state->window, IdSettingsNavAbout, state->settingsSection == SettingsSection::About, collapsed ? L"ⓘ" : L"О программе");

    SetDarkButtonState(state->window, 101, state->workingConfig.quality == L"audio");
    SetDarkButtonState(state->window, 102, state->workingConfig.quality == L"360p");
    SetDarkButtonState(state->window, 103, state->workingConfig.quality == L"480p");
    SetDarkButtonState(state->window, 104, state->workingConfig.quality == L"720p");
    SetDarkButtonState(state->window, 105, state->workingConfig.quality == L"1080p");
    SetDarkButtonState(state->window, 106, state->workingConfig.quality == L"max");

    SetDarkButtonState(state->window, 111, state->workingConfig.container == L"auto");
    SetDarkButtonState(state->window, 112, state->workingConfig.container == L"mp4");
    SetDarkButtonState(state->window, 113, state->workingConfig.container == L"mkv");
    SetDarkButtonState(state->window, 114, state->workingConfig.container == L"webm");

    SetDarkButtonState(
        state->window,
        IdAutoUpdate,
        state->workingConfig.autoUpdateApp,
        state->workingConfig.autoUpdateApp ? L"Автопроверка: Вкл" : L"Автопроверка: Выкл"
    );
    SetDarkButtonState(state->window, IdTranscriptionWhisper, state->workingConfig.transcriptionEngine == TranscriptionEngine::Whisper);
    SetDarkButtonState(state->window, IdTranscriptionVot, state->workingConfig.transcriptionEngine == TranscriptionEngine::Vot);
    SetDarkButtonState(state->window, IdVotSubtitleLanguageEdit, false, SettingsLanguageButtonText(state->workingConfig.votSubtitleLanguage));
    SetDarkButtonState(state->window, IdVoiceLanguageEdit, false, SettingsLanguageButtonText(state->workingConfig.voiceOverLanguage));
    SetDarkButtonState(state->window, IdVoiceModeOff, state->workingConfig.voiceOverFfmpegMode == VoiceOverFfmpegMode::Off, VoiceOverFfmpegModeDisplayText(VoiceOverFfmpegMode::Off));
    SetDarkButtonState(state->window, IdVoiceModeTrack, state->workingConfig.voiceOverFfmpegMode == VoiceOverFfmpegMode::AudioTrack, VoiceOverFfmpegModeDisplayText(VoiceOverFfmpegMode::AudioTrack));
    SetDarkButtonState(state->window, IdVoiceModeMix, state->workingConfig.voiceOverFfmpegMode == VoiceOverFfmpegMode::Mix, VoiceOverFfmpegModeDisplayText(VoiceOverFfmpegMode::Mix));
    SetDarkButtonState(state->window, IdSubtitleModeOff, state->workingConfig.subtitleFfmpegMode == SubtitleFfmpegMode::Off, SubtitleFfmpegModeDisplayText(SubtitleFfmpegMode::Off));
    SetDarkButtonState(state->window, IdSubtitleModeTrack, state->workingConfig.subtitleFfmpegMode == SubtitleFfmpegMode::SubtitleTrack, SubtitleFfmpegModeDisplayText(SubtitleFfmpegMode::SubtitleTrack));
    SetDarkButtonState(state->window, IdSubtitleModeBurn, state->workingConfig.subtitleFfmpegMode == SubtitleFfmpegMode::BurnIn, SubtitleFfmpegModeDisplayText(SubtitleFfmpegMode::BurnIn));
    SetDarkButtonState(state->window, IdYtDlpDetails, state->ytDlpDetailsExpanded, state->ytDlpDetailsExpanded ? L"Скрыть" : L"Подробно");
    SetDarkButtonState(state->window, IdFfmpegDetails, state->ffmpegDetailsExpanded, state->ffmpegDetailsExpanded ? L"Скрыть" : L"Подробно");
    SetDarkButtonState(state->window, IdWhisperDetails, state->whisperDetailsExpanded, state->whisperDetailsExpanded ? L"Скрыть" : L"Подробно");
    SetDarkButtonState(state->window, IdVotDetails, state->votDetailsExpanded, state->votDetailsExpanded ? L"Скрыть" : L"Подробно");

    const bool ffmpegReady = IsFfmpegReady(state);
    SetDarkButtonEnabled(state->window, IdSubtitleModeOff, true);
    SetDarkButtonEnabled(state->window, IdSubtitleModeTrack, ffmpegReady);
    SetDarkButtonEnabled(state->window, IdSubtitleModeBurn, ffmpegReady);
    SetDarkButtonEnabled(state->window, IdVoiceModeOff, true);
    SetDarkButtonEnabled(state->window, IdVoiceModeTrack, ffmpegReady);
    SetDarkButtonEnabled(state->window, IdVoiceModeMix, ffmpegReady);
}

HWND CreateScrollText(HWND parent, HINSTANCE instance, const std::wstring& text) {
    auto* state = new ScrollTextState{};
    state->text = text;

    HWND view = CreateWindowExW(
        0,
        kScrollTextClassName,
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0,
        0,
        10,
        10,
        parent,
        nullptr,
        instance,
        state
    );
    if (!view) {
        delete state;
    }
    return view;
}

void CenterWindow(HWND window, HWND owner, int width, int height) {
    RECT ownerRect = {};
    if (owner && IsWindow(owner)) {
        GetWindowRect(owner, &ownerRect);
    } else {
        ownerRect = {
            0,
            0,
            GetSystemMetrics(SM_CXSCREEN),
            GetSystemMetrics(SM_CYSCREEN)
        };
    }

    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
    SetWindowPos(window, nullptr, std::max(0, x), std::max(0, y), width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void RunModal(HWND owner, HWND window) {
    const bool ownerWasEnabled = owner && IsWindow(owner) && IsWindowEnabled(owner);
    if (ownerWasEnabled) {
        EnableWindow(owner, FALSE);
    }

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    MSG message = {};
    while (IsWindow(window) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        DialogState* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        RelayDialogTooltipMessage(state, message);
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    RestoreModalOwner(owner, ownerWasEnabled);
}

void ShowModal(DialogState* state, int width, int height) {
    RegisterDialogClasses(state->instance);

    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    DWORD exStyle = WS_EX_DLGMODALFRAME;
    if (state->type == DialogType::Logs) {
        style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    }

    HWND window = CreateWindowExW(
        exStyle,
        kDialogClassName,
        state->title.c_str(),
        style,
        0,
        0,
        width,
        height,
        state->owner,
        nullptr,
        state->instance,
        state
    );
    if (!window) {
        delete state;
        return;
    }

    CenterWindow(window, state->owner, width, height);
    RunModal(state->owner, window);
}

void LayoutMessageDialog(DialogState* state, int width, int height) {
    if (state->scrollText) {
        MoveWindow(state->scrollText, 24, 92, width - 48, height - 168, TRUE);
    }

    const int panelRight = width - kDialogPanelInset;
    const int panelBottom = height - kDialogPanelInset;
    const int buttonY = panelBottom - kDialogButtonInset - kDialogButtonHeight;
    HWND copyButton = GetDlgItem(state->window, IdCopy);
    HWND cancelButton = GetDlgItem(state->window, IdCancel);
    HWND okButton = GetDlgItem(state->window, IdOk);
    HWND updateButton = GetDlgItem(state->window, IdCheckUpdates);
    if (updateButton) {
        MoveWindow(updateButton, kDialogPanelInset + kDialogButtonInset, buttonY, 190, kDialogButtonHeight, TRUE);
    }
    if (copyButton) {
        MoveWindow(
            copyButton,
            panelRight - kDialogButtonInset - 112 - kDialogButtonGap - 140,
            buttonY,
            140,
            kDialogButtonHeight,
            TRUE
        );
    }
    if (cancelButton) {
        MoveWindow(
            cancelButton,
            panelRight - kDialogButtonInset - 112 - kDialogButtonGap - 112,
            buttonY,
            112,
            kDialogButtonHeight,
            TRUE
        );
    }
    if (okButton) {
        MoveWindow(okButton, panelRight - kDialogButtonInset - 112, buttonY, 112, kDialogButtonHeight, TRUE);
    }
}

void LayoutFfmpegDialog(DialogState* state, int width, int height) {
    const int panelLeft = kDialogPanelInset;
    const int panelRight = width - kDialogPanelInset;
    const int panelBottom = height - kDialogPanelInset;
    const int buttonY = panelBottom - kDialogButtonInset - kDialogButtonHeight;
    const int buttonLeft = panelLeft + kDialogButtonInset;
    const int availableWidth = panelRight - panelLeft - (kDialogButtonInset * 2);
    const std::vector<int> ids = state->type == DialogType::Whisper
        ? std::vector<int>{IdInstall, IdWhisperDownloadModel, IdChooseFolder, IdSkip}
        : (state->type == DialogType::Vot
            ? std::vector<int>{IdInstall, IdChooseFolder, IdChooseVotExecutable, IdSkip}
            : std::vector<int>{IdInstall, IdChooseFolder, IdSkip});
    const int buttonWidth = (availableWidth - (kDialogButtonGap * static_cast<int>(ids.size() - 1))) /
        static_cast<int>(ids.size());
    if (state->type == DialogType::Whisper && state->config) {
        const bool cudaAvailable = IsWhisperCudaCandidateAvailable();
        const WhisperBackend installBackend = SelectWhisperInstallBackend(state->config->whisperBackend, cudaAvailable);
        const ToolInstallStatus cpuStatus = state->paths
            ? WhisperManager::ResolveBackend(*state->paths, WhisperBackend::Cpu)
            : ToolInstallStatus{};
        const ToolInstallStatus cudaStatus = state->paths
            ? WhisperManager::ResolveBackend(*state->paths, WhisperBackend::Cuda)
            : ToolInstallStatus{};
        const bool installTargetInstalled = IsWhisperInstallTargetInstalled(
            installBackend,
            cpuStatus.installed,
            cudaStatus.installed
        );
        HWND install = GetDlgItem(state->window, IdInstall);
        if (install) {
            SetWindowTextW(
                install,
                WhisperInstallButtonText(
                    state->config->whisperBackend,
                    cudaAvailable,
                    installTargetInstalled
                ).c_str()
            );
        }
    }
    int x = buttonLeft;
    for (int id : ids) {
        HWND button = GetDlgItem(state->window, id);
        if (button) {
            MoveWindow(button, x, buttonY, buttonWidth, kDialogButtonHeight, TRUE);
            x += buttonWidth + kDialogButtonGap;
        }
    }
}

void LayoutWhisperModelDialog(DialogState* state, int width, int height) {
    const int panelLeft = kDialogPanelInset;
    const int panelRight = width - kDialogPanelInset;
    const int panelBottom = height - kDialogPanelInset;
    const int sidePadding = kDialogButtonInset;
    const int listTop = 118;
    const int rowHeight = 38;
    const int rowGap = 8;
    const int columnGap = 12;
    const int columnWidth = (panelRight - panelLeft - (sidePadding * 2) - columnGap) / 2;
    const int rowsPerColumn = static_cast<int>((state->whisperModels.size() + 1) / 2);

    for (size_t i = 0; i < state->whisperModels.size(); ++i) {
        HWND button = GetDlgItem(state->window, IdWhisperModelBase + static_cast<int>(i));
        if (!button) {
            continue;
        }
        const int column = static_cast<int>(i) / std::max(1, rowsPerColumn);
        const int row = static_cast<int>(i) % std::max(1, rowsPerColumn);
        const int x = panelLeft + sidePadding + (column * (columnWidth + columnGap));
        const int y = listTop + (row * (rowHeight + rowGap));
        MoveWindow(button, x, y, columnWidth, rowHeight, TRUE);
    }

    const int buttonY = panelBottom - kDialogButtonInset - kDialogButtonHeight;
    HWND download = GetDlgItem(state->window, IdWhisperModelDownloadSelected);
    HWND cancel = GetDlgItem(state->window, IdCancel);
    if (download) {
        MoveWindow(download, panelRight - sidePadding - 170 - kDialogButtonGap - 126, buttonY, 170, kDialogButtonHeight, TRUE);
    }
    if (cancel) {
        MoveWindow(cancel, panelRight - sidePadding - 126, buttonY, 126, kDialogButtonHeight, TRUE);
    }
}

void LayoutVotCandidateDialog(DialogState* state, int width, int height) {
    const int panelLeft = kDialogPanelInset;
    const int panelRight = width - kDialogPanelInset;
    const int panelBottom = height - kDialogPanelInset;
    const int sidePadding = kDialogButtonInset;
    const int listTop = 112;
    const int rowHeight = 38;
    const int rowGap = 8;
    const int listWidth = panelRight - panelLeft - (sidePadding * 2);

    for (size_t i = 0; i < state->votExecutableCandidates.size(); ++i) {
        HWND button = GetDlgItem(state->window, IdVotCandidateBase + static_cast<int>(i));
        if (!button) {
            continue;
        }
        const int y = listTop + static_cast<int>(i) * (rowHeight + rowGap);
        MoveWindow(button, panelLeft + sidePadding, y, listWidth, rowHeight, TRUE);
    }

    const int buttonY = panelBottom - kDialogButtonInset - kDialogButtonHeight;
    HWND select = GetDlgItem(state->window, IdVotCandidateSelect);
    HWND cancel = GetDlgItem(state->window, IdCancel);
    if (select) {
        MoveWindow(select, panelRight - sidePadding - 126 - kDialogButtonGap - 126, buttonY, 126, kDialogButtonHeight, TRUE);
    }
    if (cancel) {
        MoveWindow(cancel, panelRight - sidePadding - 126, buttonY, 126, kDialogButtonHeight, TRUE);
    }
}

void LayoutProgressDialog(DialogState* state, int width, int height) {
    const int panelRight = width - kDialogPanelInset;
    const int panelBottom = height - kDialogPanelInset;

    HWND cancel = GetDlgItem(state->window, IdCancel);
    if (cancel) {
        MoveWindow(
            cancel,
            panelRight - kDialogButtonInset - 112,
            panelBottom - kDialogButtonInset - kDialogButtonHeight,
            112,
            kDialogButtonHeight,
            TRUE
        );
    }
}

void LayoutLogsDialog(DialogState* state, int width, int height) {
    const int panelRight = width - kDialogPanelInset;
    const int panelBottom = height - kDialogPanelInset;
    const int buttonY = panelBottom - kDialogButtonInset - kDialogButtonHeight;

    HWND copyButton = GetDlgItem(state->window, IdCopy);
    HWND okButton = GetDlgItem(state->window, IdOk);
    if (copyButton) {
        MoveWindow(
            copyButton,
            panelRight - kDialogButtonInset - 112 - kDialogButtonGap - 150,
            buttonY,
            150,
            kDialogButtonHeight,
            TRUE
        );
    }
    if (okButton) {
        MoveWindow(okButton, panelRight - kDialogButtonInset - 112, buttonY, 112, kDialogButtonHeight, TRUE);
    }
    if (state->logView) {
        MoveWindow(
            state->logView,
            24,
            82,
            std::max(120, width - 48),
            std::max(80, buttonY - 98),
            TRUE
        );
    }
}

void LayoutSettingsDialog(DialogState* state, int width, int height) {
    RefreshSettingsButtons(state);

    const RECT sidebar = SettingsSidebarRect(state, width, height);
    const RECT content = SettingsContentRect(state, width, height);

    HWND toggle = GetDlgItem(state->window, IdSettingsToggleSidebar);
    if (toggle) {
        MoveWindow(toggle, sidebar.right - 46, sidebar.top + 16, 32, 32, TRUE);
    }
    const std::array<int, 4> navIds = {
        IdSettingsNavDownloads,
        IdSettingsNavTranscription,
        IdSettingsNavTranslation,
        IdSettingsNavTools
    };
    int navTop = sidebar.top + 70;
    for (int id : navIds) {
        HWND button = GetDlgItem(state->window, id);
        if (button) {
            MoveWindow(
                button,
                sidebar.left + 12,
                navTop,
                std::max(36, static_cast<int>(sidebar.right - sidebar.left - 24)),
                36,
                TRUE
            );
            navTop += 44;
        }
    }
    HWND aboutButton = GetDlgItem(state->window, IdSettingsNavAbout);
    if (aboutButton) {
        MoveWindow(
            aboutButton,
            sidebar.left + 12,
            sidebar.bottom - 12 - 36,
            std::max(36, static_cast<int>(sidebar.right - sidebar.left - 24)),
            36,
            TRUE
        );
    }

    const std::array<int, 6> qualityIds = {101, 102, 103, 104, 105, 106};
    RECT card = SettingsStackCardRect(state, width, height, 0, kSettingsChoiceCardHeight);
    int x = card.left + kSettingsCardPadding;
    const int qualityWidth = std::max(62, static_cast<int>((card.right - card.left - 2 * kSettingsCardPadding - 5 * 8) / 6));
    for (int id : qualityIds) {
        HWND button = GetDlgItem(state->window, id);
        if (button) {
            MoveWindow(button, x, card.top + kSettingsCardControlTop, qualityWidth, 32, TRUE);
            x += qualityWidth + 8;
        }
    }

    const std::array<int, 4> containerIds = {111, 112, 113, 114};
    card = SettingsStackCardRect(state, width, height, 1, kSettingsChoiceCardHeight);
    x = card.left + kSettingsCardPadding;
    const int containerWidth = std::max(76, static_cast<int>((card.right - card.left - 2 * kSettingsCardPadding - 3 * 10) / 4));
    for (int id : containerIds) {
        HWND button = GetDlgItem(state->window, id);
        if (button) {
            MoveWindow(button, x, card.top + kSettingsCardControlTop, containerWidth, 32, TRUE);
            x += containerWidth + 10;
        }
    }

    HWND ffmpeg = GetDlgItem(state->window, IdFfmpeg);
    HWND checkUpdates = GetDlgItem(state->window, IdCheckUpdates);
    HWND autoUpdate = GetDlgItem(state->window, IdAutoUpdate);
    HWND parallelMinus = GetDlgItem(state->window, IdParallelMinus);
    HWND parallelPlus = GetDlgItem(state->window, IdParallelPlus);
    HWND transcriptionWhisper = GetDlgItem(state->window, IdTranscriptionWhisper);
    HWND transcriptionVot = GetDlgItem(state->window, IdTranscriptionVot);
    HWND votSubtitleLanguage = GetDlgItem(state->window, IdVotSubtitleLanguageEdit);
    HWND chooseWhisper = GetDlgItem(state->window, IdChooseWhisperFolder);
    HWND chooseVot = GetDlgItem(state->window, IdChooseVotFolder);
    HWND subtitleOff = GetDlgItem(state->window, IdSubtitleModeOff);
    HWND subtitleTrack = GetDlgItem(state->window, IdSubtitleModeTrack);
    HWND subtitleBurn = GetDlgItem(state->window, IdSubtitleModeBurn);
    HWND voiceLanguage = GetDlgItem(state->window, IdVoiceLanguageEdit);
    HWND voiceOff = GetDlgItem(state->window, IdVoiceModeOff);
    HWND voiceTrack = GetDlgItem(state->window, IdVoiceModeTrack);
    HWND voiceMix = GetDlgItem(state->window, IdVoiceModeMix);
    HWND voiceVolumeMinus = GetDlgItem(state->window, IdVoiceVolumeMinus);
    HWND voiceVolumePlus = GetDlgItem(state->window, IdVoiceVolumePlus);
    HWND transcriptionTools = GetDlgItem(state->window, IdTranscriptionOpenTools);
    HWND translationTools = GetDlgItem(state->window, IdTranslationOpenTools);
    HWND chooseWhisperDetails = GetDlgItem(state->window, IdWhisperDetails);
    HWND chooseVotDetails = GetDlgItem(state->window, IdVotDetails);
    HWND ffmpegDetails = GetDlgItem(state->window, IdFfmpegDetails);
    HWND ytDlpDetails = GetDlgItem(state->window, IdYtDlpDetails);
    HWND cancel = GetDlgItem(state->window, IdCancel);
    HWND ok = GetDlgItem(state->window, IdOk);

    card = SettingsStackCardRect(state, width, height, 2, kSettingsBehaviorCardHeight);
    if (autoUpdate) {
        MoveWindow(autoUpdate, card.left + kSettingsCardPadding, card.top + 108, 210, 34, TRUE);
    }
    const RECT parallelValue = SettingsParallelValueRect(state, width, height);
    if (parallelMinus) {
        MoveWindow(parallelMinus, parallelValue.left - 46 - kDialogButtonGap, parallelValue.top, 46, 34, TRUE);
    }
    if (parallelPlus) {
        MoveWindow(parallelPlus, parallelValue.right + kDialogButtonGap, parallelValue.top, 46, 34, TRUE);
    }

    card = SettingsTranscriptionEngineCardRect(state, width, height);
    const int twoButtonWidth = std::max(120, static_cast<int>((card.right - card.left - 2 * kSettingsCardPadding - 12) / 2));
    if (transcriptionWhisper) {
        MoveWindow(transcriptionWhisper, card.left + kSettingsCardPadding, card.top + kSettingsCardControlTop, twoButtonWidth, 34, TRUE);
    }
    if (transcriptionVot) {
        MoveWindow(transcriptionVot, card.left + kSettingsCardPadding + twoButtonWidth + 12, card.top + kSettingsCardControlTop, twoButtonWidth, 34, TRUE);
    }
    card = SettingsTranscriptionSubtitleCardRect(state, width, height);
    const int transcriptionModeTop = card.top + kSettingsCardControlTop;
    if (subtitleOff) {
        MoveWindow(subtitleOff, card.left + kSettingsCardPadding, transcriptionModeTop, 86, 34, TRUE);
    }
    if (subtitleTrack) {
        MoveWindow(subtitleTrack, card.left + kSettingsCardPadding + 98, transcriptionModeTop, 164, 34, TRUE);
    }
    if (subtitleBurn) {
        MoveWindow(subtitleBurn, card.left + kSettingsCardPadding + 274, transcriptionModeTop, 142, 34, TRUE);
    }
    card = SettingsTranscriptionLanguageCardRect(state, width, height);
    if (votSubtitleLanguage) {
        MoveWindow(votSubtitleLanguage, card.left + kSettingsCardPadding, card.top + kSettingsCardControlTop, 150, 34, TRUE);
    }
    card = SettingsTranscriptionToolsCardRect(state, width, height);
    if (transcriptionTools) {
        MoveWindow(transcriptionTools, card.right - kSettingsCardPadding - 172, card.top + 28, 172, 34, TRUE);
    }

    card = SettingsTranslationWorkflowCardRect(state, width, height);
    const int translationModeTop = card.top + kSettingsCardControlTop;
    if (voiceLanguage) {
        MoveWindow(voiceLanguage, card.left + kSettingsCardPadding, translationModeTop, 150, 34, TRUE);
    }
    if (voiceOff) {
        MoveWindow(voiceOff, card.left + kSettingsCardPadding + 166, translationModeTop, 86, 34, TRUE);
    }
    if (voiceTrack) {
        MoveWindow(voiceTrack, card.left + kSettingsCardPadding + 264, translationModeTop, 142, 34, TRUE);
    }
    if (voiceMix) {
        MoveWindow(voiceMix, card.left + kSettingsCardPadding + 418, translationModeTop, 118, 34, TRUE);
    }
    const int voiceVolumeTop = card.top + 108;
    if (voiceVolumeMinus) {
        MoveWindow(voiceVolumeMinus, card.left + kSettingsCardPadding, voiceVolumeTop, 46, 34, TRUE);
    }
    if (voiceVolumePlus) {
        MoveWindow(voiceVolumePlus, card.left + kSettingsCardPadding + 118, voiceVolumeTop, 46, 34, TRUE);
    }
    card = SettingsTranslationToolsCardRect(state, width, height);
    if (translationTools) {
        MoveWindow(translationTools, card.right - kSettingsCardPadding - 172, card.top + 28, 172, 34, TRUE);
    }

    auto layoutToolCard = [&](int index, HWND action, HWND details) {
        const RECT toolCard = SettingsToolCardRect(state, width, height, index);
        const int actionWidth = index == 0 ? 0 : 142;
        const int detailsWidth = 112;
        const int y = toolCard.top + 18;
        if (details) {
            MoveWindow(details, toolCard.right - kSettingsCardPadding - detailsWidth, y, detailsWidth, 34, TRUE);
        }
        if (action) {
            MoveWindow(
                action,
                toolCard.right - kSettingsCardPadding - detailsWidth - kDialogButtonGap - actionWidth,
                y,
                actionWidth,
                34,
                TRUE
            );
        }
    };
    layoutToolCard(0, nullptr, ytDlpDetails);
    layoutToolCard(1, ffmpeg, ffmpegDetails);
    layoutToolCard(2, chooseWhisper, chooseWhisperDetails);
    layoutToolCard(3, chooseVot, chooseVotDetails);

    card = SettingsStackCardRect(state, width, height, 0, kSettingsAboutCardHeight);
    if (checkUpdates) {
        MoveWindow(checkUpdates, card.left + kSettingsCardPadding, card.top + 112, 188, 34, TRUE);
    }

    if (ffmpeg) {
        SetWindowTextW(ffmpeg, ToolSetupButtonText().c_str());
    }
    if (chooseWhisper) {
        SetWindowTextW(chooseWhisper, ToolSetupButtonText().c_str());
    }
    if (chooseVot) {
        SetWindowTextW(chooseVot, ToolSetupButtonText().c_str());
    }
    const int panelRight = width - kDialogPanelInset;
    const int bottomButtonY = SettingsBottomButtonY(height);
    if (cancel) {
        MoveWindow(
            cancel,
            panelRight - kDialogButtonInset - kSettingsBottomButtonWidth - kDialogButtonGap - kSettingsBottomButtonWidth,
            bottomButtonY,
            kSettingsBottomButtonWidth,
            kDialogButtonHeight,
            TRUE
        );
    }
    if (ok) {
        MoveWindow(ok, panelRight - kDialogButtonInset - kSettingsBottomButtonWidth, bottomButtonY, kSettingsBottomButtonWidth, kDialogButtonHeight, TRUE);
    }

    SetControlsVisible(state->window, {
        101, 102, 103, 104, 105, 106, 111, 112, 113, 114,
        IdAutoUpdate, IdParallelMinus, IdParallelPlus
    }, state->settingsSection == SettingsSection::Downloads);
    SetControlsVisible(state->window, {
        IdTranscriptionWhisper, IdTranscriptionVot, IdVotSubtitleLanguageEdit,
        IdSubtitleModeOff, IdSubtitleModeTrack, IdSubtitleModeBurn, IdTranscriptionOpenTools
    }, state->settingsSection == SettingsSection::Transcription);
    SetControlsVisible(state->window, {
        IdVoiceLanguageEdit, IdVoiceModeOff, IdVoiceModeTrack, IdVoiceModeMix,
        IdVoiceVolumeMinus, IdVoiceVolumePlus, IdTranslationOpenTools
    }, state->settingsSection == SettingsSection::Translation);
    SetControlsVisible(state->window, {
        IdFfmpeg, IdChooseWhisperFolder, IdChooseVotFolder,
        IdYtDlpDetails, IdFfmpegDetails, IdWhisperDetails, IdVotDetails
    }, state->settingsSection == SettingsSection::Tools);
    SetControlsVisible(state->window, {IdCheckUpdates}, state->settingsSection == SettingsSection::About);
}

void LayoutDialog(DialogState* state, int width, int height) {
    switch (state->type) {
    case DialogType::Info:
    case DialogType::Error:
    case DialogType::Confirmation:
    case DialogType::AffectedFiles:
    case DialogType::About:
        LayoutMessageDialog(state, width, height);
        break;
    case DialogType::Logs:
        LayoutLogsDialog(state, width, height);
        break;
    case DialogType::Ffmpeg:
    case DialogType::Whisper:
    case DialogType::Vot:
        LayoutFfmpegDialog(state, width, height);
        break;
    case DialogType::WhisperModel:
        LayoutWhisperModelDialog(state, width, height);
        break;
    case DialogType::VotCandidate:
        LayoutVotCandidateDialog(state, width, height);
        break;
    case DialogType::Progress:
        LayoutProgressDialog(state, width, height);
        break;
    case DialogType::Settings:
        LayoutSettingsDialog(state, width, height);
        break;
    }
}

void RelayoutDialog(DialogState* state) {
    if (!state || !state->window) {
        return;
    }
    RECT client = {};
    GetClientRect(state->window, &client);
    LayoutDialog(state, client.right, client.bottom);
}

void DrawDialogBackground(HDC dc, const RECT& client) {
    UiRenderer::DrawBackground(dc, client);

    RECT panel = {kDialogPanelInset, kDialogPanelInset, client.right - kDialogPanelInset, client.bottom - kDialogPanelInset};
    UiRenderer::DrawPanel(dc, panel);
}

void DrawMessageDialog(DialogState* state, HDC dc, const RECT& client) {
    DrawDialogBackground(dc, client);

    HFONT titleFont = CreateUiFont(-18, FW_SEMIBOLD);
    HFONT textFont = CreateUiFont(-15, FW_NORMAL);

    RECT titleRect = {24, 28, client.right - 24, 56};
    DrawTextBlock(dc, state->title, titleRect, kTextColor, titleFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const std::wstring subtitle = !state->subtitle.empty()
        ? state->subtitle
        : state->type == DialogType::Error
        ? L"Ошибка. Текст можно скопировать для диагностики."
        : (state->type == DialogType::Confirmation
            ? L"Доступно обновление приложения."
            : L"Информация приложения.");
    RECT subtitleRect = {24, 56, client.right - 24, 82};
    DrawTextBlock(dc, subtitle, subtitleRect, kMutedTextColor, textFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    DeleteObject(titleFont);
    DeleteObject(textFont);
}

void DrawUtilityStatusLine(
    HDC dc,
    const std::wstring& label,
    const std::filesystem::path& path,
    int left,
    int top,
    int right,
    HFONT textFont
);
void DrawRoundedPanel(HDC dc, const RECT& rect, Gdiplus::Color fill, Gdiplus::Color border, int radius);
void DrawToolStatusPill(HDC dc, const RECT& rect, const std::wstring& text, bool ok, HFONT smallFont);

void DrawFfmpegDialog(DialogState* state, HDC dc, const RECT& client) {
    DrawDialogBackground(dc, client);

    HFONT titleFont = CreateUiFont(-18, FW_SEMIBOLD);
    HFONT textFont = CreateUiFont(-15, FW_NORMAL);
    HFONT smallFont = CreateUiFont(-13, FW_NORMAL);

    RECT titleRect = {24, 28, client.right - 24, 58};
    DrawTextBlock(dc, state->title, titleRect, kTextColor, titleFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (state->type == DialogType::Whisper) {
        RECT statusCard = {24, 78, client.right - 24, client.bottom - 86};
        DrawRoundedPanel(dc, statusCard, Gdiplus::Color(255, 32, 32, 35), Gdiplus::Color(255, 48, 48, 54), 8);
        const ToolInstallStatus status = state->paths && state->config
            ? WhisperManager::Resolve(*state->paths, *state->config)
            : ToolInstallStatus{};
        const std::filesystem::path modelPath = ResolveDialogWhisperModelPath(state);
        const bool modelReady = IsRegularFile(modelPath);
        DrawToolStatusPill(dc, {statusCard.left + 18, statusCard.top + 18, statusCard.left + 112, statusCard.top + 42}, status.installed ? L"Готов" : L"Нет", status.installed, smallFont);
        DrawToolStatusPill(dc, {statusCard.left + 122, statusCard.top + 18, statusCard.left + 232, statusCard.top + 42}, modelReady ? L"Модель" : L"Нет модели", modelReady, smallFont);
        DrawUtilityStatusLine(dc, L"whisper-cli.exe:", status.executable, statusCard.left + 18, statusCard.top + 58, statusCard.right - 18, textFont);
        DrawUtilityStatusLine(dc, L"Модель:", modelReady ? modelPath : std::filesystem::path{}, statusCard.left + 18, statusCard.top + 86, statusCard.right - 18, textFont);
        DrawTextBlock(
            dc,
            L"Установите Whisper.cpp, затем выберите или скачайте модель распознавания.",
            {statusCard.left + 18, statusCard.top + 116, statusCard.right - 18, statusCard.top + 142},
            kMutedTextColor,
            smallFont,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
        );
    } else {
        RECT messageRect = {24, 76, client.right - 24, 206};
        DrawTextBlock(
            dc,
            state->message,
            messageRect,
            kTextColor,
            textFont,
            DT_LEFT | DT_WORDBREAK
        );
    }

    DeleteObject(titleFont);
    DeleteObject(textFont);
    DeleteObject(smallFont);
}

void DrawWhisperModelDialog(DialogState* state, HDC dc, const RECT& client) {
    DrawDialogBackground(dc, client);

    HFONT titleFont = CreateUiFont(-18, FW_SEMIBOLD);
    HFONT textFont = CreateUiFont(-14, FW_NORMAL);
    HFONT smallFont = CreateUiFont(-12, FW_NORMAL);

    DrawTextBlock(dc, L"Модель Whisper", {24, 24, client.right - 24, 54}, kTextColor, titleFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(
        dc,
        L"Выберите модель распознавания. Большие модели точнее, компактные быстрее скачиваются и работают.",
        {24, 54, client.right - 24, 82},
        kMutedTextColor,
        textFont,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
    );

    if (!state->whisperModels.empty()) {
        const int selected = std::clamp(
            state->selectedWhisperModelIndex,
            0,
            static_cast<int>(state->whisperModels.size()) - 1
        );
        const WhisperModelInfo& model = state->whisperModels[static_cast<size_t>(selected)];
        std::wstring selectedText = L"Выбрано: " + model.name + L" · " + model.tags;
        DrawTextBlock(
            dc,
            selectedText,
            {24, 84, client.right - 24, 108},
            kTextColor,
            smallFont,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
        );
    }

    DeleteObject(titleFont);
    DeleteObject(textFont);
    DeleteObject(smallFont);
}

void DrawVotCandidateDialog(DialogState* state, HDC dc, const RECT& client) {
    DrawDialogBackground(dc, client);

    HFONT titleFont = CreateUiFont(-18, FW_SEMIBOLD);
    HFONT textFont = CreateUiFont(-14, FW_NORMAL);
    HFONT smallFont = CreateUiFont(-12, FW_NORMAL);

    DrawTextBlock(dc, L"Выберите VOT helper", {24, 24, client.right - 24, 54}, kTextColor, titleFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(
        dc,
        L"В выбранной папке найдено несколько vot-helper.exe. Укажите, какой использовать.",
        {24, 54, client.right - 24, 82},
        kMutedTextColor,
        textFont,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
    );

    if (!state->votExecutableCandidates.empty()) {
        const int selected = std::clamp(
            state->selectedVotExecutableIndex,
            0,
            static_cast<int>(state->votExecutableCandidates.size()) - 1
        );
        const std::wstring selectedText = L"Выбрано: " + state->votExecutableCandidates[static_cast<size_t>(selected)].wstring();
        DrawTextBlock(
            dc,
            selectedText,
            {24, 84, client.right - 24, 108},
            kTextColor,
            smallFont,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
        );
    }

    DeleteObject(titleFont);
    DeleteObject(textFont);
    DeleteObject(smallFont);
}

void DrawProgressDialog(DialogState* state, HDC dc, const RECT& client) {
    DrawDialogBackground(dc, client);

    HFONT titleFont = CreateUiFont(-18, FW_SEMIBOLD);
    HFONT textFont = CreateUiFont(-15, FW_NORMAL);
    HFONT smallFont = CreateUiFont(-13, FW_NORMAL);

    RECT titleRect = {24, 28, client.right - 24, 58};
    DrawTextBlock(dc, state->title, titleRect, kTextColor, titleFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT statusRect = {24, 72, client.right - 24, 102};
    DrawTextBlock(
        dc,
        state->message,
        statusRect,
        kTextColor,
        textFont,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
    );

    const std::wstring sizes = FormatProgressBytes(state->progressDownloaded, state->progressTotal);
    if (!sizes.empty()) {
        RECT sizesRect = {24, 104, client.right - 24, 126};
        DrawTextBlock(dc, sizes, sizesRect, kMutedTextColor, smallFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    const int percent = state->progressSuccess
        ? 100
        : CalculateProgressPercent(state->progressDownloaded, state->progressTotal);
    RECT progressRect = {24, 136, client.right - 84, 144};
    UiRenderer::DrawProgressBar(dc, progressRect, percent);

    RECT percentRect = {client.right - 74, 130, client.right - 24, 150};
    DrawTextBlock(
        dc,
        std::to_wstring(percent) + L"%",
        percentRect,
        kMutedTextColor,
        smallFont,
        DT_RIGHT | DT_VCENTER | DT_SINGLELINE
    );

    DeleteObject(titleFont);
    DeleteObject(textFont);
    DeleteObject(smallFont);
}

void DrawLogsDialog(DialogState* state, HDC dc, const RECT& client) {
    UNREFERENCED_PARAMETER(state);
    DrawDialogBackground(dc, client);

    HFONT titleFont = CreateUiFont(-18, FW_SEMIBOLD);
    HFONT textFont = CreateUiFont(-14, FW_NORMAL);

    RECT titleRect = {24, 28, client.right - 24, 58};
    DrawTextBlock(dc, L"Логи", titleRect, kTextColor, titleFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT subtitleRect = {24, 56, client.right - 24, 78};
    DrawTextBlock(
        dc,
        L"Выделите нужные строки или скопируйте весь текущий лог.",
        subtitleRect,
        kMutedTextColor,
        textFont,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
    );

    DeleteObject(titleFont);
    DeleteObject(textFont);
}

void DrawRoundedPanel(HDC dc, const RECT& rect, Gdiplus::Color fill, Gdiplus::Color border, int radius) {
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    Gdiplus::GraphicsPath path;
    AddRoundedRect(path, rect, radius);
    Gdiplus::SolidBrush fillBrush(fill);
    Gdiplus::Pen borderPen(border, 1.0f);
    graphics.FillPath(&fillBrush, &path);
    graphics.DrawPath(&borderPen, &path);
}

void DrawSettingsCard(
    HDC dc,
    const RECT& rect,
    const std::wstring& title,
    const std::wstring& subtitle,
    HFONT labelFont,
    HFONT textFont,
    int rightReserve = 18
) {
    DrawRoundedPanel(dc, rect, Gdiplus::Color(255, 35, 35, 38), Gdiplus::Color(255, 48, 48, 52), 8);
    RECT titleRect = {rect.left + kSettingsCardPadding, rect.top + 14, rect.right - rightReserve, rect.top + 38};
    DrawTextBlock(dc, title, titleRect, kTextColor, labelFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT subtitleRect = {rect.left + kSettingsCardPadding, rect.top + 40, rect.right - rightReserve, rect.top + 64};
    DrawTextBlock(dc, subtitle, subtitleRect, kMutedTextColor, textFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawToolStatusPill(HDC dc, const RECT& rect, const std::wstring& text, bool ok, HFONT smallFont) {
    DrawRoundedPanel(
        dc,
        rect,
        ok ? Gdiplus::Color(255, 28, 53, 39) : Gdiplus::Color(255, 56, 37, 41),
        ok ? Gdiplus::Color(255, 70, 124, 91) : Gdiplus::Color(255, 122, 58, 66),
        8
    );
    DrawTextBlock(
        dc,
        text,
        rect,
        ok ? RGB(171, 230, 193) : RGB(255, 190, 198),
        smallFont,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
    );
}

void DrawUtilityStatusPath(
    HDC dc,
    const std::wstring& label,
    const std::filesystem::path& path,
    int left,
    int top,
    int right,
    HFONT textFont
) {
    RECT labelRect = {left, top, right, top + 20};
    DrawTextBlock(dc, label, labelRect, kMutedTextColor, textFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT pathRect = {left, top + 22, right, top + 44};
    const std::wstring text = path.empty() ? L"-" : path.wstring();
    DrawTextBlock(dc, text, pathRect, kMutedTextColor, textFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawUtilityStatusLine(
    HDC dc,
    const std::wstring& label,
    const std::filesystem::path& path,
    int left,
    int top,
    int right,
    HFONT textFont
) {
    const std::wstring text = label + L" " + (path.empty() ? L"-" : path.wstring());
    DrawTextBlock(
        dc,
        text,
        {left, top, right, top + 20},
        kMutedTextColor,
        textFont,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
    );
}

void DrawSettingsDialog(DialogState* state, HDC dc, const RECT& client) {
    DrawDialogBackground(dc, client);

    HFONT titleFont = CreateUiFont(-22, FW_SEMIBOLD);
    HFONT labelFont = CreateUiFont(-15, FW_SEMIBOLD);
    HFONT textFont = CreateUiFont(-14, FW_NORMAL);
    HFONT smallFont = CreateUiFont(-12, FW_NORMAL);

    const RECT sidebar = SettingsSidebarRect(state, client.right, client.bottom);
    const RECT content = SettingsContentRect(state, client.right, client.bottom);
    const bool collapsed = IsSettingsSidebarCollapsed(state, client.right);
    DrawRoundedPanel(dc, sidebar, Gdiplus::Color(255, 25, 25, 28), Gdiplus::Color(255, 48, 48, 52), 8);
    if (!collapsed) {
        DrawTextBlock(
            dc,
            L"Настройки",
            {sidebar.left + 16, sidebar.top + 18, sidebar.right - 54, sidebar.top + 52},
            kTextColor,
            labelFont,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE
        );
    }

    std::wstring sectionTitle;
    std::wstring sectionSubtitle;
    switch (state->settingsSection) {
    case SettingsSection::Transcription:
        sectionTitle = L"Транскрибация";
        sectionSubtitle = L"Движок, субтитры и перевод VOT-субтитров.";
        break;
    case SettingsSection::Translation:
        sectionTitle = L"Перевод";
        sectionSubtitle = L"Целевой язык озвучки и FFmpeg-интеграция перевода.";
        break;
    case SettingsSection::Tools:
        sectionTitle = L"Инструменты";
        sectionSubtitle = L"Статус yt-dlp, FFmpeg, Whisper.cpp и Voice Over Translation.";
        break;
    case SettingsSection::About:
        sectionTitle = L"О программе";
        sectionSubtitle = L"Версия приложения и обновления.";
        break;
    case SettingsSection::Downloads:
    default:
        sectionTitle = L"Загрузки";
        sectionSubtitle = L"Качество, контейнер и поведение новых задач.";
        break;
    }
    DrawTextBlock(dc, sectionTitle, {content.left, content.top + 2, content.right, content.top + 36}, kTextColor, titleFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(dc, sectionSubtitle, {content.left, content.top + 36, content.right, content.top + 64}, kMutedTextColor, textFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    const bool ffmpegReady = IsFfmpegReady(state);
    if (state->settingsSection == SettingsSection::Downloads) {
        DrawSettingsCard(dc, SettingsStackCardRect(state, client.right, client.bottom, 0, kSettingsChoiceCardHeight), L"Качество", L"Качество по умолчанию для новых загрузок.", labelFont, textFont);
        DrawSettingsCard(dc, SettingsStackCardRect(state, client.right, client.bottom, 1, kSettingsChoiceCardHeight), L"Контейнер", L"Формат итогового файла без изменения схемы имен.", labelFont, textFont);
        const RECT behaviorCard = SettingsStackCardRect(state, client.right, client.bottom, 2, kSettingsBehaviorCardHeight);
        DrawSettingsCard(dc, behaviorCard, L"Поведение", L"Автопроверка обновлений и параллельные загрузки.", labelFont, textFont);
        DrawTextBlock(
            dc,
            L"Параллельные загрузки",
            {
                behaviorCard.left + kSettingsCardPadding,
                behaviorCard.top + kSettingsCardControlTop,
                behaviorCard.right - 180,
                behaviorCard.top + kSettingsCardControlTop + 34
            },
            kTextColor,
            textFont,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
        );
        DrawTextBlock(dc, std::to_wstring(state->workingConfig.maxParallelDownloads), SettingsParallelValueRect(state, client.right, client.bottom), kTextColor, labelFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else if (state->settingsSection == SettingsSection::Transcription) {
        DrawSettingsCard(dc, SettingsTranscriptionEngineCardRect(state, client.right, client.bottom), L"Движок", L"Whisper.cpp или vot-helper.exe для создания TXT/SRT.", labelFont, textFont);
        DrawSettingsCard(
            dc,
            SettingsTranscriptionSubtitleCardRect(state, client.right, client.bottom),
            L"Субтитры",
            ffmpegReady ? L"FFmpeg-режимы доступны." : L"FFmpeg-режимы видимы, но требуют установленный FFmpeg.",
            labelFont,
            textFont
        );
        DrawSettingsCard(
            dc,
            SettingsTranscriptionLanguageCardRect(state, client.right, client.bottom),
            L"Перевод VOT-субтитров",
            L"Используется только при движке VOT.",
            labelFont,
            textFont
        );
        DrawSettingsCard(dc, SettingsTranscriptionToolsCardRect(state, client.right, client.bottom), L"Инструменты", L"Пути и установка доступны в разделе Инструменты.", labelFont, textFont, 220);
    } else if (state->settingsSection == SettingsSection::Translation) {
        DrawSettingsCard(
            dc,
            SettingsTranslationWorkflowCardRect(state, client.right, client.bottom),
            L"Язык и интеграция",
            ffmpegReady ? L"MP3 создается всегда; FFmpeg может встроить или смешать озвучку." : L"MP3 создается всегда; режимы видео требуют FFmpeg.",
            labelFont,
            textFont
        );
        const RECT translationCard = SettingsTranslationWorkflowCardRect(state, client.right, client.bottom);
        DrawTextBlock(
            dc,
            std::to_wstring(std::clamp(state->workingConfig.voiceOverOriginalVolumePercent, 0, 100)) + L"%",
            {translationCard.left + kSettingsCardPadding + 54, translationCard.top + 108, translationCard.left + kSettingsCardPadding + 110, translationCard.top + 142},
            kTextColor,
            labelFont,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE
        );
        DrawTextBlock(
            dc,
            L"Громкость оригинала при смешивании",
            {translationCard.left + kSettingsCardPadding + 180, translationCard.top + 108, translationCard.right - kSettingsCardPadding, translationCard.top + 142},
            kMutedTextColor,
            textFont,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
        );
        DrawSettingsCard(dc, SettingsTranslationToolsCardRect(state, client.right, client.bottom), L"Инструменты", L"Путь vot-helper.exe задается в разделе Инструменты.", labelFont, textFont, 220);
    } else if (state->settingsSection == SettingsSection::Tools) {
        const ToolInstallStatus ytDlpStatus = state->paths ? YtDlpManager(*state->paths).Status() : ToolInstallStatus{};
        RECT toolCard = SettingsToolCardRect(state, client.right, client.bottom, 0);
        DrawSettingsCard(dc, toolCard, L"yt-dlp", ytDlpStatus.installed ? L"Основной загрузчик найден." : L"Основной загрузчик не найден.", labelFont, textFont, 280);
        DrawToolStatusPill(dc, {toolCard.left + 18, toolCard.top + 68, toolCard.left + 112, toolCard.top + 92}, ytDlpStatus.installed ? L"Готов" : L"Нет", ytDlpStatus.installed, smallFont);
        DrawToolStatusPill(dc, {toolCard.left + 122, toolCard.top + 68, toolCard.left + 246, toolCard.top + 92}, ytDlpStatus.version.empty() ? L"Версия" : ytDlpStatus.version, ytDlpStatus.installed, smallFont);
        if (state->ytDlpDetailsExpanded) {
            DrawUtilityStatusLine(dc, L"Путь:", ytDlpStatus.executable, toolCard.left + 18, toolCard.top + 98, toolCard.right - 18, textFont);
        }

        const FfmpegStatus ffmpegStatus = state->paths
            ? FfmpegManager::Resolve(*state->paths, state->workingConfig)
            : FfmpegManager::ResolveUserPath(state->workingConfig.ffmpegPath);
        toolCard = SettingsToolCardRect(state, client.right, client.bottom, 1);
        DrawSettingsCard(dc, toolCard, L"FFmpeg", ffmpegStatus.available ? L"Готов для контейнеров, субтитров и аудиодорожек." : L"Не найден или путь недоступен.", labelFont, textFont, 280);
        DrawToolStatusPill(dc, {toolCard.left + 18, toolCard.top + 68, toolCard.left + 112, toolCard.top + 92}, ffmpegStatus.available ? L"Готов" : L"Нет", ffmpegStatus.available, smallFont);
        if (state->ffmpegDetailsExpanded) {
            DrawUtilityStatusLine(dc, ffmpegStatus.available ? L"Путь:" : L"Статус:", ffmpegStatus.ffmpegExe, toolCard.left + 18, toolCard.top + 98, toolCard.right - 18, textFont);
        }

        const ToolInstallStatus whisperStatus = state->paths ? WhisperManager::Resolve(*state->paths, state->workingConfig) : ToolInstallStatus{};
        std::filesystem::path modelPath = state->workingConfig.whisperModelPath;
        if (modelPath.empty() && state->paths) {
            modelPath = state->paths->localWhisperModelPath();
        }
        std::error_code modelEc;
        const bool modelReady = !modelPath.empty() && std::filesystem::is_regular_file(modelPath, modelEc);
        toolCard = SettingsToolCardRect(state, client.right, client.bottom, 2);
        DrawSettingsCard(dc, toolCard, L"Whisper.cpp", whisperStatus.installed ? L"whisper-cli.exe найден." : L"Нужен для локальной транскрибации.", labelFont, textFont, 280);
        DrawToolStatusPill(dc, {toolCard.left + 18, toolCard.top + 68, toolCard.left + 112, toolCard.top + 92}, whisperStatus.installed ? L"Готов" : L"Нет", whisperStatus.installed, smallFont);
        DrawToolStatusPill(dc, {toolCard.left + 122, toolCard.top + 68, toolCard.left + 232, toolCard.top + 92}, modelReady ? L"Модель" : L"Нет модели", modelReady, smallFont);
        const bool cudaAvailable = IsWhisperCudaCandidateAvailable();
        DrawToolStatusPill(
            dc,
            {toolCard.left + 242, toolCard.top + 68, toolCard.left + 352, toolCard.top + 92},
            WhisperBackendStatusText(state->workingConfig.whisperBackend, whisperStatus.whisperBackend, cudaAvailable),
            state->workingConfig.whisperBackend != WhisperBackend::Cuda ||
                (cudaAvailable && whisperStatus.whisperBackend == WhisperBackend::Cuda),
            smallFont
        );
        if (state->whisperDetailsExpanded) {
            DrawUtilityStatusLine(dc, L"whisper-cli.exe:", whisperStatus.executable, toolCard.left + 18, toolCard.top + 98, toolCard.right - 18, textFont);
            DrawUtilityStatusLine(dc, L"Модель:", modelPath, toolCard.left + 18, toolCard.top + 120, toolCard.right - 18, textFont);
        }

        const VotExeStatus votStatus = state->paths
            ? VotExeManager::Resolve(*state->paths, state->workingConfig)
            : VotExeManager::ResolveUserPath(state->workingConfig.votExePath);
        toolCard = SettingsToolCardRect(state, client.right, client.bottom, 3);
        DrawSettingsCard(dc, toolCard, L"Voice Over Translation", votStatus.available ? L"vot-helper.exe найден." : (votStatus.message.empty() ? L"vot-helper.exe не найден." : votStatus.message), labelFont, textFont, 280);
        DrawToolStatusPill(dc, {toolCard.left + 18, toolCard.top + 68, toolCard.left + 112, toolCard.top + 92}, votStatus.available ? L"Готов" : L"Нет", votStatus.available, smallFont);
        DrawToolStatusPill(dc, {toolCard.left + 122, toolCard.top + 68, toolCard.left + 202, toolCard.top + 92}, L"EXE", votStatus.available, smallFont);
        if (state->votDetailsExpanded) {
            DrawUtilityStatusLine(dc, L"vot-helper.exe:", votStatus.executable, toolCard.left + 18, toolCard.top + 98, toolCard.right - 18, textFont);
        }
    } else {
        const RECT aboutCard = SettingsStackCardRect(state, client.right, client.bottom, 0, kSettingsAboutCardHeight);
        DrawSettingsCard(dc, aboutCard, L"YouTube Downloader", L"Портативный Win32-загрузчик с yt-dlp, FFmpeg, Whisper.cpp и VOT.", labelFont, textFont);
        DrawTextBlock(
            dc,
            L"Версия: " YTD_APP_VERSION_WIDE,
            {aboutCard.left + kSettingsCardPadding, aboutCard.top + 78, aboutCard.right - kSettingsCardPadding, aboutCard.top + 104},
            kTextColor,
            textFont,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE
        );
    }

    DeleteObject(titleFont);
    DeleteObject(labelFont);
    DeleteObject(textFont);
    DeleteObject(smallFont);
}

void CreateMessageControls(DialogState* state) {
    state->scrollText = CreateScrollText(state->window, state->instance, state->message);
    if (state->type == DialogType::Confirmation || state->type == DialogType::AffectedFiles) {
        const std::wstring cancelText = state->cancelButtonText.empty() ? L"Позже" : state->cancelButtonText;
        const std::wstring primaryText = state->primaryButtonText.empty() ? L"Установить" : state->primaryButtonText;
        HWND laterButton = CreateDarkButton(state->window, state->instance, cancelText.c_str(), IdCancel, false, false);
        HWND primaryButton = CreateDarkButton(state->window, state->instance, primaryText.c_str(), IdOk, true, false);
        AddDialogTooltip(state, laterButton, L"Закрывает окно без продолжения.");
        AddDialogTooltip(state, primaryButton, L"Выполняет основное действие этого окна.");
        return;
    }
    if (state->type == DialogType::About) {
        HWND updateButton = CreateDarkButton(state->window, state->instance, L"Проверить обновление", IdCheckUpdates, false, false);
        AddDialogTooltip(state, updateButton, L"Проверяет наличие новой версии приложения.");
    }
    HWND copyButton = CreateDarkButton(state->window, state->instance, L"Скопировать", IdCopy, false, false);
    HWND okButton = CreateDarkButton(state->window, state->instance, L"OK", IdOk, true, false);
    AddDialogTooltip(state, copyButton, L"Копирует текст этого окна в буфер обмена.");
    AddDialogTooltip(state, okButton, L"Закрывает окно.");
}

void CreateLogsControls(DialogState* state) {
    state->logView = CreateLogView(state->window, state->instance, state->message);
    HWND copyButton = CreateDarkButton(state->window, state->instance, L"Скопировать всё", IdCopy, false, false);
    HWND okButton = CreateDarkButton(state->window, state->instance, L"Закрыть", IdOk, true, false);
    AddDialogTooltip(state, copyButton, L"Копирует весь текущий лог в буфер обмена.");
    AddDialogTooltip(state, okButton, L"Закрывает окно логов.");
}

void CreateFfmpegControls(DialogState* state) {
    HWND installButton = CreateDarkButton(state->window, state->instance, L"Установить", IdInstall, true, false);
    HWND folderButton = CreateDarkButton(state->window, state->instance, L"Выбрать папку", IdChooseFolder, false, false);
    HWND skipButton = CreateDarkButton(state->window, state->instance, L"Пропустить", IdSkip, false, false);
    AddDialogTooltip(state, installButton, L"Скачивает и настраивает локальный FFmpeg для объединения видео и аудио.");
    AddDialogTooltip(state, skipButton, L"Закрывает окно без настройки FFmpeg.");
    if (folderButton) {
        state->tooltip = CreateTooltip(
            state->window,
            folderButton,
            L"Выберите папку, где находится ffmpeg.exe, или папку выше, содержащую bin\\ffmpeg.exe, ffprobe.exe и ffplay.exe."
        );
    }
}

void InvalidateProgressContent(HWND window) {
    RECT client = {};
    GetClientRect(window, &client);
    RECT progressContent = {20, 68, client.right - 20, 154};
    InvalidateRect(window, &progressContent, FALSE);
}

void CreateWhisperControls(DialogState* state) {
    HWND installButton = CreateDarkButton(state->window, state->instance, L"Установить", IdInstall, true, false);
    HWND modelButton = CreateDarkButton(state->window, state->instance, L"Скачать модель", IdWhisperDownloadModel, false, false);
    HWND folderButton = CreateDarkButton(state->window, state->instance, L"Выбрать папку", IdChooseFolder, false, false);
    HWND skipButton = CreateDarkButton(state->window, state->instance, L"Закрыть", IdSkip, false, false);
    AddDialogTooltip(state, installButton, L"Скачивает и настраивает whisper.cpp CPU.");
    AddDialogTooltip(state, modelButton, L"Скачивает рекомендуемую модель Whisper.");
    AddDialogTooltip(state, folderButton, L"Выберите папку, где находится whisper-cli.exe.");
    AddDialogTooltip(state, skipButton, L"Закрывает окно без изменений.");
}

void CreateVotControls(DialogState* state) {
    HWND installButton = CreateDarkButton(state->window, state->instance, L"Установить", IdInstall, true, false);
    HWND folderButton = CreateDarkButton(state->window, state->instance, L"Выбрать папку", IdChooseFolder, false, false);
    HWND exeButton = CreateDarkButton(state->window, state->instance, L"Выбрать EXE", IdChooseVotExecutable, false, false);
    HWND skipButton = CreateDarkButton(state->window, state->instance, L"Закрыть", IdSkip, false, false);
    AddDialogTooltip(state, installButton, L"Скачивает и настраивает vot-helper.exe.");
    AddDialogTooltip(state, folderButton, L"Выберите папку, где находится vot-helper.exe.");
    AddDialogTooltip(state, exeButton, L"Выберите конкретный файл vot-helper.exe.");
    AddDialogTooltip(state, skipButton, L"Закрывает окно без изменений.");
}

void CreateWhisperModelControls(DialogState* state) {
    if (!state) {
        return;
    }
    state->whisperModels = WhisperManager::ModelCatalog();
    const WhisperModelInfo* recommended = RecommendedWhisperModel(state->whisperModels);
    if (recommended) {
        const auto it = std::find_if(state->whisperModels.begin(), state->whisperModels.end(), [recommended](const WhisperModelInfo& model) {
            return model.id == recommended->id;
        });
        if (it != state->whisperModels.end()) {
            state->selectedWhisperModelIndex = static_cast<int>(std::distance(state->whisperModels.begin(), it));
        }
    }
    for (size_t i = 0; i < state->whisperModels.size(); ++i) {
        HWND button = CreateDarkButton(
            state->window,
            state->instance,
            WhisperModelButtonText(state->whisperModels[i]).c_str(),
            IdWhisperModelBase + static_cast<int>(i),
            static_cast<int>(i) == state->selectedWhisperModelIndex,
            false
        );
        AddDialogTooltip(state, button, state->whisperModels[i].description.c_str());
    }
    HWND downloadButton = CreateDarkButton(state->window, state->instance, L"Скачать выбранную", IdWhisperModelDownloadSelected, true, false);
    HWND cancelButton = CreateDarkButton(state->window, state->instance, L"Отмена", IdCancel, false, false);
    AddDialogTooltip(state, downloadButton, L"Скачивает выбранную модель Whisper.");
    AddDialogTooltip(state, cancelButton, L"Закрывает окно без скачивания.");
}

void CreateVotCandidateControls(DialogState* state) {
    if (!state) {
        return;
    }
    for (size_t i = 0; i < state->votExecutableCandidates.size(); ++i) {
        HWND button = CreateDarkButton(
            state->window,
            state->instance,
            state->votExecutableCandidates[i].wstring().c_str(),
            IdVotCandidateBase + static_cast<int>(i),
            static_cast<int>(i) == state->selectedVotExecutableIndex,
            false
        );
        AddDialogTooltip(state, button, state->votExecutableCandidates[i].wstring());
    }
    HWND selectButton = CreateDarkButton(state->window, state->instance, L"Выбрать", IdVotCandidateSelect, true, false);
    HWND cancelButton = CreateDarkButton(state->window, state->instance, L"Отмена", IdCancel, false, false);
    AddDialogTooltip(state, selectButton, L"Использовать выбранный vot-helper.exe.");
    AddDialogTooltip(state, cancelButton, L"Закрывает окно без выбора.");
}

void StartFfmpegInstallWorker(DialogState* state) {
    if (!state || !state->paths || !state->config || !state->cancelEvent) {
        return;
    }

    HWND window = state->window;
    const AppPaths paths = *state->paths;
    AppConfig* config = state->config;
    HANDLE cancelEvent = state->cancelEvent;

    state->worker = std::jthread([state, window, paths, config, cancelEvent](std::stop_token) {
        try {
            const FfmpegStatus status = FfmpegManager::InstallEssentials(
                paths,
                [state, window](std::uint64_t downloaded, std::uint64_t total, const std::wstring& statusText) {
                    {
                        std::lock_guard lock(state->progressMutex);
                        state->pendingProgress = ProgressUpdate{downloaded, total, statusText};
                    }
                    PostMessageW(window, kProgressUpdateMessage, 0, 0);
                },
                cancelEvent
            );
            config->ffmpegPath = status.ffmpegExe;
            PostMessageW(window, kProgressDoneMessage, TRUE, 0);
        } catch (const std::exception& ex) {
            {
                std::lock_guard lock(state->progressMutex);
                state->progressError = LocalizedToolErrorText(ex.what());
            }
            PostMessageW(window, kProgressDoneMessage, FALSE, 0);
        } catch (...) {
            {
                std::lock_guard lock(state->progressMutex);
                state->progressError = L"Неизвестная ошибка установки FFmpeg";
            }
            PostMessageW(window, kProgressDoneMessage, FALSE, 0);
        }
    });
}

void StartWhisperInstallWorker(DialogState* state) {
    if (!state || !state->paths || !state->config || !state->cancelEvent) {
        return;
    }

    HWND window = state->window;
    const AppPaths paths = *state->paths;
    AppConfig* config = state->config;
    HANDLE cancelEvent = state->cancelEvent;
    const WhisperBackend installBackend = SelectWhisperInstallBackend(
        config->whisperBackend,
        IsWhisperCudaCandidateAvailable()
    );

    state->worker = std::jthread([state, window, paths, config, cancelEvent, installBackend](std::stop_token) {
        try {
            const ToolInstallStatus status = WhisperManager::Install(
                paths,
                installBackend,
                [state, window](std::uint64_t downloaded, std::uint64_t total, const std::wstring& statusText) {
                    {
                        std::lock_guard lock(state->progressMutex);
                        state->pendingProgress = ProgressUpdate{downloaded, total, statusText};
                    }
                    PostMessageW(window, kProgressUpdateMessage, 0, 0);
                },
                cancelEvent
            );
            config->whisperPath = status.executable;
            config->whisperBackend = status.whisperBackend;
            PostMessageW(window, kProgressDoneMessage, TRUE, 0);
        } catch (const std::exception& ex) {
            const std::string installError = ex.what();
            const ToolInstallStatus cpuStatus = WhisperManager::ResolveBackend(paths, WhisperBackend::Cpu);
            const std::filesystem::path modelPath = config->whisperModelPath.empty()
                ? paths.localWhisperModelPath()
                : config->whisperModelPath;
            const bool modelReady = IsRegularFile(modelPath);
            const bool cpuSelfTestPassed = cpuStatus.installed &&
                WhisperManager::SelfTestExecutable(cpuStatus.executable, cancelEvent);
            if (ShouldFallbackWhisperCudaInstallToCpu(
                    installBackend,
                    installError,
                    cpuStatus.installed,
                    cpuSelfTestPassed,
                    modelReady
                )) {
                config->whisperPath = cpuStatus.executable;
                config->whisperBackend = WhisperBackend::Cpu;
                {
                    std::lock_guard lock(state->progressMutex);
                    state->progressSuccessMessage =
                        L"CUDA Whisper установлен, но не прошёл проверку запуска. Переключено на установленный CPU backend.";
                }
                PostMessageW(window, kProgressDoneMessage, TRUE, 0);
                return;
            }
            {
                std::lock_guard lock(state->progressMutex);
                state->progressError = LocalizedToolErrorText(installError);
            }
            PostMessageW(window, kProgressDoneMessage, FALSE, 0);
        } catch (...) {
            {
                std::lock_guard lock(state->progressMutex);
                state->progressError = L"Неизвестная ошибка установки Whisper.cpp";
            }
            PostMessageW(window, kProgressDoneMessage, FALSE, 0);
        }
    });
}

void StartWhisperModelDownloadWorker(DialogState* state) {
    if (!state || !state->paths || !state->config || !state->cancelEvent) {
        return;
    }

    HWND window = state->window;
    const AppPaths paths = *state->paths;
    AppConfig* config = state->config;
    HANDLE cancelEvent = state->cancelEvent;
    const std::optional<WhisperModelInfo> selectedModel = state->progressWhisperModel;

    state->worker = std::jthread([state, window, paths, config, cancelEvent, selectedModel](std::stop_token) {
        try {
            const std::vector<WhisperModelInfo> catalog = WhisperManager::ModelCatalog();
            const WhisperModelInfo* model = selectedModel ? &*selectedModel : RecommendedWhisperModel(catalog);
            if (!model || model->id.empty()) {
                throw std::runtime_error("whisper model catalog is empty");
            }
            const std::filesystem::path modelPath = WhisperManager::DownloadModel(
                paths,
                *model,
                [state, window](std::uint64_t downloaded, std::uint64_t total, const std::wstring& statusText) {
                    {
                        std::lock_guard lock(state->progressMutex);
                        state->pendingProgress = ProgressUpdate{downloaded, total, statusText};
                    }
                    PostMessageW(window, kProgressUpdateMessage, 0, 0);
                },
                cancelEvent
            );
            config->whisperModelPath = modelPath;
            PostMessageW(window, kProgressDoneMessage, TRUE, 0);
        } catch (const std::exception& ex) {
            {
                std::lock_guard lock(state->progressMutex);
                state->progressError = LocalizedToolErrorText(ex.what());
            }
            PostMessageW(window, kProgressDoneMessage, FALSE, 0);
        } catch (...) {
            {
                std::lock_guard lock(state->progressMutex);
                state->progressError = L"Неизвестная ошибка скачивания модели Whisper";
            }
            PostMessageW(window, kProgressDoneMessage, FALSE, 0);
        }
    });
}

void StartVotInstallWorker(DialogState* state) {
    if (!state || !state->paths || !state->config || !state->cancelEvent) {
        return;
    }

    HWND window = state->window;
    const AppPaths paths = *state->paths;
    AppConfig* config = state->config;
    HANDLE cancelEvent = state->cancelEvent;

    state->worker = std::jthread([state, window, paths, config, cancelEvent](std::stop_token) {
        try {
            const VotExeStatus status = VotExeManager::Install(
                paths,
                [state, window](std::uint64_t downloaded, std::uint64_t total, const std::wstring& statusText) {
                    {
                        std::lock_guard lock(state->progressMutex);
                        state->pendingProgress = ProgressUpdate{downloaded, total, statusText};
                    }
                    PostMessageW(window, kProgressUpdateMessage, 0, 0);
                },
                cancelEvent
            );
            config->votExePath = status.executable;
            PostMessageW(window, kProgressDoneMessage, TRUE, 0);
        } catch (const std::exception& ex) {
            {
                std::lock_guard lock(state->progressMutex);
                state->progressError = LocalizedToolErrorText(ex.what());
            }
            PostMessageW(window, kProgressDoneMessage, FALSE, 0);
        } catch (...) {
            {
                std::lock_guard lock(state->progressMutex);
                state->progressError = L"Неизвестная ошибка установки VOT helper";
            }
            PostMessageW(window, kProgressDoneMessage, FALSE, 0);
        }
    });
}

void StartAppUpdateWorker(DialogState* state) {
    if (!state || !state->paths || !state->cancelEvent) {
        return;
    }

    HWND window = state->window;
    const AppPaths paths = *state->paths;
    const ReleaseAssetInfo release = state->release;
    HANDLE cancelEvent = state->cancelEvent;

    state->worker = std::jthread([state, window, paths, release, cancelEvent](std::stop_token) {
        try {
            const std::filesystem::path downloadedExe = AppUpdateService::DownloadUpdateExe(
                paths,
                release,
                [state, window](std::uint64_t downloaded, std::uint64_t total) {
                    {
                        std::lock_guard lock(state->progressMutex);
                        state->pendingProgress = ProgressUpdate{downloaded, total, L"Скачивание обновления..."};
                    }
                    PostMessageW(window, kProgressUpdateMessage, 0, 0);
                },
                cancelEvent
            );
            if (cancelEvent && WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0) {
                throw std::runtime_error("operation canceled");
            }
            AppUpdateService::StartDownloadedUpdate(paths, downloadedExe);
            PostMessageW(window, kProgressDoneMessage, TRUE, 0);
        } catch (const std::exception& ex) {
            {
                std::lock_guard lock(state->progressMutex);
                state->progressError = LocalizedToolErrorText(ex.what());
            }
            PostMessageW(window, kProgressDoneMessage, FALSE, 0);
        } catch (...) {
            {
                std::lock_guard lock(state->progressMutex);
                state->progressError = L"Неизвестная ошибка обновления приложения";
            }
            PostMessageW(window, kProgressDoneMessage, FALSE, 0);
        }
    });
}

void CreateProgressControls(DialogState* state) {
    HWND cancelButton = CreateDarkButton(state->window, state->instance, L"Отмена", IdCancel, false, false);
    AddDialogTooltip(state, cancelButton, L"Отменяет текущую операцию.");
    if (state->progressMode == ProgressMode::AppUpdate) {
        StartAppUpdateWorker(state);
    } else if (state->progressMode == ProgressMode::FfmpegInstall) {
        StartFfmpegInstallWorker(state);
    } else if (state->progressMode == ProgressMode::WhisperInstall) {
        StartWhisperInstallWorker(state);
    } else if (state->progressMode == ProgressMode::WhisperModelDownload) {
        StartWhisperModelDownloadWorker(state);
    } else if (state->progressMode == ProgressMode::VotInstall) {
        StartVotInstallWorker(state);
    }
}

void CreateSettingsControls(DialogState* state) {
    HWND collapseButton = CreateDarkButton(state->window, state->instance, L"<", IdSettingsToggleSidebar, false, false);
    HWND downloadsNav = CreateDarkButton(state->window, state->instance, L"Загрузки", IdSettingsNavDownloads, true, false);
    HWND transcriptionNav = CreateDarkButton(state->window, state->instance, L"Транскрибация", IdSettingsNavTranscription, false, false);
    HWND translationNav = CreateDarkButton(state->window, state->instance, L"Перевод", IdSettingsNavTranslation, false, false);
    HWND toolsNav = CreateDarkButton(state->window, state->instance, L"Инструменты", IdSettingsNavTools, false, false);
    HWND aboutNav = CreateDarkButton(state->window, state->instance, L"О программе", IdSettingsNavAbout, false, false);

    const std::array<std::pair<int, const wchar_t*>, 6> qualityButtons = {{
        {101, L"Аудио"},
        {102, L"360p"},
        {103, L"480p"},
        {104, L"720p"},
        {105, L"1080p"},
        {106, L"Макс."}
    }};
    for (const auto& [id, text] : qualityButtons) {
        const bool selected =
            (id == 101 && state->workingConfig.quality == L"audio") ||
            (id == 102 && state->workingConfig.quality == L"360p") ||
            (id == 103 && state->workingConfig.quality == L"480p") ||
            (id == 104 && state->workingConfig.quality == L"720p") ||
            (id == 105 && state->workingConfig.quality == L"1080p") ||
            (id == 106 && state->workingConfig.quality == L"max");
        HWND button = CreateDarkButton(state->window, state->instance, text, id, selected);
        AddDialogTooltip(state, button, L"Выбирает качество, которое будет использоваться для новых задач.");
    }

    const std::array<std::pair<int, const wchar_t*>, 4> containerButtons = {{
        {111, L"Auto"},
        {112, L"MP4"},
        {113, L"MKV"},
        {114, L"WEBM"}
    }};
    for (const auto& [id, text] : containerButtons) {
        const bool selected =
            (id == 111 && state->workingConfig.container == L"auto") ||
            (id == 112 && state->workingConfig.container == L"mp4") ||
            (id == 113 && state->workingConfig.container == L"mkv") ||
            (id == 114 && state->workingConfig.container == L"webm");
        HWND button = CreateDarkButton(state->window, state->instance, text, id, selected);
        AddDialogTooltip(state, button, L"Выбирает контейнер итогового файла для новых задач.");
    }

    HWND ffmpegButton = CreateDarkButton(state->window, state->instance, ToolSetupButtonText().c_str(), IdFfmpeg, false);
    HWND transcriptionWhisperButton = CreateDarkButton(state->window, state->instance, L"Whisper", IdTranscriptionWhisper, state->workingConfig.transcriptionEngine == TranscriptionEngine::Whisper);
    HWND transcriptionVotButton = CreateDarkButton(state->window, state->instance, L"VOT", IdTranscriptionVot, state->workingConfig.transcriptionEngine == TranscriptionEngine::Vot);
    HWND votSubtitleLanguageEdit = CreateDarkButton(
        state->window,
        state->instance,
        SettingsLanguageButtonText(state->workingConfig.votSubtitleLanguage.empty() ? L"ru" : state->workingConfig.votSubtitleLanguage).c_str(),
        IdVotSubtitleLanguageEdit,
        false
    );
    HWND chooseWhisperButton = CreateDarkButton(state->window, state->instance, ToolSetupButtonText().c_str(), IdChooseWhisperFolder, false);
    HWND chooseVotButton = CreateDarkButton(state->window, state->instance, ToolSetupButtonText().c_str(), IdChooseVotFolder, false);
    HWND subtitleOffButton = CreateDarkButton(state->window, state->instance, L"Выкл", IdSubtitleModeOff, state->workingConfig.subtitleFfmpegMode == SubtitleFfmpegMode::Off);
    HWND subtitleTrackButton = CreateDarkButton(state->window, state->instance, SubtitleFfmpegModeDisplayText(SubtitleFfmpegMode::SubtitleTrack).c_str(), IdSubtitleModeTrack, state->workingConfig.subtitleFfmpegMode == SubtitleFfmpegMode::SubtitleTrack);
    HWND subtitleBurnButton = CreateDarkButton(state->window, state->instance, SubtitleFfmpegModeDisplayText(SubtitleFfmpegMode::BurnIn).c_str(), IdSubtitleModeBurn, state->workingConfig.subtitleFfmpegMode == SubtitleFfmpegMode::BurnIn);
    HWND voiceLanguageEdit = CreateDarkButton(
        state->window,
        state->instance,
        SettingsLanguageButtonText(state->workingConfig.voiceOverLanguage.empty() ? L"ru" : state->workingConfig.voiceOverLanguage).c_str(),
        IdVoiceLanguageEdit,
        false
    );
    HWND voiceOffButton = CreateDarkButton(state->window, state->instance, L"Выкл", IdVoiceModeOff, state->workingConfig.voiceOverFfmpegMode == VoiceOverFfmpegMode::Off);
    HWND voiceTrackButton = CreateDarkButton(state->window, state->instance, VoiceOverFfmpegModeDisplayText(VoiceOverFfmpegMode::AudioTrack).c_str(), IdVoiceModeTrack, state->workingConfig.voiceOverFfmpegMode == VoiceOverFfmpegMode::AudioTrack);
    HWND voiceMixButton = CreateDarkButton(state->window, state->instance, VoiceOverFfmpegModeDisplayText(VoiceOverFfmpegMode::Mix).c_str(), IdVoiceModeMix, state->workingConfig.voiceOverFfmpegMode == VoiceOverFfmpegMode::Mix);
    HWND voiceVolumeMinusButton = CreateDarkButton(state->window, state->instance, L"-", IdVoiceVolumeMinus, false);
    HWND voiceVolumePlusButton = CreateDarkButton(state->window, state->instance, L"+", IdVoiceVolumePlus, false);
    HWND transcriptionToolsButton = CreateDarkButton(state->window, state->instance, L"Открыть Инструменты", IdTranscriptionOpenTools, false);
    HWND translationToolsButton = CreateDarkButton(state->window, state->instance, L"Открыть Инструменты", IdTranslationOpenTools, false);
    HWND ytDlpDetailsButton = CreateDarkButton(state->window, state->instance, L"Подробно", IdYtDlpDetails, false);
    HWND ffmpegDetailsButton = CreateDarkButton(state->window, state->instance, L"Подробно", IdFfmpegDetails, false);
    HWND whisperDetailsButton = CreateDarkButton(state->window, state->instance, L"Подробно", IdWhisperDetails, false);
    HWND votDetailsButton = CreateDarkButton(state->window, state->instance, L"Подробно", IdVotDetails, false);
    HWND autoUpdateButton = CreateDarkButton(
        state->window,
        state->instance,
        state->workingConfig.autoUpdateApp ? L"Автопроверка: Вкл" : L"Автопроверка: Выкл",
        IdAutoUpdate,
        state->workingConfig.autoUpdateApp
    );
    HWND minusButton = CreateDarkButton(state->window, state->instance, L"-", IdParallelMinus, false);
    HWND plusButton = CreateDarkButton(state->window, state->instance, L"+", IdParallelPlus, false);
    HWND checkUpdatesButton = CreateDarkButton(state->window, state->instance, L"Проверить обновления", IdCheckUpdates, false);
    HWND cancelButton = CreateDarkButton(state->window, state->instance, L"Отмена", IdCancel, false, false);
    HWND saveButton = CreateDarkButton(state->window, state->instance, L"Сохранить", IdOk, true, false);
    AddDialogTooltip(state, collapseButton, L"Сворачивает или разворачивает боковую навигацию.");
    AddDialogTooltip(state, downloadsNav, L"Открывает настройки загрузок.");
    AddDialogTooltip(state, transcriptionNav, L"Открывает настройки транскрибации.");
    AddDialogTooltip(state, translationNav, L"Открывает настройки перевода.");
    AddDialogTooltip(state, toolsNav, L"Открывает статус и настройку инструментов.");
    AddDialogTooltip(state, aboutNav, L"Открывает информацию о приложении.");
    AddDialogTooltip(state, ffmpegButton, L"Открывает настройку FFmpeg и существующий поток установки.");
    AddDialogTooltip(state, transcriptionWhisperButton, L"Использовать whisper-cli.exe для транскрибации.");
    AddDialogTooltip(state, transcriptionVotButton, L"Использовать vot-helper.exe subtitles для получения SRT/TXT.");
    AddDialogTooltip(state, votSubtitleLanguageEdit, L"Целевой язык VOT-субтитров.");
    AddDialogTooltip(state, chooseWhisperButton, L"Открывает настройку Whisper.cpp, модели и выбора папки.");
    AddDialogTooltip(state, chooseVotButton, L"Открывает настройку vot-helper.exe.");
    AddDialogTooltip(state, subtitleOffButton, L"Сохранять TXT/SRT рядом с видео без изменения видеофайла.");
    const std::wstring subtitleTrackTooltip = FfmpegGatedOptionTooltip(L"Добавлять SRT как отдельную дорожку субтитров.");
    const std::wstring subtitleBurnTooltip = FfmpegGatedOptionTooltip(L"Выжигать субтитры в изображение.");
    AddDialogTooltip(state, subtitleTrackButton, subtitleTrackTooltip);
    AddDialogTooltip(state, subtitleBurnButton, subtitleBurnTooltip);
    AddDialogTooltip(state, voiceLanguageEdit, L"Целевой язык перевода VOT.");
    AddDialogTooltip(state, voiceOffButton, L"Сохранять перевод отдельным MP3 рядом с видео.");
    const std::wstring voiceTrackTooltip = FfmpegGatedOptionTooltip(L"Добавлять перевод как отдельную аудиодорожку.");
    const std::wstring voiceMixTooltip = FfmpegGatedOptionTooltip(L"Смешивать перевод с оригинальной аудиодорожкой.");
    AddDialogTooltip(state, voiceTrackButton, voiceTrackTooltip);
    AddDialogTooltip(state, voiceMixButton, voiceMixTooltip);
    AddDialogTooltip(state, voiceVolumeMinusButton, L"Уменьшает громкость оригинальной дорожки при смешивании.");
    AddDialogTooltip(state, voiceVolumePlusButton, L"Увеличивает громкость оригинальной дорожки при смешивании.");
    AddDialogTooltip(state, transcriptionToolsButton, L"Переходит к выбору и установке инструментов.");
    AddDialogTooltip(state, translationToolsButton, L"Переходит к выбору и установке инструментов.");
    AddDialogTooltip(state, ytDlpDetailsButton, L"Показывает или скрывает путь yt-dlp.");
    AddDialogTooltip(state, ffmpegDetailsButton, L"Показывает или скрывает путь FFmpeg.");
    AddDialogTooltip(state, whisperDetailsButton, L"Показывает или скрывает пути Whisper.cpp и модели.");
    AddDialogTooltip(state, votDetailsButton, L"Показывает или скрывает путь vot-helper.exe.");
    AddDialogTooltip(state, autoUpdateButton, L"Включает или отключает автоматическую проверку обновлений приложения.");
    AddDialogTooltip(state, minusButton, L"Уменьшает количество параллельных загрузок.");
    AddDialogTooltip(state, plusButton, L"Увеличивает количество параллельных загрузок.");
    AddDialogTooltip(state, checkUpdatesButton, L"Проверяет наличие новой версии приложения.");
    AddDialogTooltip(state, cancelButton, L"Закрывает настройки без сохранения изменений.");
    AddDialogTooltip(state, saveButton, L"Сохраняет выбранные настройки.");
    RefreshSettingsButtons(state);
}

void RegisterDialogClasses(HINSTANCE instance) {
    WNDCLASSEXW dialogClass = {};
    dialogClass.cbSize = sizeof(dialogClass);
    dialogClass.style = CS_HREDRAW | CS_VREDRAW;
    dialogClass.lpfnWndProc = DialogWindowProc;
    dialogClass.hInstance = instance;
    dialogClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    dialogClass.hbrBackground = nullptr;
    dialogClass.lpszClassName = kDialogClassName;
    RegisterClassExW(&dialogClass);

    WNDCLASSEXW buttonClass = {};
    buttonClass.cbSize = sizeof(buttonClass);
    buttonClass.lpfnWndProc = DialogButtonProc;
    buttonClass.hInstance = instance;
    buttonClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
    buttonClass.hbrBackground = nullptr;
    buttonClass.lpszClassName = kDialogButtonClassName;
    RegisterClassExW(&buttonClass);

    WNDCLASSEXW scrollTextClass = {};
    scrollTextClass.cbSize = sizeof(scrollTextClass);
    scrollTextClass.lpfnWndProc = ScrollTextProc;
    scrollTextClass.hInstance = instance;
    scrollTextClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    scrollTextClass.hbrBackground = nullptr;
    scrollTextClass.lpszClassName = kScrollTextClassName;
    RegisterClassExW(&scrollTextClass);

    WNDCLASSEXW logViewClass = {};
    logViewClass.cbSize = sizeof(logViewClass);
    logViewClass.style = CS_HREDRAW | CS_VREDRAW;
    logViewClass.lpfnWndProc = LogViewProc;
    logViewClass.hInstance = instance;
    logViewClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    logViewClass.hbrBackground = nullptr;
    logViewClass.lpszClassName = kLogViewClassName;
    RegisterClassExW(&logViewClass);

    WNDCLASSEXW logMenuClass = {};
    logMenuClass.cbSize = sizeof(logMenuClass);
    logMenuClass.lpfnWndProc = LogCopyMenuProc;
    logMenuClass.hInstance = instance;
    logMenuClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    logMenuClass.hbrBackground = nullptr;
    logMenuClass.lpszClassName = kLogCopyMenuClassName;
    RegisterClassExW(&logMenuClass);

    WNDCLASSEXW comboMenuClass = {};
    comboMenuClass.cbSize = sizeof(comboMenuClass);
    comboMenuClass.lpfnWndProc = SettingsComboMenuProc;
    comboMenuClass.hInstance = instance;
    comboMenuClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    comboMenuClass.hbrBackground = nullptr;
    comboMenuClass.lpszClassName = kSettingsComboMenuClassName;
    RegisterClassExW(&comboMenuClass);
}

void ShowSettingsLanguageMenu(DialogState* state, HWND anchor, SettingsLanguageTarget target) {
    if (!state || !state->window || !anchor) {
        return;
    }

    auto* menuState = new SettingsComboMenuState{};
    menuState->owner = state->window;
    menuState->target = target;
    switch (target) {
    case SettingsLanguageTarget::VotSubtitle:
        menuState->values = VotSubtitleLanguageOptions();
        break;
    case SettingsLanguageTarget::VoiceOver:
        menuState->values = VoiceLanguageOptions();
        break;
    default:
        menuState->values = VotSubtitleLanguageOptions();
        break;
    }

    RECT anchorRect = {};
    GetWindowRect(anchor, &anchorRect);
    constexpr int itemHeight = 34;
    const int menuWidth = std::max(150, static_cast<int>(anchorRect.right - anchorRect.left));
    const int menuHeight = static_cast<int>(menuState->values.size()) * itemHeight + 8;

    RECT workArea = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    int x = anchorRect.left;
    int y = anchorRect.bottom + 4;
    if (x + menuWidth > workArea.right) {
        x = workArea.right - menuWidth;
    }
    if (y + menuHeight > workArea.bottom) {
        y = anchorRect.top - menuHeight - 4;
    }

    HWND menu = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kSettingsComboMenuClassName,
        L"",
        WS_POPUP,
        std::max(static_cast<int>(workArea.left), x),
        std::max(static_cast<int>(workArea.top), y),
        menuWidth,
        menuHeight,
        state->window,
        nullptr,
        state->instance,
        menuState
    );
    if (!menu) {
        delete menuState;
        return;
    }
    ShowWindow(menu, SW_SHOW);
    SetFocus(menu);
}

LRESULT CALLBACK DialogWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    DialogState* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<DialogState*>(create->lpCreateParams);
        state->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        EnableDarkTitleBar(window);
        return TRUE;
    }

    switch (message) {
    case WM_GETMINMAXINFO:
        if (state && state->type == DialogType::Settings) {
            auto* minMaxInfo = reinterpret_cast<MINMAXINFO*>(lParam);
            minMaxInfo->ptMinTrackSize.x = kSettingsMinWidth;
            minMaxInfo->ptMinTrackSize.y = kSettingsMinHeight;
            return 0;
        }
        if (state && state->type == DialogType::Logs) {
            auto* minMaxInfo = reinterpret_cast<MINMAXINFO*>(lParam);
            minMaxInfo->ptMinTrackSize.x = 720;
            minMaxInfo->ptMinTrackSize.y = 460;
            return 0;
        }
        break;

    case WM_CREATE:
        if (state) {
            if (state->type == DialogType::Settings) {
                CreateSettingsControls(state);
            } else if (state->type == DialogType::Ffmpeg) {
                CreateFfmpegControls(state);
            } else if (state->type == DialogType::Whisper) {
                CreateWhisperControls(state);
            } else if (state->type == DialogType::Vot) {
                CreateVotControls(state);
            } else if (state->type == DialogType::WhisperModel) {
                CreateWhisperModelControls(state);
            } else if (state->type == DialogType::VotCandidate) {
                CreateVotCandidateControls(state);
            } else if (state->type == DialogType::Progress) {
                CreateProgressControls(state);
            } else if (state->type == DialogType::Logs) {
                CreateLogsControls(state);
            } else {
                CreateMessageControls(state);
            }
            RECT client = {};
            GetClientRect(window, &client);
            LayoutDialog(state, client.right, client.bottom);
        }
        return 0;

    case WM_SIZE:
        if (state) {
            LayoutDialog(state, LOWORD(lParam), HIWORD(lParam));
            InvalidateRect(window, nullptr, TRUE);
            if (state->logView) {
                InvalidateRect(state->logView, nullptr, TRUE);
            }
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        if (state) {
            PaintBuffered(window, [state](HDC dc, const RECT& client) {
                if (state->type == DialogType::Settings) {
                    DrawSettingsDialog(state, dc, client);
                } else if (state->type == DialogType::Ffmpeg || state->type == DialogType::Whisper || state->type == DialogType::Vot) {
                    DrawFfmpegDialog(state, dc, client);
                } else if (state->type == DialogType::WhisperModel) {
                    DrawWhisperModelDialog(state, dc, client);
                } else if (state->type == DialogType::VotCandidate) {
                    DrawVotCandidateDialog(state, dc, client);
                } else if (state->type == DialogType::Progress) {
                    DrawProgressDialog(state, dc, client);
                } else if (state->type == DialogType::Logs) {
                    DrawLogsDialog(state, dc, client);
                } else {
                    DrawMessageDialog(state, dc, client);
                }
            });
        }
        return 0;

    case WM_COMMAND:
        if (state) {
            const int commandId = LOWORD(wParam);
            if (state->type == DialogType::WhisperModel) {
                const int modelIndex = commandId - IdWhisperModelBase;
                if (modelIndex >= 0 && modelIndex < static_cast<int>(state->whisperModels.size())) {
                    state->selectedWhisperModelIndex = modelIndex;
                    RefreshWhisperModelButtons(state);
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }
                if (commandId == IdWhisperModelDownloadSelected && state->paths && state->config && !state->whisperModels.empty()) {
                    const int selectedIndex = std::clamp(
                        state->selectedWhisperModelIndex,
                        0,
                        static_cast<int>(state->whisperModels.size()) - 1
                    );
                    const WhisperModelInfo selected = state->whisperModels[static_cast<size_t>(selectedIndex)];
                    if (ShowWhisperModelDownloadProgress(window, state->instance, *state->paths, *state->config, selected)) {
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                        DestroyWindow(window);
                    }
                    return 0;
                }
            }
            if (state->type == DialogType::VotCandidate) {
                const int candidateIndex = commandId - IdVotCandidateBase;
                if (candidateIndex >= 0 && candidateIndex < static_cast<int>(state->votExecutableCandidates.size())) {
                    state->selectedVotExecutableIndex = candidateIndex;
                    RefreshVotCandidateButtons(state);
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }
                if (commandId == IdVotCandidateSelect && state->selectedVotExecutableResult && !state->votExecutableCandidates.empty()) {
                    const int selectedIndex = std::clamp(
                        state->selectedVotExecutableIndex,
                        0,
                        static_cast<int>(state->votExecutableCandidates.size()) - 1
                    );
                    *state->selectedVotExecutableResult = state->votExecutableCandidates[static_cast<size_t>(selectedIndex)];
                    if (state->savedResult) {
                        *state->savedResult = true;
                    }
                    DestroyWindow(window);
                    return 0;
                }
            }
            switch (commandId) {
            case 101:
                state->workingConfig.quality = L"audio";
                RefreshSettingsButtons(state);
                return 0;
            case 102:
                state->workingConfig.quality = L"360p";
                RefreshSettingsButtons(state);
                return 0;
            case 103:
                state->workingConfig.quality = L"480p";
                RefreshSettingsButtons(state);
                return 0;
            case 104:
                state->workingConfig.quality = L"720p";
                RefreshSettingsButtons(state);
                return 0;
            case 105:
                state->workingConfig.quality = L"1080p";
                RefreshSettingsButtons(state);
                return 0;
            case 106:
                state->workingConfig.quality = L"max";
                RefreshSettingsButtons(state);
                return 0;
            case 111:
                state->workingConfig.container = L"auto";
                RefreshSettingsButtons(state);
                return 0;
            case 112:
                state->workingConfig.container = L"mp4";
                RefreshSettingsButtons(state);
                return 0;
            case 113:
                state->workingConfig.container = L"mkv";
                RefreshSettingsButtons(state);
                return 0;
            case 114:
                state->workingConfig.container = L"webm";
                RefreshSettingsButtons(state);
                return 0;
            case IdSettingsNavDownloads:
                state->settingsSection = SettingsSection::Downloads;
                RelayoutDialog(state);
                RefreshSettingsButtons(state);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            case IdSettingsNavTranscription:
                state->settingsSection = SettingsSection::Transcription;
                RelayoutDialog(state);
                RefreshSettingsButtons(state);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            case IdSettingsNavTranslation:
                state->settingsSection = SettingsSection::Translation;
                RelayoutDialog(state);
                RefreshSettingsButtons(state);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            case IdSettingsNavTools:
            case IdTranscriptionOpenTools:
            case IdTranslationOpenTools:
                state->settingsSection = SettingsSection::Tools;
                RelayoutDialog(state);
                RefreshSettingsButtons(state);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            case IdSettingsNavAbout:
                state->settingsSection = SettingsSection::About;
                RelayoutDialog(state);
                RefreshSettingsButtons(state);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            case IdSettingsToggleSidebar:
                state->settingsSidebarCollapsed = !state->settingsSidebarCollapsed;
                {
                    RECT client = {};
                    GetClientRect(window, &client);
                    LayoutDialog(state, client.right, client.bottom);
                }
                RefreshSettingsButtons(state);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            case IdAutoUpdate:
                state->workingConfig.autoUpdateApp = !state->workingConfig.autoUpdateApp;
                RefreshSettingsButtons(state);
                return 0;
            case IdParallelMinus:
                state->workingConfig.maxParallelDownloads = std::clamp(state->workingConfig.maxParallelDownloads - 1, 3, 10);
                RefreshSettingsButtons(state);
                InvalidateRect(state->window, nullptr, FALSE);
                return 0;
            case IdParallelPlus:
                state->workingConfig.maxParallelDownloads = std::clamp(state->workingConfig.maxParallelDownloads + 1, 3, 10);
                RefreshSettingsButtons(state);
                InvalidateRect(state->window, nullptr, FALSE);
                return 0;
            case IdTranscriptionWhisper:
                state->workingConfig.transcriptionEngine = TranscriptionEngine::Whisper;
                RefreshSettingsButtons(state);
                return 0;
            case IdTranscriptionVot:
                state->workingConfig.transcriptionEngine = TranscriptionEngine::Vot;
                RefreshSettingsButtons(state);
                return 0;
            case IdVotSubtitleLanguageEdit:
                ShowSettingsLanguageMenu(state, GetDlgItem(window, IdVotSubtitleLanguageEdit), SettingsLanguageTarget::VotSubtitle);
                return 0;
            case IdVoiceLanguageEdit:
                ShowSettingsLanguageMenu(state, GetDlgItem(window, IdVoiceLanguageEdit), SettingsLanguageTarget::VoiceOver);
                return 0;
            case IdVoiceModeOff:
                state->workingConfig.voiceOverFfmpegMode = VoiceOverFfmpegMode::Off;
                RefreshSettingsButtons(state);
                return 0;
            case IdVoiceModeTrack:
                if (!IsFfmpegReady(state)) {
                    return 0;
                }
                state->workingConfig.voiceOverFfmpegMode = VoiceOverFfmpegMode::AudioTrack;
                RefreshSettingsButtons(state);
                return 0;
            case IdVoiceModeMix:
                if (!IsFfmpegReady(state)) {
                    return 0;
                }
                state->workingConfig.voiceOverFfmpegMode = VoiceOverFfmpegMode::Mix;
                RefreshSettingsButtons(state);
                return 0;
            case IdVoiceVolumeMinus:
                state->workingConfig.voiceOverOriginalVolumePercent = std::clamp(
                    state->workingConfig.voiceOverOriginalVolumePercent - 5,
                    0,
                    100
                );
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            case IdVoiceVolumePlus:
                state->workingConfig.voiceOverOriginalVolumePercent = std::clamp(
                    state->workingConfig.voiceOverOriginalVolumePercent + 5,
                    0,
                    100
                );
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            case IdSubtitleModeOff:
                state->workingConfig.subtitleFfmpegMode = SubtitleFfmpegMode::Off;
                RefreshSettingsButtons(state);
                return 0;
            case IdSubtitleModeTrack:
                if (!IsFfmpegReady(state)) {
                    return 0;
                }
                state->workingConfig.subtitleFfmpegMode = SubtitleFfmpegMode::SubtitleTrack;
                RefreshSettingsButtons(state);
                return 0;
            case IdSubtitleModeBurn:
                if (!IsFfmpegReady(state)) {
                    return 0;
                }
                state->workingConfig.subtitleFfmpegMode = SubtitleFfmpegMode::BurnIn;
                RefreshSettingsButtons(state);
                return 0;
            case IdChooseWhisperFolder:
                if (state->type == DialogType::Settings) {
                    if (state->paths && ShowWhisperDialog(window, state->instance, *state->paths, state->workingConfig)) {
                        RefreshSettingsButtons(state);
                        InvalidateRect(window, nullptr, FALSE);
                        if (state->config) {
                            *state->config = state->workingConfig;
                        }
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                    }
                    return 0;
                }
                return 0;
            case IdChooseVotFolder:
                if (state->type == DialogType::Settings) {
                    if (state->paths && ShowVotDialog(window, state->instance, *state->paths, state->workingConfig)) {
                        RefreshSettingsButtons(state);
                        InvalidateRect(window, nullptr, FALSE);
                        if (state->config) {
                            *state->config = state->workingConfig;
                        }
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                    }
                    return 0;
                }
                return 0;
            case IdYtDlpDetails:
                state->ytDlpDetailsExpanded = !state->ytDlpDetailsExpanded;
                RelayoutDialog(state);
                RefreshSettingsButtons(state);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            case IdFfmpegDetails:
                state->ffmpegDetailsExpanded = !state->ffmpegDetailsExpanded;
                RelayoutDialog(state);
                RefreshSettingsButtons(state);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            case IdWhisperDetails:
                state->whisperDetailsExpanded = !state->whisperDetailsExpanded;
                RelayoutDialog(state);
                RefreshSettingsButtons(state);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            case IdVotDetails:
                state->votDetailsExpanded = !state->votDetailsExpanded;
                RelayoutDialog(state);
                RefreshSettingsButtons(state);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            case IdCopy:
                CopyTextToClipboard(window, state->message);
                return 0;
            case IdCheckUpdates:
                if (!state->paths) {
                    ShowErrorDialog(window, state->instance, L"Обновления", L"Путь приложения недоступен.");
                    return 0;
                }
                if (CheckAndOfferAppUpdate(window, state->instance, *state->paths, true)) {
                    HWND owner = GetAncestor(window, GA_ROOTOWNER);
                    DestroyWindow(window);
                    if (owner && IsWindow(owner)) {
                        PostMessageW(owner, WM_CLOSE, 0, 0);
                    }
                }
                return 0;
            case IdAbout:
                if (state->paths) {
                    ShowAboutDialog(window, state->instance, *state->paths);
                }
                return 0;
            case IdFfmpeg:
                if (state->paths && state->config) {
                    if (ShowFfmpegDialog(window, state->instance, *state->paths, state->workingConfig)) {
                        RefreshSettingsButtons(state);
                        InvalidateRect(window, nullptr, FALSE);
                        *state->config = state->workingConfig;
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                    }
                }
                return 0;
            case IdInstall:
                if (state->type == DialogType::Ffmpeg && state->paths && state->config) {
                    if (ShowFfmpegInstallProgress(window, state->instance, *state->paths, *state->config)) {
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                        DestroyWindow(window);
                    }
                    return 0;
                }
                if (state->type == DialogType::Whisper && state->paths && state->config) {
                    if (ShowWhisperInstallProgress(window, state->instance, *state->paths, *state->config)) {
                        if (!IsRegularFile(ResolveDialogWhisperModelPath(state))) {
                            ShowWhisperModelDialog(window, state->instance, *state->paths, *state->config);
                        }
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                        DestroyWindow(window);
                    }
                    return 0;
                }
                if (state->type == DialogType::Vot && state->paths && state->config) {
                    if (ShowVotInstallProgress(window, state->instance, *state->paths, *state->config)) {
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                        DestroyWindow(window);
                    }
                    return 0;
                }
                DestroyWindow(window);
                return 0;
            case IdWhisperDownloadModel:
                if (state->type == DialogType::Whisper && state->paths && state->config) {
                    if (ShowWhisperModelDialog(window, state->instance, *state->paths, *state->config)) {
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                        DestroyWindow(window);
                    }
                    return 0;
                }
                return 0;
            case IdChooseFolder:
                if (state->type == DialogType::Ffmpeg && state->config) {
                    const std::optional<std::filesystem::path> selected = PickFfmpegFolder(window);
                    if (selected && ApplySelectedFfmpegPath(window, state->instance, *state->config, *selected)) {
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                        DestroyWindow(window);
                    }
                    return 0;
                }
                if (state->type == DialogType::Whisper && state->config) {
                    const std::optional<std::filesystem::path> selected = PickFolder(window, L"Выберите папку Whisper");
                    if (selected && ApplySelectedWhisperPath(window, state->instance, *state->config, *selected)) {
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                        DestroyWindow(window);
                    }
                    return 0;
                }
                if (state->type == DialogType::Vot && state->config) {
                    const std::optional<std::filesystem::path> selected = PickFolder(window, L"Выберите папку VOT helper");
                    if (selected && ApplySelectedVotPath(window, state->instance, *state->config, *selected)) {
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                        DestroyWindow(window);
                    }
                    return 0;
                }
                DestroyWindow(window);
                return 0;
            case IdChooseVotExecutable:
                if (state->type == DialogType::Vot && state->config) {
                    const std::optional<std::filesystem::path> selected = PickVotExecutableFile(window);
                    if (selected && ApplySelectedVotPath(window, state->instance, *state->config, *selected)) {
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                        DestroyWindow(window);
                    }
                    return 0;
                }
                return 0;
            case IdSkip:
                DestroyWindow(window);
                return 0;
            case IdCancel:
                if (state->type == DialogType::Progress && !state->progressDone) {
                    if (state->cancelEvent) {
                        SetEvent(state->cancelEvent);
                    }
                    state->message = L"Отмена...";
                    InvalidateProgressContent(window);
                    return 0;
                }
                DestroyWindow(window);
                return 0;
            case IdOk:
                if (state->type == DialogType::Settings && state->config) {
                    if (state->workingConfig.whisperLanguage.empty()) {
                        state->workingConfig.whisperLanguage = L"auto";
                    }
                    if (state->workingConfig.votSubtitleLanguage.empty()) {
                        state->workingConfig.votSubtitleLanguage = L"ru";
                    }
                    if (state->workingConfig.voiceOverLanguage.empty()) {
                        state->workingConfig.voiceOverLanguage = L"ru";
                    }
                    state->workingConfig.voiceOverOriginalVolumePercent = std::clamp(
                        state->workingConfig.voiceOverOriginalVolumePercent,
                        0,
                        100
                    );
                    *state->config = state->workingConfig;
                    if (state->savedResult) {
                        *state->savedResult = true;
                    }
                } else if ((state->type == DialogType::Confirmation || state->type == DialogType::AffectedFiles) && state->savedResult) {
                    *state->savedResult = true;
                }
                DestroyWindow(window);
                return 0;
            default:
                return 0;
            }
        }
        break;

    case kProgressUpdateMessage:
        if (state && state->type == DialogType::Progress) {
            std::optional<ProgressUpdate> update;
            {
                std::lock_guard lock(state->progressMutex);
                update = std::move(state->pendingProgress);
                state->pendingProgress.reset();
            }
            if (!update) {
                return 0;
            }
            state->message = update->status;
            state->progressDownloaded = update->downloaded;
            state->progressTotal = update->total;
            InvalidateProgressContent(window);
        }
        return 0;

    case kProgressDoneMessage:
        if (state && state->type == DialogType::Progress) {
            state->progressDone = true;
            state->progressSuccess = wParam == TRUE;
            if (state->progressSuccess) {
                std::optional<std::wstring> successMessage;
                {
                    std::lock_guard lock(state->progressMutex);
                    successMessage = std::move(state->progressSuccessMessage);
                    state->progressSuccessMessage.reset();
                }
                if (successMessage) {
                    state->message = *successMessage;
                } else if (state->progressMode == ProgressMode::AppUpdate) {
                    state->message = L"Обновление скачано. Приложение будет закрыто и запущено заново.";
                } else if (state->progressMode == ProgressMode::WhisperInstall) {
                    const bool modelReady = IsRegularFile(ResolveDialogWhisperModelPath(state));
                    state->message = modelReady
                        ? L"Whisper.cpp установлен."
                        : L"Whisper.cpp установлен. Теперь скачайте модель; далее откроется окно моделей.";
                } else if (state->progressMode == ProgressMode::WhisperModelDownload) {
                    state->message = L"Модель Whisper скачана.";
                } else if (state->progressMode == ProgressMode::VotInstall) {
                    state->message = L"VOT helper установлен.";
                } else {
                    state->message = L"FFmpeg установлен.";
                }
                if (state->savedResult) {
                    *state->savedResult = true;
                }
                if (state->progressMode == ProgressMode::AppUpdate) {
                    DestroyWindow(window);
                    return 0;
                }
            } else {
                std::optional<std::wstring> error;
                {
                    std::lock_guard lock(state->progressMutex);
                    error = std::move(state->progressError);
                    state->progressError.reset();
                }
                state->message = error
                    ? *error
                    : (state->progressMode == ProgressMode::AppUpdate
                        ? L"Не удалось обновить приложение."
                        : (state->progressMode == ProgressMode::WhisperModelDownload
                            ? L"Не удалось скачать модель Whisper."
                            : (state->progressMode == ProgressMode::VotInstall
                                ? L"Не удалось установить VOT helper."
                                : (state->progressMode == ProgressMode::WhisperInstall
                                    ? L"Не удалось установить Whisper.cpp."
                                    : L"Не удалось установить FFmpeg."))));
            }
            const std::wstring doneButtonText =
                state->progressSuccess &&
                state->progressMode == ProgressMode::WhisperInstall &&
                !IsRegularFile(ResolveDialogWhisperModelPath(state))
                    ? L"Модели"
                    : L"OK";
            SetDarkButtonState(window, IdCancel, state->progressSuccess, doneButtonText);
            InvalidateProgressContent(window);
        }
        return 0;

    case WM_CLOSE:
        if (state && state->type == DialogType::Progress && !state->progressDone) {
            if (state->cancelEvent) {
                SetEvent(state->cancelEvent);
            }
            state->message = L"Отмена...";
            InvalidateProgressContent(window);
            return 0;
        }
        DestroyWindow(window);
        return 0;

    case WM_NCDESTROY:
        if (state && state->tooltip) {
            DestroyWindow(state->tooltip);
            state->tooltip = nullptr;
        }
        if (state) {
            for (HWND tooltip : state->tooltips) {
                if (tooltip) {
                    DestroyWindow(tooltip);
                }
            }
            state->tooltips.clear();
        }
        if (state && state->worker.joinable()) {
            if (state->cancelEvent) {
                SetEvent(state->cancelEvent);
            }
            state->worker.request_stop();
            state->worker.join();
        }
        if (state && state->cancelEvent) {
            CloseHandle(state->cancelEvent);
            state->cancelEvent = nullptr;
        }
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK DialogButtonProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    ButtonState* state = reinterpret_cast<ButtonState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<ButtonState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }

    switch (message) {
    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEMOVE:
        if (state && state->enabled && !state->hot) {
            state->hot = true;
            TRACKMOUSEEVENT track = {};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = window;
            TrackMouseEvent(&track);
            InvalidateRect(window, nullptr, TRUE);
        }
        return 0;

    case WM_MOUSELEAVE:
        if (state) {
            state->hot = false;
            state->pressed = false;
            InvalidateRect(window, nullptr, TRUE);
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (state && state->enabled) {
            state->pressed = true;
            SetCapture(window);
            SetFocus(window);
            InvalidateRect(window, nullptr, TRUE);
        }
        return 0;

    case WM_LBUTTONUP:
        if (state && state->enabled) {
            if (GetCapture() == window) {
                ReleaseCapture();
            }
            const bool wasPressed = state->pressed;
            state->pressed = false;
            InvalidateRect(window, nullptr, TRUE);

            POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT client = {};
            GetClientRect(window, &client);
            if (wasPressed && PtInRect(&client, point)) {
                SendMessageW(
                    GetParent(window),
                    WM_COMMAND,
                    MAKEWPARAM(state->commandId, BN_CLICKED),
                    reinterpret_cast<LPARAM>(window)
                );
            }
        }
        return 0;

    case WM_KEYDOWN:
        if (state && state->enabled && (wParam == VK_SPACE || wParam == VK_RETURN)) {
            SendMessageW(
                GetParent(window),
                WM_COMMAND,
                MAKEWPARAM(state->commandId, BN_CLICKED),
                reinterpret_cast<LPARAM>(window)
            );
            return 0;
        }
        break;

    case WM_PAINT:
        {
            PaintBuffered(window, [state](HDC dc, const RECT& client) {
                if (state) {
                    UiRenderer::DrawButton(
                        dc,
                        client,
                        state->text.c_str(),
                        state->primary,
                        state->pressed,
                        state->hot,
                        true,
                        state->enabled,
                        state->onCard
                    );
                }
            });
        }
        return 0;

    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

int MeasureTextHeight(HDC dc, HFONT font, const std::wstring& text, int width) {
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(dc, font));
    RECT measure = {0, 0, width, 1};
    DrawTextW(dc, text.c_str(), -1, &measure, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    return measure.bottom - measure.top;
}

bool ClampScroll(HWND window, ScrollTextState* state, int visibleHeight) {
    UNREFERENCED_PARAMETER(window);
    const int previousScrollY = state->scrollY;
    const int maxScroll = std::max(0, state->contentHeight - visibleHeight);
    state->scrollY = std::clamp(state->scrollY, 0, maxScroll);
    return state->scrollY != previousScrollY;
}

bool SetScrollY(HWND window, ScrollTextState* state, int desiredScrollY, int visibleHeight) {
    const int previousScrollY = state->scrollY;
    state->scrollY = desiredScrollY;
    ClampScroll(window, state, visibleHeight);
    return state->scrollY != previousScrollY;
}

bool ClampScroll(HWND window, LogViewState* state, int visibleHeight) {
    UNREFERENCED_PARAMETER(window);
    const int previousScrollY = state->scrollY;
    const int maxScroll = std::max(0, state->contentHeight - visibleHeight);
    state->scrollY = std::clamp(state->scrollY, 0, maxScroll);
    return state->scrollY != previousScrollY;
}

bool SetScrollY(HWND window, LogViewState* state, int desiredScrollY, int visibleHeight) {
    const int previousScrollY = state->scrollY;
    state->scrollY = desiredScrollY;
    ClampScroll(window, state, visibleHeight);
    return state->scrollY != previousScrollY;
}

int GetScrollVisibleEnd(const RECT& client) {
    return std::max(1, static_cast<int>(client.bottom - client.top) - kScrollTextClipBottomPadding);
}

RECT GetScrollbarThumb(const RECT& client, int contentHeight, int scrollY) {
    constexpr int barWidth = 8;
    constexpr int padding = 6;
    RECT track = {client.right - barWidth - padding, client.top + padding, client.right - padding, client.bottom - padding};
    const int trackHeight = std::max(1, static_cast<int>(track.bottom - track.top));
    const int visibleHeight = GetScrollVisibleEnd(client);
    const int thumbHeight = std::clamp((visibleHeight * trackHeight) / std::max(visibleHeight, contentHeight), 28, trackHeight);
    const int maxScroll = std::max(1, contentHeight - visibleHeight);
    const int maxThumbTravel = std::max(0, trackHeight - thumbHeight);
    const int thumbTop = static_cast<int>(track.top) + (scrollY * maxThumbTravel) / maxScroll;
    return {track.left, thumbTop, track.right, thumbTop + thumbHeight};
}

LRESULT CALLBACK ScrollTextProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    ScrollTextState* state = reinterpret_cast<ScrollTextState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<ScrollTextState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }

    switch (message) {
    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEWHEEL:
        if (state) {
            RECT client = {};
            GetClientRect(window, &client);
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            const int desiredScrollY = state->scrollY - ((delta / WHEEL_DELTA) * 42);
            if (SetScrollY(window, state, desiredScrollY, GetScrollVisibleEnd(client))) {
                InvalidateRect(window, nullptr, TRUE);
            }
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (state) {
            RECT client = {};
            GetClientRect(window, &client);
            RECT thumb = GetScrollbarThumb(client, state->contentHeight, state->scrollY);
            POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (state->contentHeight > GetScrollVisibleEnd(client) && PtInRect(&thumb, point)) {
                state->draggingThumb = true;
                state->dragStartY = point.y;
                state->dragStartScrollY = state->scrollY;
                SetCapture(window);
            }
        }
        return 0;

    case WM_MOUSEMOVE:
        if (state && state->draggingThumb) {
            RECT client = {};
            GetClientRect(window, &client);
            RECT thumb = GetScrollbarThumb(client, state->contentHeight, state->scrollY);
            const int visibleHeight = GetScrollVisibleEnd(client);
            const int maxScroll = std::max(0, state->contentHeight - visibleHeight);
            const int trackTravel = std::max(
                1,
                static_cast<int>(client.bottom - client.top) - 12 - static_cast<int>(thumb.bottom - thumb.top)
            );
            const int dy = GET_Y_LPARAM(lParam) - state->dragStartY;
            const int desiredScrollY = state->dragStartScrollY + (dy * maxScroll) / trackTravel;
            if (SetScrollY(window, state, desiredScrollY, visibleHeight)) {
                InvalidateRect(window, nullptr, TRUE);
            }
        }
        return 0;

    case WM_LBUTTONUP:
        if (state && state->draggingThumb) {
            state->draggingThumb = false;
            if (GetCapture() == window) {
                ReleaseCapture();
            }
        }
        return 0;

    case WM_PAINT:
        if (state) {
            PaintBuffered(window, [window, state](HDC dc, const RECT& client) {
                UiRenderer::DrawInputFrame(dc, client);

                HFONT font = CreateUiFont(-15, FW_NORMAL);
                const int textWidth = std::max(40, static_cast<int>(client.right - client.left) - 38);
                const int textHeight = MeasureTextHeight(dc, font, state->text, textWidth);
                state->contentHeight = textHeight + kScrollTextTopPadding + kScrollTextBottomPadding;
                ClampScroll(window, state, GetScrollVisibleEnd(client));

                RECT textRect = {
                    client.left + 14,
                    client.top + kScrollTextTopPadding - state->scrollY,
                    client.right - 24,
                    client.top + kScrollTextTopPadding - state->scrollY + textHeight
                };
                HRGN clip = CreateRectRgn(
                    client.left + 10,
                    client.top + kScrollTextClipTopPadding,
                    client.right - 16,
                    client.bottom - kScrollTextClipBottomPadding
                );
                SelectClipRgn(dc, clip);
                DrawTextBlock(dc, state->text, textRect, kTextColor, font, DT_LEFT | DT_WORDBREAK);
                SelectClipRgn(dc, nullptr);
                DeleteObject(clip);

                if (state->contentHeight > GetScrollVisibleEnd(client)) {
                    Gdiplus::Graphics graphics(dc);
                    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
                    RECT thumb = GetScrollbarThumb(client, state->contentHeight, state->scrollY);
                    Gdiplus::SolidBrush trackBrush(Gdiplus::Color(255, 39, 39, 43));
                    Gdiplus::SolidBrush thumbBrush(Gdiplus::Color(255, 86, 86, 92));
                    graphics.FillRectangle(
                        &trackBrush,
                        static_cast<INT>(client.right - 14),
                        static_cast<INT>(client.top + 8),
                        6,
                        static_cast<INT>(client.bottom - client.top - 16)
                    );
                    graphics.FillRectangle(
                        &thumbBrush,
                        static_cast<INT>(thumb.left),
                        static_cast<INT>(thumb.top),
                        static_cast<INT>(thumb.right - thumb.left),
                        static_cast<INT>(thumb.bottom - thumb.top)
                    );
                }

                DeleteObject(font);
            });
        }
        return 0;

    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

std::vector<std::wstring> SplitLogLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find(L'\n', start);
        std::wstring line = end == std::wstring::npos
            ? text.substr(start)
            : text.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
        if (end == std::wstring::npos) {
            break;
        }
        start = end + 1;
    }
    if (lines.empty()) {
        lines.push_back({});
    }
    return lines;
}

void RebuildLogLayout(HDC dc, HFONT font, LogViewState* state, const RECT& client) {
    const int textWidth = std::max(40, static_cast<int>(client.right - client.left) - 42);
    int y = kScrollTextTopPadding;
    state->layouts.clear();
    state->layouts.reserve(state->lines.size());
    for (const std::wstring& line : state->lines) {
        const int textHeight = std::max(18, MeasureTextHeight(dc, font, line.empty() ? L" " : line, textWidth));
        LogLineLayout layout;
        layout.rect = {0, y, textWidth, y + textHeight + 8};
        state->layouts.push_back(layout);
        y = layout.rect.bottom;
    }
    state->contentHeight = y + kScrollTextBottomPadding;
    ClampScroll(nullptr, state, GetScrollVisibleEnd(client));
}

int HitTestLogLine(LogViewState* state, int contentY) {
    for (size_t index = 0; index < state->layouts.size(); ++index) {
        const RECT& rect = state->layouts[index].rect;
        if (contentY >= rect.top && contentY < rect.bottom) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void ShowLogCopyMenu(HWND owner, HINSTANCE instance, POINT screenPoint, const std::wstring& text) {
    if (text.empty()) {
        return;
    }

    auto* state = new LogCopyMenuState{};
    state->owner = owner;
    state->text = text;

    constexpr int menuWidth = 148;
    constexpr int menuHeight = 36;
    RECT workArea = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int x = std::max(
        static_cast<int>(workArea.left),
        std::min(static_cast<int>(screenPoint.x), static_cast<int>(workArea.right) - menuWidth)
    );
    const int y = std::max(
        static_cast<int>(workArea.top),
        std::min(static_cast<int>(screenPoint.y), static_cast<int>(workArea.bottom) - menuHeight)
    );

    HWND menu = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kLogCopyMenuClassName,
        L"",
        WS_POPUP,
        x,
        y,
        menuWidth,
        menuHeight,
        owner,
        nullptr,
        instance,
        state
    );
    if (!menu) {
        delete state;
        return;
    }
    ShowWindow(menu, SW_SHOW);
    SetFocus(menu);
}

HWND CreateLogView(HWND parent, HINSTANCE instance, const std::wstring& text) {
    auto* state = new LogViewState{};
    state->text = text;
    state->lines = SplitLogLines(text);

    HWND view = CreateWindowExW(
        0,
        kLogViewClassName,
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0,
        0,
        10,
        10,
        parent,
        nullptr,
        instance,
        state
    );
    if (!view) {
        delete state;
    }
    return view;
}

LRESULT CALLBACK LogCopyMenuProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    LogCopyMenuState* state = reinterpret_cast<LogCopyMenuState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<LogCopyMenuState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }

    switch (message) {
    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEMOVE:
        if (state && !state->hot) {
            state->hot = true;
            TRACKMOUSEEVENT track = {};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = window;
            TrackMouseEvent(&track);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;

    case WM_MOUSELEAVE:
        if (state && state->hot) {
            state->hot = false;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;

    case WM_PAINT:
        PaintBuffered(window, [state](HDC dc, const RECT& client) {
            const std::vector<UiRenderer::PopupMenuItem> items = {
                {1, L"Копировать", false}
            };
            UiRenderer::DrawPopupMenu(dc, client, items, state && state->hot ? 1 : 0);
        });
        return 0;

    case WM_LBUTTONUP:
        if (state) {
            CopyTextToClipboard(state->owner ? state->owner : window, state->text);
        }
        DestroyWindow(window);
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(window);
            return 0;
        }
        if (wParam == VK_RETURN || wParam == VK_SPACE) {
            if (state) {
                CopyTextToClipboard(state->owner ? state->owner : window, state->text);
            }
            DestroyWindow(window);
            return 0;
        }
        break;

    case WM_KILLFOCUS:
        DestroyWindow(window);
        return 0;

    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

void ApplySettingsComboSelection(SettingsComboMenuState* menuState, HWND menu, int index) {
    if (!menuState || index < 0 || index >= static_cast<int>(menuState->values.size())) {
        return;
    }
    DialogState* ownerState = reinterpret_cast<DialogState*>(GetWindowLongPtrW(menuState->owner, GWLP_USERDATA));
    if (!ownerState) {
        return;
    }

    const std::wstring value = menuState->values[static_cast<size_t>(index)];
    if (menuState->target == SettingsLanguageTarget::VotSubtitle) {
        ownerState->workingConfig.votSubtitleLanguage = value;
    } else {
        ownerState->workingConfig.voiceOverLanguage = value;
    }
    RefreshSettingsButtons(ownerState);
    InvalidateRect(ownerState->window, nullptr, FALSE);
    DestroyWindow(menu);
}

LRESULT CALLBACK SettingsComboMenuProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    SettingsComboMenuState* state = reinterpret_cast<SettingsComboMenuState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<SettingsComboMenuState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }

    constexpr int itemTop = 4;
    constexpr int itemHeight = 34;
    switch (message) {
    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEMOVE:
        if (state) {
            const int index = (GET_Y_LPARAM(lParam) - itemTop) / itemHeight;
            const int hot = (index >= 0 && index < static_cast<int>(state->values.size())) ? index : -1;
            if (hot != state->hotIndex) {
                state->hotIndex = hot;
                TRACKMOUSEEVENT track = {};
                track.cbSize = sizeof(track);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = window;
                TrackMouseEvent(&track);
                InvalidateRect(window, nullptr, FALSE);
            }
        }
        return 0;

    case WM_MOUSELEAVE:
        if (state && state->hotIndex != -1) {
            state->hotIndex = -1;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONUP:
        if (state) {
            const int index = (GET_Y_LPARAM(lParam) - itemTop) / itemHeight;
            ApplySettingsComboSelection(state, window, index);
        }
        return 0;

    case WM_KEYDOWN:
        if (state) {
            if (wParam == VK_ESCAPE) {
                DestroyWindow(window);
                return 0;
            }
            if (wParam == VK_DOWN) {
                state->hotIndex = std::min(static_cast<int>(state->values.size()) - 1, state->hotIndex + 1);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_UP) {
                state->hotIndex = std::max(0, state->hotIndex == -1 ? 0 : state->hotIndex - 1);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_RETURN || wParam == VK_SPACE) {
                ApplySettingsComboSelection(state, window, state->hotIndex == -1 ? 0 : state->hotIndex);
                return 0;
            }
        }
        break;

    case WM_KILLFOCUS:
        DestroyWindow(window);
        return 0;

    case WM_PAINT:
        PaintBuffered(window, [state](HDC dc, const RECT& client) {
            std::vector<UiRenderer::PopupMenuItem> items;
            if (state) {
                items.reserve(state->values.size());
                for (size_t index = 0; index < state->values.size(); ++index) {
                    items.push_back({static_cast<UINT>(index + 1), state->values[index], false});
                }
            }
            UiRenderer::DrawPopupMenu(dc, client, items, state && state->hotIndex >= 0 ? static_cast<UINT>(state->hotIndex + 1) : 0);
        });
        return 0;

    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK LogViewProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    LogViewState* state = reinterpret_cast<LogViewState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<LogViewState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }

    switch (message) {
    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEWHEEL:
        if (state) {
            RECT client = {};
            GetClientRect(window, &client);
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            const int desiredScrollY = state->scrollY - ((delta / WHEEL_DELTA) * 44);
            if (SetScrollY(window, state, desiredScrollY, GetScrollVisibleEnd(client))) {
                InvalidateRect(window, nullptr, FALSE);
            }
        }
        return 0;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        if (state) {
            SetFocus(window);
            RECT client = {};
            GetClientRect(window, &client);
            POINT point = {};
            if (message == WM_CONTEXTMENU) {
                point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (point.x == -1 && point.y == -1) {
                    if (state->selectedLine >= 0 && state->selectedLine < static_cast<int>(state->layouts.size())) {
                        const RECT selected = state->layouts[static_cast<size_t>(state->selectedLine)].rect;
                        point = {client.left + 18, client.top + selected.top - state->scrollY + 4};
                    } else {
                        return 0;
                    }
                } else {
                    ScreenToClient(window, &point);
                }
            } else {
                point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            }
            RECT thumb = GetScrollbarThumb(client, state->contentHeight, state->scrollY);
            if (message == WM_LBUTTONDOWN &&
                state->contentHeight > GetScrollVisibleEnd(client) &&
                PtInRect(&thumb, point)) {
                state->draggingThumb = true;
                state->dragStartY = point.y;
                state->dragStartScrollY = state->scrollY;
                SetCapture(window);
                return 0;
            }

            HDC dc = GetDC(window);
            HFONT font = CreateUiFont(-13, FW_NORMAL);
            RebuildLogLayout(dc, font, state, client);
            DeleteObject(font);
            ReleaseDC(window, dc);

            const int hit = HitTestLogLine(state, point.y + state->scrollY);
            if (hit >= 0) {
                state->selectedLine = hit;
                InvalidateRect(window, nullptr, FALSE);
                if (message == WM_RBUTTONUP || message == WM_CONTEXTMENU) {
                    POINT screenPoint = point;
                    ClientToScreen(window, &screenPoint);
                    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE));
                    ShowLogCopyMenu(window, instance, screenPoint, state->lines[static_cast<size_t>(hit)]);
                }
            }
        }
        return 0;

    case WM_MOUSEMOVE:
        if (state && state->draggingThumb) {
            RECT client = {};
            GetClientRect(window, &client);
            RECT thumb = GetScrollbarThumb(client, state->contentHeight, state->scrollY);
            const int visibleHeight = GetScrollVisibleEnd(client);
            const int maxScroll = std::max(0, state->contentHeight - visibleHeight);
            const int trackTravel = std::max(
                1,
                static_cast<int>(client.bottom - client.top) - 12 - static_cast<int>(thumb.bottom - thumb.top)
            );
            const int dy = GET_Y_LPARAM(lParam) - state->dragStartY;
            const int desiredScrollY = state->dragStartScrollY + (dy * maxScroll) / trackTravel;
            if (SetScrollY(window, state, desiredScrollY, visibleHeight)) {
                InvalidateRect(window, nullptr, FALSE);
            }
        }
        return 0;

    case WM_LBUTTONUP:
        if (state && state->draggingThumb) {
            state->draggingThumb = false;
            if (GetCapture() == window) {
                ReleaseCapture();
            }
        }
        return 0;

    case WM_KEYDOWN:
        if (state) {
            const bool controlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (controlDown && wParam == 'C' && state->selectedLine >= 0 &&
                state->selectedLine < static_cast<int>(state->lines.size())) {
                CopyTextToClipboard(window, state->lines[static_cast<size_t>(state->selectedLine)]);
                return 0;
            }
        }
        break;

    case WM_PAINT:
        if (state) {
            PaintBuffered(window, [window, state](HDC dc, const RECT& client) {
                UiRenderer::DrawInputFrame(dc, client);

                HFONT font = CreateUiFont(-13, FW_NORMAL);
                RebuildLogLayout(dc, font, state, client);

                HRGN clip = CreateRectRgn(
                    client.left + 10,
                    client.top + kScrollTextClipTopPadding,
                    client.right - 16,
                    client.bottom - kScrollTextClipBottomPadding
                );
                SelectClipRgn(dc, clip);

                const int textLeft = client.left + 14;
                const int textRight = client.right - 28;
                for (size_t index = 0; index < state->lines.size(); ++index) {
                    const RECT layout = state->layouts[index].rect;
                    RECT visualRect = {
                        textLeft,
                        client.top + layout.top - state->scrollY,
                        textRight,
                        client.top + layout.bottom - state->scrollY
                    };
                    if (visualRect.bottom < client.top || visualRect.top > client.bottom) {
                        continue;
                    }
                    if (static_cast<int>(index) == state->selectedLine) {
                        Gdiplus::Graphics graphics(dc);
                        graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
                        RECT selected = {visualRect.left - 6, visualRect.top - 2, visualRect.right + 2, visualRect.bottom - 4};
                        Gdiplus::GraphicsPath path;
                        AddRoundedRect(path, selected, 5);
                        Gdiplus::SolidBrush fill(Gdiplus::Color(255, 48, 48, 54));
                        graphics.FillPath(&fill, &path);
                    }

                    RECT textRect = {
                        visualRect.left,
                        visualRect.top,
                        visualRect.right,
                        visualRect.bottom - 6
                    };
                    DrawTextBlock(
                        dc,
                        state->lines[index],
                        textRect,
                        kTextColor,
                        font,
                        DT_LEFT | DT_WORDBREAK
                    );
                }
                SelectClipRgn(dc, nullptr);
                DeleteObject(clip);

                if (state->contentHeight > GetScrollVisibleEnd(client)) {
                    Gdiplus::Graphics graphics(dc);
                    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
                    RECT thumb = GetScrollbarThumb(client, state->contentHeight, state->scrollY);
                    Gdiplus::SolidBrush trackBrush(Gdiplus::Color(255, 39, 39, 43));
                    Gdiplus::SolidBrush thumbBrush(Gdiplus::Color(255, 86, 86, 92));
                    graphics.FillRectangle(
                        &trackBrush,
                        static_cast<INT>(client.right - 14),
                        static_cast<INT>(client.top + 8),
                        6,
                        static_cast<INT>(client.bottom - client.top - 16)
                    );
                    graphics.FillRectangle(
                        &thumbBrush,
                        static_cast<INT>(thumb.left),
                        static_cast<INT>(thumb.top),
                        static_cast<INT>(thumb.right - thumb.left),
                        static_cast<INT>(thumb.bottom - thumb.top)
                    );
                }

                DeleteObject(font);
            });
        }
        return 0;

    case WM_SIZE:
        if (state) {
            state->layouts.clear();
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;

    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

bool ShowConfirmationDialog(
    HWND owner,
    HINSTANCE instance,
    const std::wstring& title,
    const std::wstring& message
) {
    auto* state = new DialogState{};
    state->type = DialogType::Confirmation;
    state->instance = instance;
    state->owner = owner;
    state->title = title;
    state->message = message;
    bool confirmed = false;
    state->savedResult = &confirmed;
    ShowModal(state, 560, 360);
    return confirmed;
}

} // namespace

void ShowInfoDialog(HWND owner, HINSTANCE instance, const std::wstring& title, const std::wstring& message) {
    auto* state = new DialogState{};
    state->type = DialogType::Info;
    state->instance = instance;
    state->owner = owner;
    state->title = title;
    state->message = message;
    ShowModal(state, 560, 360);
}

void ShowErrorDialog(HWND owner, HINSTANCE instance, const std::wstring& title, const std::wstring& message) {
    auto* state = new DialogState{};
    state->type = DialogType::Error;
    state->instance = instance;
    state->owner = owner;
    state->title = title;
    state->message = message;
    ShowModal(state, 620, 420);
}

bool ShowToolReadinessDialog(HWND owner, HINSTANCE instance, ToolReadinessIssue issue) {
    const ToolReadinessDialogContent content = BuildToolReadinessDialogContent(issue);
    auto* state = new DialogState{};
    state->type = DialogType::Confirmation;
    state->instance = instance;
    state->owner = owner;
    state->title = content.title;
    state->subtitle = L"Не хватает инструмента для выбранного действия.";
    state->message = content.message;
    state->primaryButtonText = content.openToolsText;
    state->cancelButtonText = content.cancelText;
    bool openTools = false;
    state->savedResult = &openTools;
    ShowModal(state, 620, 420);
    return openTools;
}

bool ShowAffectedFilesOverwriteDialog(
    HWND owner,
    HINSTANCE instance,
    const std::wstring& title,
    const std::vector<std::filesystem::path>& affectedFiles
) {
    if (affectedFiles.empty()) {
        return true;
    }

    auto* state = new DialogState{};
    state->type = DialogType::AffectedFiles;
    state->instance = instance;
    state->owner = owner;
    state->title = title;
    state->subtitle = L"Подтвердите перезапись перед запуском операции.";
    state->message = BuildAffectedFilesMessage(affectedFiles);
    state->primaryButtonText = L"Перезаписать";
    state->cancelButtonText = L"Отмена";
    bool confirmed = false;
    state->savedResult = &confirmed;
    ShowModal(state, 720, 460);
    return confirmed;
}

void ShowLogsDialog(HWND owner, HINSTANCE instance, const std::wstring& logText) {
    auto* state = new DialogState{};
    state->type = DialogType::Logs;
    state->instance = instance;
    state->owner = owner;
    state->title = L"Логи";
    state->message = logText.empty() ? L"Лог пока пуст." : logText;
    ShowModal(state, 860, 560);
}

bool ShowSettingsDialog(
    HWND owner,
    HINSTANCE instance,
    const AppPaths& paths,
    AppConfig& config,
    SettingsInitialSection initialSection
) {
    auto* state = new DialogState{};
    state->type = DialogType::Settings;
    state->instance = instance;
    state->owner = owner;
    state->title = L"Настройки";
    state->paths = paths.root().empty() ? nullptr : &paths;
    state->config = &config;
    state->workingConfig = config;
    state->settingsSection = ToSettingsSection(initialSection);
    bool saved = false;
    state->savedResult = &saved;
    ShowModal(state, kSettingsDialogWidth, kSettingsDialogHeight);
    return saved;
}

void ShowAboutDialog(HWND owner, HINSTANCE instance, const AppPaths& paths) {
    auto* state = new DialogState{};
    state->type = DialogType::About;
    state->instance = instance;
    state->owner = owner;
    state->paths = paths.root().empty() ? nullptr : &paths;
    state->title = L"О программе";
    state->message =
        L"YouTube Downloader\n\n"
        L"Портативный Win32 загрузчик видео с YouTube.\n\n"
        L"Версия: " YTD_APP_VERSION_WIDE;
    ShowModal(state, 560, 360);
}

bool ShowFfmpegDialog(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config) {
    auto* state = new DialogState{};
    state->type = DialogType::Ffmpeg;
    state->instance = instance;
    state->owner = owner;
    state->paths = paths.root().empty() ? nullptr : &paths;
    state->config = &config;
    const FfmpegStatus status = ResolveDialogFfmpegStatus(state);
    state->title = FfmpegDialogTitle(status);
    state->message = FfmpegDialogMessage(status);
    bool saved = false;
    state->savedResult = &saved;
    ShowModal(state, 600, 370);
    return saved;
}

bool ShowWhisperDialog(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config) {
    auto* state = new DialogState{};
    state->type = DialogType::Whisper;
    state->instance = instance;
    state->owner = owner;
    state->paths = paths.root().empty() ? nullptr : &paths;
    state->config = &config;
    const ToolInstallStatus status = state->paths ? WhisperManager::Resolve(*state->paths, config) : ToolInstallStatus{};
    const std::filesystem::path modelPath = ResolveDialogWhisperModelPath(state);
    const bool modelReady = IsRegularFile(modelPath);
    state->title = WhisperDialogTitle(status, modelReady);
    state->message = WhisperDialogMessage(status, modelPath, modelReady);
    bool saved = false;
    state->savedResult = &saved;
    ShowModal(state, 680, 360);
    return saved;
}

bool ShowWhisperModelDialog(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config) {
    auto* state = new DialogState{};
    state->type = DialogType::WhisperModel;
    state->instance = instance;
    state->owner = owner;
    state->paths = paths.root().empty() ? nullptr : &paths;
    state->config = &config;
    state->title = L"Модель Whisper";
    bool saved = false;
    state->savedResult = &saved;
    ShowModal(state, 760, 520);
    return saved;
}

bool ShowVotDialog(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config) {
    auto* state = new DialogState{};
    state->type = DialogType::Vot;
    state->instance = instance;
    state->owner = owner;
    state->paths = paths.root().empty() ? nullptr : &paths;
    state->config = &config;
    const VotExeStatus status = state->paths
        ? VotExeManager::Resolve(*state->paths, config)
        : VotExeManager::ResolveUserPath(config.votExePath);
    state->title = VotDialogTitle(status);
    state->message = VotDialogMessage(status);
    bool saved = false;
    state->savedResult = &saved;
    ShowModal(state, 620, 370);
    return saved;
}

bool ShowFfmpegInstallProgress(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config) {
    auto* state = new DialogState{};
    state->type = DialogType::Progress;
    state->instance = instance;
    state->owner = owner;
    state->title = L"Установка FFmpeg";
    state->message = L"Подготовка...";
    state->paths = &paths;
    state->config = &config;
    state->cancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool saved = false;
    state->savedResult = &saved;
    ShowModal(state, 560, 270);
    return saved;
}

bool ShowWhisperInstallProgress(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config) {
    auto* state = new DialogState{};
    state->type = DialogType::Progress;
    state->progressMode = ProgressMode::WhisperInstall;
    state->instance = instance;
    state->owner = owner;
    state->title = L"Установка Whisper.cpp";
    state->message = L"Подготовка...";
    state->paths = &paths;
    state->config = &config;
    state->cancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool saved = false;
    state->savedResult = &saved;
    ShowModal(state, 560, 270);
    return saved;
}

bool ShowWhisperModelDownloadProgress(
    HWND owner,
    HINSTANCE instance,
    const AppPaths& paths,
    AppConfig& config,
    const WhisperModelInfo& model
) {
    auto* state = new DialogState{};
    state->type = DialogType::Progress;
    state->progressMode = ProgressMode::WhisperModelDownload;
    state->instance = instance;
    state->owner = owner;
    state->title = L"Скачивание модели Whisper";
    state->message = L"Подготовка...";
    state->paths = &paths;
    state->config = &config;
    state->progressWhisperModel = model;
    state->cancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool saved = false;
    state->savedResult = &saved;
    ShowModal(state, 560, 270);
    return saved;
}

bool ShowVotInstallProgress(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config) {
    auto* state = new DialogState{};
    state->type = DialogType::Progress;
    state->progressMode = ProgressMode::VotInstall;
    state->instance = instance;
    state->owner = owner;
    state->title = L"Установка VOT helper";
    state->message = L"Подготовка...";
    state->paths = &paths;
    state->config = &config;
    state->cancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool saved = false;
    state->savedResult = &saved;
    ShowModal(state, 560, 270);
    return saved;
}

bool ShowAppUpdateProgress(HWND owner, HINSTANCE instance, const AppPaths& paths, const ReleaseAssetInfo& release) {
    auto* state = new DialogState{};
    state->type = DialogType::Progress;
    state->progressMode = ProgressMode::AppUpdate;
    state->instance = instance;
    state->owner = owner;
    state->title = L"Обновление приложения";
    state->message = L"Подготовка...";
    state->paths = &paths;
    state->release = release;
    state->cancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool updated = false;
    state->savedResult = &updated;
    ShowModal(state, 560, 270);
    return updated;
}

bool OfferAppUpdate(HWND owner, HINSTANCE instance, const AppPaths& paths, const ReleaseAssetInfo& release, bool notifyWhenCurrent) {
    if (!release.found) {
        if (notifyWhenCurrent) {
            ShowInfoDialog(owner, instance, L"Обновления", L"В последнем релизе не найден файл YoutubeDownloader.exe.");
        }
        return false;
    }

    if (!ShouldInstallAppUpdate(release)) {
        if (notifyWhenCurrent) {
            ShowInfoDialog(
                owner,
                instance,
                L"Обновления",
                L"Установлена актуальная версия: " YTD_APP_VERSION_WIDE
            );
        }
        return false;
    }

    if (!ShowConfirmationDialog(
            owner,
            instance,
            L"Обновление доступно",
            BuildAppUpdatePromptMessage(release)
        )) {
        return false;
    }

    return ShowAppUpdateProgress(owner, instance, paths, release);
}

bool CheckAndOfferAppUpdate(HWND owner, HINSTANCE instance, const AppPaths& paths, bool notifyWhenCurrent) {
    try {
        const ReleaseAssetInfo release = AppUpdateService::CheckLatestRelease();
        return OfferAppUpdate(owner, instance, paths, release, notifyWhenCurrent);
    } catch (const std::exception& ex) {
        if (notifyWhenCurrent) {
            ShowErrorDialog(
                owner,
                instance,
                L"Проверка обновлений не удалась",
                LocalizedToolErrorText(ex.what())
            );
        }
    } catch (...) {
        if (notifyWhenCurrent) {
            ShowErrorDialog(owner, instance, L"Проверка обновлений не удалась", L"Неизвестная ошибка.");
        }
    }
    return false;
}
