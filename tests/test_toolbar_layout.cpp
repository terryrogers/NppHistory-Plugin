#include "TestHarness.h"
#include "ToolbarVisibility.h"
#include <commctrl.h>

using namespace npphistory;
void runToolbarLayoutTests(TestContext& context)
{
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    const auto instance = GetModuleHandleW(nullptr);
    HWND owner = CreateWindowExW(0, L"Static", L"Toolbar regression", WS_POPUP,
        -10000, -10000, 700, 200, nullptr, nullptr, instance, nullptr);
    HWND toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, L"", WS_CHILD | CCS_NORESIZE,
        0, 0, 700, 34, owner, nullptr, instance, nullptr);
    context.expect(owner && toolbar, "disposable native toolbar created");
    if (!owner || !toolbar) { if (owner) DestroyWindow(owner); return; }
    SendMessageW(toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(toolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(30,30));
    TBBUTTON buttons[5]{};
    for (int i=0;i<5;++i)
    {
        buttons[i].idCommand = 100+i;
        buttons[i].iBitmap = I_IMAGENONE;
        buttons[i].fsState = TBSTATE_ENABLED;
        buttons[i].iString = reinterpret_cast<INT_PTR>(L"Command tooltip");
    }
    SendMessageW(toolbar, TB_ADDBUTTONSW, 5, reinterpret_cast<LPARAM>(buttons));
    const auto size = SendMessageW(toolbar, TB_GETBUTTONSIZE, 0, 0);
    const auto images = SendMessageW(toolbar, TB_GETIMAGELIST, 0, 0);
    const auto count = [&] {return SendMessageW(toolbar, TB_BUTTONCOUNT, 0, 0);};
    const auto index = [&](int id) {return static_cast<int>(SendMessageW(toolbar, TB_COMMANDTOINDEX,id,0));};
    const std::vector<std::pair<int,bool>> hidden{{101,false},{103,false},{104,false}};
    const std::vector<std::pair<int,bool>> shown{{101,true},{103,true},{104,true}};
    context.expect(syncToolbarCommands(nullptr,shown)==0, "invalid toolbar ignored");
    context.expect(syncToolbarCommands(toolbar,{{999,true}})==0 && count()==5, "unrelated toolbar untouched");
    for (int iteration=0;iteration<10;++iteration)
    {
        context.expect(syncToolbarCommands(toolbar,hidden)==0 && count()==2 && index(101)<0 && index(104)<0,
            "unselected commands removed, including trailing command");
        RECT last{};
        context.expect(SendMessageW(toolbar,TB_GETITEMRECT,count()-1,reinterpret_cast<LPARAM>(&last))
            && last.bottom-last.top>=HIWORD(size), "last remaining button can be measured by toolbar extensions");
        context.expect(syncToolbarCommands(toolbar,hidden)==0 && count()==2, "repeated sync is idempotentent");
        context.expect(syncToolbarCommands(toolbar,shown)==3 && count()==5, "commands restored live without duplicates");
        context.expect(index(100)==0 && index(101)==1 && index(102)==2 && index(103)==3 && index(104)==4,
            "native and plugin command order preserved");
    }
    wchar_t label[64]{};
    SendMessageW(toolbar,TB_GETBUTTONTEXTW,104,reinterpret_cast<LPARAM>(label));
    context.expect(std::wstring(label)==L"Command tooltip", "removed button text remains valid when restored");
    context.expect(SendMessageW(toolbar,TB_GETBUTTONSIZE,0,0)==size
        && SendMessageW(toolbar,TB_GETIMAGELIST,0,0)==images, "visibility never changes button size or images");
    SendMessageW(toolbar,TB_ENABLEBUTTON,101,FALSE);
    syncToolbarCommands(toolbar,hidden);
    syncToolbarCommands(toolbar,shown);
    context.expect(!(SendMessageW(toolbar,TB_GETSTATE,101,0)&TBSTATE_ENABLED), "disabled state survives reinsertion");
    while(count()) SendMessageW(toolbar,TB_DELETEBUTTON,0,0);
    buttons[4].iBitmap=7;
    SendMessageW(toolbar,TB_ADDBUTTONSW,5,reinterpret_cast<LPARAM>(buttons));
    syncToolbarCommands(toolbar,hidden);
    syncToolbarCommands(toolbar,shown);
    TBBUTTON restored{};
    SendMessageW(toolbar,TB_GETBUTTON,index(104),reinterpret_cast<LPARAM>(&restored));
    context.expect(restored.iBitmap==7, "image indices refresh after a same-window toolbar rebuild");
    DestroyWindow(owner);
}
