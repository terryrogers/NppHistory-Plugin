#include "Settings.h"
#include "resource.h"
#include "Utilities.h"
#include "UpdateChecker.h"

#include <algorithm>
#include <commctrl.h>
#include <cwchar>
#include <shlobj.h>
#include <string>

namespace npphistory
{
namespace
{
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
        IDC_HISTORY_ADJACENT, IDC_HISTORY_CUSTOM})
        EnableWindow(GetDlgItem(dialog, control), enabled);
    updateLocationControls(dialog);
}

void updateAutoSaveControls(HWND dialog)
{
    const BOOL enabled = IsDlgButtonChecked(dialog, IDC_ENABLED) == BST_CHECKED;
    EnableWindow(GetDlgItem(dialog, IDC_AFTER_EDIT), enabled);
    const BOOL afterEdit = enabled && IsDlgButtonChecked(dialog, IDC_AFTER_EDIT) == BST_CHECKED;
    EnableWindow(GetDlgItem(dialog, IDC_AFTER_EDIT_SECONDS), afterEdit);
    EnableWindow(GetDlgItem(dialog, IDC_AFTER_EDIT_LABEL), afterEdit);
    for (const int control : {IDC_AUTOSAVE_FOCUS_LOSS, IDC_AUTOSAVE_INTERVAL,
        IDC_AUTOSAVE_TAB_CHANGE, IDC_AUTOSAVE_EXIT, IDC_AUTOSAVE_CURRENT_FILE,
        IDC_AUTOSAVE_ALL_FILES})
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

void showSettingsPage(HWND dialog, int page)
{
    const int general[] = {IDC_GENERAL_TOOLBAR_GROUP, IDC_TOOLBAR_CAPTURE,
        IDC_TOOLBAR_COMPARE, IDC_TOOLBAR_RESTORE, IDC_TOOLBAR_DESCRIPTION,
        IDC_TOOLBAR_RESTART_NOTE, IDC_GENERAL_UPDATE_GROUP, IDC_AUTO_UPDATE,
        IDC_UPDATE_FREQUENCY_LABEL, IDC_UPDATE_FREQUENCY, IDC_UPDATE_PRERELEASES,
        IDC_UPDATE_CHECK_NOW};
    const int autoSave[] = {IDC_ENABLED, IDC_AUTOSAVE_WHEN_GROUP, IDC_AFTER_EDIT,
        IDC_AFTER_EDIT_SECONDS, IDC_AFTER_EDIT_LABEL, IDC_AUTOSAVE_FOCUS_LOSS,
        IDC_AUTOSAVE_INTERVAL, IDC_AUTOSAVE_INTERVAL_MINUTES, IDC_INTERVAL_LABEL,
        IDC_AUTOSAVE_TAB_CHANGE, IDC_AUTOSAVE_EXIT, IDC_AUTOSAVE_WHAT_GROUP,
        IDC_AUTOSAVE_CURRENT_FILE, IDC_AUTOSAVE_ALL_FILES};
    const int history[] = {IDC_HISTORY_ENABLED, IDC_HISTORY_CREATE_GROUP,
        IDC_HISTORY_BEFORE_SAVE, IDC_HISTORY_AFTER_SAVE, IDC_HISTORY_BEFORE_RESTORE,
        IDC_HISTORY_GROUP, IDC_HISTORY_ADJACENT, IDC_HISTORY_CUSTOM,
        IDC_HISTORY_PATH, IDC_HISTORY_BROWSE};
    const auto setVisibility = [&](const int* controls, std::size_t count, bool visible)
    {
        for (std::size_t index = 0; index < count; ++index)
            ShowWindow(GetDlgItem(dialog, controls[index]), visible ? SW_SHOW : SW_HIDE);
    };
    setVisibility(general, std::size(general), page == 0);
    setVisibility(autoSave, std::size(autoSave), page == 1);
    setVisibility(history, std::size(history), page == 2);
}

void browseForHistoryRoot(HWND dialog)
{
    BROWSEINFOW browse{};
    browse.hwndOwner = dialog;
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

INT_PTR CALLBACK settingsProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* settings = reinterpret_cast<Settings*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        settings = reinterpret_cast<Settings*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        const HICON icon = LoadIconW(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog,
            GWLP_HINSTANCE)), MAKEINTRESOURCEW(IDI_NPPHISTORY));
        SendMessageW(dialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        SendMessageW(dialog, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));

        const HWND tabs = GetDlgItem(dialog, IDC_SETTINGS_TABS);
        for (const wchar_t* label : {L"General", L"Auto Save", L"History"})
        {
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<wchar_t*>(label);
            TabCtrl_InsertItem(tabs, TabCtrl_GetItemCount(tabs), &item);
        }
        TabCtrl_SetCurSel(tabs, 0);

        const COLORREF pageColour = GetSysColor(COLOR_WINDOW);
        SetPropW(dialog, L"NppHistorySettingsPageBrush",
            CreateSolidBrush(pageColour));
        SetPropW(dialog, L"NppHistorySettingsPageColour",
            reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(pageColour) + 1));

        CheckDlgButton(dialog, IDC_TOOLBAR_CAPTURE,
            settings->toolbarCapture ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_TOOLBAR_COMPARE,
            settings->toolbarCompare ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_TOOLBAR_RESTORE,
            settings->toolbarRestore ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_AUTO_UPDATE,
            settings->autoUpdateEnabled ? BST_CHECKED : BST_UNCHECKED);
        const HWND frequency = GetDlgItem(dialog, IDC_UPDATE_FREQUENCY);
        for (const wchar_t* label : {L"Daily", L"Weekly", L"Monthly"})
            SendMessageW(frequency, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessageW(frequency, CB_SETCURSEL, static_cast<int>(settings->updateFrequency), 0);
        CheckDlgButton(dialog, IDC_UPDATE_PRERELEASES,
            settings->includePrereleaseUpdates ? BST_CHECKED : BST_UNCHECKED);
        updateUpdateControls(dialog);

        CheckDlgButton(dialog, IDC_ENABLED,
            settings->autoSaveEnabled ? BST_CHECKED : BST_UNCHECKED);
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
        updateHistoryControls(dialog);
        updateAutoSaveControls(dialog);
        showSettingsPage(dialog, 0);
        centerWindowOnOwner(dialog, GetParent(dialog));
        return TRUE;
    }
    if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(lParam)->idFrom == IDC_SETTINGS_TABS
        && reinterpret_cast<NMHDR*>(lParam)->code == TCN_SELCHANGE)
    {
        showSettingsPage(dialog, TabCtrl_GetCurSel(GetDlgItem(dialog, IDC_SETTINGS_TABS)));
        return TRUE;
    }
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN)
    {
        const HDC dc = reinterpret_cast<HDC>(wParam);
        const HWND control = reinterpret_cast<HWND>(lParam);
        const int id = GetDlgCtrlID(control);
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
    if (message == WM_COMMAND && LOWORD(wParam) == IDC_UPDATE_CHECK_NOW)
    {
        settings->checkForUpdatesNow = true;
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
        settings->toolbarCapture = IsDlgButtonChecked(dialog, IDC_TOOLBAR_CAPTURE) == BST_CHECKED;
        settings->toolbarCompare = IsDlgButtonChecked(dialog, IDC_TOOLBAR_COMPARE) == BST_CHECKED;
        settings->toolbarRestore = IsDlgButtonChecked(dialog, IDC_TOOLBAR_RESTORE) == BST_CHECKED;
        settings->autoUpdateEnabled = IsDlgButtonChecked(dialog, IDC_AUTO_UPDATE) == BST_CHECKED;
        const LRESULT frequency = SendDlgItemMessageW(dialog, IDC_UPDATE_FREQUENCY,
            CB_GETCURSEL, 0, 0);
        settings->updateFrequency = static_cast<UpdateFrequency>(
            frequency >= 0 && frequency <= 2 ? frequency : 1);
        settings->includePrereleaseUpdates = IsDlgButtonChecked(dialog,
            IDC_UPDATE_PRERELEASES) == BST_CHECKED;

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
        if (settings->historyEnabled
            && settings->historyLocationMode == HistoryLocationMode::customRoot
            && settings->customHistoryRoot.empty())
        {
            TabCtrl_SetCurSel(GetDlgItem(dialog, IDC_SETTINGS_TABS), 2);
            showSettingsPage(dialog, 2);
            MessageBoxW(dialog, L"Choose a folder for custom history storage.", L"NppHistory",
                MB_OK | MB_ICONWARNING);
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

bool Settings::shouldAutoSave(AutoSaveTrigger trigger) const noexcept
{
    if (!autoSaveEnabled)
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
    const unsigned days = updateFrequency == UpdateFrequency::daily ? 1
        : updateFrequency == UpdateFrequency::weekly ? 7 : 30;
    return elapsedFrequencyDue(nowSeconds, lastUpdateCheck, days);
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
    toolbarCapture = readBoolean(file, L"ToolbarCapture", false);
    toolbarCompare = readBoolean(file, L"ToolbarCompare", false);
    toolbarRestore = readBoolean(file, L"ToolbarRestore", false);
    autoUpdateEnabled = readBoolean(file, L"AutoUpdateEnabled", false);
    const int frequency = GetPrivateProfileIntW(L"NppHistory", L"UpdateFrequency", 1,
        file.c_str());
    updateFrequency = static_cast<UpdateFrequency>((std::max)(0, (std::min)(2, frequency)));
    includePrereleaseUpdates = readBoolean(file, L"IncludePrereleaseUpdates", true);
    lastUpdateCheck = readUnsigned64(file, L"LastUpdateCheck", 0);
    std::wstring notified(128, L'\0');
    GetPrivateProfileStringW(L"NppHistory", L"LastNotifiedVersion", L"", notified.data(),
        static_cast<DWORD>(notified.size()), file.c_str());
    notified.resize(wcslen(notified.c_str()));
    lastNotifiedVersion = std::move(notified);
    checkForUpdatesNow = false;
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
    return write(L"AutoSaveEnabled", autoSaveEnabled ? L"1" : L"0")
        && write(L"AutoSaveAfterEdit", autoSaveAfterEdit ? L"1" : L"0")
        && write(L"AfterEditSeconds", std::to_wstring(afterEditSeconds))
        && write(L"AutoSaveOnFocusLoss", autoSaveOnFocusLoss ? L"1" : L"0")
        && write(L"AutoSaveAtIntervals", autoSaveAtIntervals ? L"1" : L"0")
        && write(L"IntervalMinutes", std::to_wstring(intervalMinutes))
        && write(L"AutoSaveOnTabChange", autoSaveOnTabChange ? L"1" : L"0")
        && write(L"AutoSaveOnExit", autoSaveOnExit ? L"1" : L"0")
        && write(L"AutoSaveScope", autoSaveScope == AutoSaveScope::currentFile ? L"0" : L"1")
        && write(L"ToolbarCapture", toolbarCapture ? L"1" : L"0")
        && write(L"ToolbarCompare", toolbarCompare ? L"1" : L"0")
        && write(L"ToolbarRestore", toolbarRestore ? L"1" : L"0")
        && write(L"AutoUpdateEnabled", autoUpdateEnabled ? L"1" : L"0")
        && write(L"UpdateFrequency", std::to_wstring(static_cast<int>(updateFrequency)))
        && write(L"IncludePrereleaseUpdates", includePrereleaseUpdates ? L"1" : L"0")
        && write(L"LastUpdateCheck", std::to_wstring(lastUpdateCheck))
        && write(L"LastNotifiedVersion", lastNotifiedVersion)
        && write(L"HistoryEnabled", historyEnabled ? L"1" : L"0")
        && write(L"HistoryBeforeSave", historyBeforeSave ? L"1" : L"0")
        && write(L"HistoryAfterSave", historyAfterSave ? L"1" : L"0")
        && write(L"HistoryBeforeRestore", historyBeforeRestore ? L"1" : L"0")
        && write(L"HistoryLocationMode", historyLocationMode == HistoryLocationMode::customRoot ? L"1" : L"0")
        && write(L"CustomHistoryRoot", customHistoryRoot.wstring());
}

bool Settings::edit(HWND owner, HINSTANCE instance)
{
    Settings edited = *this;
    edited.checkForUpdatesNow = false;
    if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), owner, settingsProc,
        reinterpret_cast<LPARAM>(&edited)) != IDOK)
        return false;
    *this = edited;
    return true;
}
}
