#include "DocumentTabIndicators.h"
#include "resource.h"

#include <commctrl.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace npphistory
{
namespace
{
constexpr UINT relayoutMessage = WM_APP + 274;
constexpr UINT_PTR subclassId = 0x4E484449;
struct Decoration
{
    std::wstring caption;
    std::wstring nativeCaption;
    DocumentTabDecorationMetrics metrics;
    HFONT font = nullptr;
    UINT dpi = 96;
    int height = 0;
};
struct TabState
{
    HWND tabs = nullptr;
    HINSTANCE resources = nullptr;
    std::unordered_map<LPARAM, unsigned> masks;
    std::unordered_map<LPARAM, Decoration> decorations;
    bool internalWrite = false;
    bool queued = false;
};
std::unordered_map<HWND, std::unique_ptr<TabState>> states;

int scaled(HWND window, int value)
{
    const UINT dpi = GetDpiForWindow(window);
    return MulDiv(value, dpi ? dpi : 96, 96);
}

int length(const RECT& rect, bool vertical)
{
    return vertical ? rect.bottom - rect.top : rect.right - rect.left;
}

int thickness(const RECT& rect, bool vertical)
{
    return vertical ? rect.right - rect.left : rect.bottom - rect.top;
}

LPARAM identity(HWND tabs, int index)
{
    TCITEMW item{};
    item.mask = TCIF_PARAM;
    return SendMessageW(tabs, TCM_GETITEMW, index, reinterpret_cast<LPARAM>(&item))
        ? item.lParam : 0;
}

std::wstring caption(HWND tabs, int index)
{
    std::vector<wchar_t> text(32768, 0);
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = text.data();
    item.cchTextMax = static_cast<int>(text.size());
    if (!SendMessageW(tabs, TCM_GETITEMW, index, reinterpret_cast<LPARAM>(&item)))
        return {};
    return text.data();
}

bool writeNativeCaption(TabState& state, int index, const std::wstring& text)
{
    // Only TEXT is changed. Never overwrite Notepad++'s Buffer pointer, image, or state.
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<wchar_t*>(text.c_str());
    state.internalWrite = true;
    const bool result = SendMessageW(state.tabs, TCM_SETITEMW, index,
        reinterpret_cast<LPARAM>(&item)) != 0;
    state.internalWrite = false;
    return result;
}

std::wstring displayCaption(const std::wstring& encoded)
{
    std::wstring result;
    for (std::size_t i = 0; i < encoded.size(); ++i)
    {
        if (encoded[i] == L'&')
        {
            if (i + 1 < encoded.size() && encoded[i + 1] == L'&')
                result += encoded[++i];
        }
        else result += encoded[i];
    }
    return result;
}

void layout(TabState& state)
{
    const HWND tabs = state.tabs;
    const HDC dc = GetDC(tabs);
    if (!dc) return;
    const HFONT font = reinterpret_cast<HFONT>(SendMessageW(tabs, WM_GETFONT, 0, 0));
    const auto oldFont = font ? SelectObject(dc, font) : nullptr;
    SIZE space{};
    GetTextExtentPoint32W(dc, L"\x00A0", 1, &space);
    const int unit = (std::max)(1L, space.cx);
    const int gap = scaled(tabs, 6);
    const int iconSize = scaled(tabs, 16);
    const int stride = iconSize + scaled(tabs, 4);
    const auto style = GetWindowLongPtrW(tabs, GWL_STYLE);
    const bool vertical = (style & TCS_VERTICAL) != 0;
    const bool supported = (style & TCS_FIXEDWIDTH) == 0;
    std::unordered_map<LPARAM, Decoration> next;
    const int count = TabCtrl_GetItemCount(tabs);
    for (int index = 0; index < count; ++index)
    {
        const LPARAM key = identity(tabs, index);
        if (!key) continue;
        const std::wstring label = caption(tabs, index); // canonical, never the spacer suffix
        const auto previous = state.decorations.find(key);
        const auto maskEntry = state.masks.find(key);
        const unsigned mask = supported && maskEntry != state.masks.end() ? maskEntry->second & 3U : 0;
        RECT current{};
        TabCtrl_GetItemRect(tabs, index, &current);
        if (previous != state.decorations.end() && previous->second.caption == label
            && previous->second.metrics.mask == mask && previous->second.font == font
            && previous->second.metrics.vertical == vertical
            && previous->second.dpi == GetDpiForWindow(tabs)
            && previous->second.height == thickness(current, vertical)
            && length(current, vertical) == previous->second.metrics.originalWidth + previous->second.metrics.reservedWidth)
        {
            next.emplace(key, previous->second);
            continue; // no resize/repaint cycle when nothing relevant has changed
        }
        if (previous != state.decorations.end() && previous->second.nativeCaption != label)
            writeNativeCaption(state, index, label);
        RECT original{};
        if (!TabCtrl_GetItemRect(tabs, index, &original)) continue;
        if (!mask) continue;
        const int icons = ((mask & 1U) ? 1 : 0) + ((mask & 2U) ? 1 : 0);
        Decoration entry;
        entry.font = font;
        entry.dpi = GetDpiForWindow(tabs);
        entry.height = thickness(original, vertical);
        entry.metrics.vertical = vertical;
        entry.caption = label;
        entry.metrics.originalWidth = length(original, vertical);
        SIZE textSize{};
        const std::wstring text = displayCaption(label);
        GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &textSize);
        int imageWidth = 0, imageHeight = 0;
        const auto images = TabCtrl_GetImageList(tabs);
        if (images) ImageList_GetIconSize(images, &imageWidth, &imageHeight);
        const int imageAcross = vertical ? imageWidth : imageHeight;
        const int imageAlong = vertical ? imageHeight : imageWidth;
        const int imageInset = images ? (std::max)(0, (thickness(original, vertical) - imageAcross + 1) / 2)
            + imageAlong : 0;
        SIZE ordinarySpace{};
        GetTextExtentPoint32W(dc, L" ", 1, &ordinarySpace);
        entry.metrics.textRight = imageInset + ordinarySpace.cx + textSize.cx;
        entry.metrics.iconsLeft = entry.metrics.textRight + gap;
        entry.metrics.iconsRight = entry.metrics.iconsLeft + icons * stride - scaled(tabs, 4);
        // Notepad++ exposes the item rectangle, not individual pin/close rectangles.
        // Preserve the *measured* native trailing area, including its existing gap,
        // instead of reserving another fixed allowance for buttons already accounted for.
        entry.metrics.nativeTrailingSpace = entry.metrics.originalWidth - entry.metrics.textRight;
        entry.metrics.spacerUnit = unit;
        const int extra = entry.metrics.iconsRight - entry.metrics.textRight;
        entry.nativeCaption = label + std::wstring((extra + unit - 1) / unit, L'\x00A0');
        if (!writeNativeCaption(state, index, entry.nativeCaption)) continue;
        RECT widened{};
        TabCtrl_GetItemRect(tabs, index, &widened);
        entry.metrics.reservedWidth = length(widened, vertical) - entry.metrics.originalWidth;
        entry.metrics.nativeTrailingStart = length(widened, vertical) - entry.metrics.nativeTrailingSpace;
        entry.metrics.mask = mask;
        if (entry.metrics.nativeTrailingSpace < 0
            || entry.metrics.iconsRight > entry.metrics.nativeTrailingStart
            || entry.metrics.nativeTrailingStart - entry.metrics.iconsRight >= unit)
        {
            // Unsupported/clamped native geometry: never fall back to painting over text/buttons.
            writeNativeCaption(state, index, label);
            continue;
        }
        next.emplace(key, std::move(entry));
    }
    state.decorations = std::move(next);
    if (oldFont) SelectObject(dc, oldFont);
    ReleaseDC(tabs, dc);
    InvalidateRect(tabs, nullptr, FALSE);
}

void queueLayout(TabState& state)
{
    if (!state.queued)
        state.queued = PostMessageW(state.tabs, relayoutMessage, 0, 0) != FALSE;
}

void paint(TabState& state, HDC supplied = nullptr)
{
    const HDC dc = supplied ? supplied : GetDC(state.tabs);
    if (!dc) return;
    const int saved = SaveDC(dc);
    RECT client{};
    GetClientRect(state.tabs, &client);
    // The scroll-arrow child owns its area, even while the final tab is partially visible.
    for (HWND child = GetWindow(state.tabs, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
    {
        if (!IsWindowVisible(child)) continue;
        RECT bounds{};
        GetWindowRect(child, &bounds);
        MapWindowPoints(nullptr, state.tabs, reinterpret_cast<POINT*>(&bounds), 2);
        ExcludeClipRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
    }
    IntersectClipRect(dc, client.left, client.top, client.right, client.bottom);
    const int size = scaled(state.tabs, 16);
    const int stride = size + scaled(state.tabs, 4);
    for (int index = 0; index < TabCtrl_GetItemCount(state.tabs); ++index)
    {
        const auto found = state.decorations.find(identity(state.tabs, index));
        if (found == state.decorations.end()) continue;
        RECT item{};
        if (!TabCtrl_GetItemRect(state.tabs, index, &item)) continue;
        const auto& geometry = found->second.metrics;
        if (((GetWindowLongPtrW(state.tabs, GWL_STYLE) & TCS_VERTICAL) != 0) != geometry.vertical)
            continue; // a queued orientation change must not paint stale-axis coordinates
        if (length(item, geometry.vertical) < geometry.originalWidth + geometry.reservedWidth) continue;
        int along = geometry.iconsLeft;
        for (unsigned bit : {1U, 2U})
        {
            if (!(geometry.mask & bit)) continue;
            const auto icon = static_cast<HICON>(LoadImageW(state.resources,
                MAKEINTRESOURCEW(bit == 1U ? IDI_AUTOSAVE_DISABLED : IDI_HISTORY_DISABLED),
                IMAGE_ICON, size, size, LR_SHARED));
            // Native vertical captions read from bottom to top. Keep artwork upright,
            // but place each icon along that same axis before the pin/close region.
            const int x = geometry.vertical ? item.left + (item.right - item.left - size) / 2
                : item.left + along;
            const int y = geometry.vertical ? item.bottom - along - size
                : item.top + (item.bottom - item.top - size) / 2;
            if (icon) DrawIconEx(dc, x, y, icon, size, size, 0, nullptr, DI_NORMAL);
            along += stride;
        }
    }
    RestoreDC(dc, saved);
    if (!supplied) ReleaseDC(state.tabs, dc);
}

LRESULT CALLBACK subclass(HWND tabs, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR data)
{
    auto& state = *reinterpret_cast<TabState*>(data);
    if (message == WM_NCDESTROY)
    {
        RemoveWindowSubclass(tabs, subclass, subclassId);
        states.erase(tabs);
        return DefSubclassProc(tabs, message, wParam, lParam);
    }
    if (state.internalWrite) return DefSubclassProc(tabs, message, wParam, lParam);
    if (message == relayoutMessage)
    {
        state.queued = false;
        layout(state);
        return 0;
    }
    if (message == TCM_SETITEMW || message == TCM_INSERTITEMW)
    {
        auto* item = reinterpret_cast<TCITEMW*>(lParam);
        if (item && (item->mask & TCIF_TEXT))
        {
            const LPARAM key = item->mask & TCIF_PARAM ? item->lParam
                : identity(tabs, static_cast<int>(wParam));
            // Host rename/reorder wins. Do not retain a former display-only spacer suffix.
            state.decorations.erase(key);
        }
        const LRESULT result = DefSubclassProc(tabs, message, wParam, lParam);
        queueLayout(state);
        return result;
    }
    const LRESULT result = DefSubclassProc(tabs, message, wParam, lParam);
    if (message == TCM_GETITEMW && result)
    {
        auto* item = reinterpret_cast<TCITEMW*>(lParam);
        if (item && (item->mask & TCIF_TEXT) && item->pszText && item->cchTextMax > 0)
        {
            const LPARAM key = item->mask & TCIF_PARAM ? item->lParam : identity(tabs, static_cast<int>(wParam));
            const auto found = state.decorations.find(key);
            if (found != state.decorations.end())
                wcsncpy_s(item->pszText, item->cchTextMax, found->second.caption.c_str(), _TRUNCATE);
        }
    }
    if (message == WM_PAINT) paint(state);
    if (message == WM_PRINTCLIENT) paint(state, reinterpret_cast<HDC>(wParam));
    if (message == WM_SETFONT || message == WM_DPICHANGED_AFTERPARENT || message == WM_STYLECHANGED
        || message == TCM_DELETEITEM || message == TCM_DELETEALLITEMS || message == TCM_SETIMAGELIST
        || message == TCM_SETPADDING)
        queueLayout(state);
    return result;
}
}

void updateDocumentTabDecorations(HWND tabs, const std::unordered_map<LPARAM, unsigned>& masks,
    HINSTANCE resources)
{
    auto& state = states[tabs];
    if (!state)
    {
        state = std::make_unique<TabState>();
        state->tabs = tabs;
        SetWindowSubclass(tabs, subclass, subclassId, reinterpret_cast<DWORD_PTR>(state.get()));
    }
    state->resources = resources;
    state->masks = masks;
    layout(*state);
}

void removeDocumentTabDecorations(HWND tabs)
{
    const auto found = states.find(tabs);
    if (found == states.end()) return;
    found->second->masks.clear();
    layout(*found->second);
    RemoveWindowSubclass(tabs, subclass, subclassId);
    states.erase(found);
}

bool documentTabDecorationMetrics(HWND tabs, int index, DocumentTabDecorationMetrics& metrics)
{
    const auto state = states.find(tabs);
    if (state == states.end()) return false;
    const auto entry = state->second->decorations.find(identity(tabs, index));
    if (entry == state->second->decorations.end()) return false;
    metrics = entry->second.metrics;
    return true;
}
}
