#include "TestHarness.h"
#include "TemporaryStatusBar.h"

using namespace npphistory;

void runTemporaryStatusTests(TestContext& context)
{
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    const HWND owner = CreateWindowExW(0, L"STATIC", L"Status test", WS_OVERLAPPED,
        0, 0, 800, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND bar = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr, WS_CHILD,
        0, 0, 800, 20, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    context.expect(owner && bar, "Temporary status hidden test windows created");
    if (!owner || !bar) { if (owner) DestroyWindow(owner); return; }
    const int parts[]{240, 350, 460, 570, 680, -1};
    SendMessageW(bar, SB_SETPARTS, 6, reinterpret_cast<LPARAM>(parts));
    const auto set = [&](int part, const wchar_t* text, UINT flags = 0) {
        SendMessageW(bar, SB_SETTEXTW, part | flags, reinterpret_cast<LPARAM>(text));
    };
    const auto get = [&](int part = 0) {
        const unsigned length = LOWORD(SendMessageW(bar, SB_GETTEXTLENGTHW, part, 0));
        std::wstring text(length + 1, L'\0');
        SendMessageW(bar, SB_GETTEXTW, part, reinterpret_cast<LPARAM>(text.data()));
        text.resize(length);
        return text;
    };
    const auto pump = [&] {
        MSG message{};
        while (PeekMessageW(&message, bar, TemporaryStatusBar::showMessage,
            TemporaryStatusBar::showMessage, PM_REMOVE)) DispatchMessageW(&message);
    };
    TemporaryStatusBar status;
    status.initialize(owner);
    set(0, L"Normal text", SBT_NOBORDERS);
    for (int part = 1; part < 6; ++part) set(part, L"Native field");

    status.show(L"Revision captured");
    context.expect(get() == L"Normal text", "Status queued until action notifications finish");
    pump();
    context.expect(get() == L"NppHistory: Revision captured", "Queued action displayed with plugin prefix");
    context.expect(HIWORD(SendMessageW(bar, SB_GETTEXTLENGTHW, 0, 0)) == SBT_NOBORDERS,
        "Status preserves native text style");
    status.show(L"History refreshed"); pump();
    context.expect(get() == L"NppHistory: History refreshed", "New action replaces previous action");
    status.clear();
    context.expect(get() == L"Normal text", "Replacement retains original restoration baseline");
    for (int part = 1; part < 6; ++part)
        context.expect(get(part) == L"Native field", "Other native status fields unchanged");
    int actualParts[6]{};
    context.expect(SendMessageW(bar, SB_GETPARTS, 6, reinterpret_cast<LPARAM>(actualParts)) == 6,
        "Status part count unchanged");
    for (int part = 0; part < 6; ++part)
        context.expect(actualParts[part] == parts[part], "Status field widths unchanged");

    status.show(L"First queued"); status.show(L"Latest queued"); pump();
    context.expect(get() == L"NppHistory: Latest queued", "Pending action messages coalesce");
    status.clear();
    status.show(L"Cancelled before display"); status.clear(); pump();
    context.expect(get() == L"Normal text", "Clear discards queued feedback");

    status.show(L"Short action", 1); pump();
    Sleep(30);
    SendMessageW(bar, WM_TIMER, TemporaryStatusBar::timerId, 0);
    context.expect(get() == L"Normal text", "Expired feedback restores original status");
    status.show(L"Old timer", 1); pump(); Sleep(30);
    status.show(L"Replacement timer", 5000); pump();
    SendMessageW(bar, WM_TIMER, TemporaryStatusBar::timerId, 0);
    context.expect(get() == L"NppHistory: Replacement timer", "Stale timer cannot expire a replacement early");

    set(0, L"Host language changed");
    status.clear();
    context.expect(get() == L"Host language changed", "Host Unicode update takes ownership without stale restore");
    status.show(L"Another action"); pump(); status.clear();
    context.expect(get() == L"Host language changed", "Later action restores latest host text");
    status.show(L"Before ANSI update"); pump();
    SendMessageA(bar, SB_SETTEXTA, 0, reinterpret_cast<LPARAM>("Host ANSI text"));
    status.clear();
    context.expect(get() == L"Host ANSI text", "Host ANSI update also takes ownership");
    status.show(L"Other field test"); pump(); set(2, L"New cursor location");
    context.expect(get() == L"NppHistory: Other field test", "Other-field updates do not cancel feedback");
    status.clear();
    context.expect(get(2) == L"New cursor location", "Restoration retains updated cursor field");

    status.show(L"Before simple mode"); pump();
    SendMessageW(bar, SB_SIMPLE, TRUE, 0);
    status.show(L"Skipped in simple mode"); pump();
    SendMessageW(bar, SB_SIMPLE, FALSE, 0);
    context.expect(get() == L"Host ANSI text", "Simple mode clears and suppresses temporary feedback");
    SendMessageW(bar, SB_SETTEXTW, SBT_OWNERDRAW, 123);
    status.show(L"Skipped owner-draw"); pump(); status.clear();
    context.expect((HIWORD(SendMessageW(bar, SB_GETTEXTLENGTHW, 0, 0)) & SBT_OWNERDRAW) != 0,
        "Owner-drawn field is never overwritten or interpreted as text");
    set(0, L"Native baseline");
    status.show(L"Before repartition"); pump();
    SendMessageW(bar, SB_SETPARTS, 6, reinterpret_cast<LPARAM>(parts));
    context.expect(get() == L"Native baseline", "Native layout changes restore borrowed field");
    status.show(L"Before shutdown"); pump(); status.shutdown();
    context.expect(get() == L"Native baseline", "Shutdown restores owned text and detaches");
    status.show(L"After shutdown"); pump();
    context.expect(get() == L"Native baseline", "Shutdown prevents subsequent feedback until initialization");

    status.initialize(owner); status.show(L"Before destruction"); pump();
    DestroyWindow(bar);
    status.show(L"Missing bar"); // Must not recreate the host's hidden/missing status control.
    bar = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr, WS_CHILD,
        0, 0, 800, 20, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    context.expect(bar != nullptr, "Native status control recreated for lifecycle test");
    if (bar)
    {
        SendMessageW(bar, SB_SETPARTS, 6, reinterpret_cast<LPARAM>(parts));
        set(0, L"Recreated baseline");
        status.show(L"Reattached"); pump();
        context.expect(get() == L"NppHistory: Reattached", "Feedback reattaches after native control recreation");
        status.shutdown();
        context.expect(get() == L"Recreated baseline", "Recreated control restores its own baseline");
    }
    DestroyWindow(owner);
    status.initialize(nullptr); status.show(L"No owner"); status.shutdown();
}
