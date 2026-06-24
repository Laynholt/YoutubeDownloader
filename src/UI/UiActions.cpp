#include "UiActions.h"

#include <cstring>

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
