#include "ToolbarVisibility.h"
#include <commctrl.h>
#include <algorithm>
#include <map>
#include <string>

namespace npphistory
{
namespace
{
struct SavedButton { TBBUTTON button{}; std::wstring text; bool hasText = false; };
struct ToolbarState { std::map<int, SavedButton> buttons; std::vector<int> order; };
constexpr UINT_PTR subclassId = 0x4E485456;
LRESULT CALLBACK toolbarSubclass(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR id, DWORD_PTR data)
{
    if (message == WM_NCDESTROY)
    {
        RemoveWindowSubclass(window, toolbarSubclass, id);
        delete reinterpret_cast<ToolbarState*>(data);
    }
    return DefSubclassProc(window, message, wParam, lParam);
}
int indexOf(HWND toolbar, int id)
{
    return static_cast<int>(SendMessageW(toolbar, TB_COMMANDTOINDEX, id, 0));
}
}

int syncToolbarCommands(HWND toolbar, const std::vector<std::pair<int, bool>>& commands)
{
    try
    {
    DWORD_PTR data = 0;
    GetWindowSubclass(toolbar, toolbarSubclass, subclassId, &data);
    auto* state = reinterpret_cast<ToolbarState*>(data);
    bool allPresent = true, anyPresent = false;
    for (const auto& command : commands)
    {
        const bool present = command.first > 0 && indexOf(toolbar, command.first) >= 0;
        allPresent = allPresent && present;
        anyPresent = anyPresent || present;
    }
    if (!state && !anyPresent) return 0;
    if (!state)
    {
        state = new ToolbarState;
        if (!SetWindowSubclass(toolbar, toolbarSubclass, subclassId, reinterpret_cast<DWORD_PTR>(state)))
        { delete state; return 0; }
    }
    // Refresh after host/Customize Toolbar rebuilds, including new image indices.
    // Keep the complete anchor order while some of our buttons are absent.
    if (state->order.empty() || allPresent)
    {
        state->order.clear();
        const int count = static_cast<int>(SendMessageW(toolbar, TB_BUTTONCOUNT, 0, 0));
        for (int i = 0; i < count; ++i)
        {
            TBBUTTON button{};
            if (SendMessageW(toolbar, TB_GETBUTTON, i, reinterpret_cast<LPARAM>(&button)))
                state->order.push_back(button.idCommand);
        }
    }
    // Snapshot before deleting anything. Copy text rather than retaining a pointer
    // owned by a toolbar item that may subsequently be removed.
    for (const auto& command : commands)
    {
        const int index = indexOf(toolbar, command.first);
        if (command.first <= 0 || index < 0) continue;
        SavedButton saved;
        if (!SendMessageW(toolbar, TB_GETBUTTON, index, reinterpret_cast<LPARAM>(&saved.button))) continue;
        const LRESULT length = SendMessageW(toolbar, TB_GETBUTTONTEXTW, command.first, 0);
        if (length >= 0 && length < 32768)
        {
            saved.text.resize(static_cast<size_t>(length) + 1);
            SendMessageW(toolbar, TB_GETBUTTONTEXTW, command.first, reinterpret_cast<LPARAM>(saved.text.data()));
            saved.text.resize(static_cast<size_t>(length));
            saved.hasText = true;
        }
        state->buttons[command.first] = std::move(saved);
    }
    bool changed = false;
    int visible = 0;
    for (const auto& command : commands)
    {
        if (command.first <= 0) continue;
        int index = indexOf(toolbar, command.first);
        if (!command.second)
        {
            if (index >= 0) changed |= SendMessageW(toolbar, TB_DELETEBUTTON, index, 0) != FALSE;
            continue;
        }
        if (index < 0)
        {
            auto saved = state->buttons.find(command.first);
            if (saved == state->buttons.end()) continue;
            index = static_cast<int>(SendMessageW(toolbar, TB_BUTTONCOUNT, 0, 0));
            const auto anchor = std::find(state->order.begin(), state->order.end(), command.first);
            if (anchor != state->order.end())
            {
                for (auto next = anchor + 1; next != state->order.end(); ++next)
                    if (*next > 0) if (const int found = indexOf(toolbar, *next); found >= 0) { index = found; break; }
            }
            TBBUTTON button = saved->second.button;
            button.fsState &= ~TBSTATE_HIDDEN;
            if (saved->second.hasText) button.iString = reinterpret_cast<INT_PTR>(saved->second.text.c_str());
            changed |= SendMessageW(toolbar, TB_INSERTBUTTONW, index, reinterpret_cast<LPARAM>(&button)) != FALSE;
        }
        else if (SendMessageW(toolbar, TB_GETSTATE, command.first, 0) & TBSTATE_HIDDEN)
            changed |= SendMessageW(toolbar, TB_HIDEBUTTON, command.first, FALSE) != FALSE;
        if (indexOf(toolbar, command.first) >= 0) ++visible;
    }
    if (changed)
        RedrawWindow(GetParent(toolbar), nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    return visible;
    }
    catch (...) { return 0; }
}
}
