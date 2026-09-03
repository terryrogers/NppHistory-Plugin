#include "Settings.h"
#include "LiveHotkeys.h"
#include "Logger.h"
#include "resource.h"
#include "Utilities.h"
#include "UpdateChecker.h"

#include <algorithm>
#include <commdlg.h>
#include <commctrl.h>
#include <cwchar>
#include <shlobj.h>
#include <string>
#include <unordered_set>
#include <vector>

namespace npphistory
{
namespace
{
HWND activeSettingsDialog = nullptr;
Settings* activeSettings = nullptr;
constexpr UINT_PTR settingsTooltipTimer = 0x4E51;

struct SettingsTooltipEntry
{
    int id;
    std::wstring text;
};

std::vector<SettingsTooltipEntry> settingsTooltips = {
    {IDC_TOOLBAR_CAPTURE, L"Show or hide Capture on the Notepad++ toolbar when you click OK."},
    {IDC_TOOLBAR_COMPARE, L"Show or hide Compare on the Notepad++ toolbar when you click OK."},
    {IDC_TOOLBAR_HISTORY, L"Show or hide History on the Notepad++ toolbar when you click OK."},
    {IDC_HOTKEY_CAPTURE_ENABLED, L"Enable the Capture keyboard shortcut when you click OK."},
    {IDC_HOTKEY_CAPTURE_INPUT, L"Press the complete key combination to use for Capture."},
    {IDC_HOTKEY_COMPARE_ENABLED, L"Enable the Compare keyboard shortcut when you click OK."},
    {IDC_HOTKEY_COMPARE_INPUT, L"Press the complete key combination to use for Compare."},
    {IDC_HOTKEY_HISTORY_ENABLED, L"Enable the History keyboard shortcut when you click OK."},
    {IDC_HOTKEY_HISTORY_INPUT, L"Press the complete key combination to use for History."},
    {IDC_ENABLED, L"Enable NppHistory automatic file saving. This is unavailable while AutoSave.dll is installed."},
    {IDC_AFTER_EDIT, L"Automatically save after editing has stopped for the configured number of seconds."},
    {IDC_AFTER_EDIT_SECONDS, L"Seconds of editing inactivity before automatic saving; the minimum is 10 seconds."},
    {IDC_AUTOSAVE_FOCUS_LOSS, L"Automatically save when the Notepad++ window loses focus."},
    {IDC_AUTOSAVE_INTERVAL, L"Automatically save repeatedly at the configured interval."},
    {IDC_AUTOSAVE_INTERVAL_MINUTES, L"Number of minutes between interval-based automatic saves."},
    {IDC_AUTOSAVE_TAB_CHANGE, L"Automatically save when you switch to another file tab."},
    {IDC_AUTOSAVE_EXIT, L"Automatically save when Notepad++ exits, subject to normal save permissions."},
    {IDC_AUTOSAVE_CURRENT_FILE, L"Apply an automatic-save trigger only to the currently active file."},
    {IDC_AUTOSAVE_ALL_FILES, L"Apply an automatic-save trigger to every open modified file."},
    {IDC_AUTOSAVE_EXCLUSIONS, L"One case-insensitive wildcard per line. Matching files are excluded from NppHistory Auto Save."},
    {IDC_HISTORY_ENABLED, L"Enable creation of local NppHistory file revisions."},
    {IDC_HISTORY_BEFORE_SAVE, L"Create a revision of the existing file on disk before saving overwrites it."},
    {IDC_HISTORY_AFTER_SAVE, L"Create a revision containing the saved content immediately after a file is saved."},
    {IDC_HISTORY_BEFORE_RESTORE, L"Create a safety revision before replacing a file with an older revision."},
    {IDC_HISTORY_ADJACENT, L"Store each file's revisions in a hidden .npphistory folder beside that file."},
    {IDC_HISTORY_CUSTOM, L"Store revisions beneath one common folder instead of beside each source file."},
    {IDC_HISTORY_PATH, L"Common root folder used for revision storage when Common folder is selected."},
    {IDC_HISTORY_BROWSE, L"Choose the common revision-history folder."},
    {IDC_HISTORY_EXCLUSIONS, L"One case-insensitive wildcard per line. Matching files cannot create, refresh, compare or restore revisions."},
    {IDC_LOGGING_ENABLED, L"Enable NppHistory diagnostic and audit logging."},
    {IDC_LOGGING_LEVEL, L"Choose the minimum severity recorded: Errors, Warnings, Informational or Debug. Critical failures are included at every enabled level."},
    {IDC_LOGGING_DEFAULT, L"Write the log in the normal Notepad++ plugin configuration folder."},
    {IDC_LOGGING_CUSTOM, L"Write the log to a custom file path."},
    {IDC_LOGGING_PATH, L"Full path of the custom NppHistory log file."},
    {IDC_LOGGING_BROWSE, L"Choose a custom log file location."},
    {IDC_LOGGING_OPEN, L"Open the active NppHistory log file in Notepad++."},
    {IDC_LOGGING_MAX_SIZE, L"Maximum log size in megabytes before the selected rollover action is applied."},
    {IDC_LOGGING_ROLLOVER, L"Choose whether a full log is overwritten or renamed as an archive."},
    {IDC_LOGGING_ARCHIVES, L"Maximum number of archived log files retained when archive rollover is selected."},
    {IDC_AUTO_UPDATE, L"Allow NppHistory to check GitHub automatically for eligible releases."},
    {IDC_UPDATE_FREQUENCY, L"Choose how often automatic update checks are due: daily, weekly or monthly."},
    {IDC_UPDATE_PRERELEASES, L"Include beta and other prerelease builds when checking for updates."},
    {IDC_UPDATE_CHECK_NOW, L"Check for an eligible update now and offer installation if one is available."},
    {IDOK, L"Save all Settings changes and close this window."},
    {IDCANCEL, L"Discard unsaved Settings changes and close this window."}
};

void configureSettingsTooltips(HWND dialog)
{
    // Generate semantic hints for every command cell, including locked choices.
    settingsTooltips.erase(std::remove_if(settingsTooltips.begin(), settingsTooltips.end(),
        [](const auto& entry) { return entry.id >= 1200 || entry.id == IDC_CONTEXT_SUBMENU; }),
        settingsTooltips.end());
    const wchar_t* surfaces[] = {L"History Pane", L"tab bar right-click menu",
        L"Notepad++ toolbar (applies on OK)", L"document right-click menu"};
    for (int row = 0; row < commandCount; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            const int id = column == 2 ? toolbarControlIds[row] : placementControl(row, column + 1);
            std::wstring hint = std::wstring(commands[row].name) + L": show in the " + surfaces[column] + L".";
            if (placementLocked(static_cast<Command>(row), placementSurfaces[column]))
                hint = L"History opens the History Pane, so it cannot be added inside that pane.";
            if (std::none_of(settingsTooltips.begin(), settingsTooltips.end(),
                [=](const auto& entry) { return entry.id == id; }))
                settingsTooltips.push_back({id, hint});
        }
        for (const int id : {hotkeyEnableIds[row], hotkeyInputIds[row]})
            if (std::none_of(settingsTooltips.begin(), settingsTooltips.end(),
                [=](const auto& entry) { return entry.id == id; }))
                settingsTooltips.push_back({id, std::wstring(commands[row].name)
                    + (id == hotkeyEnableIds[row]
                        ? L": enable this keyboard shortcut when you click OK."
                        : L": press the complete key combination; applies on OK, only while Notepad++ is active.")});
    }
    settingsTooltips.push_back({IDC_CONTEXT_SUBMENU,
        L"For both document and tab bar menus: checked places selected commands in an NppHistory submenu; unchecked places them between separator lines."});
    const HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog,
        GWLP_HINSTANCE));
    const HWND tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        dialog, nullptr, instance, nullptr);
    if (!tooltip)
        return;
    SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, 420);
    int added = 0;
    for (const auto& entry : settingsTooltips)
    {
        if (!isTooltipInputControl(GetDlgItem(dialog, entry.id)))
            continue;
        TOOLINFOW tool{sizeof(tool)};
        tool.uFlags = TTF_TRACK | TTF_ABSOLUTE;
        tool.hwnd = dialog;
        tool.uId = static_cast<UINT_PTR>(entry.id);
        tool.lpszText = const_cast<wchar_t*>(entry.text.c_str());
        if (SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool)))
            ++added;
    }
    SetPropW(dialog, L"NppHistorySettingsTooltipWindow", tooltip);
    SetPropW(dialog, L"NppHistorySettingsTooltipCount",
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(added)));
    SetTimer(dialog, settingsTooltipTimer, 100, nullptr);
}

void updateSettingsTooltip(HWND dialog)
{
    const HWND tooltip = static_cast<HWND>(GetPropW(dialog,
        L"NppHistorySettingsTooltipWindow"));
    if (!tooltip)
        return;
    POINT cursor{};
    GetCursorPos(&cursor);
    int hovered = 0;
    for (const auto& entry : settingsTooltips)
    {
        const HWND control = GetDlgItem(dialog, entry.id);
        RECT bounds{};
        if (isTooltipInputControl(control) && IsWindowVisible(control) && GetWindowRect(control, &bounds)
            && PtInRect(&bounds, cursor))
        {
            hovered = entry.id;
            break;
        }
    }
    const int previous = static_cast<int>(reinterpret_cast<INT_PTR>(
        GetPropW(dialog, L"NppHistorySettingsTooltipTarget")));
    if (hovered != previous)
    {
        if (previous)
        {
            TOOLINFOW oldTool{sizeof(oldTool)};
            oldTool.hwnd = dialog;
            oldTool.uId = static_cast<UINT_PTR>(previous);
            SendMessageW(tooltip, TTM_TRACKACTIVATE, FALSE,
                reinterpret_cast<LPARAM>(&oldTool));
        }
        SetPropW(dialog, L"NppHistorySettingsTooltipTarget",
            reinterpret_cast<HANDLE>(static_cast<INT_PTR>(hovered)));
        SetPropW(dialog, L"NppHistorySettingsTooltipStarted",
            reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(GetTickCount64())));
        RemovePropW(dialog, L"NppHistorySettingsTooltipActive");
    }
    const unsigned long long started = static_cast<unsigned long long>(
        reinterpret_cast<ULONG_PTR>(GetPropW(dialog, L"NppHistorySettingsTooltipStarted")));
    if (!hovered || GetTickCount64() - started < 400)
        return;
    RECT bounds{};
    GetWindowRect(GetDlgItem(dialog, hovered), &bounds);
    TOOLINFOW tool{sizeof(tool)};
    tool.hwnd = dialog;
    tool.uId = static_cast<UINT_PTR>(hovered);
    SendMessageW(tooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(bounds.left, bounds.bottom + 2));
    SendMessageW(tooltip, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&tool));
    SetPropW(dialog, L"NppHistorySettingsTooltipActive",
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(hovered)));
}

