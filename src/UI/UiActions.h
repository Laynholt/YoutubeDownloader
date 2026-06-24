#pragma once

#include <windows.h>

#include <string>
#include <vector>

enum class DownloadAttemptAction { Enqueue, ShowYtDlpNotReady, ShowPreviewLoading };

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
