#include "TestHarness.h"
#include "Utilities.h"
#include <commctrl.h>

using namespace npphistory;

void runToolbarLayoutTests(TestContext& context)
{
    context.expect(!repairClippedToolbarBand(nullptr), "null toolbar is not repaired");
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    const auto instance = GetModuleHandleW(nullptr);
    // Off-screen, disposable native controls: never resize a user's application.
    HWND owner = CreateWindowExW(0, L"Static", L"Toolbar regression", WS_POPUP | WS_VISIBLE,
        -10000, -10000, 700, 200, nullptr, nullptr, instance, nullptr);
    HWND rebar = CreateWindowExW(0, REBARCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | RBS_VARHEIGHT,
        0, 0, 700, 40, owner, nullptr, instance, nullptr);
    HWND toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE
        | TBSTYLE_FLAT | CCS_NORESIZE | CCS_NOPARENTALIGN | CCS_NODIVIDER,
        0, 0, 400, 34, rebar, nullptr, instance, nullptr);
    context.expect(owner && rebar && toolbar, "native toolbar/rebar fixture created");
    if (!owner || !rebar || !toolbar) { if (owner) DestroyWindow(owner); return; }
    SendMessageW(toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(toolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(30, 30));
    TBBUTTON buttons[2]{};
    for (int i = 0; i < 2; ++i)
    {
        buttons[i].iBitmap = I_IMAGENONE;
        buttons[i].idCommand = 100 + i;
        buttons[i].fsState = TBSTATE_ENABLED;
        buttons[i].fsStyle = BTNS_BUTTON;
    }
    SendMessageW(toolbar, TB_ADDBUTTONS, 2, reinterpret_cast<LPARAM>(buttons));
    const UINT height = HIWORD(SendMessageW(toolbar, TB_GETBUTTONSIZE, 0, 0));
    REBARBANDINFOW band{REBARBANDINFO_V6_SIZE};
    band.fMask = RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_STYLE | RBBIM_SIZE | RBBIM_ID;
    band.hwndChild = toolbar;
    band.fStyle = RBBS_VARIABLEHEIGHT | RBBS_NOGRIPPER;
    band.cyMinChild = band.cyChild = band.cyMaxChild = height + 4;
    band.cyIntegral = 1;
    band.cx = 400;
    band.wID = 42;
    context.expect(SendMessageW(rebar, RB_INSERTBANDW, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(&band)) != 0,
        "toolbar inserted into native rebar band");
    context.expect(!repairClippedToolbarBand(toolbar), "healthy toolbar is left unchanged");
    context.expect(!repairClippedToolbarBand(owner), "non-toolbar windows are rejected");
    HWND neighbour = CreateWindowExW(0, L"Static", L"Other band", WS_CHILD | WS_VISIBLE,
        0, 0, 174, 20, rebar, nullptr, instance, nullptr);
    REBARBANDINFOW other{REBARBANDINFO_V6_SIZE};
    other.fMask = RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_STYLE | RBBIM_SIZE | RBBIM_ID;
    other.hwndChild = neighbour;
    other.fStyle = RBBS_NOGRIPPER;
    other.cx = other.cxMinChild = 174;
    other.cyMinChild = 20;
    other.wID = 77;
    context.expect(SendMessageW(rebar, RB_INSERTBANDW, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(&other)) != 0,
        "unrelated search-style band added to fixture");

    // Reproduce the observed state: valid band constraints, clipped 4px child.
    MoveWindow(toolbar, 0, 0, 400, 4, FALSE);
    RECT clipped{};
    GetClientRect(toolbar, &clipped);
    context.expect(clipped.bottom == 4 && height > 4, "native fixture recreates a four-pixel toolbar");
    context.expect(repairClippedToolbarBand(toolbar), "rebar resynchronization repairs clipped toolbar");
    RECT restored{};
    GetClientRect(toolbar, &restored);
    context.expect(restored.bottom >= static_cast<int>(height), "repaired child contains full button height");
    context.expect(HIWORD(SendMessageW(toolbar, TB_GETBUTTONSIZE, 0, 0)) == height,
        "repair preserves native button dimensions");
    context.expect(SendMessageW(toolbar, TB_BUTTONCOUNT, 0, 0) == 2
        && SendMessageW(toolbar, TB_COMMANDTOINDEX, 100, 0) == 0
        && SendMessageW(toolbar, TB_COMMANDTOINDEX, 101, 0) == 1,
        "repair preserves native buttons and order");
    context.expect(!repairClippedToolbarBand(toolbar), "repeated healthy checks do not resize again");
    band.fMask = RBBIM_ID | RBBIM_STYLE | RBBIM_CHILDSIZE;
    SendMessageW(rebar, RB_GETBANDINFOW, 0, reinterpret_cast<LPARAM>(&band));
    context.expect(band.wID == 42 && band.cyMinChild == height + 4
        && band.cyMaxChild == height + 4 && band.cyIntegral == 1,
        "repair preserves valid native band metadata");
    other.fMask = RBBIM_ID | RBBIM_CHILD | RBBIM_CHILDSIZE;
    context.expect(SendMessageW(rebar, RB_GETBANDINFOW, 1, reinterpret_cast<LPARAM>(&other))
        && other.wID == 77 && other.hwndChild == neighbour && other.cxMinChild == 174
        && other.cyMinChild == 20, "recovery leaves unrelated rebar band metadata intact");
    SendMessageW(toolbar, TB_HIDEBUTTON, 100, TRUE);
    SendMessageW(toolbar, TB_HIDEBUTTON, 101, TRUE);
    MoveWindow(toolbar, 0, 0, 400, 4, FALSE);
    context.expect(!repairClippedToolbarBand(toolbar), "toolbar with all commands intentionally hidden is left alone");
    SendMessageW(toolbar, TB_HIDEBUTTON, 100, FALSE);
    SendMessageW(toolbar, TB_HIDEBUTTON, 101, FALSE);

    ShowWindow(toolbar, SW_HIDE);
    MoveWindow(toolbar, 0, 0, 400, 4, FALSE);
    context.expect(!repairClippedToolbarBand(toolbar) && !IsWindowVisible(toolbar),
        "intentionally hidden toolbar is not forced visible");
    ShowWindow(toolbar, SW_SHOWNA);
    SendMessageW(rebar, RB_SHOWBAND, 0, FALSE);
    context.expect(!repairClippedToolbarBand(toolbar), "hidden rebar band is not repaired");
    DestroyWindow(owner);
}