unsigned readNumber(HWND dialog, int control, unsigned minimum, unsigned fallback)
{
    BOOL translated = FALSE;
    const UINT value = GetDlgItemInt(dialog, control, &translated, FALSE);
    return translated ? (std::max)(minimum, value) : fallback;
}

bool readBoolean(const std::filesystem::path& file, const wchar_t* key, bool fallback)
{
    return GetPrivateProfileIntW(L"NppHistory", key, fallback ? 1 : 0, file.c_str()) != 0;
}

std::wstring readTextSetting(const std::filesystem::path& file, const wchar_t* key)
{
    std::wstring value(32768, L'\0');
    GetPrivateProfileStringW(L"NppHistory", key, L"", value.data(),
        static_cast<DWORD>(value.size()), file.c_str());
    value.resize(wcslen(value.c_str()));
    return value;
}

std::wstring decodePatternSetting(std::wstring value)
{
    std::wstring decoded;
    for (const wchar_t character : value)
        decoded += character == L'|' ? L"\r\n" : std::wstring(1, character);
    return decoded;
}

std::wstring encodePatternSetting(std::wstring_view value)
{
    std::wstring encoded;
    for (std::size_t index = 0; index < value.size(); ++index)
    {
        if (value[index] == L'\r')
            continue;
        encoded += value[index] == L'\n' ? L'|' : value[index];
    }
    return encoded;
}

std::wstring dialogText(HWND dialog, int control)
{
    const int length = GetWindowTextLengthW(GetDlgItem(dialog, control));
    std::wstring value(static_cast<std::size_t>((std::max)(0, length)) + 1, L'\0');
    GetDlgItemTextW(dialog, control, value.data(), static_cast<int>(value.size()));
    value.resize(wcslen(value.c_str()));
    return value;
}

unsigned long long readUnsigned64(const std::filesystem::path& file,
    const wchar_t* key, unsigned long long fallback)
{
    wchar_t text[32]{};
    GetPrivateProfileStringW(L"NppHistory", key, L"", text,
        static_cast<DWORD>(std::size(text)), file.c_str());
    if (!text[0])
        return fallback;
    wchar_t* end = nullptr;
    const unsigned long long value = _wcstoui64(text, &end, 10);
    return end && *end == L'\0' ? value : fallback;
}

bool ensureUnicodeIni(const std::filesystem::path& file)
{
    const auto existing = readAllBytes(file);
    if (existing.size() >= 2 && existing[0] == 0xFF && existing[1] == 0xFE)
        return true;
    const std::wstring text = decodeText(existing);
    std::vector<std::uint8_t> unicode{0xFF, 0xFE};
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.data());
    unicode.insert(unicode.end(), bytes, bytes + text.size() * sizeof(wchar_t));
    return writeAllBytesAtomic(file, unicode);
}

// Command row/control mappings are shared with the placement model.

void populateHotkeyControls(HWND dialog, int row, const HotkeySetting& hotkey)
{
    CheckDlgButton(dialog, hotkeyEnableIds[row], hotkey.enabled ? BST_CHECKED : BST_UNCHECKED);
    WORD modifiers = 0;
    if (hotkey.shift) modifiers |= HOTKEYF_SHIFT;
    if (hotkey.ctrl) modifiers |= HOTKEYF_CONTROL;
    if (hotkey.alt) modifiers |= HOTKEYF_ALT;
    SendDlgItemMessageW(dialog, hotkeyInputIds[row], HKM_SETHOTKEY,
        MAKEWORD(hotkey.key, modifiers), 0);
}

HotkeySetting hotkeyFromControls(HWND dialog, int row)
{
    HotkeySetting result;
    result.enabled = IsDlgButtonChecked(dialog, hotkeyEnableIds[row]) == BST_CHECKED;
    const WORD value = static_cast<WORD>(SendDlgItemMessageW(dialog,
        hotkeyInputIds[row], HKM_GETHOTKEY, 0, 0));
    result.key = LOBYTE(value);
    const BYTE modifiers = HIBYTE(value);
    result.ctrl = (modifiers & HOTKEYF_CONTROL) != 0;
    result.alt = (modifiers & HOTKEYF_ALT) != 0;
    result.shift = (modifiers & HOTKEYF_SHIFT) != 0;
    return result;
}

void updateHotkeyControls(HWND dialog)
{
    for (int row = 0; row < commandCount; ++row)
    {
        const BOOL enabled = IsDlgButtonChecked(dialog, hotkeyEnableIds[row]) == BST_CHECKED;
        EnableWindow(GetDlgItem(dialog, hotkeyInputIds[row]), enabled);
    }
}

std::wstring hotkeyText(const HotkeySetting& hotkey)
{
    return commandHotkeyText(hotkey);
}

struct MenuShortcut
{
    int command = 0;
    std::wstring text;
};

void collectMenuShortcuts(HMENU menu, std::vector<MenuShortcut>& shortcuts)
{
    const int count = GetMenuItemCount(menu);
    for (int index = 0; index < count; ++index)
    {
        if (const HMENU child = GetSubMenu(menu, index))
            collectMenuShortcuts(child, shortcuts);
        else
        {
            const UINT command = GetMenuItemID(menu, index);
            wchar_t label[512]{};
            GetMenuStringW(menu, index, label, static_cast<int>(std::size(label)),
                MF_BYPOSITION);
            const wchar_t* separator = wcschr(label, L'\t');
            if (command != static_cast<UINT>(-1) && command != 0 && separator
                && separator[1])
                shortcuts.push_back({static_cast<int>(command), separator + 1});
        }
    }
}

bool validateHotkeys(HWND dialog, const Settings& settings, bool showMessage)
{
    std::array<HotkeySetting, commandCount> selected{};
    for (int row = 0; row < commandCount; ++row) selected[row] = hotkeyFromControls(dialog, row);
    std::wstring issue;
    for (int row = 0; row < commandCount && issue.empty(); ++row)
    {
        if (!selected[row].enabled)
            continue;
        if (!selected[row].key)
            issue = std::wstring(commands[row].name) + L" needs a key.";
        else if (!safeCommandHotkey(selected[row]))
            issue = L"Use Ctrl/Alt with a key, or a function key; system shortcuts are reserved.";
        else if (!settings.liveHotkeysAvailable)
            issue = L"Live shortcuts are unavailable. Disable shortcuts or reopen Settings to retry.";
        for (int previous = 0; previous < row && issue.empty(); ++previous)
        {
            if (selected[previous].enabled
                && selected[previous].ctrl == selected[row].ctrl
                && selected[previous].alt == selected[row].alt
                && selected[previous].shift == selected[row].shift
                && selected[previous].key == selected[row].key)
                issue = hotkeyText(selected[row]) + L" is selected more than once.";
        }
    }

    HWND owner = GetWindow(dialog, GW_OWNER);
    if (issue.empty() && owner)
    {
        std::vector<MenuShortcut> shortcuts;
        collectMenuShortcuts(GetMenu(owner), shortcuts);
        const std::unordered_set<int> ownCommands(settings.hotkeyCommandIds.begin(),
            settings.hotkeyCommandIds.end());
        for (const auto& shortcut : shortcuts)
        {
            if (ownCommands.count(shortcut.command) != 0)
                continue;
            for (int row = 0; row < commandCount && issue.empty(); ++row)
            {
                if (selected[row].enabled
                    && _wcsicmp(shortcut.text.c_str(), hotkeyText(selected[row]).c_str()) == 0)
                    issue = hotkeyText(selected[row]) + L" is already assigned in Notepad++.";
            }
        }
    }
    SetDlgItemTextW(dialog, IDC_HOTKEY_STATUS,
        issue.empty() ? L"No hotkey conflicts." : issue.c_str());
    SetPropW(dialog, L"NppHistoryHotkeysValid",
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(issue.empty() ? 2 : 1)));
    InvalidateRect(GetDlgItem(dialog, IDC_HOTKEY_STATUS), nullptr, TRUE);
    if (!issue.empty() && showMessage)
        centeredMessageBox(GetWindow(dialog, GW_OWNER), issue.c_str(), L"NppHistory Hotkey Conflict",
            MB_OK | MB_ICONWARNING);
    return issue.empty();
}

void updateLocationControls(HWND dialog)
{
    const BOOL historyEnabled = IsDlgButtonChecked(dialog, IDC_HISTORY_ENABLED) == BST_CHECKED;
    const BOOL custom = historyEnabled
        && IsDlgButtonChecked(dialog, IDC_HISTORY_CUSTOM) == BST_CHECKED;
    EnableWindow(GetDlgItem(dialog, IDC_HISTORY_PATH), custom);
    EnableWindow(GetDlgItem(dialog, IDC_HISTORY_BROWSE), custom);
}

void updateHistoryControls(HWND dialog)
{
    const BOOL enabled = IsDlgButtonChecked(dialog, IDC_HISTORY_ENABLED) == BST_CHECKED;
    for (const int control : {IDC_HISTORY_CREATE_GROUP, IDC_HISTORY_BEFORE_SAVE,
        IDC_HISTORY_AFTER_SAVE, IDC_HISTORY_BEFORE_RESTORE, IDC_HISTORY_GROUP,
        IDC_HISTORY_ADJACENT, IDC_HISTORY_CUSTOM, IDC_HISTORY_EXCLUSIONS_GROUP,
        IDC_HISTORY_EXCLUSIONS, IDC_HISTORY_EXCLUSIONS_NOTE})
        EnableWindow(GetDlgItem(dialog, control), enabled);
    updateLocationControls(dialog);
}

