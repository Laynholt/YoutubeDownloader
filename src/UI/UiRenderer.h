#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <string>
#include <vector>

namespace UiRenderer {
struct PopupMenuItem {
    UINT id = 0;
    std::wstring text;
    bool separator = false;
    bool enabled = true;
};

void DrawBackground(HDC dc, const RECT& rect);
void DrawPanel(HDC dc, const RECT& rect);
void DrawPreviewCard(HDC dc, const RECT& rect);
void DrawInputFrame(HDC dc, const RECT& rect);
void DrawProgressBar(HDC dc, const RECT& rect, double percent);
void DrawIndeterminateProgressBar(HDC dc, const RECT& rect, double phase);
void DrawButton(
    HDC dc,
    const RECT& rect,
    const wchar_t* text,
    bool primary,
    bool pressed,
    bool hot,
    bool onPanel,
    bool enabled = true,
    bool onCard = false
);
void DrawPopupMenu(HDC dc, const RECT& rect, const std::vector<PopupMenuItem>& items, UINT hoveredItemId);
}
