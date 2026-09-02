#pragma once

#include "resource.h"
#include <array>

namespace npphistory
{
enum class Command { capture, compare, restore, refresh, settings, about, history };
enum class CommandSurface { pane, plugins, toolbar, context };
inline constexpr int commandCount = 7;
// Internal indices remain stable; every visible surface uses this order.
inline constexpr std::array<Command, commandCount> commandOrder{
    Command::capture, Command::compare, Command::restore, Command::history,
    Command::refresh, Command::settings, Command::about};
struct CommandDefinition { const wchar_t* name; int icon; int paneControl; };
inline constexpr std::array<CommandDefinition, commandCount> commands{{
    {L"Capture", IDI_CAPTURE, IDC_CAPTURE}, {L"Compare", IDI_COMPARE, IDC_COMPARE},
    {L"Restore", IDI_RESTORE, IDC_RESTORE}, {L"Refresh", IDI_REFRESH, IDC_REFRESH},
    {L"Settings", IDI_SETTINGS, IDC_PANEL_SETTINGS}, {L"About", IDI_ABOUT, IDC_PANEL_ABOUT},
    {L"History", IDI_NPPHISTORY, IDC_PANEL_HISTORY}
}};
inline constexpr bool placementLocked(Command command, CommandSurface surface)
{
    (void)command;
    return surface == CommandSurface::plugins;
}
struct CommandPlacement
{
    bool pane = true;
    bool plugins = true;
    bool toolbar = false;
    bool context = false;
};
inline constexpr bool commandAvailable(Command command, bool saved, bool excluded,
    bool historyEnabled, bool hasRevisions, bool selected, bool paneVisible)
{
    switch (command)
    {
    case Command::capture: return saved && !excluded && historyEnabled;
    case Command::compare: return saved && !excluded && hasRevisions && (!paneVisible || selected);
    case Command::restore: return saved && !excluded && paneVisible && selected;
    case Command::refresh: return saved && !excluded;
    default: return true;
    }
}
inline constexpr int placementControl(int row, int column) { return 1200 + row * 10 + column; }
inline constexpr std::array<int, commandCount> toolbarControlIds{
    IDC_TOOLBAR_CAPTURE, IDC_TOOLBAR_COMPARE, 1223, 1233, 1243, 1253, IDC_TOOLBAR_HISTORY};
inline constexpr std::array<int, commandCount> hotkeyEnableIds{
    IDC_HOTKEY_CAPTURE_ENABLED, IDC_HOTKEY_COMPARE_ENABLED, 1225, 1235, 1245, 1255, IDC_HOTKEY_HISTORY_ENABLED};
inline constexpr std::array<int, commandCount> hotkeyInputIds{
    IDC_HOTKEY_CAPTURE_INPUT, IDC_HOTKEY_COMPARE_INPUT, 1226, 1236, 1246, 1256, IDC_HOTKEY_HISTORY_INPUT};
}