void updateAutoSaveControls(HWND dialog)
{
    const BOOL available = !activeSettings || !activeSettings->externalAutoSavePluginDetected;
    const BOOL enabled = available
        && IsDlgButtonChecked(dialog, IDC_ENABLED) == BST_CHECKED;
    EnableWindow(GetDlgItem(dialog, IDC_ENABLED), available);
    EnableWindow(GetDlgItem(dialog, IDC_AFTER_EDIT), enabled);
    const BOOL afterEdit = enabled && IsDlgButtonChecked(dialog, IDC_AFTER_EDIT) == BST_CHECKED;
    EnableWindow(GetDlgItem(dialog, IDC_AFTER_EDIT_SECONDS), afterEdit);
    EnableWindow(GetDlgItem(dialog, IDC_AFTER_EDIT_LABEL), afterEdit);
    for (const int control : {IDC_AUTOSAVE_FOCUS_LOSS, IDC_AUTOSAVE_INTERVAL,
        IDC_AUTOSAVE_TAB_CHANGE, IDC_AUTOSAVE_EXIT, IDC_AUTOSAVE_CURRENT_FILE,
        IDC_AUTOSAVE_ALL_FILES, IDC_AUTOSAVE_EXCLUSIONS_GROUP,
        IDC_AUTOSAVE_EXCLUSIONS, IDC_AUTOSAVE_EXCLUSIONS_NOTE})
        EnableWindow(GetDlgItem(dialog, control), enabled);
    const BOOL interval = enabled
        && IsDlgButtonChecked(dialog, IDC_AUTOSAVE_INTERVAL) == BST_CHECKED;
    EnableWindow(GetDlgItem(dialog, IDC_AUTOSAVE_INTERVAL_MINUTES), interval);
    EnableWindow(GetDlgItem(dialog, IDC_INTERVAL_LABEL), interval);
}

void updateUpdateControls(HWND dialog)
{
    const BOOL enabled = IsDlgButtonChecked(dialog, IDC_AUTO_UPDATE) == BST_CHECKED;
    EnableWindow(GetDlgItem(dialog, IDC_UPDATE_FREQUENCY_LABEL), enabled);
    EnableWindow(GetDlgItem(dialog, IDC_UPDATE_FREQUENCY), enabled);
    EnableWindow(GetDlgItem(dialog, IDC_UPDATE_PRERELEASES), TRUE);
}

std::wstring updateStatusText(const Settings& settings)
{
    std::wstring result = L"Status: " + (settings.lastUpdateStatus.empty()
        ? (settings.lastUpdateCheck == 0 ? std::wstring(L"Never checked.")
            : std::wstring(L"Last check completed."))
        : settings.lastUpdateStatus);
    const auto localTime = [](unsigned long long timestamp) {
        std::wstring text;
        ULARGE_INTEGER value{};
        value.QuadPart = (timestamp + 11644473600ULL) * 10000000ULL;
        FILETIME utc{value.LowPart, value.HighPart}, local{};
        SYSTEMTIME time{};
        if (FileTimeToLocalFileTime(&utc, &local) && FileTimeToSystemTime(&local, &time))
        {
            wchar_t date[128]{}, clock[128]{};
            GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &time, nullptr,
                date, static_cast<int>(std::size(date)), nullptr);
            GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &time, nullptr,
                clock, static_cast<int>(std::size(clock)));
            text = std::wstring(date) + L" " + clock;
        }
        return text;
    };
    if (settings.lastUpdateCheck != 0)
    {
        const std::wstring display = localTime(settings.lastUpdateCheck);
        if (!display.empty())
            result += L"\r\n\r\nLast successful check: " + display;
    }
    if (!settings.autoUpdateEnabled)
        result += L"\r\n\r\nAutomatic checks are disabled.";
    else
    {
        const unsigned long long now = currentUnixSeconds();
        const unsigned long long next = settings.nextUpdateCheckTime(now);
        if (next <= now)
            result += L"\r\n\r\nNext automatic check: Due now.";
        else
        {
            const unsigned long long remaining = next - now;
            const auto unit = [](unsigned long long value, const wchar_t* singular,
                const wchar_t* plural) {
                return std::to_wstring(value) + L" " + (value == 1 ? singular : plural);
            };
            std::wstring duration;
            if (remaining >= 86400)
            {
                const unsigned long long days = remaining / 86400;
                const unsigned long long hours = (remaining % 86400) / 3600;
                duration = unit(days, L"day", L"days");
                if (hours != 0)
                    duration += L", " + unit(hours, L"hour", L"hours");
            }
            else if (remaining >= 3600)
            {
                const unsigned long long hours = remaining / 3600;
                const unsigned long long minutes = (remaining % 3600) / 60;
                duration = unit(hours, L"hour", L"hours");
                if (minutes != 0)
                    duration += L", " + unit(minutes, L"minute", L"minutes");
            }
            else
                duration = unit((remaining + 59) / 60, L"minute", L"minutes");
            const std::wstring display = localTime(next);
            result += L"\r\n\r\nNext automatic check: "
                + (display.empty() ? std::wstring() : display + L" ")
                + L"(in " + duration + L")";
        }
    }
    return result;
}

std::filesystem::path selectedLogPath(HWND dialog, const Settings& settings)
{
    if (IsDlgButtonChecked(dialog, IDC_LOGGING_CUSTOM) != BST_CHECKED)
        return settings.defaultLogFile;
    wchar_t path[32768]{};
    GetDlgItemTextW(dialog, IDC_LOGGING_PATH, path, static_cast<int>(std::size(path)));
    return path;
}

void updateLoggingControls(HWND dialog, const Settings& settings)
{
    const BOOL enabled = IsDlgButtonChecked(dialog, IDC_LOGGING_ENABLED) == BST_CHECKED;
    const BOOL custom = enabled
        && IsDlgButtonChecked(dialog, IDC_LOGGING_CUSTOM) == BST_CHECKED;
    for (const int control : {IDC_LOGGING_LEVEL_GROUP, IDC_LOGGING_LEVEL_LABEL,
        IDC_LOGGING_LEVEL, IDC_LOGGING_FILE_GROUP, IDC_LOGGING_DEFAULT,
        IDC_LOGGING_CUSTOM, IDC_LOGGING_ROTATION_GROUP, IDC_LOGGING_MAX_SIZE_LABEL,
        IDC_LOGGING_MAX_SIZE, IDC_LOGGING_MAX_SIZE_UNIT, IDC_LOGGING_ROLLOVER_LABEL,
        IDC_LOGGING_ROLLOVER})
        EnableWindow(GetDlgItem(dialog, control), enabled);
    EnableWindow(GetDlgItem(dialog, IDC_LOGGING_PATH), custom);
    EnableWindow(GetDlgItem(dialog, IDC_LOGGING_BROWSE), custom);
    EnableWindow(GetDlgItem(dialog, IDC_LOGGING_OPEN), TRUE);
    const bool archives = enabled && SendDlgItemMessageW(dialog, IDC_LOGGING_ROLLOVER,
        CB_GETCURSEL, 0, 0) == 1;
    EnableWindow(GetDlgItem(dialog, IDC_LOGGING_ARCHIVES_LABEL), archives);
    EnableWindow(GetDlgItem(dialog, IDC_LOGGING_ARCHIVES), archives);
    const std::filesystem::path effective = selectedLogPath(dialog, settings);
    SetDlgItemTextW(dialog, IDC_LOGGING_EFFECTIVE_PATH,
        effective.empty() ? L"No log file selected" : effective.c_str());
}

void showSettingsPage(HWND dialog, int page)
{
    std::vector<int> general{IDC_GENERAL_TOOLBAR_GROUP, IDC_TOOLBAR_DESCRIPTION,
        IDC_CONTEXT_GROUP, IDC_CONTEXT_SUBMENU, IDC_CONTEXT_NOTE, IDC_HOTKEY_NOTE, IDC_HOTKEY_STATUS};
    for (int row = 0; row < commandCount; ++row)
    {
        for (int column : {0, 1, 2, 4}) general.push_back(placementControl(row, column));
        general.push_back(toolbarControlIds[row]);
        general.push_back(hotkeyEnableIds[row]);
        general.push_back(hotkeyInputIds[row]);
    }
    for (int id = 1270; id <= 1275; ++id) general.push_back(id);
    const int autoSave[] = {IDC_ENABLED, IDC_AUTOSAVE_WHEN_GROUP, IDC_AFTER_EDIT,
        IDC_AFTER_EDIT_SECONDS, IDC_AFTER_EDIT_LABEL, IDC_AUTOSAVE_FOCUS_LOSS,
        IDC_AUTOSAVE_INTERVAL, IDC_AUTOSAVE_INTERVAL_MINUTES, IDC_INTERVAL_LABEL,
        IDC_AUTOSAVE_TAB_CHANGE, IDC_AUTOSAVE_EXIT, IDC_AUTOSAVE_WHAT_GROUP,
        IDC_AUTOSAVE_CURRENT_FILE, IDC_AUTOSAVE_ALL_FILES,
        IDC_AUTOSAVE_EXCLUSIONS_GROUP, IDC_AUTOSAVE_EXCLUSIONS,
        IDC_AUTOSAVE_EXCLUSIONS_NOTE, IDC_AUTOSAVE_CONFLICT_NOTICE};
    const int history[] = {IDC_HISTORY_ENABLED, IDC_HISTORY_CREATE_GROUP,
        IDC_HISTORY_BEFORE_SAVE, IDC_HISTORY_AFTER_SAVE, IDC_HISTORY_BEFORE_RESTORE,
        IDC_HISTORY_GROUP, IDC_HISTORY_ADJACENT, IDC_HISTORY_CUSTOM,
        IDC_HISTORY_PATH, IDC_HISTORY_BROWSE, IDC_HISTORY_EXCLUSIONS_GROUP,
        IDC_HISTORY_EXCLUSIONS, IDC_HISTORY_EXCLUSIONS_NOTE};
    const int logging[] = {IDC_LOGGING_ENABLED, IDC_LOGGING_LEVEL_GROUP,
        IDC_LOGGING_LEVEL_LABEL, IDC_LOGGING_LEVEL, IDC_LOGGING_FILE_GROUP,
        IDC_LOGGING_DEFAULT, IDC_LOGGING_CUSTOM, IDC_LOGGING_PATH,
        IDC_LOGGING_BROWSE, IDC_LOGGING_OPEN, IDC_LOGGING_EFFECTIVE_PATH,
        IDC_LOGGING_ROTATION_GROUP, IDC_LOGGING_MAX_SIZE_LABEL, IDC_LOGGING_MAX_SIZE,
        IDC_LOGGING_MAX_SIZE_UNIT, IDC_LOGGING_ROLLOVER_LABEL, IDC_LOGGING_ROLLOVER,
        IDC_LOGGING_ARCHIVES_LABEL, IDC_LOGGING_ARCHIVES};
    const int updates[] = {IDC_GENERAL_UPDATE_GROUP, IDC_AUTO_UPDATE,
        IDC_UPDATE_FREQUENCY_LABEL, IDC_UPDATE_FREQUENCY, IDC_UPDATE_PRERELEASES,
        IDC_UPDATE_CHECK_NOW, IDC_UPDATE_STATUS_GROUP, IDC_UPDATE_STATUS};
    const auto setVisibility = [&](const int* controls, std::size_t count, bool visible)
    {
        for (std::size_t index = 0; index < count; ++index)
            ShowWindow(GetDlgItem(dialog, controls[index]), visible ? SW_SHOW : SW_HIDE);
    };
    setVisibility(general.data(), general.size(), page == 0);
    setVisibility(autoSave, std::size(autoSave), page == 1);
    ShowWindow(GetDlgItem(dialog, IDC_AUTOSAVE_CONFLICT_NOTICE),
        page == 1 && activeSettings && activeSettings->externalAutoSavePluginDetected
            ? SW_SHOW : SW_HIDE);
    setVisibility(history, std::size(history), page == 2);
    setVisibility(logging, std::size(logging), page == 3);
    setVisibility(updates, std::size(updates), page == 4);
}

