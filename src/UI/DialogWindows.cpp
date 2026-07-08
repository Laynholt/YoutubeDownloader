#include "DialogWindows.h"

#include "AppVersion.h"
#include "BackendText.h"
#include "Localization.h"
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
#include <cwctype>
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
constexpr int kSettingsParallelCardHeight = 120;
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
    Additional,
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
    IdVotSubtitleLanguageEdit = 153,
    IdVoiceVolumeMinus = 154,
    IdVoiceVolumePlus = 155,
    IdWhisperModelChooseFolder = 156,
    IdUiLanguage = 157,
    IdSettingsNavAdditional = 158,
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
    VoiceOver,
    Interface
};

struct SettingsComboMenuState {
    HWND owner = nullptr;
    SettingsLanguageTarget target = SettingsLanguageTarget::VotSubtitle;
    std::vector<std::wstring> values;
    std::vector<std::wstring> labels;
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
    int settingsScrollY = 0;
    int whisperStatusScrollY = 0;
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
    std::vector<UiLanguage> uiLanguages;
};

void ShowModal(DialogState* state, int width, int height);

void CloseDialogWindow(HWND window) {
    ShowWindow(window, SW_HIDE);
    DestroyWindow(window);
}

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

RECT SettingsDownloadsParallelCardRect(const DialogState* state, int width, int height) {
    const RECT previous = SettingsStackCardRect(state, width, height, 1, kSettingsChoiceCardHeight);
    return {previous.left, previous.bottom + kSettingsCardGap, previous.right, previous.bottom + kSettingsCardGap + kSettingsParallelCardHeight};
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
        return state && state->whisperDetailsExpanded ? 226 : kSettingsToolCardHeight;
    }
    return state && state->votDetailsExpanded ? 124 : kSettingsToolCardHeight;
}

RECT SettingsToolCardRect(const DialogState* state, int width, int height, int index) {
    const RECT content = SettingsContentRect(state, width, height);
    int top = content.top + kSettingsContentTop - (state ? state->settingsScrollY : 0);
    for (int i = 0; i < index; ++i) {
        top += SettingsToolCardHeight(state, i) + kSettingsCardGap;
    }
    const int cardHeight = SettingsToolCardHeight(state, index);
    return {content.left, top, content.right, top + cardHeight};
}

int SettingsToolsContentHeight(const DialogState* state) {
    int height = kSettingsContentTop;
    for (int i = 0; i < 4; ++i) {
        height += SettingsToolCardHeight(state, i);
        if (i != 3) {
            height += kSettingsCardGap;
        }
    }
    return height;
}

int SettingsMaxScroll(const DialogState* state, int width, int height) {
    if (!state || state->settingsSection != SettingsSection::Tools) {
        return 0;
    }
    const RECT content = SettingsContentRect(state, width, height);
    return std::max(0, SettingsToolsContentHeight(state) - static_cast<int>(content.bottom - content.top));
}

bool ClampSettingsScroll(DialogState* state, int width, int height) {
    if (!state) {
        return false;
    }
    const int previous = state->settingsScrollY;
    state->settingsScrollY = std::clamp(state->settingsScrollY, 0, SettingsMaxScroll(state, width, height));
    return state->settingsScrollY != previous;
}

bool IsRegularFile(const std::filesystem::path& path);
std::filesystem::path ResolveDialogWhisperModelPath(const DialogState* state);
std::wstring WrapPathText(std::wstring text, size_t maxLine);
RECT GetScrollbarThumb(const RECT& client, int contentHeight, int scrollY);

bool RectInside(const RECT& inner, const RECT& outer) {
    return inner.left >= outer.left && inner.right <= outer.right &&
        inner.top >= outer.top && inner.bottom <= outer.bottom;
}

RECT WhisperStatusCardRect(const RECT& client) {
    return {24, 78, client.right - 24, client.bottom - 86};
}

RECT WhisperPathViewportRect(const RECT& statusCard) {
    return {statusCard.left + 18, statusCard.top + 54, statusCard.right - 18, statusCard.bottom - 42};
}

int WrappedLineCount(const std::wstring& text) {
    return 1 + static_cast<int>(std::count(text.begin(), text.end(), L'\n'));
}

std::wstring WrappedPathForStatus(const std::filesystem::path& path) {
    return path.empty() ? L"-" : WrapPathText(path.wstring(), 76);
}

int WhisperPathBlockHeight(const std::filesystem::path& path) {
    return 22 + std::max(24, WrappedLineCount(WrappedPathForStatus(path)) * 22);
}

int WhisperPathContentHeight(const std::filesystem::path& executable, const std::filesystem::path& model) {
    return WhisperPathBlockHeight(executable) + 12 + WhisperPathBlockHeight(model);
}

int WhisperStatusMaxScroll(const DialogState* state, const RECT& client) {
    if (!state || state->type != DialogType::Whisper) {
        return 0;
    }
    const ToolInstallStatus status = state->paths && state->config
        ? WhisperManager::Resolve(*state->paths, *state->config)
        : ToolInstallStatus{};
    const std::filesystem::path modelPath = ResolveDialogWhisperModelPath(state);
    const std::filesystem::path model = IsRegularFile(modelPath) ? modelPath : std::filesystem::path{};
    const RECT viewport = WhisperPathViewportRect(WhisperStatusCardRect(client));
    return std::max(0, WhisperPathContentHeight(status.executable, model) - static_cast<int>(viewport.bottom - viewport.top));
}

RECT WhisperScrollbarThumb(const RECT& viewport, int contentHeight, int scrollY) {
    RECT local = {0, 0, viewport.right - viewport.left, viewport.bottom - viewport.top};
    RECT thumb = GetScrollbarThumb(local, contentHeight, scrollY);
    OffsetRect(&thumb, viewport.left, viewport.top);
    return thumb;
}

RECT PaddedRect(RECT rect, int padding) {
    InflateRect(&rect, padding, padding);
    return rect;
}

RECT SettingsParallelValueRect(const DialogState* state, int width, int height) {
    const RECT card = SettingsDownloadsParallelCardRect(state, width, height);
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
    const std::wstring translated = Localization::UiText(text);
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(dc, font));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, translated.c_str(), -1, &rect, format | DT_NOPREFIX);
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
    BitBlt(
        screenDc,
        paint.rcPaint.left,
        paint.rcPaint.top,
        paint.rcPaint.right - paint.rcPaint.left,
        paint.rcPaint.bottom - paint.rcPaint.top,
        bufferDc,
        paint.rcPaint.left,
        paint.rcPaint.top,
        SRCCOPY
    );

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
    return PickFolder(owner, L"dialog.choose_the_ffmpeg_folder_or_the_bin_folder");
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

std::wstring InterfaceLanguageButtonText(const DialogState* state) {
    const std::wstring selected = state && !state->workingConfig.uiLanguage.empty()
        ? state->workingConfig.uiLanguage
        : L"ru";
    if (state) {
        for (const UiLanguage& language : state->uiLanguages) {
            if (language.id == selected) {
                return language.name + L"  ▾";
            }
        }
    }
    return L"dialog.russian";
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

void AddDialogTooltip(DialogState* state, HWND tool, std::wstring text);

void AddDialogTooltip(DialogState* state, HWND tool, const wchar_t* text) {
    if (!state || !tool || !text) {
        return;
    }
    AddDialogTooltip(state, tool, std::wstring(text));
}

void AddDialogTooltip(DialogState* state, HWND tool, std::wstring text) {
    if (!state || !tool) {
        return;
    }
    state->tooltipTexts.push_back(Localization::UiText(text));
    state->tooltips.push_back(CreateTooltip(state->window, tool, state->tooltipTexts.back().c_str()));
}

const WhisperModelInfo* RecommendedWhisperModel(const std::vector<WhisperModelInfo>& catalog);
std::optional<std::filesystem::path> FindWhisperModelNear(
    const std::filesystem::path& folder,
    const std::filesystem::path& executable,
    const WhisperModelInfo& selected,
    const std::vector<WhisperModelInfo>& catalog
);

bool ApplySelectedFfmpegPath(HWND owner, HINSTANCE instance, AppConfig& config, const std::filesystem::path& path) {
    const FfmpegStatus status = FfmpegManager::ResolveUserPath(path);
    if (!status.available) {
        ShowErrorDialog(owner, instance, L"dialog.ffmpeg_not_found", status.message);
        return false;
    }
    config.ffmpegPath = status.ffmpegExe;
    config.ffmpegVersion = FfmpegManager::ExecutableVersion(status.ffmpegExe);
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
        ShowErrorDialog(owner, instance, L"dialog.whisper_not_found", L"dialog.whisper_cli_exe_was_not_found_in_the_selected_folder");
        return false;
    }
    if (!WhisperManager::SelfTestExecutable(executable)) {
        ShowErrorDialog(
            owner,
            instance,
            L"dialog.whisper_does_not_start",
            L"dialog.the_selected_whisper_cli_exe_was_found_but_failed_the_la"
        );
        return false;
    }
    config.whisperPath = executable;
    config.whisperVersion = WhisperManager::ExecutableVersion(executable);
    config.whisperBackend = WhisperBackend::Custom;
    const std::vector<WhisperModelInfo> catalog = WhisperManager::ModelCatalog();
    if (const WhisperModelInfo* recommended = RecommendedWhisperModel(catalog)) {
        if (auto modelPath = FindWhisperModelNear(path, executable, *recommended, catalog)) {
            config.whisperModelPath = *modelPath;
        }
    }
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
    state->title = L"dialog.choose_vot_helper";
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
            ShowErrorDialog(owner, instance, L"dialog.vot_helper_not_found", L"dialog.vot_helper_exe_was_not_found_at_the_selected_path");
        }
        return false;
    }
    if (!VotExeManager::SelfTestExecutable(*selected)) {
        ShowErrorDialog(
            owner,
            instance,
            L"dialog.vot_helper_does_not_start",
            L"dialog.the_selected_vot_helper_exe_was_found_but_failed_the_lau"
        );
        return false;
    }
    config.votExePath = *selected;
    config.votExeVersion = VotExeManager::ExecutableVersion(*selected);
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
    return status.available ? L"dialog.ffmpeg_selected" : L"dialog.ffmpeg_not_found";
}

