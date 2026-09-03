#pragma once
#include "Settings.h"
#include <string>

namespace npphistory
{
inline std::wstring commandHotkeyText(const HotkeySetting& key)
{
    if (!key.enabled || !key.key) return {};
    std::wstring text;
    if (key.ctrl) text += L"Ctrl+";
    if (key.alt) text += L"Alt+";
    if (key.shift) text += L"Shift+";
    if ((key.key >= 'A' && key.key <= 'Z') || (key.key >= '0' && key.key <= '9'))
        return text + static_cast<wchar_t>(key.key);
    if (key.key >= VK_F1 && key.key <= VK_F24)
        return text + L"F" + std::to_wstring(key.key - VK_F1 + 1);
    UINT scan = MapVirtualKeyW(key.key, MAPVK_VK_TO_VSC) << 16;
    if (key.key == VK_LEFT || key.key == VK_RIGHT || key.key == VK_UP || key.key == VK_DOWN
        || key.key == VK_HOME || key.key == VK_END || key.key == VK_PRIOR || key.key == VK_NEXT
        || key.key == VK_INSERT || key.key == VK_DELETE) scan |= 1U << 24;
    wchar_t name[64]{};
    if (GetKeyNameTextW(static_cast<LONG>(scan), name, 64)) return text + name;
    return text + L"Key " + std::to_wstring(key.key);
}

inline bool safeCommandHotkey(const HotkeySetting& key) noexcept
{
    if (!key.enabled) return true;
    if (key.key < VK_BACK || key.key > 254 || key.key == VK_PROCESSKEY || key.key == VK_PACKET
        || key.key == VK_SHIFT || key.key == VK_CONTROL
        || key.key == VK_MENU || key.key == VK_LWIN || key.key == VK_RWIN
        || (key.key >= VK_LSHIFT && key.key <= VK_RMENU)) return false;
    // Never consume ordinary typing, Shift+typing, or OS/window management shortcuts.
    if (!key.ctrl && !key.alt && !(key.key >= VK_F1 && key.key <= VK_F24)) return false;
    if (!key.ctrl && key.alt && ((key.key >= 'A' && key.key <= 'Z')
        || (key.key >= '0' && key.key <= '9'))) return false; // Host menu mnemonics.
    if (key.alt && (key.key == VK_TAB || key.key == VK_ESCAPE || key.key == VK_F4
        || key.key == VK_SPACE)) return false;
    if (key.ctrl && (key.key == VK_ESCAPE || (key.alt && key.key == VK_DELETE))) return false;
    return true;
}

struct HotkeyDispatch { bool consume = false; int command = -1; };

// Pure state machine, separate from Windows message routing so replacement, repeat,
// focus and modifier rules can be tested without sending real keystrokes.
struct LiveHotkeys
{
    std::array<HotkeySetting, commandCount> keys{};
    std::array<bool, 256> consumed{};
    unsigned generation = 0;

    void apply(const Settings& settings) noexcept
    {
        for (int row = 0; row < commandCount; ++row)
            keys[row] = settings.commandHotkey(static_cast<Command>(row));
        ++generation;
    }
    void resetPressed() noexcept { consumed.fill(false); }
    HotkeyDispatch event(unsigned key, bool down, bool repeat, bool inScope,
        bool ctrl, bool alt, bool shift, bool windowsKey, bool altGr) noexcept
    {
        if (key >= consumed.size()) return {};
        if (!down)
        {
            const bool wasConsumed = consumed[key];
            consumed[key] = false;
            return {wasConsumed, -1};
        }
        // A stale entry can remain if focus changed before key-up reached our thread.
        if (!repeat) consumed[key] = false;
        if (consumed[key]) return {true, -1};
        if (!inScope || windowsKey || altGr) return {};
        for (int row = 0; row < commandCount; ++row)
        {
            const auto& binding = keys[row];
            if (binding.enabled && safeCommandHotkey(binding) && binding.key == key
                && binding.ctrl == ctrl && binding.alt == alt && binding.shift == shift)
            {
                consumed[key] = true;
                return {true, repeat ? -1 : row};
            }
        }
        return {};
    }
};
}