void browseForHistoryRoot(HWND dialog)
{
    BROWSEINFOW browse{};
    browse.hwndOwner = GetWindow(dialog, GW_OWNER);
    browse.lpszTitle = L"Choose the NppHistory storage folder";
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&browse);
    if (!item)
        return;
    wchar_t path[MAX_PATH]{};
    if (SHGetPathFromIDListW(item, path))
        SetDlgItemTextW(dialog, IDC_HISTORY_PATH, path);
    CoTaskMemFree(item);
}

void browseForLogFile(HWND dialog)
{
    wchar_t path[32768]{};
    GetDlgItemTextW(dialog, IDC_LOGGING_PATH, path, static_cast<int>(std::size(path)));
    OPENFILENAMEW options{sizeof(options)};
    options.hwndOwner = GetWindow(dialog, GW_OWNER);
    options.lpstrFilter = L"Log files (*.log)\0*.log\0All files (*.*)\0*.*\0";
    options.lpstrFile = path;
    options.nMaxFile = static_cast<DWORD>(std::size(path));
    options.lpstrDefExt = L"log";
    options.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameW(&options))
        SetDlgItemTextW(dialog, IDC_LOGGING_PATH, path);
}

const wchar_t* settingsCommandName(int id)
{
    static std::wstring commandName;
    for (int row = 0; row < commandCount; ++row)
    {
        const wchar_t* surfaces[] = {L"History Pane", L"Tab bar context menu", L"Toolbar", L"Document context menu"};
        for (int column = 0; column < 4; ++column)
        {
            const int control = column == 2 ? toolbarControlIds[row] : placementControl(row, column + 1);
            if (id == control)
            {
                commandName = std::wstring(surfaces[column]) + L": " + commands[row].name;
                return commandName.c_str();
            }
        }
        if (id == hotkeyEnableIds[row] || id == hotkeyInputIds[row])
        {
            commandName = std::wstring(L"Hotkey: ") + commands[row].name;
            return commandName.c_str();
        }
    }
    switch (id)
    {
    case IDC_CONTEXT_SUBMENU: return L"Document context menu: Group in submenu";
    case IDC_TOOLBAR_CAPTURE: return L"Toolbar: Capture";
    case IDC_TOOLBAR_COMPARE: return L"Toolbar: Compare";
    case IDC_TOOLBAR_HISTORY: return L"Toolbar: History";
    case IDC_HOTKEY_CAPTURE_ENABLED: return L"Hotkey: Capture enabled";
    case IDC_HOTKEY_COMPARE_ENABLED: return L"Hotkey: Compare enabled";
    case IDC_HOTKEY_HISTORY_ENABLED: return L"Hotkey: History enabled";
    case IDC_ENABLED: return L"Auto Save enabled";
    case IDC_AFTER_EDIT: return L"Auto Save: After editing stops";
    case IDC_AUTOSAVE_FOCUS_LOSS: return L"Auto Save: Notepad++ loses focus";
    case IDC_AUTOSAVE_INTERVAL: return L"Auto Save: Timed intervals";
    case IDC_AUTOSAVE_TAB_CHANGE: return L"Auto Save: File tab changes";
    case IDC_AUTOSAVE_EXIT: return L"Auto Save: Notepad++ exits";
    case IDC_AUTOSAVE_CURRENT_FILE: return L"Auto Save scope: Current file";
    case IDC_AUTOSAVE_ALL_FILES: return L"Auto Save scope: All open files";
    case IDC_HISTORY_ENABLED: return L"Revision history enabled";
    case IDC_HISTORY_BEFORE_SAVE: return L"History: Before save";
    case IDC_HISTORY_AFTER_SAVE: return L"History: After save";
    case IDC_HISTORY_BEFORE_RESTORE: return L"History: Before restore";
    case IDC_HISTORY_ADJACENT: return L"History location: Adjacent folder";
    case IDC_HISTORY_CUSTOM: return L"History location: Common folder";
    case IDC_HISTORY_BROWSE: return L"Browse history location";
    case IDC_LOGGING_ENABLED: return L"Plugin logging enabled";
    case IDC_LOGGING_LEVEL: return L"Logging level";
    case IDC_LOGGING_DEFAULT: return L"Log location: Plugin configuration folder";
    case IDC_LOGGING_CUSTOM: return L"Log location: Custom file";
    case IDC_LOGGING_BROWSE: return L"Browse log location";
    case IDC_LOGGING_OPEN: return L"Open Log";
    case IDC_LOGGING_ROLLOVER: return L"Log rollover mode";
    case IDC_AUTO_UPDATE: return L"Automatic update checks enabled";
    case IDC_UPDATE_FREQUENCY: return L"Update frequency";
    case IDC_UPDATE_PRERELEASES: return L"Include prerelease versions";
    case IDC_UPDATE_CHECK_NOW: return L"Check for updates now";
    case IDOK: return L"OK";
    case IDCANCEL: return L"Cancel";
    default: return nullptr;
    }
}

bool isUserSettingsCommand(int id, int notification)
{
    if (id == IDOK || id == IDCANCEL)
        return notification == BN_CLICKED;
    if (notification == BN_CLICKED)
        return settingsCommandName(id) != nullptr;
    if (notification == CBN_SELCHANGE)
        return id == IDC_LOGGING_LEVEL || id == IDC_LOGGING_ROLLOVER
            || id == IDC_UPDATE_FREQUENCY;
    return false;
}