std::wstring FfmpegDialogMessage(const FfmpegStatus& status) {
    if (status.available) {
        return L"dialog.ffmpeg_found_and_will_be_used_to_merge_video_audio_track" +
            status.ffmpegExe.wstring();
    }
    return L"dialog.ffmpeg_not_found_without_it_the_application_can_download";
}

std::wstring WhisperDialogTitle(const ToolInstallStatus& status, bool modelReady) {
    if (status.installed && modelReady) {
        return L"dialog.whisper_cpp_ready";
    }
    if (status.installed) {
        return L"dialog.whisper_model_required";
    }
    return L"dialog.whisper_cpp_not_found";
}

std::wstring WhisperDialogMessage(const ToolInstallStatus& status, const std::filesystem::path& modelPath, bool modelReady) {
    std::wstring message = status.installed
        ? L"dialog.whisper_cli_exe_found_and_can_be_used_for_local_transcri"
        : L"dialog.whisper_cpp_is_required_for_local_transcription_without";
    message += L"\n\nwhisper-cli.exe:\n";
    message += status.executable.empty() ? L"-" : status.executable.wstring();
    message += L"dialog.model";
    message += modelReady ? modelPath.wstring() : L"dialog.not_found_you_can_download_the_recommended_model";
    return message;
}

std::wstring VotDialogTitle(const VotExeStatus& status) {
    return status.available ? L"dialog.vot_helper_ready" : L"dialog.vot_helper_not_found";
}

std::wstring VotDialogMessage(const VotExeStatus& status) {
    if (status.available) {
        return L"dialog.vot_helper_exe_found_and_will_be_used_for_translation_an" +
            status.executable.wstring();
    }
    return L"dialog.vot_helper_exe_is_required_for_voice_over_translation_an";
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

std::wstring ToLower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return text;
}

std::wstring InferCustomWhisperBackendText(const std::filesystem::path& executable) {
    const std::wstring pathText = ToLower(executable.wstring());
    if (pathText.find(L"cuda") != std::wstring::npos || pathText.find(L"cublas") != std::wstring::npos) {
        return L"GPU";
    }
    if (pathText.find(L"cpu") != std::wstring::npos) {
        return L"CPU";
    }

    const std::filesystem::path dir = executable.parent_path();
    const std::array<const wchar_t*, 3> gpuDlls = {L"cublas64_12.dll", L"cudart64_12.dll", L"ggml-cuda.dll"};
    for (const wchar_t* dll : gpuDlls) {
        if (IsRegularFile(dir / dll)) {
            return L"GPU";
        }
    }
    return L"dialog.custom";
}

std::wstring WhisperStatusPillText(
    const AppConfig& config,
    const ToolInstallStatus& status,
    bool cudaAvailable
) {
    if (status.whisperBackend == WhisperBackend::Custom) {
        return InferCustomWhisperBackendText(status.executable);
    }
    return WhisperBackendStatusText(config.whisperBackend, status.whisperBackend, cudaAvailable);
}

