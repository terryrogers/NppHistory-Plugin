#include "TestHarness.h"
#include "DocumentTabIndicators.h"
#include <commctrl.h>

using namespace npphistory;
namespace
{
int width(HWND tabs, int index)
{
    RECT rect{};
    TabCtrl_GetItemRect(tabs, index, &rect);
    return (GetWindowLongPtrW(tabs, GWL_STYLE) & TCS_VERTICAL)
        ? rect.bottom - rect.top : rect.right - rect.left;
}
std::wstring text(HWND tabs, int index, LPARAM* key = nullptr)
{
    wchar_t value[512]{};
    TCITEMW item{};
    item.mask = TCIF_TEXT | TCIF_PARAM;
    item.pszText = value;
    item.cchTextMax = 512;
    SendMessageW(tabs, TCM_GETITEMW, index, reinterpret_cast<LPARAM>(&item));
    if (key) *key = item.lParam;
    return value;
}
void pump()
{
    MSG message{};
    for (int i = 0; i < 1000 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++i)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
}
void runDocumentTabTests(TestContext& context)
{
    INITCOMMONCONTROLSEX init{sizeof(init), ICC_TAB_CLASSES};
    InitCommonControlsEx(&init);
    for (bool vertical : {false, true})
    {
    const HWND parent = CreateWindowExW(0, L"STATIC", L"tab geometry test", WS_POPUP,
        0, 0, 1600, 1600, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    const HWND tabs = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | TCS_OWNERDRAWFIXED
        | (vertical ? TCS_VERTICAL | TCS_MULTILINE : 0),
        0, 0, 1600, 1600, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(tabs, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), FALSE);
    SendMessageW(tabs, TCM_SETPADDING, 0, MAKELPARAM(16, 0));
    for (int i = 0; i < 3; ++i)
    {
        TCITEMW item{};
        item.mask = TCIF_TEXT | TCIF_PARAM;
        item.pszText = const_cast<wchar_t*>(L"A && B.txt");
        item.lParam = i + 101;
        SendMessageW(tabs, TCM_INSERTITEMW, i, reinterpret_cast<LPARAM>(&item));
    }
    const int baseline = width(tabs, 0);
    updateDocumentTabDecorations(tabs, {{101, 0}, {102, 1}, {103, 3}}, nullptr);
    const int one = width(tabs, 1), two = width(tabs, 2);
    context.expect(width(tabs, 0) == baseline, "normal tabs keep native width");
    context.expect(one > baseline, "one indicator reserves real native tab width");
    context.expect(two > one, "two indicators reserve more width than one");
    for (int i = 0; i < 3; ++i)
    {
        LPARAM key = 0;
        context.expect(text(tabs, i, &key) == L"A && B.txt" && key == i + 101,
            "native callers receive the original filename and unchanged buffer identity");
    }
    DocumentTabDecorationMetrics metrics;
    context.expect(documentTabDecorationMetrics(tabs, 2, metrics)
        && metrics.vertical == vertical
        && metrics.iconsLeft > metrics.textRight && metrics.iconsRight < metrics.buttonsLeft,
        "icons have separate space after filename and before native buttons");
    for (int i = 0; i < 20; ++i)
        updateDocumentTabDecorations(tabs, {{101, 0}, {102, 1}, {103, 3}}, nullptr);
    context.expect(width(tabs, 1) == one && width(tabs, 2) == two,
        "repeated refresh does not accumulate width or spacer suffixes");
    const auto originalStyle = GetWindowLongPtrW(tabs, GWL_STYLE);
    SetWindowLongPtrW(tabs, GWL_STYLE, originalStyle ^ TCS_VERTICAL);
    pump();
    context.expect(documentTabDecorationMetrics(tabs, 2, metrics)
        && metrics.vertical != vertical && metrics.iconsLeft > metrics.textRight
        && metrics.iconsRight < metrics.buttonsLeft && text(tabs, 2) == L"A && B.txt",
        "changing orientation recomputes icon geometry and preserves canonical captions");
    SetWindowLongPtrW(tabs, GWL_STYLE, originalStyle);
    pump();
    context.expect(documentTabDecorationMetrics(tabs, 2, metrics) && metrics.vertical == vertical
        && width(tabs, 1) == one && width(tabs, 2) == two,
        "returning to original orientation restores the same lengths without accumulation");
    // Rename the two-icon buffer exactly as Notepad++ does: change TEXT only.
    TCITEMW rename{};
    rename.mask = TCIF_TEXT;
    rename.pszText = const_cast<wchar_t*>(L"renamed-file.txt");
    SendMessageW(tabs, TCM_SETITEMW, 2, reinterpret_cast<LPARAM>(&rename));
    pump();
    LPARAM key = 0;
    context.expect(text(tabs, 2, &key) == L"renamed-file.txt" && key == 103,
        "rename preserves buffer identity and canonical filename");
    context.expect(documentTabDecorationMetrics(tabs, 2, metrics) && metrics.mask == 3,
        "renamed tab retains both indicator slots");
    // Reordering copies canonical text and PARAM; decorations follow the buffer, not the index.
    TCITEMW first{};
    first.mask = TCIF_TEXT | TCIF_PARAM;
    first.pszText = const_cast<wchar_t*>(L"renamed-file.txt"); first.lParam = 103;
    SendMessageW(tabs, TCM_SETITEMW, 0, reinterpret_cast<LPARAM>(&first));
    first.pszText = const_cast<wchar_t*>(L"A && B.txt"); first.lParam = 101;
    SendMessageW(tabs, TCM_SETITEMW, 2, reinterpret_cast<LPARAM>(&first));
    pump();
    context.expect(documentTabDecorationMetrics(tabs, 0, metrics) && metrics.mask == 3
        && !documentTabDecorationMetrics(tabs, 2, metrics) && width(tabs, 2) == baseline,
        "reordered indicators stay with their buffers and leave normal tabs unmodified");
    updateDocumentTabDecorations(tabs, {}, nullptr);
    context.expect(width(tabs, 1) == baseline && text(tabs, 0) == L"renamed-file.txt",
        "disabling exclusions removes reserved width without changing names");
    updateDocumentTabDecorations(tabs, {{102, 1}}, nullptr);
    removeDocumentTabDecorations(tabs);
    context.expect(width(tabs, 1) == baseline && text(tabs, 1) == L"A && B.txt",
        "detaching restores native text and geometry");
    DestroyWindow(parent);
    }
}