INT_PTR CALLBACK settingsProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* settings = reinterpret_cast<Settings*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        settings = reinterpret_cast<Settings*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        activeSettingsDialog = dialog;
        activeSettings = settings;
        const HICON icon = LoadIconW(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog,
            GWLP_HINSTANCE)), MAKEINTRESOURCEW(IDI_NPPHISTORY));
        SendMessageW(dialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        SendMessageW(dialog, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));

        const HWND tabs = GetDlgItem(dialog, IDC_SETTINGS_TABS);
        for (const wchar_t* label : {L"Commands && Hotkeys", L"Auto Save", L"History", L"Logging", L"Updates"})
        {
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<wchar_t*>(label);
            TabCtrl_InsertItem(tabs, TabCtrl_GetItemCount(tabs), &item);
        }
        TabCtrl_SetCurSel(tabs, 0);

        // Keep persisted/control identities stable while presenting the shared order.
        HWND previousCommandControl = GetDlgItem(dialog, 1275);
        for (int position = 0; position < commandCount; ++position)
        {
            const int row = static_cast<int>(commandOrder[position]);
            for (const int id : {placementControl(row, 0), placementControl(row, 1),
                placementControl(row, 2), toolbarControlIds[row], placementControl(row, 4),
                hotkeyEnableIds[row], hotkeyInputIds[row]})
            {
                const HWND control = GetDlgItem(dialog, id);
                RECT bounds{};
                GetWindowRect(control, &bounds);
                MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&bounds), 2);
                RECT offset{0, (position - row) * 17, 0, 0};
                MapDialogRect(dialog, &offset);
                SetWindowPos(control, previousCommandControl, bounds.left, bounds.top + offset.top,
                    0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
                previousCommandControl = control;
            }
        }

        const COLORREF pageColour = GetSysColor(COLOR_WINDOW);
        SetPropW(dialog, L"NppHistorySettingsPageBrush",
            CreateSolidBrush(pageColour));
        SetPropW(dialog, L"NppHistorySettingsPageColour",
            reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(pageColour) + 1));

        for (int row = 0; row < commandCount; ++row)
        {
            const auto command = static_cast<Command>(row);
            for (int column = 0; column < 4; ++column)
            {
                const auto surface = placementSurfaces[column];
                const int id = column == 2 ? toolbarControlIds[row] : placementControl(row, column + 1);
                CheckDlgButton(dialog, id, settings->commandVisible(command, surface) ? BST_CHECKED : BST_UNCHECKED);
                EnableWindow(GetDlgItem(dialog, id), !placementLocked(command, surface));
            }
            populateHotkeyControls(dialog, row, settings->commandHotkey(command));
        }
        CheckDlgButton(dialog, IDC_CONTEXT_SUBMENU, settings->contextSubmenu ? BST_CHECKED : BST_UNCHECKED);
        updateHotkeyControls(dialog);
        validateHotkeys(dialog, *settings, false);
        CheckDlgButton(dialog, IDC_AUTO_UPDATE,
            settings->autoUpdateEnabled ? BST_CHECKED : BST_UNCHECKED);
        const HWND frequency = GetDlgItem(dialog, IDC_UPDATE_FREQUENCY);
        for (const wchar_t* label : {L"Daily", L"Weekly", L"Monthly"})
            SendMessageW(frequency, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessageW(frequency, CB_SETCURSEL, static_cast<int>(settings->updateFrequency), 0);
        CheckDlgButton(dialog, IDC_UPDATE_PRERELEASES,
            settings->includePrereleaseUpdates ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemTextW(dialog, IDC_UPDATE_STATUS, updateStatusText(*settings).c_str());
        updateUpdateControls(dialog);

        CheckDlgButton(dialog, IDC_LOGGING_ENABLED,
            settings->loggingEnabled ? BST_CHECKED : BST_UNCHECKED);
        const HWND logLevel = GetDlgItem(dialog, IDC_LOGGING_LEVEL);
        for (const wchar_t* label : {L"Errors", L"Warnings", L"Informational", L"Debug"})
            SendMessageW(logLevel, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessageW(logLevel, CB_SETCURSEL, static_cast<int>(settings->logLevel), 0);
        CheckRadioButton(dialog, IDC_LOGGING_DEFAULT, IDC_LOGGING_CUSTOM,
            settings->logLocationMode == LogLocationMode::pluginConfig
                ? IDC_LOGGING_DEFAULT : IDC_LOGGING_CUSTOM);
        SetDlgItemTextW(dialog, IDC_LOGGING_PATH, settings->customLogFile.c_str());
        SetDlgItemInt(dialog, IDC_LOGGING_MAX_SIZE, settings->logMaximumSizeMb, FALSE);
        const HWND rollover = GetDlgItem(dialog, IDC_LOGGING_ROLLOVER);
        for (const wchar_t* label : {L"Overwrite log", L"Start new archive"})
            SendMessageW(rollover, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessageW(rollover, CB_SETCURSEL,
            static_cast<int>(settings->logRolloverMode), 0);
        SetDlgItemInt(dialog, IDC_LOGGING_ARCHIVES, settings->logArchivesToRetain, FALSE);
        updateLoggingControls(dialog, *settings);

        CheckDlgButton(dialog, IDC_ENABLED,
            settings->autoSaveEnabled && !settings->externalAutoSavePluginDetected
                ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_AFTER_EDIT,
            settings->autoSaveAfterEdit ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemInt(dialog, IDC_AFTER_EDIT_SECONDS, settings->afterEditSeconds, FALSE);
        CheckDlgButton(dialog, IDC_AUTOSAVE_FOCUS_LOSS,
            settings->autoSaveOnFocusLoss ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_AUTOSAVE_INTERVAL,
            settings->autoSaveAtIntervals ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemInt(dialog, IDC_AUTOSAVE_INTERVAL_MINUTES, settings->intervalMinutes, FALSE);
        CheckDlgButton(dialog, IDC_AUTOSAVE_TAB_CHANGE,
            settings->autoSaveOnTabChange ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_AUTOSAVE_EXIT,
            settings->autoSaveOnExit ? BST_CHECKED : BST_UNCHECKED);
        CheckRadioButton(dialog, IDC_AUTOSAVE_CURRENT_FILE, IDC_AUTOSAVE_ALL_FILES,
            settings->autoSaveScope == AutoSaveScope::currentFile
                ? IDC_AUTOSAVE_CURRENT_FILE : IDC_AUTOSAVE_ALL_FILES);
        SetDlgItemTextW(dialog, IDC_AUTOSAVE_EXCLUSIONS,
            settings->autoSaveExclusions.c_str());

        CheckDlgButton(dialog, IDC_HISTORY_ENABLED,
            settings->historyEnabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_HISTORY_BEFORE_SAVE,
            settings->historyBeforeSave ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_HISTORY_AFTER_SAVE,
            settings->historyAfterSave ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_HISTORY_BEFORE_RESTORE,
            settings->historyBeforeRestore ? BST_CHECKED : BST_UNCHECKED);
        CheckRadioButton(dialog, IDC_HISTORY_ADJACENT, IDC_HISTORY_CUSTOM,
            settings->historyLocationMode == HistoryLocationMode::adjacent
                ? IDC_HISTORY_ADJACENT : IDC_HISTORY_CUSTOM);
        SetDlgItemTextW(dialog, IDC_HISTORY_PATH, settings->customHistoryRoot.c_str());
        SetDlgItemTextW(dialog, IDC_HISTORY_EXCLUSIONS,
            settings->historyExclusions.c_str());
        updateHistoryControls(dialog);
        updateAutoSaveControls(dialog);
        showSettingsPage(dialog, 0);
        configureSettingsTooltips(dialog);
        centerWindowOnOwner(dialog, GetParent(dialog));
        return TRUE;
    }
    if (message == WM_TIMER && wParam == settingsTooltipTimer)
    {
        updateSettingsTooltip(dialog);
        return TRUE;
    }
    if (message == WM_COMMAND && isUserSettingsCommand(LOWORD(wParam), HIWORD(wParam)))
    {
        pluginLogger().write(LogLevel::debug, L"Settings control",
            settingsCommandName(LOWORD(wParam)));
    }
    if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(lParam)->idFrom == IDC_SETTINGS_TABS
        && reinterpret_cast<NMHDR*>(lParam)->code == TCN_SELCHANGE)
    {
        const int page = TabCtrl_GetCurSel(GetDlgItem(dialog, IDC_SETTINGS_TABS));
        showSettingsPage(dialog, page);
        const wchar_t* pages[] = {L"Commands & Hotkeys", L"Auto Save", L"History",
            L"Logging", L"Updates"};
        if (page >= 0 && page < static_cast<int>(std::size(pages)))
            pluginLogger().write(LogLevel::debug, L"Settings tab", pages[page]);
        return TRUE;
    }
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN)
    {
        const HDC dc = reinterpret_cast<HDC>(wParam);
        const HWND control = reinterpret_cast<HWND>(lParam);
        const int id = GetDlgCtrlID(control);
        if (id == IDC_AUTOSAVE_CONFLICT_NOTICE
            || (id == IDC_HOTKEY_STATUS && reinterpret_cast<INT_PTR>(
                GetPropW(dialog, L"NppHistoryHotkeysValid")) == 1))
        {
            SetTextColor(dc, RGB(200, 0, 0));
            SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<INT_PTR>(GetPropW(dialog,
                L"NppHistorySettingsPageBrush"));
        }
        if (id != IDOK && id != IDCANCEL)
        {
            const COLORREF pageColour = static_cast<COLORREF>(
                reinterpret_cast<ULONG_PTR>(GetPropW(dialog,
                    L"NppHistorySettingsPageColour")) - 1);
            SetBkMode(dc, TRANSPARENT);
            SetBkColor(dc, pageColour);
            return reinterpret_cast<INT_PTR>(GetPropW(dialog,
                L"NppHistorySettingsPageBrush"));
        }
    }
    if (message == WM_DESTROY)
    {
        KillTimer(dialog, settingsTooltipTimer);
        RemovePropW(dialog, L"NppHistorySettingsTooltipWindow");
        RemovePropW(dialog, L"NppHistorySettingsTooltipCount");
        RemovePropW(dialog, L"NppHistorySettingsTooltipTarget");
        RemovePropW(dialog, L"NppHistorySettingsTooltipStarted");
        RemovePropW(dialog, L"NppHistorySettingsTooltipActive");
        RemovePropW(dialog, L"NppHistoryHotkeysValid");
        if (activeSettingsDialog == dialog)
        {
            activeSettingsDialog = nullptr;
            activeSettings = nullptr;
        }
        DeleteObject(RemovePropW(dialog, L"NppHistorySettingsPageBrush"));
        RemovePropW(dialog, L"NppHistorySettingsPageColour");
    }
    if (message == WM_COMMAND && (LOWORD(wParam) == IDC_HISTORY_ENABLED
        || LOWORD(wParam) == IDC_HISTORY_ADJACENT
        || LOWORD(wParam) == IDC_HISTORY_CUSTOM))
    {
        updateHistoryControls(dialog);
        return TRUE;
    }
    if (message == WM_COMMAND && (LOWORD(wParam) == IDC_ENABLED
        || LOWORD(wParam) == IDC_AFTER_EDIT || LOWORD(wParam) == IDC_AUTOSAVE_INTERVAL))
    {
        updateAutoSaveControls(dialog);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDC_AUTO_UPDATE)
    {
        updateUpdateControls(dialog);
        return TRUE;
    }
    if (message == WM_COMMAND
        && (std::find(hotkeyEnableIds.begin(), hotkeyEnableIds.end(), LOWORD(wParam)) != hotkeyEnableIds.end()
            || std::find(hotkeyInputIds.begin(), hotkeyInputIds.end(), LOWORD(wParam)) != hotkeyInputIds.end()))
    {
        updateHotkeyControls(dialog);
        validateHotkeys(dialog, *settings, false);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDC_UPDATE_CHECK_NOW)
    {
        settings->refreshUpdateStatus(true);
        const bool includePrereleases = IsDlgButtonChecked(dialog,
            IDC_UPDATE_PRERELEASES) == BST_CHECKED;
        PostMessageW(GetWindow(dialog, GW_OWNER), settingsCheckUpdateMessage,
            includePrereleases ? TRUE : FALSE, 0);
        return TRUE;
    }
    if (message == WM_COMMAND && (LOWORD(wParam) == IDC_LOGGING_ENABLED
        || LOWORD(wParam) == IDC_LOGGING_DEFAULT || LOWORD(wParam) == IDC_LOGGING_CUSTOM
        || (LOWORD(wParam) == IDC_LOGGING_ROLLOVER && HIWORD(wParam) == CBN_SELCHANGE)
        || (LOWORD(wParam) == IDC_LOGGING_PATH && HIWORD(wParam) == EN_CHANGE)))
    {
        updateLoggingControls(dialog, *settings);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDC_LOGGING_BROWSE)
    {
        browseForLogFile(dialog);
        updateLoggingControls(dialog, *settings);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDC_LOGGING_OPEN)
    {
        settings->openLogNow = true;
        SendMessageW(dialog, WM_COMMAND, IDOK, 0);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDC_HISTORY_BROWSE)
    {
        browseForHistoryRoot(dialog);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDOK)
    {
        if (!validateHotkeys(dialog, *settings, true))
        {
            TabCtrl_SetCurSel(GetDlgItem(dialog, IDC_SETTINGS_TABS), 0);
            showSettingsPage(dialog, 0);
            return TRUE;
        }
        for (int row = 0; row < commandCount; ++row)
        {
            const auto command = static_cast<Command>(row);
            for (int column = 0; column < 4; ++column)
            {
                const int id = column == 2 ? toolbarControlIds[row] : placementControl(row, column + 1);
                settings->setCommandVisible(command, placementSurfaces[column],
                    IsDlgButtonChecked(dialog, id) == BST_CHECKED);
            }
            settings->commandHotkey(command) = hotkeyFromControls(dialog, row);
        }
        settings->contextSubmenu = IsDlgButtonChecked(dialog, IDC_CONTEXT_SUBMENU) == BST_CHECKED;
        settings->autoUpdateEnabled = IsDlgButtonChecked(dialog, IDC_AUTO_UPDATE) == BST_CHECKED;
        const LRESULT frequency = SendDlgItemMessageW(dialog, IDC_UPDATE_FREQUENCY,
            CB_GETCURSEL, 0, 0);
        settings->updateFrequency = static_cast<UpdateFrequency>(
            frequency >= 0 && frequency <= 2 ? frequency : 1);
        settings->includePrereleaseUpdates = IsDlgButtonChecked(dialog,
            IDC_UPDATE_PRERELEASES) == BST_CHECKED;

        settings->loggingEnabled = IsDlgButtonChecked(dialog,
            IDC_LOGGING_ENABLED) == BST_CHECKED;
        const LRESULT logLevel = SendDlgItemMessageW(dialog, IDC_LOGGING_LEVEL,
            CB_GETCURSEL, 0, 0);
        settings->logLevel = static_cast<LogLevel>(logLevel >= 0 && logLevel <= 3
            ? logLevel : 2);
        settings->logLocationMode = IsDlgButtonChecked(dialog,
            IDC_LOGGING_CUSTOM) == BST_CHECKED
            ? LogLocationMode::customFile : LogLocationMode::pluginConfig;
        wchar_t logPath[32768]{};
        GetDlgItemTextW(dialog, IDC_LOGGING_PATH, logPath,
            static_cast<int>(std::size(logPath)));
        settings->customLogFile = logPath;
        settings->logMaximumSizeMb = (std::min)(1024U,
            readNumber(dialog, IDC_LOGGING_MAX_SIZE, 1, settings->logMaximumSizeMb));
        const LRESULT rollover = SendDlgItemMessageW(dialog, IDC_LOGGING_ROLLOVER,
            CB_GETCURSEL, 0, 0);
        settings->logRolloverMode = rollover == 0
            ? LogRolloverMode::overwrite : LogRolloverMode::archive;
        settings->logArchivesToRetain = (std::min)(100U,
            readNumber(dialog, IDC_LOGGING_ARCHIVES, 0, settings->logArchivesToRetain));

        // The conflict UI deliberately appears unchecked. Preserve the configured preference
        // while AutoSave.dll is present so it can resume if that plugin is later removed.
        if (!settings->externalAutoSavePluginDetected)
            settings->autoSaveEnabled = IsDlgButtonChecked(dialog, IDC_ENABLED) == BST_CHECKED;
        settings->autoSaveAfterEdit = IsDlgButtonChecked(dialog, IDC_AFTER_EDIT) == BST_CHECKED;
        settings->afterEditSeconds = readNumber(dialog, IDC_AFTER_EDIT_SECONDS, 10,
            settings->afterEditSeconds);
        settings->autoSaveOnFocusLoss = IsDlgButtonChecked(dialog,
            IDC_AUTOSAVE_FOCUS_LOSS) == BST_CHECKED;
        settings->autoSaveAtIntervals = IsDlgButtonChecked(dialog,
            IDC_AUTOSAVE_INTERVAL) == BST_CHECKED;
        settings->intervalMinutes = readNumber(dialog, IDC_AUTOSAVE_INTERVAL_MINUTES, 1,
            settings->intervalMinutes);
        settings->autoSaveOnTabChange = IsDlgButtonChecked(dialog,
            IDC_AUTOSAVE_TAB_CHANGE) == BST_CHECKED;
        settings->autoSaveOnExit = IsDlgButtonChecked(dialog, IDC_AUTOSAVE_EXIT) == BST_CHECKED;
        settings->autoSaveScope = IsDlgButtonChecked(dialog,
            IDC_AUTOSAVE_CURRENT_FILE) == BST_CHECKED
            ? AutoSaveScope::currentFile : AutoSaveScope::allOpenFiles;
        settings->autoSaveExclusions = dialogText(dialog, IDC_AUTOSAVE_EXCLUSIONS);

        settings->historyEnabled = IsDlgButtonChecked(dialog,
            IDC_HISTORY_ENABLED) == BST_CHECKED;
        settings->historyBeforeSave = IsDlgButtonChecked(dialog,
            IDC_HISTORY_BEFORE_SAVE) == BST_CHECKED;
        settings->historyAfterSave = IsDlgButtonChecked(dialog,
            IDC_HISTORY_AFTER_SAVE) == BST_CHECKED;
        settings->historyBeforeRestore = IsDlgButtonChecked(dialog,
            IDC_HISTORY_BEFORE_RESTORE) == BST_CHECKED;
        settings->historyLocationMode = IsDlgButtonChecked(dialog, IDC_HISTORY_CUSTOM) == BST_CHECKED
            ? HistoryLocationMode::customRoot : HistoryLocationMode::adjacent;
        wchar_t path[32768]{};
        GetDlgItemTextW(dialog, IDC_HISTORY_PATH, path, static_cast<int>(std::size(path)));
        settings->customHistoryRoot = path;
        settings->historyExclusions = dialogText(dialog, IDC_HISTORY_EXCLUSIONS);
        if (settings->historyEnabled
            && settings->historyLocationMode == HistoryLocationMode::customRoot
            && settings->customHistoryRoot.empty())
        {
            TabCtrl_SetCurSel(GetDlgItem(dialog, IDC_SETTINGS_TABS), 2);
            showSettingsPage(dialog, 2);
            centeredMessageBox(GetWindow(dialog, GW_OWNER), L"Choose a folder for custom history storage.", L"NppHistory",
                MB_OK | MB_ICONWARNING);
            return TRUE;
        }
        if (settings->loggingEnabled
            && settings->logLocationMode == LogLocationMode::customFile
            && settings->customLogFile.empty())
        {
            TabCtrl_SetCurSel(GetDlgItem(dialog, IDC_SETTINGS_TABS), 3);
            showSettingsPage(dialog, 3);
            centeredMessageBox(GetWindow(dialog, GW_OWNER), L"Choose a custom log file, or use the Notepad++ plugin configuration folder.",
                L"NppHistory", MB_OK | MB_ICONWARNING);
            return TRUE;
        }
        EndDialog(dialog, IDOK);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL)
    {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}
}

std::wstring autoSaveScopeDisplayName(AutoSaveScope value)
{
    return value == AutoSaveScope::currentFile ? L"Current file only" : L"All open files";
}

std::wstring updateFrequencyDisplayName(UpdateFrequency value)
{
    switch (value)
    {
    case UpdateFrequency::daily: return L"Daily";
    case UpdateFrequency::weekly: return L"Weekly";
    case UpdateFrequency::monthly: return L"Monthly";
    }
    return L"Unknown";
}

std::wstring logLevelDisplayName(LogLevel value)
{
    switch (value)
    {
    case LogLevel::critical: return L"Critical";
    case LogLevel::error: return L"Error";
    case LogLevel::warning: return L"Warning";
    case LogLevel::informational: return L"Informational";
    case LogLevel::debug: return L"Debug";
    }
    return L"Unknown";
}

std::wstring logLocationDisplayName(LogLocationMode value)
{
    return value == LogLocationMode::pluginConfig
        ? L"Notepad++ plugin configuration folder" : L"Custom file";
}

std::wstring logRolloverDisplayName(LogRolloverMode value)
{
    return value == LogRolloverMode::overwrite
        ? L"Overwrite current log" : L"Create archives";
}

std::wstring historyLocationDisplayName(HistoryLocationMode value)
{
    return value == HistoryLocationMode::adjacent
        ? L"Hidden .npphistory folder beside each file" : L"Common folder";
}

bool Settings::shouldAutoSave(AutoSaveTrigger trigger) const noexcept
{
    if (!autoSaveEnabled || externalAutoSavePluginDetected)
        return false;
    switch (trigger)
    {
    case AutoSaveTrigger::afterEdit: return autoSaveAfterEdit;
    case AutoSaveTrigger::focusLoss: return autoSaveOnFocusLoss;
    case AutoSaveTrigger::timedInterval: return autoSaveAtIntervals;
    case AutoSaveTrigger::tabChange: return autoSaveOnTabChange;
    case AutoSaveTrigger::exit: return autoSaveOnExit;
    }
    return false;
}

bool Settings::shouldCreateRevision(RevisionTrigger trigger) const noexcept
{
    if (!historyEnabled)
        return false;
    switch (trigger)
    {
    case RevisionTrigger::beforeSave: return historyBeforeSave;
    case RevisionTrigger::afterSave: return historyAfterSave;
    case RevisionTrigger::beforeRestore: return historyBeforeRestore;
    case RevisionTrigger::manual: return true;
    }
    return false;
}

bool Settings::isAutoSaveExcluded(const std::filesystem::path& path) const
{
    return !path.empty() && pathMatchesWildcardList(path, autoSaveExclusions);
}

bool Settings::isHistoryExcluded(const std::filesystem::path& path) const
{
    return !path.empty() && pathMatchesWildcardList(path, historyExclusions);
}

bool Settings::afterEditDue(unsigned long long now, unsigned long long lastEdit) const noexcept
{
    return shouldAutoSave(AutoSaveTrigger::afterEdit) && now >= lastEdit
        && now - lastEdit >= static_cast<unsigned long long>(afterEditSeconds) * 1000ULL;
}

bool Settings::intervalDue(unsigned long long now, unsigned long long lastInterval) const noexcept
{
    return shouldAutoSave(AutoSaveTrigger::timedInterval) && now >= lastInterval
        && now - lastInterval >= static_cast<unsigned long long>(intervalMinutes) * 60ULL * 1000ULL;
}

bool Settings::updateCheckDue(unsigned long long nowSeconds) const noexcept
{
    if (!autoUpdateEnabled)
        return false;
    if (nextUpdateRetry != 0 && nowSeconds < nextUpdateRetry)
        return false;
    const unsigned days = updateFrequency == UpdateFrequency::daily ? 1
        : updateFrequency == UpdateFrequency::weekly ? 7 : 30;
    return elapsedFrequencyDue(nowSeconds, lastUpdateCheck, days);
}

unsigned long long Settings::nextUpdateCheckTime(unsigned long long nowSeconds) const noexcept
{
    if (!autoUpdateEnabled)
        return 0;
    const unsigned long long days = updateFrequency == UpdateFrequency::daily ? 1ULL
        : updateFrequency == UpdateFrequency::weekly ? 7ULL : 30ULL;
    const unsigned long long scheduled = lastUpdateCheck == 0
        ? nowSeconds : lastUpdateCheck + days * 86400ULL;
    if (scheduled > nowSeconds)
        return scheduled;
    return nextUpdateRetry > nowSeconds ? nextUpdateRetry : nowSeconds;
}

void Settings::recordUpdateSuccess(unsigned long long nowSeconds) noexcept
{
    lastUpdateCheck = nowSeconds;
    lastUpdateAttempt = nowSeconds;
    nextUpdateRetry = 0;
    updateFailureCount = 0;
}

void Settings::recordUpdateFailure(unsigned long long nowSeconds) noexcept
{
    lastUpdateAttempt = nowSeconds;
    updateFailureCount = (std::min)(updateFailureCount + 1U, 3U);
    const unsigned long long delay = updateFailureCount == 1 ? 15ULL * 60ULL
        : updateFailureCount == 2 ? 60ULL * 60ULL : 6ULL * 60ULL * 60ULL;
    nextUpdateRetry = nowSeconds + delay;
}

void Settings::load(const std::filesystem::path& file)
{
    autoSaveEnabled = readBoolean(file, L"AutoSaveEnabled",
        readBoolean(file, L"Enabled", true));
    const int legacyMode = GetPrivateProfileIntW(L"NppHistory", L"Mode", 0, file.c_str());
    autoSaveAfterEdit = readBoolean(file, L"AutoSaveAfterEdit", legacyMode != 1);
    afterEditSeconds = (std::max)(10U, static_cast<unsigned>(GetPrivateProfileIntW(
        L"NppHistory", L"AfterEditSeconds", 30, file.c_str())));
    autoSaveOnFocusLoss = readBoolean(file, L"AutoSaveOnFocusLoss", false);
    autoSaveAtIntervals = readBoolean(file, L"AutoSaveAtIntervals", legacyMode == 1);
    const unsigned legacyInterval = (std::max)(1U, static_cast<unsigned>(
        GetPrivateProfileIntW(L"NppHistory", L"PeriodicSeconds", 600, file.c_str())) / 60U);
    intervalMinutes = (std::max)(1U, static_cast<unsigned>(GetPrivateProfileIntW(
        L"NppHistory", L"IntervalMinutes", legacyInterval, file.c_str())));
    autoSaveOnTabChange = readBoolean(file, L"AutoSaveOnTabChange", false);
    autoSaveOnExit = readBoolean(file, L"AutoSaveOnExit", false);
    autoSaveScope = GetPrivateProfileIntW(L"NppHistory", L"AutoSaveScope", 1,
        file.c_str()) == 0 ? AutoSaveScope::currentFile : AutoSaveScope::allOpenFiles;
    autoSaveExclusions = decodePatternSetting(readTextSetting(file, L"AutoSaveExclusions"));
    toolbarCapture = readBoolean(file, L"ToolbarCapture", false);
    toolbarCompare = readBoolean(file, L"ToolbarCompare", false);
    toolbarHistory = readBoolean(file, L"ToolbarHistory",
        readBoolean(file, L"ToolbarRestore", false));
    const auto readHotkey = [&](const wchar_t* prefix, unsigned fallbackKey) {
        HotkeySetting hotkey;
        const std::wstring base(prefix);
        hotkey.enabled = readBoolean(file, (base + L"Enabled").c_str(), false);
        hotkey.ctrl = readBoolean(file, (base + L"Ctrl").c_str(), true);
        hotkey.alt = readBoolean(file, (base + L"Alt").c_str(), true);
        hotkey.shift = readBoolean(file, (base + L"Shift").c_str(), false);
        const int key = GetPrivateProfileIntW(L"NppHistory", (base + L"Key").c_str(),
            static_cast<int>(fallbackKey), file.c_str());
        hotkey.key = key >= 0 && key <= 255 ? static_cast<unsigned>(key) : fallbackKey;
        return hotkey;
    };
    hotkeyCapture = readHotkey(L"HotkeyCapture", 'C');
    hotkeyCompare = readHotkey(L"HotkeyCompare", 'M');
    hotkeyHistory = readHotkey(L"HotkeyHistory", 'H');
    contextSubmenu = readBoolean(file, L"ContextSubmenu", true);
    for (int row = 0; row < commandCount; ++row)
    {
        const auto command = static_cast<Command>(row);
        const std::wstring name(commands[row].name);
        for (int column = 0; column < 5; ++column)
        {
            const wchar_t* prefixes[] = {L"Pane", L"PluginMenu", L"Toolbar", L"Context", L"TabContext"};
            const auto surface = static_cast<CommandSurface>(column);
            const bool fallback = column == 0 || column == 1 ? true
                : column == 2 ? commandVisible(command, surface) : false;
            setCommandVisible(command, surface, readBoolean(file,
                (prefixes[column] + name).c_str(), fallback));
        }
        if (row >= 2 && row <= 5)
            commandHotkey(command) = readHotkey((L"Hotkey" + name).c_str(), 0);
    }
    autoUpdateEnabled = readBoolean(file, L"AutoUpdateEnabled", false);
    const int frequency = GetPrivateProfileIntW(L"NppHistory", L"UpdateFrequency", 1,
        file.c_str());
    updateFrequency = static_cast<UpdateFrequency>((std::max)(0, (std::min)(2, frequency)));
    includePrereleaseUpdates = readBoolean(file, L"IncludePrereleaseUpdates", true);
    lastUpdateCheck = readUnsigned64(file, L"LastUpdateCheck", 0);
    lastUpdateAttempt = readUnsigned64(file, L"LastUpdateAttempt", 0);
    nextUpdateRetry = readUnsigned64(file, L"NextUpdateRetry", 0);
    const int loadedFailures = GetPrivateProfileIntW(L"NppHistory", L"UpdateFailureCount", 0,
        file.c_str());
    updateFailureCount = static_cast<unsigned>((std::max)(0, (std::min)(3, loadedFailures)));
    std::wstring notified(128, L'\0');
    GetPrivateProfileStringW(L"NppHistory", L"LastNotifiedVersion", L"", notified.data(),
        static_cast<DWORD>(notified.size()), file.c_str());
    notified.resize(wcslen(notified.c_str()));
    lastNotifiedVersion = std::move(notified);
    std::wstring updateStatus(1024, L'\0');
    GetPrivateProfileStringW(L"NppHistory", L"LastUpdateStatus", L"",
        updateStatus.data(), static_cast<DWORD>(updateStatus.size()), file.c_str());
    updateStatus.resize(wcslen(updateStatus.c_str()));
    lastUpdateStatus = std::move(updateStatus);
    loggingEnabled = readBoolean(file, L"LoggingEnabled", false);
    const int loadedLogLevel = GetPrivateProfileIntW(L"NppHistory", L"LogLevel", 2,
        file.c_str());
    logLevel = static_cast<LogLevel>((std::max)(0, (std::min)(3, loadedLogLevel)));
    logLocationMode = GetPrivateProfileIntW(L"NppHistory", L"LogLocationMode", 0,
        file.c_str()) == 1 ? LogLocationMode::customFile : LogLocationMode::pluginConfig;
    std::wstring logPath(32768, L'\0');
    GetPrivateProfileStringW(L"NppHistory", L"CustomLogFile", L"", logPath.data(),
        static_cast<DWORD>(logPath.size()), file.c_str());
    logPath.resize(wcslen(logPath.c_str()));
    customLogFile = logPath;
    const int loadedMaximum = GetPrivateProfileIntW(L"NppHistory", L"LogMaximumSizeMb", 5,
        file.c_str());
    logMaximumSizeMb = static_cast<unsigned>((std::max)(1, (std::min)(1024, loadedMaximum)));
    logRolloverMode = GetPrivateProfileIntW(L"NppHistory", L"LogRolloverMode", 1,
        file.c_str()) == 0 ? LogRolloverMode::overwrite : LogRolloverMode::archive;
    const int loadedArchives = GetPrivateProfileIntW(L"NppHistory",
        L"LogArchivesToRetain", 5, file.c_str());
    logArchivesToRetain = static_cast<unsigned>((std::max)(0, (std::min)(100, loadedArchives)));
    defaultLogFile.clear();
    openLogNow = false;
    historyEnabled = readBoolean(file, L"HistoryEnabled", true);
    historyBeforeSave = readBoolean(file, L"HistoryBeforeSave", true);
    historyAfterSave = readBoolean(file, L"HistoryAfterSave", true);
    historyBeforeRestore = readBoolean(file, L"HistoryBeforeRestore", true);
    historyLocationMode = GetPrivateProfileIntW(L"NppHistory", L"HistoryLocationMode", 0,
        file.c_str()) == 1 ? HistoryLocationMode::customRoot : HistoryLocationMode::adjacent;
    std::wstring path(32768, L'\0');
    GetPrivateProfileStringW(L"NppHistory", L"CustomHistoryRoot", L"", path.data(),
        static_cast<DWORD>(path.size()), file.c_str());
    path.resize(wcslen(path.c_str()));
    customHistoryRoot = path;
    historyExclusions = decodePatternSetting(readTextSetting(file, L"HistoryExclusions"));
}

bool Settings::save(const std::filesystem::path& file) const
{
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    if (error || !ensureUnicodeIni(file))
        return false;
    const auto write = [&](const wchar_t* key, const std::wstring& value) {
        return WritePrivateProfileStringW(L"NppHistory", key, value.c_str(), file.c_str()) != FALSE;
    };
    if (!write(L"ContextSubmenu", contextSubmenu ? L"1" : L"0")) return false;
    for (int row = 0; row < commandCount; ++row)
    {
        const auto command = static_cast<Command>(row);
        const std::wstring name(commands[row].name);
        const wchar_t* prefixes[] = {L"Pane", L"PluginMenu", L"Toolbar", L"Context", L"TabContext"};
        for (int column = 0; column < 5; ++column)
            if (!write((prefixes[column] + name).c_str(),
                commandVisible(command, static_cast<CommandSurface>(column)) ? L"1" : L"0")) return false;
        const auto& key = commandHotkey(command);
        const std::wstring prefix = L"Hotkey" + name;
        if (!write((prefix + L"Enabled").c_str(), key.enabled ? L"1" : L"0")
            || !write((prefix + L"Ctrl").c_str(), key.ctrl ? L"1" : L"0")
            || !write((prefix + L"Alt").c_str(), key.alt ? L"1" : L"0")
            || !write((prefix + L"Shift").c_str(), key.shift ? L"1" : L"0")
            || !write((prefix + L"Key").c_str(), std::to_wstring(key.key))) return false;
    }
    return write(L"AutoSaveEnabled", autoSaveEnabled ? L"1" : L"0")
        && write(L"AutoSaveAfterEdit", autoSaveAfterEdit ? L"1" : L"0")
        && write(L"AfterEditSeconds", std::to_wstring(afterEditSeconds))
        && write(L"AutoSaveOnFocusLoss", autoSaveOnFocusLoss ? L"1" : L"0")
        && write(L"AutoSaveAtIntervals", autoSaveAtIntervals ? L"1" : L"0")
        && write(L"IntervalMinutes", std::to_wstring(intervalMinutes))
        && write(L"AutoSaveOnTabChange", autoSaveOnTabChange ? L"1" : L"0")
        && write(L"AutoSaveOnExit", autoSaveOnExit ? L"1" : L"0")
        && write(L"AutoSaveScope", autoSaveScope == AutoSaveScope::currentFile ? L"0" : L"1")
        && write(L"AutoSaveExclusions", encodePatternSetting(autoSaveExclusions))
        && write(L"ToolbarCapture", toolbarCapture ? L"1" : L"0")
        && write(L"ToolbarCompare", toolbarCompare ? L"1" : L"0")
        && write(L"ToolbarHistory", toolbarHistory ? L"1" : L"0")
        && write(L"HotkeyCaptureEnabled", hotkeyCapture.enabled ? L"1" : L"0")
        && write(L"HotkeyCaptureCtrl", hotkeyCapture.ctrl ? L"1" : L"0")
        && write(L"HotkeyCaptureAlt", hotkeyCapture.alt ? L"1" : L"0")
        && write(L"HotkeyCaptureShift", hotkeyCapture.shift ? L"1" : L"0")
        && write(L"HotkeyCaptureKey", std::to_wstring(hotkeyCapture.key))
        && write(L"HotkeyCompareEnabled", hotkeyCompare.enabled ? L"1" : L"0")
        && write(L"HotkeyCompareCtrl", hotkeyCompare.ctrl ? L"1" : L"0")
        && write(L"HotkeyCompareAlt", hotkeyCompare.alt ? L"1" : L"0")
        && write(L"HotkeyCompareShift", hotkeyCompare.shift ? L"1" : L"0")
        && write(L"HotkeyCompareKey", std::to_wstring(hotkeyCompare.key))
        && write(L"HotkeyHistoryEnabled", hotkeyHistory.enabled ? L"1" : L"0")
        && write(L"HotkeyHistoryCtrl", hotkeyHistory.ctrl ? L"1" : L"0")
        && write(L"HotkeyHistoryAlt", hotkeyHistory.alt ? L"1" : L"0")
        && write(L"HotkeyHistoryShift", hotkeyHistory.shift ? L"1" : L"0")
        && write(L"HotkeyHistoryKey", std::to_wstring(hotkeyHistory.key))
        && write(L"AutoUpdateEnabled", autoUpdateEnabled ? L"1" : L"0")
        && write(L"UpdateFrequency", std::to_wstring(static_cast<int>(updateFrequency)))
        && write(L"IncludePrereleaseUpdates", includePrereleaseUpdates ? L"1" : L"0")
        && write(L"LastUpdateCheck", std::to_wstring(lastUpdateCheck))
        && write(L"LastUpdateAttempt", std::to_wstring(lastUpdateAttempt))
        && write(L"NextUpdateRetry", std::to_wstring(nextUpdateRetry))
        && write(L"UpdateFailureCount", std::to_wstring(updateFailureCount))
        && write(L"LastNotifiedVersion", lastNotifiedVersion)
        && write(L"LastUpdateStatus", lastUpdateStatus)
        && write(L"LoggingEnabled", loggingEnabled ? L"1" : L"0")
        && write(L"LogLevel", std::to_wstring(static_cast<int>(logLevel)))
        && write(L"LogLocationMode", logLocationMode == LogLocationMode::customFile ? L"1" : L"0")
        && write(L"CustomLogFile", customLogFile.wstring())
        && write(L"LogMaximumSizeMb", std::to_wstring(logMaximumSizeMb))
        && write(L"LogRolloverMode", logRolloverMode == LogRolloverMode::overwrite ? L"0" : L"1")
        && write(L"LogArchivesToRetain", std::to_wstring(logArchivesToRetain))
        && write(L"HistoryEnabled", historyEnabled ? L"1" : L"0")
        && write(L"HistoryBeforeSave", historyBeforeSave ? L"1" : L"0")
        && write(L"HistoryAfterSave", historyAfterSave ? L"1" : L"0")
        && write(L"HistoryBeforeRestore", historyBeforeRestore ? L"1" : L"0")
        && write(L"HistoryLocationMode", historyLocationMode == HistoryLocationMode::customRoot ? L"1" : L"0")
        && write(L"CustomHistoryRoot", customHistoryRoot.wstring())
        && write(L"HistoryExclusions", encodePatternSetting(historyExclusions));
}

bool Settings::commandVisible(Command command, CommandSurface surface) const noexcept
{
    if (placementLocked(command, surface)) return surface == CommandSurface::plugins;
    const auto& placement = commandPlacement[static_cast<int>(command)];
    switch (surface)
    {
    case CommandSurface::pane: return placement.pane;
    case CommandSurface::plugins: return placement.plugins;
    case CommandSurface::context: return placement.context;
    case CommandSurface::tabContext: return placement.tabContext;
    case CommandSurface::toolbar:
        if (command == Command::capture) return toolbarCapture;
        if (command == Command::compare) return toolbarCompare;
        if (command == Command::history) return toolbarHistory;
        return placement.toolbar;
    }
    return false;
}

void Settings::setCommandVisible(Command command, CommandSurface surface, bool visible) noexcept
{
    if (placementLocked(command, surface)) return;
    auto& placement = commandPlacement[static_cast<int>(command)];
    switch (surface)
    {
    case CommandSurface::pane: placement.pane = visible; break;
    case CommandSurface::plugins: placement.plugins = visible; break;
    case CommandSurface::context: placement.context = visible; break;
    case CommandSurface::tabContext: placement.tabContext = visible; break;
    case CommandSurface::toolbar:
        if (command == Command::capture) toolbarCapture = visible;
        else if (command == Command::compare) toolbarCompare = visible;
        else if (command == Command::history) toolbarHistory = visible;
        else placement.toolbar = visible;
        break;
    }
}

HotkeySetting& Settings::commandHotkey(Command command) noexcept
{
    if (command == Command::capture) return hotkeyCapture;
    if (command == Command::compare) return hotkeyCompare;
    if (command == Command::history) return hotkeyHistory;
    return additionalHotkeys[static_cast<int>(command) - 2];
}

const HotkeySetting& Settings::commandHotkey(Command command) const noexcept
{
    return const_cast<Settings*>(this)->commandHotkey(command);
}

bool Settings::edit(HWND owner, HINSTANCE instance)
{
    Settings edited = *this;
    edited.openLogNow = false;
    edited.installUpdateNow = false;
    if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), owner, settingsProc,
        reinterpret_cast<LPARAM>(&edited)) != IDOK)
        return false;
    *this = edited;
    return true;
}