std::optional<std::filesystem::path> FindWhisperModelInFolder(
    const std::filesystem::path& folder,
    const WhisperModelInfo& selected,
    const std::vector<WhisperModelInfo>& catalog
) {
    const std::filesystem::path selectedPath = folder / selected.fileName;
    if (IsRegularFile(selectedPath)) {
        return selectedPath;
    }
    for (const WhisperModelInfo& model : catalog) {
        const std::filesystem::path path = folder / model.fileName;
        if (IsRegularFile(path)) {
            return path;
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> FindWhisperModelNear(
    const std::filesystem::path& folder,
    const std::filesystem::path& executable,
    const WhisperModelInfo& selected,
    const std::vector<WhisperModelInfo>& catalog
) {
    std::vector<std::filesystem::path> folders = {folder, folder / L"models"};
    if (!executable.empty()) {
        const std::filesystem::path exeDir = executable.parent_path();
        folders.push_back(exeDir);
        folders.push_back(exeDir / L"models");
        folders.push_back(exeDir.parent_path() / L"models");
    }
    for (const auto& candidate : folders) {
        if (auto model = FindWhisperModelInFolder(candidate, selected, catalog)) {
            return model;
        }
    }
    return std::nullopt;
}

bool IsWhisperReady(const DialogState* state) {
    return state && state->paths &&
        WhisperManager::Resolve(*state->paths, state->workingConfig).installed;
}

bool IsVotReady(const DialogState* state) {
    return state && state->paths &&
        VotExeManager::Resolve(*state->paths, state->workingConfig).available;
}

bool ShowTranscriptionToolsCard(const DialogState* state) {
    return !IsFfmpegReady(state) || !IsWhisperReady(state) || !IsVotReady(state);
}

bool ShowTranslationToolsCard(const DialogState* state) {
    return !IsFfmpegReady(state) || !IsVotReady(state);
}

void AppendMissingTool(std::wstring& text, const wchar_t* tool) {
    if (!text.empty()) {
        text += L", ";
    }
    text += tool;
}

std::wstring MissingToolsText(const DialogState* state, bool includeWhisper) {
    std::wstring missing;
    if (!IsFfmpegReady(state)) {
        AppendMissingTool(missing, L"FFmpeg");
    }
    if (includeWhisper && !IsWhisperReady(state)) {
        AppendMissingTool(missing, L"Whisper.cpp");
    }
    if (!IsVotReady(state)) {
        AppendMissingTool(missing, L"VOT");
    }
    return L"dialog.missing" + missing + L"dialog.install_it_or_set_the_path_in_tools";
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
        text += L"dialog.recommended";
    } else if (model.bestQuality) {
        text += L"dialog.best_quality";
    }
    const std::wstring size = FormatModelSize(model.sizeBytes);
    if (!size.empty()) {
        text += L" · " + size;
    }
    return text;
}

std::wstring BuildAffectedFilesMessage(const std::vector<std::filesystem::path>& affectedFiles) {
    std::wstring message = L"dialog.the_following_files_will_be_overwritten_or_changed";
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
    state->text = Localization::UiText(text);

    HWND button = CreateWindowExW(
        0,
        kDialogButtonClassName,
        state->text.c_str(),
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
    const std::wstring translated = text.empty() ? std::wstring{} : Localization::UiText(text);
    const bool textChanged = !translated.empty() && state->text != translated;
    if (state->primary == primary && !textChanged) {
        return;
    }
    state->primary = primary;
    if (textChanged) {
        state->text = translated;
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
    SetDarkButtonState(state->window, IdSettingsNavDownloads, state->settingsSection == SettingsSection::Downloads, collapsed ? L"⬇" : L"dialog.downloads");
    SetDarkButtonState(state->window, IdSettingsNavTranscription, state->settingsSection == SettingsSection::Transcription, collapsed ? L"✎" : L"dialog.transcription");
    SetDarkButtonState(state->window, IdSettingsNavTranslation, state->settingsSection == SettingsSection::Translation, collapsed ? TranslationSettingsCollapsedIcon() : L"dialog.translation");
    SetDarkButtonState(state->window, IdSettingsNavAdditional, state->settingsSection == SettingsSection::Additional, collapsed ? L"+" : L"dialog.additional");
    SetDarkButtonState(state->window, IdSettingsNavTools, state->settingsSection == SettingsSection::Tools, collapsed ? L"⚙" : L"dialog.tools");
    SetDarkButtonState(state->window, IdSettingsNavAbout, state->settingsSection == SettingsSection::About, collapsed ? L"ⓘ" : L"dialog.about");

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
        state->workingConfig.autoUpdateApp ? L"dialog.auto_check_on" : L"dialog.auto_check_off"
    );
    SetDarkButtonState(state->window, IdUiLanguage, false, InterfaceLanguageButtonText(state));
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
    SetDarkButtonState(state->window, IdYtDlpDetails, state->ytDlpDetailsExpanded, state->ytDlpDetailsExpanded ? L"dialog.hide" : L"dialog.details");
    SetDarkButtonState(state->window, IdFfmpegDetails, state->ffmpegDetailsExpanded, state->ffmpegDetailsExpanded ? L"dialog.hide" : L"dialog.details");
    SetDarkButtonState(state->window, IdWhisperDetails, state->whisperDetailsExpanded, state->whisperDetailsExpanded ? L"dialog.hide" : L"dialog.details");
    SetDarkButtonState(state->window, IdVotDetails, state->votDetailsExpanded, state->votDetailsExpanded ? L"dialog.hide" : L"dialog.details");

    const bool ffmpegReady = IsFfmpegReady(state);
    const bool whisperReady = IsWhisperReady(state);
    const bool votReady = IsVotReady(state);
    const bool selectedTranscriptionReady =
        state->workingConfig.transcriptionEngine == TranscriptionEngine::Whisper
            ? ffmpegReady && whisperReady
            : votReady;
    SetDarkButtonEnabled(state->window, 111, true);
    SetDarkButtonEnabled(state->window, 112, ffmpegReady);
    SetDarkButtonEnabled(state->window, 113, ffmpegReady);
    SetDarkButtonEnabled(state->window, 114, ffmpegReady);
    SetDarkButtonEnabled(state->window, IdTranscriptionWhisper, ffmpegReady && whisperReady);
    SetDarkButtonEnabled(state->window, IdTranscriptionVot, votReady);
    SetDarkButtonEnabled(state->window, IdVotSubtitleLanguageEdit, votReady);
    SetDarkButtonEnabled(state->window, IdSubtitleModeOff, selectedTranscriptionReady);
    SetDarkButtonEnabled(state->window, IdSubtitleModeTrack, selectedTranscriptionReady && ffmpegReady);
    SetDarkButtonEnabled(state->window, IdSubtitleModeBurn, selectedTranscriptionReady && ffmpegReady);
    SetDarkButtonEnabled(state->window, IdVoiceLanguageEdit, votReady);
    SetDarkButtonEnabled(state->window, IdVoiceModeOff, votReady);
    SetDarkButtonEnabled(state->window, IdVoiceModeTrack, votReady && ffmpegReady);
    SetDarkButtonEnabled(state->window, IdVoiceModeMix, votReady && ffmpegReady);
    SetDarkButtonEnabled(state->window, IdVoiceVolumeMinus, votReady && ffmpegReady && state->workingConfig.voiceOverFfmpegMode == VoiceOverFfmpegMode::Mix);
    SetDarkButtonEnabled(state->window, IdVoiceVolumePlus, votReady && ffmpegReady && state->workingConfig.voiceOverFfmpegMode == VoiceOverFfmpegMode::Mix);
    SetDarkButtonEnabled(state->window, IdTranscriptionOpenTools, ShowTranscriptionToolsCard(state));
    SetDarkButtonEnabled(state->window, IdTranslationOpenTools, ShowTranslationToolsCard(state));
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
        if (state &&
            (state->type == DialogType::Whisper ||
                (state->type == DialogType::Settings && state->settingsSection == SettingsSection::Tools)) &&
            message.message == WM_MOUSEWHEEL) {
            SendMessageW(window, WM_MOUSEWHEEL, message.wParam, message.lParam);
            continue;
        }
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
        Localization::UiText(state->title).c_str(),
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
    const int okButtonWidth = state->primaryButtonText == L"dialog.open_tools" ? 172 : 112;
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
            panelRight - kDialogButtonInset - okButtonWidth - kDialogButtonGap - 112,
            buttonY,
            112,
            kDialogButtonHeight,
            TRUE
        );
    }
    if (okButton) {
        MoveWindow(okButton, panelRight - kDialogButtonInset - okButtonWidth, buttonY, okButtonWidth, kDialogButtonHeight, TRUE);
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
            ? std::vector<int>{IdInstall, IdChooseFolder, IdSkip}
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
            const std::wstring installText = Localization::UiText(WhisperInstallButtonText(
                state->config->whisperBackend,
                cudaAvailable,
                installTargetInstalled
            ));
            SetWindowTextW(install, installText.c_str());
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
    HWND chooseFolder = GetDlgItem(state->window, IdWhisperModelChooseFolder);
    HWND cancel = GetDlgItem(state->window, IdCancel);
    if (download) {
        MoveWindow(download, panelRight - sidePadding - 170 - kDialogButtonGap - 142 - kDialogButtonGap - 126, buttonY, 170, kDialogButtonHeight, TRUE);
    }
    if (chooseFolder) {
        MoveWindow(chooseFolder, panelRight - sidePadding - 142 - kDialogButtonGap - 126, buttonY, 142, kDialogButtonHeight, TRUE);
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
    ClampSettingsScroll(state, width, height);

    const RECT sidebar = SettingsSidebarRect(state, width, height);
    const RECT content = SettingsContentRect(state, width, height);

    HWND toggle = GetDlgItem(state->window, IdSettingsToggleSidebar);
    if (toggle) {
        MoveWindow(toggle, sidebar.right - 46, sidebar.top + 16, 32, 32, TRUE);
    }
    const std::array<int, 5> navIds = {
        IdSettingsNavDownloads,
        IdSettingsNavTranscription,
        IdSettingsNavTranslation,
        IdSettingsNavAdditional,
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
    HWND uiLanguage = GetDlgItem(state->window, IdUiLanguage);
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

    card = SettingsDownloadsParallelCardRect(state, width, height);
    const RECT parallelValue = SettingsParallelValueRect(state, width, height);
    if (parallelMinus) {
        MoveWindow(parallelMinus, parallelValue.left - 46 - kDialogButtonGap, parallelValue.top, 46, 34, TRUE);
    }
    if (parallelPlus) {
        MoveWindow(parallelPlus, parallelValue.right + kDialogButtonGap, parallelValue.top, 46, 34, TRUE);
    }

    card = SettingsStackCardRect(state, width, height, 0, kSettingsChoiceCardHeight);
    if (uiLanguage) {
        MoveWindow(uiLanguage, card.right - kSettingsCardPadding - 260, card.top + kSettingsCardControlTop, 260, 34, TRUE);
    }
    card = SettingsCardBelow(card, kSettingsChoiceCardHeight);
    if (autoUpdate) {
        MoveWindow(autoUpdate, card.right - kSettingsCardPadding - 260, card.top + kSettingsCardControlTop, 260, 34, TRUE);
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
    if (transcriptionTools && ShowTranscriptionToolsCard(state)) {
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
    if (translationTools && ShowTranslationToolsCard(state)) {
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
        const std::wstring text = Localization::UiText(ToolSetupButtonText());
        SetWindowTextW(ffmpeg, text.c_str());
    }
    if (chooseWhisper) {
        const std::wstring text = Localization::UiText(ToolSetupButtonText());
        SetWindowTextW(chooseWhisper, text.c_str());
    }
    if (chooseVot) {
        const std::wstring text = Localization::UiText(ToolSetupButtonText());
        SetWindowTextW(chooseVot, text.c_str());
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
        IdParallelMinus, IdParallelPlus
    }, state->settingsSection == SettingsSection::Downloads);
    SetControlsVisible(state->window, {
        IdAutoUpdate, IdUiLanguage
    }, state->settingsSection == SettingsSection::Additional);
    SetControlsVisible(state->window, {
        IdTranscriptionWhisper, IdTranscriptionVot, IdVotSubtitleLanguageEdit,
        IdSubtitleModeOff, IdSubtitleModeTrack, IdSubtitleModeBurn
    }, state->settingsSection == SettingsSection::Transcription);
    SetControlsVisible(
        state->window,
        {IdTranscriptionOpenTools},
        state->settingsSection == SettingsSection::Transcription && ShowTranscriptionToolsCard(state)
    );
    SetControlsVisible(state->window, {
        IdVoiceLanguageEdit, IdVoiceModeOff, IdVoiceModeTrack, IdVoiceModeMix,
        IdVoiceVolumeMinus, IdVoiceVolumePlus
    }, state->settingsSection == SettingsSection::Translation);
    SetControlsVisible(
        state->window,
        {IdTranslationOpenTools},
        state->settingsSection == SettingsSection::Translation && ShowTranslationToolsCard(state)
    );
    SetControlsVisible(state->window, {
        IdFfmpeg, IdChooseWhisperFolder, IdChooseVotFolder,
        IdYtDlpDetails, IdFfmpegDetails, IdWhisperDetails, IdVotDetails
    }, state->settingsSection == SettingsSection::Tools);
    if (state->settingsSection == SettingsSection::Tools) {
        const RECT viewport = {content.left, content.top + kSettingsContentTop, content.right, content.bottom};
        for (int id : {IdFfmpeg, IdChooseWhisperFolder, IdChooseVotFolder, IdYtDlpDetails, IdFfmpegDetails, IdWhisperDetails, IdVotDetails}) {
            HWND control = GetDlgItem(state->window, id);
            if (!control) {
                continue;
            }
            RECT rect = {};
            GetWindowRect(control, &rect);
            MapWindowPoints(HWND_DESKTOP, state->window, reinterpret_cast<POINT*>(&rect), 2);
            ShowWindow(control, RectInside(rect, viewport) ? SW_SHOW : SW_HIDE);
        }
    }
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
        ? L"dialog.error_the_text_can_be_copied_for_diagnostics"
        : (state->type == DialogType::Confirmation
            ? L"dialog.application_update_is_available"
            : L"dialog.application_information");
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
void DrawUtilityStatusWrapped(
    HDC dc,
    const std::wstring& label,
    const std::filesystem::path& path,
    const RECT& rect,
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
        RECT statusCard = WhisperStatusCardRect(client);
        DrawRoundedPanel(dc, statusCard, Gdiplus::Color(255, 32, 32, 35), Gdiplus::Color(255, 48, 48, 54), 8);
        const ToolInstallStatus status = state->paths && state->config
            ? WhisperManager::Resolve(*state->paths, *state->config)
            : ToolInstallStatus{};
        const std::filesystem::path modelPath = ResolveDialogWhisperModelPath(state);
        const bool modelReady = IsRegularFile(modelPath);
        DrawToolStatusPill(dc, {statusCard.left + 18, statusCard.top + 18, statusCard.left + 112, statusCard.top + 42}, status.installed ? L"dialog.ready" : L"dialog.no", status.installed, smallFont);
        DrawToolStatusPill(dc, {statusCard.left + 122, statusCard.top + 18, statusCard.left + 232, statusCard.top + 42}, modelReady ? L"dialog.model_2" : L"dialog.no_model", modelReady, smallFont);
        if (state->config) {
            const bool cudaAvailable = IsWhisperCudaCandidateAvailable();
            DrawToolStatusPill(
                dc,
                {statusCard.left + 242, statusCard.top + 18, statusCard.left + 352, statusCard.top + 42},
                WhisperStatusPillText(*state->config, status, cudaAvailable),
                status.installed,
                smallFont
            );
        }
        const std::filesystem::path visibleModelPath = modelReady ? modelPath : std::filesystem::path{};
        const int pathContentHeight = WhisperPathContentHeight(status.executable, visibleModelPath);
        state->whisperStatusScrollY = std::clamp(state->whisperStatusScrollY, 0, WhisperStatusMaxScroll(state, client));
        const RECT pathViewport = WhisperPathViewportRect(statusCard);
        RECT textViewport = pathViewport;
        if (pathContentHeight > static_cast<int>(pathViewport.bottom - pathViewport.top)) {
            textViewport.right -= 24;
        }
        const int clipState = SaveDC(dc);
        IntersectClipRect(dc, textViewport.left, textViewport.top, textViewport.right, textViewport.bottom);
        int pathTop = textViewport.top - state->whisperStatusScrollY;
        auto drawPathBlock = [&](const std::wstring& label, const std::filesystem::path& path) {
            const std::wstring text = WrappedPathForStatus(path);
            const int textHeight = std::max(24, WrappedLineCount(text) * 22);
            DrawTextBlock(dc, label, {textViewport.left, pathTop, textViewport.right, pathTop + 22}, kMutedTextColor, textFont, DT_LEFT | DT_SINGLELINE);
            DrawTextBlock(dc, text, {textViewport.left, pathTop + 22, textViewport.right, pathTop + 22 + textHeight}, kMutedTextColor, textFont, DT_LEFT | DT_WORDBREAK);
            pathTop += 22 + textHeight + 12;
        };
        drawPathBlock(L"whisper-cli.exe:", status.executable);
        drawPathBlock(L"dialog.model_3", visibleModelPath);
        RestoreDC(dc, clipState);
        if (pathContentHeight > static_cast<int>(pathViewport.bottom - pathViewport.top)) {
            Gdiplus::Graphics graphics(dc);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
            const RECT thumb = WhisperScrollbarThumb(pathViewport, pathContentHeight, state->whisperStatusScrollY);
            Gdiplus::SolidBrush trackBrush(Gdiplus::Color(255, 39, 39, 43));
            Gdiplus::SolidBrush thumbBrush(Gdiplus::Color(255, 86, 86, 92));
            graphics.FillRectangle(
                &trackBrush,
                static_cast<INT>(pathViewport.right - 14),
                static_cast<INT>(pathViewport.top + 8),
                6,
                static_cast<INT>(pathViewport.bottom - pathViewport.top - 16)
            );
            graphics.FillRectangle(
                &thumbBrush,
                static_cast<INT>(thumb.left),
                static_cast<INT>(thumb.top),
                static_cast<INT>(thumb.right - thumb.left),
                static_cast<INT>(thumb.bottom - thumb.top)
            );
        }
        DrawTextBlock(
            dc,
            L"dialog.install_whisper_cpp_then_choose_or_download_a_recognitio",
            {statusCard.left + 18, statusCard.bottom - 34, statusCard.right - 18, statusCard.bottom - 10},
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

    DrawTextBlock(dc, L"dialog.whisper_model", {24, 24, client.right - 24, 54}, kTextColor, titleFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(
        dc,
        L"dialog.choose_the_recognition_model_larger_models_are_more_accu",
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
        std::wstring selectedText = L"dialog.selected" + model.name + L" · " + model.tags;
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

    DrawTextBlock(dc, L"dialog.choose_vot_helper", {24, 24, client.right - 24, 54}, kTextColor, titleFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(
        dc,
        L"dialog.several_vot_helper_exe_files_were_found_in_the_selected",
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
        const std::wstring selectedText = L"dialog.selected" + state->votExecutableCandidates[static_cast<size_t>(selected)].wstring();
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
    DrawTextBlock(dc, L"app.logs", titleRect, kTextColor, titleFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT subtitleRect = {24, 56, client.right - 24, 78};
    DrawTextBlock(
        dc,
        L"dialog.select_the_needed_lines_or_copy_the_whole_current_log",
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
    int rightReserve = 18,
    bool wrapSubtitle = false
) {
    DrawRoundedPanel(dc, rect, Gdiplus::Color(255, 35, 35, 38), Gdiplus::Color(255, 48, 48, 52), 8);
    RECT titleRect = {rect.left + kSettingsCardPadding, rect.top + 14, rect.right - rightReserve, rect.top + 38};
    DrawTextBlock(dc, title, titleRect, kTextColor, labelFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT subtitleRect = {
        rect.left + kSettingsCardPadding,
        rect.top + 40,
        rect.right - rightReserve,
        wrapSubtitle ? rect.bottom - 12 : rect.top + 64
    };
    DrawTextBlock(
        dc,
        subtitle,
        subtitleRect,
        kMutedTextColor,
        textFont,
        wrapSubtitle ? DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS : DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
    );
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

std::wstring WrapPathText(std::wstring text, size_t maxLine = 76) {
    size_t lineStart = 0;
    size_t lastSeparator = std::wstring::npos;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\\' || text[i] == L'/') {
            lastSeparator = i;
        }
        if (i - lineStart < maxLine) {
            continue;
        }
        const size_t breakAt = lastSeparator != std::wstring::npos && lastSeparator >= lineStart
            ? lastSeparator + 1
            : i;
        text.insert(text.begin() + static_cast<std::ptrdiff_t>(breakAt), L'\n');
        lineStart = breakAt + 1;
        lastSeparator = std::wstring::npos;
        ++i;
    }
    return text;
}

void DrawUtilityStatusWrapped(
    HDC dc,
    const std::wstring& label,
    const std::filesystem::path& path,
    const RECT& rect,
    HFONT textFont
) {
    const std::wstring text = label + L" " + (path.empty() ? L"-" : WrapPathText(path.wstring()));
    DrawTextBlock(dc, text, rect, kMutedTextColor, textFont, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
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
            L"app.settings",
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
        sectionTitle = L"dialog.transcription";
        sectionSubtitle = L"dialog.engine_subtitles_and_vot_subtitle_translation";
        break;
    case SettingsSection::Translation:
        sectionTitle = L"dialog.translation";
        sectionSubtitle = L"dialog.voice_over_target_language_and_ffmpeg_integration";
        break;
    case SettingsSection::Additional:
        sectionTitle = L"dialog.additional";
        sectionSubtitle = L"dialog.application_language_and_updates";
        break;
    case SettingsSection::Tools:
        sectionTitle = L"dialog.tools";
        sectionSubtitle = L"dialog.yt_dlp_ffmpeg_whisper_cpp_and_voice_over_translation_sta";
        break;
    case SettingsSection::About:
        sectionTitle = L"dialog.about";
        sectionSubtitle = L"dialog.application_version_and_updates";
        break;
    case SettingsSection::Downloads:
    default:
        sectionTitle = L"dialog.downloads";
        sectionSubtitle = L"dialog.quality_container_and_new_task_behavior";
        break;
    }
    DrawTextBlock(dc, sectionTitle, {content.left, content.top + 2, content.right, content.top + 36}, kTextColor, titleFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextBlock(dc, sectionSubtitle, {content.left, content.top + 36, content.right, content.top + 64}, kMutedTextColor, textFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    const bool ffmpegReady = IsFfmpegReady(state);
    if (state->settingsSection == SettingsSection::Downloads) {
        DrawSettingsCard(dc, SettingsStackCardRect(state, client.right, client.bottom, 0, kSettingsChoiceCardHeight), L"dialog.quality", L"dialog.default_quality_for_new_downloads", labelFont, textFont);
        DrawSettingsCard(dc, SettingsStackCardRect(state, client.right, client.bottom, 1, kSettingsChoiceCardHeight), L"dialog.container", L"dialog.final_file_container_without_changing_naming", labelFont, textFont);
        const RECT behaviorCard = SettingsDownloadsParallelCardRect(state, client.right, client.bottom);
        DrawSettingsCard(dc, behaviorCard, L"dialog.parallelism", L"dialog.how_many_tasks_can_download_at_the_same_time", labelFont, textFont);
        DrawTextBlock(
            dc,
            L"dialog.parallel_downloads",
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
    } else if (state->settingsSection == SettingsSection::Additional) {
        const RECT languageCard = SettingsStackCardRect(state, client.right, client.bottom, 0, kSettingsChoiceCardHeight);
        DrawSettingsCard(dc, languageCard, L"dialog.application_language", L"dialog.interface_language_applies_after_restart", labelFont, textFont);
        if (state->config && state->workingConfig.uiLanguage != state->config->uiLanguage) {
            DrawTextBlock(
                dc,
                L"dialog.language_will_apply_after_restart",
                {
                    languageCard.left + kSettingsCardPadding,
                    languageCard.top + kSettingsCardControlTop,
                    languageCard.right - kSettingsCardPadding - 280,
                    languageCard.top + kSettingsCardControlTop + 34
                },
                kMutedTextColor,
                textFont,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
            );
        }
        DrawSettingsCard(
            dc,
            SettingsCardBelow(languageCard, kSettingsChoiceCardHeight),
            L"dialog.update_checks",
            L"dialog.automatically_check_for_new_versions_on_startup",
            labelFont,
            textFont
        );
    } else if (state->settingsSection == SettingsSection::Transcription) {
        DrawSettingsCard(dc, SettingsTranscriptionEngineCardRect(state, client.right, client.bottom), L"dialog.engine", L"dialog.whisper_cpp_or_vot_helper_exe_for_txt_srt_creation", labelFont, textFont);
        DrawSettingsCard(
            dc,
            SettingsTranscriptionSubtitleCardRect(state, client.right, client.bottom),
            L"dialog.subtitles",
            ffmpegReady ? L"dialog.ffmpeg_modes_are_available" : L"dialog.ffmpeg_modes_are_visible_but_require_ffmpeg",
            labelFont,
            textFont
        );
        DrawSettingsCard(
            dc,
            SettingsTranscriptionLanguageCardRect(state, client.right, client.bottom),
            L"dialog.vot_subtitle_translation",
            L"dialog.used_only_with_the_vot_engine",
            labelFont,
            textFont
        );
        if (ShowTranscriptionToolsCard(state)) {
            DrawSettingsCard(
                dc,
                SettingsTranscriptionToolsCardRect(state, client.right, client.bottom),
                L"dialog.tools",
                MissingToolsText(state, true),
                labelFont,
                textFont,
                220,
                true
            );
        }
    } else if (state->settingsSection == SettingsSection::Translation) {
        DrawSettingsCard(
            dc,
            SettingsTranslationWorkflowCardRect(state, client.right, client.bottom),
            L"dialog.language_and_integration",
            ffmpegReady ? L"dialog.mp3_is_always_created_ffmpeg_can_embed_or_mix_the_voice" : L"dialog.mp3_is_always_created_video_modes_require_ffmpeg",
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
            L"dialog.original_volume_while_mixing",
            {translationCard.left + kSettingsCardPadding + 180, translationCard.top + 108, translationCard.right - kSettingsCardPadding, translationCard.top + 142},
            kMutedTextColor,
            textFont,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
        );
        if (ShowTranslationToolsCard(state)) {
            DrawSettingsCard(
                dc,
                SettingsTranslationToolsCardRect(state, client.right, client.bottom),
                L"dialog.tools",
                MissingToolsText(state, false),
                labelFont,
                textFont,
                220,
                true
            );
        }
    } else if (state->settingsSection == SettingsSection::Tools) {
        const int clipState = SaveDC(dc);
        IntersectClipRect(dc, content.left, content.top + kSettingsContentTop, content.right, content.bottom);

        const ToolInstallStatus ytDlpStatus = state->paths ? YtDlpManager(*state->paths).Status() : ToolInstallStatus{};
        RECT toolCard = SettingsToolCardRect(state, client.right, client.bottom, 0);
        DrawSettingsCard(dc, toolCard, L"yt-dlp", ytDlpStatus.installed ? L"dialog.main_downloader_found" : L"dialog.main_downloader_not_found", labelFont, textFont, 280);
        DrawToolStatusPill(dc, {toolCard.left + 18, toolCard.top + 68, toolCard.left + 112, toolCard.top + 92}, ytDlpStatus.installed ? L"dialog.ready" : L"dialog.no", ytDlpStatus.installed, smallFont);
        DrawToolStatusPill(dc, {toolCard.left + 122, toolCard.top + 68, toolCard.left + 246, toolCard.top + 92}, ytDlpStatus.version.empty() ? L"dialog.version" : ytDlpStatus.version, ytDlpStatus.installed, smallFont);
        if (state->ytDlpDetailsExpanded) {
            DrawUtilityStatusLine(dc, L"dialog.path", ytDlpStatus.executable, toolCard.left + 18, toolCard.top + 98, toolCard.right - 18, textFont);
        }

        const FfmpegStatus ffmpegStatus = state->paths
            ? FfmpegManager::Resolve(*state->paths, state->workingConfig)
            : FfmpegManager::ResolveUserPath(state->workingConfig.ffmpegPath);
        toolCard = SettingsToolCardRect(state, client.right, client.bottom, 1);
        DrawSettingsCard(dc, toolCard, L"FFmpeg", ffmpegStatus.available ? L"dialog.ready_for_containers_subtitles_and_audio_tracks" : L"dialog.not_found_or_path_unavailable", labelFont, textFont, 280);
        DrawToolStatusPill(dc, {toolCard.left + 18, toolCard.top + 68, toolCard.left + 112, toolCard.top + 92}, ffmpegStatus.available ? L"dialog.ready" : L"dialog.no", ffmpegStatus.available, smallFont);
        DrawToolStatusPill(dc, {toolCard.left + 122, toolCard.top + 68, toolCard.left + 246, toolCard.top + 92}, ffmpegStatus.version.empty() ? L"dialog.version" : ffmpegStatus.version, ffmpegStatus.available, smallFont);
        if (state->ffmpegDetailsExpanded) {
            DrawUtilityStatusLine(dc, ffmpegStatus.available ? L"dialog.path" : L"dialog.status", ffmpegStatus.ffmpegExe, toolCard.left + 18, toolCard.top + 98, toolCard.right - 18, textFont);
        }

        const ToolInstallStatus whisperStatus = state->paths ? WhisperManager::Resolve(*state->paths, state->workingConfig) : ToolInstallStatus{};
        std::filesystem::path modelPath = state->workingConfig.whisperModelPath;
        if (modelPath.empty() && state->paths) {
            modelPath = state->paths->localWhisperModelPath();
        }
        std::error_code modelEc;
        const bool modelReady = !modelPath.empty() && std::filesystem::is_regular_file(modelPath, modelEc);
        toolCard = SettingsToolCardRect(state, client.right, client.bottom, 2);
        DrawSettingsCard(dc, toolCard, L"Whisper.cpp", whisperStatus.installed ? L"dialog.whisper_cli_exe_found" : L"dialog.required_for_local_transcription", labelFont, textFont, 280);
        DrawToolStatusPill(dc, {toolCard.left + 18, toolCard.top + 68, toolCard.left + 112, toolCard.top + 92}, whisperStatus.installed ? L"dialog.ready" : L"dialog.no", whisperStatus.installed, smallFont);
        DrawToolStatusPill(dc, {toolCard.left + 122, toolCard.top + 68, toolCard.left + 246, toolCard.top + 92}, whisperStatus.version.empty() ? L"dialog.version" : whisperStatus.version, whisperStatus.installed, smallFont);
        DrawToolStatusPill(dc, {toolCard.left + 256, toolCard.top + 68, toolCard.left + 366, toolCard.top + 92}, modelReady ? L"dialog.model_2" : L"dialog.no_model", modelReady, smallFont);
        const bool cudaAvailable = IsWhisperCudaCandidateAvailable();
        DrawToolStatusPill(
            dc,
            {toolCard.left + 376, toolCard.top + 68, toolCard.left + 486, toolCard.top + 92},
            WhisperStatusPillText(state->workingConfig, whisperStatus, cudaAvailable),
            state->workingConfig.whisperBackend != WhisperBackend::Cuda ||
                (cudaAvailable && whisperStatus.whisperBackend == WhisperBackend::Cuda),
            smallFont
        );
        if (state->whisperDetailsExpanded) {
            DrawUtilityStatusWrapped(
                dc,
                L"whisper-cli.exe:",
                whisperStatus.executable,
                {toolCard.left + 18, toolCard.top + 98, toolCard.right - 18, toolCard.top + 150},
                textFont
            );
            DrawUtilityStatusWrapped(
                dc,
                L"dialog.model_3",
                modelReady ? modelPath : std::filesystem::path{},
                {toolCard.left + 18, toolCard.top + 154, toolCard.right - 18, toolCard.top + 206},
                textFont
            );
        }

        const VotExeStatus votStatus = state->paths
            ? VotExeManager::Resolve(*state->paths, state->workingConfig)
            : VotExeManager::ResolveUserPath(state->workingConfig.votExePath);
        toolCard = SettingsToolCardRect(state, client.right, client.bottom, 3);
        DrawSettingsCard(dc, toolCard, L"Voice Over Translation", votStatus.available ? L"dialog.vot_helper_exe_found" : (votStatus.message.empty() ? L"dialog.vot_helper_exe_not_found" : votStatus.message), labelFont, textFont, 280);
        DrawToolStatusPill(dc, {toolCard.left + 18, toolCard.top + 68, toolCard.left + 112, toolCard.top + 92}, votStatus.available ? L"dialog.ready" : L"dialog.no", votStatus.available, smallFont);
        DrawToolStatusPill(dc, {toolCard.left + 122, toolCard.top + 68, toolCard.left + 246, toolCard.top + 92}, votStatus.version.empty() ? L"dialog.version" : votStatus.version, votStatus.available, smallFont);
        if (state->votDetailsExpanded) {
            DrawUtilityStatusLine(dc, L"vot-helper.exe:", votStatus.executable, toolCard.left + 18, toolCard.top + 98, toolCard.right - 18, textFont);
        }
        RestoreDC(dc, clipState);
    } else {
        const RECT aboutCard = SettingsStackCardRect(state, client.right, client.bottom, 0, kSettingsAboutCardHeight);
        DrawSettingsCard(dc, aboutCard, L"YouTube Downloader", L"dialog.portable_win32_downloader_with_yt_dlp_ffmpeg_whisper_cpp", labelFont, textFont);
        DrawTextBlock(
            dc,
            L"dialog.version_2" YTD_APP_VERSION_WIDE,
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
        const std::wstring cancelText = state->cancelButtonText.empty() ? L"dialog.later" : state->cancelButtonText;
        const std::wstring primaryText = state->primaryButtonText.empty() ? L"dialog.install" : state->primaryButtonText;
        HWND laterButton = CreateDarkButton(state->window, state->instance, cancelText.c_str(), IdCancel, false, false);
        HWND primaryButton = CreateDarkButton(state->window, state->instance, primaryText.c_str(), IdOk, true, false);
        AddDialogTooltip(state, laterButton, L"dialog.closes_the_window_without_continuing");
        AddDialogTooltip(state, primaryButton, L"dialog.runs_the_main_action_in_this_window");
        return;
    }
    if (state->type == DialogType::About) {
        HWND updateButton = CreateDarkButton(state->window, state->instance, L"dialog.check_for_update", IdCheckUpdates, false, false);
        AddDialogTooltip(state, updateButton, L"dialog.checks_for_a_new_application_version");
    }
    HWND copyButton = CreateDarkButton(state->window, state->instance, L"dialog.copy", IdCopy, false, false);
    HWND okButton = CreateDarkButton(state->window, state->instance, L"OK", IdOk, true, false);
    AddDialogTooltip(state, copyButton, L"dialog.copies_this_window_text_to_the_clipboard");
    AddDialogTooltip(state, okButton, L"dialog.closes_the_window");
}

void CreateLogsControls(DialogState* state) {
    state->logView = CreateLogView(state->window, state->instance, state->message);
    HWND copyButton = CreateDarkButton(state->window, state->instance, L"dialog.copy_all", IdCopy, false, false);
    HWND okButton = CreateDarkButton(state->window, state->instance, L"app.close", IdOk, true, false);
    AddDialogTooltip(state, copyButton, L"dialog.copies_the_whole_current_log_to_the_clipboard");
    AddDialogTooltip(state, okButton, L"dialog.closes_the_log_window");
}

void CreateFfmpegControls(DialogState* state) {
    HWND installButton = CreateDarkButton(state->window, state->instance, L"dialog.install", IdInstall, true, false);
    HWND folderButton = CreateDarkButton(state->window, state->instance, L"dialog.choose_folder", IdChooseFolder, false, false);
    HWND skipButton = CreateDarkButton(state->window, state->instance, L"dialog.skip", IdSkip, false, false);
    AddDialogTooltip(state, installButton, L"dialog.downloads_and_configures_local_ffmpeg_for_merging_video");
    AddDialogTooltip(state, skipButton, L"dialog.closes_the_window_without_configuring_ffmpeg");
    if (folderButton) {
        AddDialogTooltip(state, folderButton, L"dialog.choose_the_folder_containing_ffmpeg_exe_or_the_parent_fo");
    }
}

void InvalidateProgressContent(HWND window) {
    RECT client = {};
    GetClientRect(window, &client);
    RECT progressContent = {20, 68, client.right - 20, 154};
    InvalidateRect(window, &progressContent, FALSE);
}

void CreateWhisperControls(DialogState* state) {
    HWND installButton = CreateDarkButton(state->window, state->instance, L"dialog.install", IdInstall, true, false);
    HWND modelButton = CreateDarkButton(state->window, state->instance, L"dialog.choose_model", IdWhisperDownloadModel, false, false);
    HWND folderButton = CreateDarkButton(state->window, state->instance, L"dialog.choose_folder", IdChooseFolder, false, false);
    HWND skipButton = CreateDarkButton(state->window, state->instance, L"app.close", IdSkip, false, false);
    AddDialogTooltip(state, installButton, L"dialog.downloads_and_configures_whisper_cpp_gpu_version_when_av");
    AddDialogTooltip(state, modelButton, L"dialog.opens_whisper_model_selection_download_the_selected_mode");
    AddDialogTooltip(state, folderButton, L"dialog.choose_the_folder_containing_whisper_cli_exe");
    AddDialogTooltip(state, skipButton, L"dialog.closes_the_window_without_changes");
}

void CreateVotControls(DialogState* state) {
    HWND installButton = CreateDarkButton(state->window, state->instance, L"dialog.install", IdInstall, true, false);
    HWND folderButton = CreateDarkButton(state->window, state->instance, L"dialog.choose_folder", IdChooseFolder, false, false);
    HWND skipButton = CreateDarkButton(state->window, state->instance, L"app.close", IdSkip, false, false);
    AddDialogTooltip(state, installButton, L"dialog.downloads_and_configures_vot_helper_exe");
    AddDialogTooltip(state, folderButton, L"dialog.choose_the_folder_containing_vot_helper_exe");
    AddDialogTooltip(state, skipButton, L"dialog.closes_the_window_without_changes");
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
    HWND downloadButton = CreateDarkButton(state->window, state->instance, L"dialog.download_selected", IdWhisperModelDownloadSelected, true, false);
    HWND folderButton = CreateDarkButton(state->window, state->instance, L"dialog.choose_folder_2", IdWhisperModelChooseFolder, false, false);
    HWND cancelButton = CreateDarkButton(state->window, state->instance, L"dialog.cancel", IdCancel, false, false);
    AddDialogTooltip(state, downloadButton, L"dialog.downloads_the_selected_whisper_model");
    AddDialogTooltip(state, folderButton, L"dialog.choose_the_folder_with_already_downloaded_whisper_models");
    AddDialogTooltip(state, cancelButton, L"dialog.closes_the_window_without_downloading");
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
    HWND selectButton = CreateDarkButton(state->window, state->instance, L"dialog.select", IdVotCandidateSelect, true, false);
    HWND cancelButton = CreateDarkButton(state->window, state->instance, L"dialog.cancel", IdCancel, false, false);
    AddDialogTooltip(state, selectButton, L"dialog.use_the_selected_vot_helper_exe");
    AddDialogTooltip(state, cancelButton, L"dialog.closes_the_window_without_choosing");
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
            config->ffmpegVersion = status.version;
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
                state->progressError = L"dialog.unknown_ffmpeg_installation_error";
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
            config->whisperVersion = status.version;
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
                config->whisperVersion = cpuStatus.version;
                config->whisperBackend = WhisperBackend::Cpu;
                {
                    std::lock_guard lock(state->progressMutex);
                    state->progressSuccessMessage =
                        L"dialog.cuda_whisper_is_installed_but_failed_the_launch_check_sw";
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
                state->progressError = L"dialog.unknown_whisper_cpp_installation_error";
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
                state->progressError = L"dialog.unknown_whisper_model_download_error";
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
            config->votExeVersion = status.version;
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
                state->progressError = L"dialog.unknown_vot_helper_installation_error";
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
                        state->pendingProgress = ProgressUpdate{downloaded, total, L"dialog.downloading_update"};
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
                state->progressError = L"dialog.unknown_application_update_error";
            }
            PostMessageW(window, kProgressDoneMessage, FALSE, 0);
        }
    });
}

void CreateProgressControls(DialogState* state) {
    HWND cancelButton = CreateDarkButton(state->window, state->instance, L"dialog.cancel", IdCancel, false, false);
    AddDialogTooltip(state, cancelButton, L"dialog.cancels_the_current_operation");
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
    HWND downloadsNav = CreateDarkButton(state->window, state->instance, L"dialog.downloads", IdSettingsNavDownloads, true, false);
    HWND transcriptionNav = CreateDarkButton(state->window, state->instance, L"dialog.transcription", IdSettingsNavTranscription, false, false);
    HWND translationNav = CreateDarkButton(state->window, state->instance, L"dialog.translation", IdSettingsNavTranslation, false, false);
    HWND additionalNav = CreateDarkButton(state->window, state->instance, L"dialog.additional", IdSettingsNavAdditional, false, false);
    HWND toolsNav = CreateDarkButton(state->window, state->instance, L"dialog.tools", IdSettingsNavTools, false, false);
    HWND aboutNav = CreateDarkButton(state->window, state->instance, L"dialog.about", IdSettingsNavAbout, false, false);

    const std::array<std::pair<int, const wchar_t*>, 6> qualityButtons = {{
        {101, L"app.audio"},
        {102, L"360p"},
        {103, L"480p"},
        {104, L"720p"},
        {105, L"1080p"},
        {106, L"dialog.max"}
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
        AddDialogTooltip(state, button, L"dialog.selects_the_quality_used_for_new_tasks");
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
        AddDialogTooltip(state, button, L"dialog.selects_the_final_file_container_for_new_tasks");
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
    HWND subtitleOffButton = CreateDarkButton(state->window, state->instance, L"dialog.off", IdSubtitleModeOff, state->workingConfig.subtitleFfmpegMode == SubtitleFfmpegMode::Off);
    HWND subtitleTrackButton = CreateDarkButton(state->window, state->instance, SubtitleFfmpegModeDisplayText(SubtitleFfmpegMode::SubtitleTrack).c_str(), IdSubtitleModeTrack, state->workingConfig.subtitleFfmpegMode == SubtitleFfmpegMode::SubtitleTrack);
    HWND subtitleBurnButton = CreateDarkButton(state->window, state->instance, SubtitleFfmpegModeDisplayText(SubtitleFfmpegMode::BurnIn).c_str(), IdSubtitleModeBurn, state->workingConfig.subtitleFfmpegMode == SubtitleFfmpegMode::BurnIn);
    HWND voiceLanguageEdit = CreateDarkButton(
        state->window,
        state->instance,
        SettingsLanguageButtonText(state->workingConfig.voiceOverLanguage.empty() ? L"ru" : state->workingConfig.voiceOverLanguage).c_str(),
        IdVoiceLanguageEdit,
        false
    );
    HWND voiceOffButton = CreateDarkButton(state->window, state->instance, L"dialog.off", IdVoiceModeOff, state->workingConfig.voiceOverFfmpegMode == VoiceOverFfmpegMode::Off);
    HWND voiceTrackButton = CreateDarkButton(state->window, state->instance, VoiceOverFfmpegModeDisplayText(VoiceOverFfmpegMode::AudioTrack).c_str(), IdVoiceModeTrack, state->workingConfig.voiceOverFfmpegMode == VoiceOverFfmpegMode::AudioTrack);
    HWND voiceMixButton = CreateDarkButton(state->window, state->instance, VoiceOverFfmpegModeDisplayText(VoiceOverFfmpegMode::Mix).c_str(), IdVoiceModeMix, state->workingConfig.voiceOverFfmpegMode == VoiceOverFfmpegMode::Mix);
    HWND voiceVolumeMinusButton = CreateDarkButton(state->window, state->instance, L"-", IdVoiceVolumeMinus, false);
    HWND voiceVolumePlusButton = CreateDarkButton(state->window, state->instance, L"+", IdVoiceVolumePlus, false);
    HWND transcriptionToolsButton = CreateDarkButton(state->window, state->instance, L"dialog.open_tools", IdTranscriptionOpenTools, false);
    HWND translationToolsButton = CreateDarkButton(state->window, state->instance, L"dialog.open_tools", IdTranslationOpenTools, false);
    HWND ytDlpDetailsButton = CreateDarkButton(state->window, state->instance, L"dialog.details", IdYtDlpDetails, false);
    HWND ffmpegDetailsButton = CreateDarkButton(state->window, state->instance, L"dialog.details", IdFfmpegDetails, false);
    HWND whisperDetailsButton = CreateDarkButton(state->window, state->instance, L"dialog.details", IdWhisperDetails, false);
    HWND votDetailsButton = CreateDarkButton(state->window, state->instance, L"dialog.details", IdVotDetails, false);
    HWND autoUpdateButton = CreateDarkButton(
        state->window,
        state->instance,
        state->workingConfig.autoUpdateApp ? L"dialog.auto_check_on" : L"dialog.auto_check_off",
        IdAutoUpdate,
        state->workingConfig.autoUpdateApp
    );
    HWND uiLanguageButton = CreateDarkButton(
        state->window,
        state->instance,
        InterfaceLanguageButtonText(state).c_str(),
        IdUiLanguage,
        false
    );
    HWND minusButton = CreateDarkButton(state->window, state->instance, L"-", IdParallelMinus, false);
    HWND plusButton = CreateDarkButton(state->window, state->instance, L"+", IdParallelPlus, false);
    HWND checkUpdatesButton = CreateDarkButton(state->window, state->instance, L"dialog.check_for_updates", IdCheckUpdates, false);
    HWND cancelButton = CreateDarkButton(state->window, state->instance, L"dialog.cancel", IdCancel, false, false);
    HWND saveButton = CreateDarkButton(state->window, state->instance, L"dialog.save", IdOk, true, false);
    AddDialogTooltip(state, collapseButton, L"dialog.collapses_or_expands_the_side_navigation");
    AddDialogTooltip(state, downloadsNav, L"dialog.opens_download_settings");
    AddDialogTooltip(state, transcriptionNav, L"dialog.opens_transcription_settings");
    AddDialogTooltip(state, translationNav, L"dialog.opens_translation_settings");
    AddDialogTooltip(state, additionalNav, L"dialog.opens_additional_application_settings");
    AddDialogTooltip(state, toolsNav, L"dialog.opens_tool_status_and_setup");
    AddDialogTooltip(state, aboutNav, L"dialog.opens_application_information");
    AddDialogTooltip(state, uiLanguageButton, L"dialog.selects_the_interface_language_after_application_restart");
    AddDialogTooltip(state, ffmpegButton, L"dialog.opens_ffmpeg_setup_and_the_existing_installation_flow");
    AddDialogTooltip(state, transcriptionWhisperButton, L"dialog.use_whisper_cli_exe_for_transcription");
    AddDialogTooltip(state, transcriptionVotButton, L"dialog.use_vot_helper_exe_subtitles_to_get_srt_txt");
    AddDialogTooltip(state, votSubtitleLanguageEdit, L"dialog.target_language_for_vot_subtitles");
    AddDialogTooltip(state, chooseWhisperButton, L"dialog.opens_whisper_cpp_model_and_folder_setup");
    AddDialogTooltip(state, chooseVotButton, L"dialog.opens_vot_helper_exe_setup");
    AddDialogTooltip(state, subtitleOffButton, L"dialog.save_txt_srt_next_to_the_video_without_changing_the_vide");
    const std::wstring subtitleTrackTooltip = FfmpegGatedOptionTooltip(L"dialog.add_srt_as_a_separate_subtitle_track");
    const std::wstring subtitleBurnTooltip = FfmpegGatedOptionTooltip(L"dialog.burn_subtitles_into_the_video_image");
    AddDialogTooltip(state, subtitleTrackButton, subtitleTrackTooltip);
    AddDialogTooltip(state, subtitleBurnButton, subtitleBurnTooltip);
    AddDialogTooltip(state, voiceLanguageEdit, L"dialog.target_language_for_vot_translation");
    AddDialogTooltip(state, voiceOffButton, L"dialog.save_translation_as_a_separate_mp3_next_to_the_video");
    const std::wstring voiceTrackTooltip = FfmpegGatedOptionTooltip(L"dialog.add_translation_as_a_separate_audio_track");
    const std::wstring voiceMixTooltip = FfmpegGatedOptionTooltip(L"dialog.mix_translation_with_the_original_audio_track");
    AddDialogTooltip(state, voiceTrackButton, voiceTrackTooltip);
    AddDialogTooltip(state, voiceMixButton, voiceMixTooltip);
    AddDialogTooltip(state, voiceVolumeMinusButton, L"dialog.decreases_the_original_track_volume_while_mixing");
    AddDialogTooltip(state, voiceVolumePlusButton, L"dialog.increases_the_original_track_volume_while_mixing");
    AddDialogTooltip(state, transcriptionToolsButton, L"dialog.goes_to_tool_selection_and_installation");
    AddDialogTooltip(state, translationToolsButton, L"dialog.goes_to_tool_selection_and_installation");
    AddDialogTooltip(state, ytDlpDetailsButton, L"dialog.shows_or_hides_the_yt_dlp_path");
    AddDialogTooltip(state, ffmpegDetailsButton, L"dialog.shows_or_hides_the_ffmpeg_path");
    AddDialogTooltip(state, whisperDetailsButton, L"dialog.shows_or_hides_whisper_cpp_and_model_paths");
    AddDialogTooltip(state, votDetailsButton, L"dialog.shows_or_hides_the_vot_helper_exe_path");
    AddDialogTooltip(state, autoUpdateButton, L"dialog.enables_or_disables_automatic_application_update_checks");
    AddDialogTooltip(state, minusButton, L"dialog.decreases_the_number_of_parallel_downloads");
    AddDialogTooltip(state, plusButton, L"dialog.increases_the_number_of_parallel_downloads");
    AddDialogTooltip(state, checkUpdatesButton, L"dialog.checks_for_a_new_application_version");
    AddDialogTooltip(state, cancelButton, L"dialog.closes_settings_without_saving_changes");
    AddDialogTooltip(state, saveButton, L"dialog.saves_the_selected_settings");
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
    case SettingsLanguageTarget::Interface:
        for (const UiLanguage& language : state->uiLanguages) {
            menuState->values.push_back(language.id);
            menuState->labels.push_back(language.name);
        }
        break;
    default:
        menuState->values = VotSubtitleLanguageOptions();
        break;
    }
    if (menuState->labels.empty()) {
        menuState->labels = menuState->values;
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

    case WM_MOUSEWHEEL:
        if (state && state->type == DialogType::Whisper) {
            RECT client = {};
            GetClientRect(window, &client);
            const int maxScroll = WhisperStatusMaxScroll(state, client);
            if (maxScroll > 0) {
                const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                const int nextScroll = std::clamp(
                    state->whisperStatusScrollY - ((delta / WHEEL_DELTA) * 44),
                    0,
                    maxScroll
                );
                if (nextScroll != state->whisperStatusScrollY) {
                    state->whisperStatusScrollY = nextScroll;
                    RECT dirty = PaddedRect(WhisperPathViewportRect(WhisperStatusCardRect(client)), 2);
                    InvalidateRect(window, &dirty, FALSE);
                }
                return 0;
            }
        }
        if (state && state->type == DialogType::Settings && state->settingsSection == SettingsSection::Tools) {
            RECT client = {};
            GetClientRect(window, &client);
            const int maxScroll = SettingsMaxScroll(state, client.right, client.bottom);
            if (maxScroll > 0) {
                const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                const int nextScroll = std::clamp(
                    state->settingsScrollY - ((delta / WHEEL_DELTA) * 44),
                    0,
                    maxScroll
                );
                if (nextScroll != state->settingsScrollY) {
                    state->settingsScrollY = nextScroll;
                    LayoutDialog(state, client.right, client.bottom);
                    InvalidateRect(window, nullptr, FALSE);
                }
                return 0;
            }
        }
        break;

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
                if (commandId == IdWhisperModelChooseFolder && state->config && !state->whisperModels.empty()) {
                    const std::optional<std::filesystem::path> selectedFolder = PickFolder(window, L"dialog.choose_the_folder_with_whisper_models");
                    if (!selectedFolder) {
                        return 0;
                    }
                    const int selectedIndex = std::clamp(
                        state->selectedWhisperModelIndex,
                        0,
                        static_cast<int>(state->whisperModels.size()) - 1
                    );
                    const WhisperModelInfo selected = state->whisperModels[static_cast<size_t>(selectedIndex)];
                    const std::optional<std::filesystem::path> modelPath =
                        FindWhisperModelNear(*selectedFolder, {}, selected, state->whisperModels);
                    if (!modelPath) {
                        ShowErrorDialog(
                            window,
                            state->instance,
                            L"dialog.whisper_model_not_found",
                            L"dialog.no_known_whisper_models_were_found_in_the_selected_folde"
                        );
                        return 0;
                    }
                    state->config->whisperModelPath = *modelPath;
                    if (state->savedResult) {
                        *state->savedResult = true;
                    }
                    DestroyWindow(window);
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
                if (!IsFfmpegReady(state)) {
                    return 0;
                }
                state->workingConfig.container = L"mp4";
                RefreshSettingsButtons(state);
                return 0;
            case 113:
                if (!IsFfmpegReady(state)) {
                    return 0;
                }
                state->workingConfig.container = L"mkv";
                RefreshSettingsButtons(state);
                return 0;
            case 114:
                if (!IsFfmpegReady(state)) {
                    return 0;
                }
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
            case IdSettingsNavAdditional:
                state->settingsSection = SettingsSection::Additional;
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
            case IdUiLanguage:
                ShowSettingsLanguageMenu(state, GetDlgItem(window, IdUiLanguage), SettingsLanguageTarget::Interface);
                return 0;
            case IdParallelMinus:
                state->workingConfig.maxParallelDownloads = std::clamp(state->workingConfig.maxParallelDownloads - 1, 1, 10);
                RefreshSettingsButtons(state);
                InvalidateRect(state->window, nullptr, FALSE);
                return 0;
            case IdParallelPlus:
                state->workingConfig.maxParallelDownloads = std::clamp(state->workingConfig.maxParallelDownloads + 1, 1, 10);
                RefreshSettingsButtons(state);
                InvalidateRect(state->window, nullptr, FALSE);
                return 0;
            case IdTranscriptionWhisper:
                if (!IsFfmpegReady(state) || !IsWhisperReady(state)) {
                    return 0;
                }
                state->workingConfig.transcriptionEngine = TranscriptionEngine::Whisper;
                RefreshSettingsButtons(state);
                return 0;
            case IdTranscriptionVot:
                if (!IsVotReady(state)) {
                    return 0;
                }
                state->workingConfig.transcriptionEngine = TranscriptionEngine::Vot;
                RefreshSettingsButtons(state);
                return 0;
            case IdVotSubtitleLanguageEdit:
                if (!IsVotReady(state)) {
                    return 0;
                }
                ShowSettingsLanguageMenu(state, GetDlgItem(window, IdVotSubtitleLanguageEdit), SettingsLanguageTarget::VotSubtitle);
                return 0;
            case IdVoiceLanguageEdit:
                if (!IsVotReady(state)) {
                    return 0;
                }
                ShowSettingsLanguageMenu(state, GetDlgItem(window, IdVoiceLanguageEdit), SettingsLanguageTarget::VoiceOver);
                return 0;
            case IdVoiceModeOff:
                if (!IsVotReady(state)) {
                    return 0;
                }
                state->workingConfig.voiceOverFfmpegMode = VoiceOverFfmpegMode::Off;
                RefreshSettingsButtons(state);
                return 0;
            case IdVoiceModeTrack:
                if (!IsVotReady(state) || !IsFfmpegReady(state)) {
                    return 0;
                }
                state->workingConfig.voiceOverFfmpegMode = VoiceOverFfmpegMode::AudioTrack;
                RefreshSettingsButtons(state);
                return 0;
            case IdVoiceModeMix:
                if (!IsVotReady(state) || !IsFfmpegReady(state)) {
                    return 0;
                }
                state->workingConfig.voiceOverFfmpegMode = VoiceOverFfmpegMode::Mix;
                RefreshSettingsButtons(state);
                return 0;
            case IdVoiceVolumeMinus:
                if (!IsVotReady(state) || !IsFfmpegReady(state) || state->workingConfig.voiceOverFfmpegMode != VoiceOverFfmpegMode::Mix) {
                    return 0;
                }
                state->workingConfig.voiceOverOriginalVolumePercent = std::clamp(
                    state->workingConfig.voiceOverOriginalVolumePercent - 5,
                    0,
                    100
                );
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            case IdVoiceVolumePlus:
                if (!IsVotReady(state) || !IsFfmpegReady(state) || state->workingConfig.voiceOverFfmpegMode != VoiceOverFfmpegMode::Mix) {
                    return 0;
                }
                state->workingConfig.voiceOverOriginalVolumePercent = std::clamp(
                    state->workingConfig.voiceOverOriginalVolumePercent + 5,
                    0,
                    100
                );
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            case IdSubtitleModeOff:
                if (state->workingConfig.transcriptionEngine == TranscriptionEngine::Whisper) {
                    if (!IsFfmpegReady(state) || !IsWhisperReady(state)) {
                        return 0;
                    }
                } else if (!IsVotReady(state)) {
                    return 0;
                }
                state->workingConfig.subtitleFfmpegMode = SubtitleFfmpegMode::Off;
                RefreshSettingsButtons(state);
                return 0;
            case IdSubtitleModeTrack:
                if (!IsFfmpegReady(state) ||
                    (state->workingConfig.transcriptionEngine == TranscriptionEngine::Whisper
                        ? !IsWhisperReady(state)
                        : !IsVotReady(state))) {
                    return 0;
                }
                state->workingConfig.subtitleFfmpegMode = SubtitleFfmpegMode::SubtitleTrack;
                RefreshSettingsButtons(state);
                return 0;
            case IdSubtitleModeBurn:
                if (!IsFfmpegReady(state) ||
                    (state->workingConfig.transcriptionEngine == TranscriptionEngine::Whisper
                        ? !IsWhisperReady(state)
                        : !IsVotReady(state))) {
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
                    ShowErrorDialog(window, state->instance, L"dialog.updates", L"dialog.application_path_is_unavailable");
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
                CloseDialogWindow(window);
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
                    const std::optional<std::filesystem::path> selected = PickFolder(window, L"dialog.choose_the_whisper_folder");
                    if (selected && ApplySelectedWhisperPath(window, state->instance, *state->config, *selected)) {
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                        DestroyWindow(window);
                    }
                    return 0;
                }
                if (state->type == DialogType::Vot && state->config) {
                    const std::optional<std::filesystem::path> selected = PickFolder(window, L"dialog.choose_the_vot_helper_folder");
                    if (selected && ApplySelectedVotPath(window, state->instance, *state->config, *selected)) {
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                        DestroyWindow(window);
                    }
                    return 0;
                }
                CloseDialogWindow(window);
                return 0;
            case IdSkip:
                CloseDialogWindow(window);
                return 0;
            case IdCancel:
                if (state->type == DialogType::Progress && !state->progressDone) {
                    if (state->cancelEvent) {
                        SetEvent(state->cancelEvent);
                    }
                    state->message = L"dialog.canceling";
                    InvalidateProgressContent(window);
                    return 0;
                }
                CloseDialogWindow(window);
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
                    state->message = L"dialog.update_downloaded_the_application_will_close_and_restart";
                } else if (state->progressMode == ProgressMode::WhisperInstall) {
                    const bool modelReady = IsRegularFile(ResolveDialogWhisperModelPath(state));
                    state->message = modelReady
                        ? L"dialog.whisper_cpp_installed"
                        : L"dialog.whisper_cpp_installed_now_download_a_model_the_model_win";
                } else if (state->progressMode == ProgressMode::WhisperModelDownload) {
                    state->message = L"dialog.whisper_model_downloaded";
                } else if (state->progressMode == ProgressMode::VotInstall) {
                    state->message = L"dialog.vot_helper_installed";
                } else {
                    state->message = L"dialog.ffmpeg_installed";
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
                        ? L"dialog.failed_to_update_the_application"
                        : (state->progressMode == ProgressMode::WhisperModelDownload
                            ? L"dialog.failed_to_download_the_whisper_model"
                            : (state->progressMode == ProgressMode::VotInstall
                                ? L"dialog.failed_to_install_vot_helper"
                                : (state->progressMode == ProgressMode::WhisperInstall
                                    ? L"dialog.failed_to_install_whisper_cpp"
                                    : L"dialog.failed_to_install_ffmpeg"))));
            }
            const std::wstring doneButtonText =
                state->progressSuccess &&
                state->progressMode == ProgressMode::WhisperInstall &&
                !IsRegularFile(ResolveDialogWhisperModelPath(state))
                    ? L"dialog.models"
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
            state->message = L"dialog.canceling";
            InvalidateProgressContent(window);
            return 0;
        }
        CloseDialogWindow(window);
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
    const std::wstring translated = Localization::UiText(text);
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(dc, font));
    RECT measure = {0, 0, width, 1};
    DrawTextW(dc, translated.c_str(), -1, &measure, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
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
                {1, L"dialog.copy_2", false}
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
    } else if (menuState->target == SettingsLanguageTarget::VoiceOver) {
        ownerState->workingConfig.voiceOverLanguage = value;
    } else {
        ownerState->workingConfig.uiLanguage = value.empty() ? L"ru" : value;
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
                items.reserve(state->labels.size());
                for (size_t index = 0; index < state->labels.size(); ++index) {
                    items.push_back({static_cast<UINT>(index + 1), Localization::UiText(state->labels[index]), false});
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
    state->subtitle = L"dialog.a_tool_required_for_this_action_is_missing";
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
    state->subtitle = L"dialog.confirm_overwrite_before_starting_the_operation";
    state->message = BuildAffectedFilesMessage(affectedFiles);
    state->primaryButtonText = L"dialog.overwrite";
    state->cancelButtonText = L"dialog.cancel";
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
    state->title = L"app.logs";
    state->message = logText.empty() ? L"dialog.log_is_empty" : logText;
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
    state->title = L"app.settings";
    state->paths = paths.root().empty() ? nullptr : &paths;
    state->config = &config;
    state->workingConfig = config;
    state->uiLanguages = Localization::AvailableLanguages(paths);
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
    state->title = L"dialog.about";
    state->message =
        L"YouTube Downloader\n\n"
        L"dialog.portable_win32_video_downloader_for_youtube"
        L"dialog.version_2" YTD_APP_VERSION_WIDE;
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
    ShowModal(state, 680, 400);
    return saved;
}

bool ShowWhisperModelDialog(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config) {
    auto* state = new DialogState{};
    state->type = DialogType::WhisperModel;
    state->instance = instance;
    state->owner = owner;
    state->paths = paths.root().empty() ? nullptr : &paths;
    state->config = &config;
    state->title = L"dialog.whisper_model";
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
    state->title = L"dialog.installing_ffmpeg";
    state->message = L"dialog.preparing";
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
    state->title = L"dialog.installing_whisper_cpp";
    state->message = L"dialog.preparing";
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
    state->title = L"dialog.downloading_whisper_model";
    state->message = L"dialog.preparing";
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
    state->title = L"dialog.installing_vot_helper";
    state->message = L"dialog.preparing";
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
    state->title = L"dialog.application_update";
    state->message = L"dialog.preparing";
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
            ShowInfoDialog(owner, instance, L"dialog.updates", L"dialog.youtubedownloader_exe_was_not_found_in_the_latest_releas");
        }
        return false;
    }

    if (!ShouldInstallAppUpdate(release)) {
        if (notifyWhenCurrent) {
            ShowInfoDialog(
                owner,
                instance,
                L"dialog.updates",
                L"dialog.current_version_installed" YTD_APP_VERSION_WIDE
            );
        }
        return false;
    }

    if (!ShowConfirmationDialog(
            owner,
            instance,
            L"dialog.update_available",
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
                L"dialog.update_check_failed",
                LocalizedToolErrorText(ex.what())
            );
        }
    } catch (...) {
        if (notifyWhenCurrent) {
            ShowErrorDialog(owner, instance, L"dialog.update_check_failed", L"dialog.unknown_error");
        }
    }
    return false;
}