void Settings::refreshUpdateStatus(bool checking) const
{
    if (!activeSettingsDialog || !IsWindow(activeSettingsDialog))
        return;
    if (!checking && activeSettings && activeSettings != this)
    {
        activeSettings->lastUpdateCheck = lastUpdateCheck;
        activeSettings->lastUpdateAttempt = lastUpdateAttempt;
        activeSettings->nextUpdateRetry = nextUpdateRetry;
        activeSettings->updateFailureCount = updateFailureCount;
        activeSettings->lastUpdateStatus = lastUpdateStatus;
        activeSettings->lastNotifiedVersion = lastNotifiedVersion;
    }
    SetDlgItemTextW(activeSettingsDialog, IDC_UPDATE_STATUS,
        checking ? L"Status: Checking..." : updateStatusText(*this).c_str());
    EnableWindow(GetDlgItem(activeSettingsDialog, IDC_UPDATE_CHECK_NOW), !checking);
}

HWND Settings::activeDialogWindow() const noexcept
{
    return activeSettingsDialog && IsWindow(activeSettingsDialog)
        ? activeSettingsDialog : nullptr;
}

bool Settings::closeForUpdateInstall() const
{
    if (!activeSettingsDialog || !IsWindow(activeSettingsDialog) || !activeSettings)
        return false;
    activeSettings->installUpdateNow = true;
    SendMessageW(activeSettingsDialog, WM_COMMAND, IDOK, 0);
    return true;
}
}
