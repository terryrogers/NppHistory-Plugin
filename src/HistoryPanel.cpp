#include "HistoryPanel.h"
#include "Logger.h"
#include "TemporaryStatusBar.h"
#include "DockingFeature/Docking.h"
#include "TextDiff.h"
#include "Utilities.h"
#include "resource.h"

#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>

namespace npphistory
{
namespace
{
constexpr UINT comparisonWheelMessage = WM_APP + 42;
constexpr UINT comparisonMarkerClickMessage = WM_APP + 43;
constexpr UINT_PTR panelTooltipTimer = 0x4E50;
constexpr UINT dockingNotificationFirst = 1050;
constexpr UINT dockingClose = dockingNotificationFirst + 1;
constexpr UINT dockingSwitchIn = dockingNotificationFirst + 4;
constexpr UINT dockingSwitchOff = dockingNotificationFirst + 5;
const wchar_t* toolbarHint(int image);

HBITMAP createMenuBitmap(HINSTANCE instance, int resource)
{
    const HICON icon = static_cast<HICON>(LoadImageW(instance,
        MAKEINTRESOURCEW(resource), IMAGE_ICON, 16, 16, LR_SHARED));
    if (!icon)
        return nullptr;
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, 16, 16);
    const HGDIOBJ previous = SelectObject(memory, bitmap);
    RECT bounds{0, 0, 16, 16};
    FillRect(memory, &bounds, GetSysColorBrush(COLOR_MENU));
    DrawIconEx(memory, 0, 0, icon, 16, 16, 0, nullptr, DI_NORMAL);
    SelectObject(memory, previous);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return bitmap;
}

void applyMenuBitmap(HMENU menu, UINT command, HBITMAP bitmap)
{
    MENUITEMINFOW item{sizeof(item)};
    item.fMask = MIIM_BITMAP;
    item.hbmpItem = bitmap;
    SetMenuItemInfoW(menu, command, FALSE, &item);
}

int toolbarCommandAtPoint(HWND toolbar, POINT point)
{
    const int count = static_cast<int>(SendMessageW(toolbar, TB_BUTTONCOUNT, 0, 0));
    for (int index = 0; index < count; ++index)
    {
        RECT bounds{};
        TBBUTTON button{};
        if (SendMessageW(toolbar, TB_GETITEMRECT, index,
                reinterpret_cast<LPARAM>(&bounds))
            && PtInRect(&bounds, point)
            && SendMessageW(toolbar, TB_GETBUTTON, index,
                reinterpret_cast<LPARAM>(&button)))
        {
            return button.fsStyle & BTNS_SEP ? 0 : button.idCommand;
        }
    }
    return 0;
}

void hideToolbarTooltip(HWND toolbar)
{
    const HWND tooltip = static_cast<HWND>(GetPropW(toolbar, L"NppHistoryHoverTooltip"));
    if (tooltip) ShowWindow(tooltip, SW_HIDE);
}

void showToolbarTooltip(HWND toolbar, int command)
{
    const HWND tooltip = static_cast<HWND>(GetPropW(toolbar, L"NppHistoryHoverTooltip"));
    if (!tooltip || command < ID_COMPARE_TOOL_FIRST || command > ID_COMPARE_TOOL_LAST)
        return;
    const wchar_t* text = toolbarHint(command - ID_COMPARE_TOOL_FIRST);
    SetWindowTextW(tooltip, text);
    HDC dc = GetDC(tooltip);
    HGDIOBJ previousFont = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    RECT measured{0, 0, 420, 0};
    DrawTextW(dc, text, -1, &measured, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, previousFont);
    ReleaseDC(tooltip, dc);
    RECT button{};
    SendMessageW(toolbar, TB_GETRECT, command, reinterpret_cast<LPARAM>(&button));
    MapWindowPoints(toolbar, HWND_DESKTOP, reinterpret_cast<POINT*>(&button), 2);
    SetWindowPos(tooltip, HWND_TOPMOST, button.left, button.bottom + 2,
        static_cast<int>(measured.right - measured.left + 14),
        (std::max)(24, static_cast<int>(measured.bottom - measured.top + 8)),
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetPropW(toolbar, L"NppHistoryHoverShown", reinterpret_cast<HANDLE>(1));
}

LRESULT CALLBACK comparisonToolbarSubclass(HWND toolbar, UINT message, WPARAM wParam,
    LPARAM lParam, UINT_PTR, DWORD_PTR)
{
    if (message == WM_MOUSEMOVE)
    {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const int command = toolbarCommandAtPoint(toolbar, point);
        const int previous = static_cast<int>(reinterpret_cast<INT_PTR>(
            GetPropW(toolbar, L"NppHistoryHoverCommand"))) - 1;
        if (command != previous)
        {
            hideToolbarTooltip(toolbar);
            SetPropW(toolbar, L"NppHistoryHoverCommand",
                reinterpret_cast<HANDLE>(static_cast<INT_PTR>(command + 1)));
            if (command >= ID_COMPARE_TOOL_FIRST && command <= ID_COMPARE_TOOL_LAST)
                showToolbarTooltip(toolbar, command);
        }
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, toolbar, 0};
        TrackMouseEvent(&tracking);
    }
    else if (message == WM_MOUSELEAVE || message == WM_LBUTTONDOWN
        || message == WM_RBUTTONDOWN)
    {
        hideToolbarTooltip(toolbar);
        SetPropW(toolbar, L"NppHistoryHoverCommand", nullptr);
    }
    else if (message == WM_NCDESTROY)
    {
        hideToolbarTooltip(toolbar);
        const HWND tooltip = static_cast<HWND>(
            RemovePropW(toolbar, L"NppHistoryHoverTooltip"));
        RemovePropW(toolbar, L"NppHistoryHoverCommand");
        RemovePropW(toolbar, L"NppHistoryHoverShown");
        if (tooltip) DestroyWindow(tooltip);
        RemoveWindowSubclass(toolbar, comparisonToolbarSubclass, 1);
    }
    return DefSubclassProc(toolbar, message, wParam, lParam);
}

LRESULT CALLBACK comparisonEditSubclass(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR referenceData)
{
    if (message == WM_MOUSEWHEEL)
    {
        SendMessageW(reinterpret_cast<HWND>(referenceData), comparisonWheelMessage, wParam, lParam);
        return 0;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK comparisonMarkerSubclass(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR referenceData)
{
    if (message == WM_LBUTTONDOWN)
    {
        SendMessageW(reinterpret_cast<HWND>(referenceData), comparisonMarkerClickMessage,
            static_cast<WPARAM>(GetDlgCtrlID(window)), lParam);
        return 0;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK comparisonHeaderSubclass(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR referenceData)
{
    if (message == WM_LBUTTONDBLCLK)
    {
        const HWND dialog = reinterpret_cast<HWND>(referenceData);
        ShowWindow(dialog, IsZoomed(dialog) ? SW_RESTORE : SW_MAXIMIZE);
        return 0;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}
}

bool HistoryPanel::create(HINSTANCE instance, const NppData& nppData, HistoryStore& store,
    const Settings& settings,
    int commandId, PFUNCPLUGINCMD captureCallback, PFUNCPLUGINCMD settingsCallback,
    PFUNCPLUGINCMD aboutCallback, PFUNCPLUGINCMD stateChangedCallback,
    PFUNCPLUGINCMD prepareRestoreSaveCallback,
    PFUNCPLUGINCMD cancelRestoreSaveCallback)
{
    LoadLibraryW(L"Msftedit.dll");
    _instance = instance;
    _nppData = nppData;
    _store = &store;
    _settings = &settings;
    _captureCallback = captureCallback;
    _settingsCallback = settingsCallback;
    _aboutCallback = aboutCallback;
    _stateChangedCallback = stateChangedCallback;
    _prepareRestoreSaveCallback = prepareRestoreSaveCallback;
    _cancelRestoreSaveCallback = cancelRestoreSaveCallback;
    _dialog = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_HISTORY_PANEL), nppData._nppHandle,
        dialogProc, reinterpret_cast<LPARAM>(this));
    if (!_dialog)
        return false;

    DockedWidgetData data{};
    data.hClient = _dialog;
    data.pszName = L"NppHistory";
    data.dlgID = commandId;
    data.uMask = DWS_DF_CONT_RIGHT | DWS_ICONTAB;
    data.hIconTab = LoadIconW(instance, MAKEINTRESOURCEW(IDI_NPPHISTORY));
    SetPropW(_dialog, L"NppHistoryDockIconReady",
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(data.hIconTab ? 1 : 0)));
    data.pszModuleName = L"NppHistory.dll";
    const bool registered = SendMessageW(nppData._nppHandle, NPPM_DMMREGASDCKDLG, 0,
        reinterpret_cast<LPARAM>(&data)) != FALSE;
    _registered = registered;
    return registered;
}

void HistoryPanel::show()
{
    if (_dialog)
    {
        _opened = true;
        SendMessageW(_nppData._nppHandle, NPPM_DMMSHOW, 0, reinterpret_cast<LPARAM>(_dialog));
    }
}

void HistoryPanel::refresh(const std::filesystem::path& file)
{
    _currentFile = file;
    std::error_code error;
    _fileSaved = !file.empty() && std::filesystem::is_regular_file(file, error);
    _revisions = _store && _fileSaved ? _store->revisionsFor(file) : std::vector<RevisionInfo>{};
    if (!_dialog)
        return;
    SetDlgItemTextW(_dialog, IDC_CURRENT_FILE, file.empty() ? L"No file selected" : file.c_str());
    const bool autoSaveExcluded = _fileSaved && _settings
        && _settings->autoSaveEnabled && !_settings->externalAutoSavePluginDetected
        && _settings->isAutoSaveExcluded(file);
    const bool historyExcluded = _fileSaved && _settings && _settings->historyEnabled
        && _settings->isHistoryExcluded(file);
    std::wstring status;
    if (!_fileSaved)
        status = L"Save File First";
    else if (autoSaveExcluded || historyExcluded)
        status = L"File Excluded in Settings";
    SetDlgItemTextW(_dialog, IDC_SAVE_FILE_FIRST, status.c_str());
    ShowWindow(GetDlgItem(_dialog, IDC_SAVE_FILE_FIRST), status.empty() ? SW_HIDE : SW_SHOW);
    const HWND list = GetDlgItem(_dialog, IDC_REVISIONS);
    ListView_DeleteAllItems(list);
    for (int index = 0; index < static_cast<int>(_revisions.size()); ++index)
    {
        const auto& revision = _revisions[index];
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = index;
        item.pszText = const_cast<wchar_t*>(revision.timestamp.c_str());
        ListView_InsertItem(list, &item);
        ListView_SetItemText(list, index, 1, const_cast<wchar_t*>(revision.reason.c_str()));
        const std::wstring size = formatFileSize(revision.size);
        ListView_SetItemText(list, index, 2, const_cast<wchar_t*>(size.c_str()));
    }
    updateActionButtons();
    layout();
}

int HistoryPanel::selectedIndex() const
{
    return ListView_GetNextItem(GetDlgItem(_dialog, IDC_REVISIONS), -1, LVNI_SELECTED);
}

void HistoryPanel::updateActionButtons()
{
    if (!_dialog)
        return;
    const bool historyExcluded = _fileSaved && _settings
        && _settings->isHistoryExcluded(_currentFile);
    const bool captureAllowed = _fileSaved && !historyExcluded && _settings
        && _settings->shouldCreateRevision(RevisionTrigger::manual)
        && !_settings->isHistoryExcluded(_currentFile);
    EnableWindow(GetDlgItem(_dialog, IDC_CAPTURE), captureAllowed);
    EnableWindow(GetDlgItem(_dialog, IDC_REFRESH), _fileSaved && !historyExcluded);
    const BOOL revisionSelected = _fileSaved && !historyExcluded && selectedIndex() >= 0;
    EnableWindow(GetDlgItem(_dialog, IDC_COMPARE), revisionSelected);
    EnableWindow(GetDlgItem(_dialog, IDC_RESTORE), revisionSelected);
    if (_stateChangedCallback)
        _stateChangedCallback();
}

void HistoryPanel::showRevisionActions(int index, POINT anchor)
{
    if (index < 0 || index >= static_cast<int>(_revisions.size()))
        return;
    const HWND list = GetDlgItem(_dialog, IDC_REVISIONS);
    ListView_SetItemState(list, index, LVIS_SELECTED | LVIS_FOCUSED,
        LVIS_SELECTED | LVIS_FOCUSED);
    ClientToScreen(list, &anchor);

    const HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, L"Revision Actions");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_REVISION_DELETE, L"Delete");
    AppendMenuW(menu, MF_STRING, ID_REVISION_EDIT, L"Edit");
    AppendMenuW(menu, MF_STRING, ID_REVISION_COMPARE, L"Compare");
    AppendMenuW(menu, MF_STRING, ID_REVISION_RESTORE, L"Restore");
    const int revisionCommands[] = {ID_REVISION_DELETE, ID_REVISION_EDIT,
        ID_REVISION_COMPARE, ID_REVISION_RESTORE};
    const int resources[] = {IDI_DELETE, IDI_EDIT, IDI_COMPARE, IDI_RESTORE};
    HBITMAP bitmaps[4]{};
    int iconsAdded = 0;
    for (int actionIndex = 0; actionIndex < 4; ++actionIndex)
    {
        bitmaps[actionIndex] = createMenuBitmap(_instance, resources[actionIndex]);
        if (bitmaps[actionIndex])
        {
            applyMenuBitmap(menu, revisionCommands[actionIndex], bitmaps[actionIndex]);
            ++iconsAdded;
        }
    }
    SetPropW(_dialog, L"NppHistoryRevisionActionsReady",
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(iconsAdded + 1)));
    const int command = TrackPopupMenu(menu,
        TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON,
        anchor.x, anchor.y, 0, _dialog, nullptr);
    DestroyMenu(menu);
    for (const HBITMAP bitmap : bitmaps)
        if (bitmap) DeleteObject(bitmap);
    if (command)
        pluginLogger().write(LogLevel::debug, L"Revision action",
            std::to_wstring(command));
    if (command)
        SendMessageW(_dialog, WM_COMMAND, MAKEWPARAM(command, 0), 0);
}

void HistoryPanel::editSelectedComment()
{
    const int index = selectedIndex();
    if (index < 0 || index >= static_cast<int>(_revisions.size()))
        return;
    EditCommentContext context{_revisions[index].reason};
    const std::wstring previousComment = context.comment;
    if (DialogBoxParamW(_instance, MAKEINTRESOURCEW(IDD_EDIT_COMMENT),
        _nppData._nppHandle, editCommentProc, reinterpret_cast<LPARAM>(&context)) != IDOK)
        return;
    if (!_store->updateComment(_revisions[index], context.comment))
    {
        centeredMessageBox(_nppData._nppHandle, L"The revision comment could not be updated.",
            L"NppHistory", MB_OK | MB_ICONERROR);
        pluginLogger().write(LogLevel::error, L"Edit revision comment failed",
            _currentFile.wstring());
        actionStatus().show(L"Comment update failed");
        return;
    }
    pluginLogger().write(LogLevel::informational, L"Revision comment updated",
        _currentFile.wstring() + L" | " + _revisions[index].timestamp);
    pluginLogger().write(LogLevel::debug, L"Option change",
        L"Revision comment: " + previousComment + L" -> " + context.comment);
    refresh(_currentFile);
    if (index < static_cast<int>(_revisions.size()))
        ListView_SetItemState(GetDlgItem(_dialog, IDC_REVISIONS), index,
            LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    actionStatus().show(L"Comment updated");
}

void HistoryPanel::deleteSelected()
{
    const int index = selectedIndex();
    if (index < 0 || index >= static_cast<int>(_revisions.size()))
        return;
    const RevisionInfo revision = _revisions[index];
    const std::wstring prompt = L"Delete the revision from " + revision.timestamp
        + L"?\n\nComment: " + revision.reason
        + L"\n\nThis permanently removes this stored revision.";
    if (centeredMessageBox(_nppData._nppHandle, prompt.c_str(), L"Delete Revision",
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;
    if (!_store->deleteRevision(_currentFile, revision))
    {
        centeredMessageBox(_nppData._nppHandle, L"The revision could not be deleted completely.",
            L"NppHistory", MB_OK | MB_ICONERROR);
        pluginLogger().write(LogLevel::error, L"Delete revision failed",
            _currentFile.wstring());
        actionStatus().show(L"Revision deletion failed");
        return;
    }
    pluginLogger().write(LogLevel::informational, L"Revision deleted",
        _currentFile.wstring() + L" | " + revision.timestamp + L" | " + revision.reason);
    refresh(_currentFile);
    actionStatus().show(L"Revision deleted");
}

void HistoryPanel::compareSelected()
{
    int index = selectedIndex();
    if (_revisions.empty())
    {
        centeredMessageBox(_nppData._nppHandle, L"No revisions are available for this file yet.", L"NppHistory", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (index < 0 || index >= static_cast<int>(_revisions.size()))
        index = 0;
    CompareContext context{this, index, false};
    const INT_PTR result = DialogBoxParamW(_instance, MAKEINTRESOURCEW(IDD_COMPARE),
        _nppData._nppHandle,
        compareProc, reinterpret_cast<LPARAM>(&context));
    if (result == -1)
    {
        centeredMessageBox(_nppData._nppHandle, L"The comparison window could not be opened.", L"NppHistory", MB_OK | MB_ICONERROR);
        pluginLogger().write(LogLevel::error, L"Compare failed", _currentFile.wstring());
        actionStatus().show(L"Comparison failed");
    }
    else
    {
        pluginLogger().write(LogLevel::informational, L"Compare", _currentFile.wstring());
        actionStatus().show(L"Comparison closed");
    }
}

std::wstring HistoryPanel::currentSourceText() const
{
    int view = 0;
    SendMessageW(_nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&view));
    const HWND editor = view == 1 ? _nppData._scintillaSecondHandle : _nppData._scintillaMainHandle;
    const LRESULT length = SendMessageW(editor, SCI_GETTEXTLENGTH, 0, 0);
    if (length <= 0)
        return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length) + 1);
    SendMessageW(editor, SCI_GETTEXT, bytes.size(), reinterpret_cast<LPARAM>(bytes.data()));
    bytes.resize(static_cast<std::size_t>(length));
    return decodeText(bytes);
}

namespace
{
struct RenderedSide
{
    std::string utf8;
    std::vector<int> lineStarts;
};

RenderedSide renderSide(const std::vector<DiffRow>& rows, bool current)
{
    RenderedSide result;
    result.lineStarts.reserve(rows.size());
    for (const auto& row : rows)
    {
        result.lineStarts.push_back(static_cast<int>(result.utf8.size()));
        result.utf8 += wideToUtf8(current ? row.currentLine : row.revisionLine);
        result.utf8 += '\n';
    }
    return result;
}

void setStyle(HWND editor, int style, COLORREF foreground, bool bold = false)
{
    SendMessageW(editor, SCI_STYLESETFORE, style, foreground);
    SendMessageW(editor, SCI_STYLESETBOLD, style, bold);
}

void styleText(HWND editor, const std::string& text, bool codeSyntax)
{
    constexpr unsigned char normal = 0, comment = 1, keyword = 2, stringStyle = 3, number = 4;
    setStyle(editor, normal, RGB(0, 0, 0));
    setStyle(editor, comment, RGB(0, 128, 0));
    setStyle(editor, keyword, RGB(0, 0, 220), true);
    setStyle(editor, stringStyle, RGB(180, 0, 0));
    setStyle(editor, number, RGB(0, 125, 125));
    std::vector<unsigned char> styles(text.size(), normal);
    if (!codeSyntax)
    {
        SendMessageW(editor, SCI_STARTSTYLING, 0, 0);
        if (!styles.empty())
            SendMessageW(editor, SCI_SETSTYLINGEX, styles.size(), reinterpret_cast<LPARAM>(styles.data()));
        return;
    }
    const std::string keywords = " alignas alignof and asm auto bool break case catch char class const constexpr continue default delete do double else enum explicit export extern false float for friend goto if inline int long namespace new nullptr operator private protected public register reinterpret_cast return short signed sizeof static struct switch template this throw true try typedef typename union unsigned using virtual void volatile wchar_t while ";
    for (std::size_t i = 0; i < text.size();)
    {
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/')
        {
            const std::size_t end = text.find('\n', i);
            std::fill(styles.begin() + i, styles.begin() + (end == std::string::npos ? text.size() : end), comment);
            i = end == std::string::npos ? text.size() : end;
        }
        else if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*')
        {
            const std::size_t found = text.find("*/", i + 2);
            const std::size_t end = found == std::string::npos ? text.size() : found + 2;
            std::fill(styles.begin() + i, styles.begin() + end, comment); i = end;
        }
        else if (text[i] == '"' || text[i] == '\'')
        {
            const char quote = text[i]; std::size_t end = i + 1;
            while (end < text.size()) { if (text[end] == '\\') ++end; else if (text[end] == quote) { ++end; break; } ++end; }
            std::fill(styles.begin() + i, styles.begin() + end, stringStyle); i = end;
        }
        else if (std::isdigit(static_cast<unsigned char>(text[i])))
        {
            std::size_t end = i + 1;
            while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '.')) ++end;
            std::fill(styles.begin() + i, styles.begin() + end, number); i = end;
        }
        else if (std::isalpha(static_cast<unsigned char>(text[i])) || text[i] == '_')
        {
            std::size_t end = i + 1;
            while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_')) ++end;
            const std::string token = " " + text.substr(i, end - i) + " ";
            if (keywords.find(token) != std::string::npos)
                std::fill(styles.begin() + i, styles.begin() + end, keyword);
            i = end;
        }
        else ++i;
    }
    SendMessageW(editor, SCI_STARTSTYLING, 0, 0);
    if (!styles.empty())
        SendMessageW(editor, SCI_SETSTYLINGEX, styles.size(), reinterpret_cast<LPARAM>(styles.data()));
}

void configureScintilla(HWND editor, HWND source, int lineNumberWidth)
{
    SendMessageW(editor, SCI_SETCODEPAGE, SC_CP_UTF8, 0);
    SendMessageW(editor, SCI_SETUNDOCOLLECTION, FALSE, 0);
    SendMessageW(editor, SCI_SETWRAPMODE, 0, 0);
    SendMessageW(editor, SCI_SETVSCROLLBAR, TRUE, 0);
    SendMessageW(editor, SCI_SETHSCROLLBAR, TRUE, 0);
    SendMessageW(editor, SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
    SendMessageW(editor, SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<LPARAM>("Consolas"));
    SendMessageW(editor, SCI_STYLESETSIZE, STYLE_DEFAULT, 11);
    if (source)
    {
        const COLORREF fore = static_cast<COLORREF>(SendMessageW(source, SCI_STYLEGETFORE, STYLE_DEFAULT, 0));
        const COLORREF back = static_cast<COLORREF>(SendMessageW(source, SCI_STYLEGETBACK, STYLE_DEFAULT, 0));
        SendMessageW(editor, SCI_STYLESETFORE, STYLE_DEFAULT, fore);
        SendMessageW(editor, SCI_STYLESETBACK, STYLE_DEFAULT, back);
    }
    SendMessageW(editor, SCI_STYLECLEARALL, 0, 0);
    SendMessageW(editor, SCI_STYLESETFORE, STYLE_LINENUMBER, RGB(90, 90, 90));
    SendMessageW(editor, SCI_STYLESETBACK, STYLE_LINENUMBER, RGB(245, 245, 245));
    const std::string digits(lineNumberWidth + 1, '9');
    const int width = static_cast<int>(SendMessageW(editor, SCI_TEXTWIDTH, STYLE_LINENUMBER,
        reinterpret_cast<LPARAM>(digits.c_str())));
    SendMessageW(editor, SCI_SETMARGINWIDTHN, 0, width + 8);
    SendMessageW(editor, SCI_SETMARGINWIDTHN, 1, 0);
    SendMessageW(editor, SCI_SETMARGINWIDTHN, 2, 0);
    SendMessageW(editor, SCI_MARKERDEFINE, 0, SC_MARK_BACKGROUND);
    SendMessageW(editor, SCI_MARKERSETBACK, 0, RGB(239, 203, 5));
    SendMessageW(editor, SCI_MARKERDEFINE, 1, SC_MARK_BACKGROUND);
    SendMessageW(editor, SCI_MARKERSETBACK, 1, RGB(224, 224, 224));
    SendMessageW(editor, SCI_MARKERDEFINE, 2, SC_MARK_BACKGROUND);
    SendMessageW(editor, SCI_MARKERSETBACK, 2, RGB(239, 119, 116));
    SendMessageW(editor, SCI_INDICSETSTYLE, 0, INDIC_FULLBOX);
    SendMessageW(editor, SCI_INDICSETFORE, 0, RGB(255, 150, 45));
    SendMessageW(editor, SCI_INDICSETALPHA, 0, 120);
    SendMessageW(editor, SCI_INDICSETOUTLINEALPHA, 0, 150);
    SendMessageW(editor, SCI_INDICSETUNDER, 0, FALSE);
    SendMessageW(editor, SCI_SETREADONLY, TRUE, 0);
}

enum class CompareToolbarIcon
{
    chooseRevision,
    nextDifference,
    previousDifference,
    firstDifference,
    currentDifference,
    lastDifference,
    firstRevision,
    previousRevision,
    nextRevision,
    lastRevision,
    options,
    refresh,
    count
};

void drawArrow(HDC dc, int centreX, int centreY, int dx, int dy, COLORREF colour)
{
    const HPEN pen = CreatePen(PS_SOLID, 3, colour);
    const HBRUSH brush = CreateSolidBrush(colour);
    const auto previousPen = SelectObject(dc, pen);
    const auto previousBrush = SelectObject(dc, brush);
    MoveToEx(dc, centreX - dx * 5, centreY - dy * 5, nullptr);
    LineTo(dc, centreX + dx * 4, centreY + dy * 4);
    POINT head[] = {
        {centreX + dx * 7, centreY + dy * 7},
        {centreX + dx * 2 - dy * 4, centreY + dy * 2 - dx * 4},
        {centreX + dx * 2 + dy * 4, centreY + dy * 2 + dx * 4}};
    Polygon(dc, head, static_cast<int>(std::size(head)));
    SelectObject(dc, previousBrush);
    SelectObject(dc, previousPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void drawDocument(HDC dc, int left, int top, COLORREF outline)
{
    const HPEN pen = CreatePen(PS_SOLID, 2, outline);
    const HBRUSH brush = CreateSolidBrush(RGB(255, 255, 255));
    const auto previousPen = SelectObject(dc, pen);
    const auto previousBrush = SelectObject(dc, brush);
    Rectangle(dc, left, top, left + 11, top + 15);
    MoveToEx(dc, left + 3, top + 5, nullptr); LineTo(dc, left + 8, top + 5);
    MoveToEx(dc, left + 3, top + 9, nullptr); LineTo(dc, left + 8, top + 9);
    SelectObject(dc, previousBrush);
    SelectObject(dc, previousPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

HBITMAP createCompareToolbarBitmap(CompareToolbarIcon icon)
{
    constexpr int size = 24;
    const COLORREF mask = RGB(255, 0, 255);
    const COLORREF blue = RGB(32, 112, 181);
    const COLORREF darkBlue = RGB(22, 75, 126);
    const COLORREF orange = RGB(238, 142, 28);
    const COLORREF green = RGB(36, 158, 91);
    const COLORREF grey = RGB(135, 145, 155);
    const HDC screen = GetDC(nullptr);
    const HDC dc = CreateCompatibleDC(screen);
    const HBITMAP bitmap = CreateCompatibleBitmap(screen, size, size);
    ReleaseDC(nullptr, screen);
    const auto previousBitmap = SelectObject(dc, bitmap);
    RECT area{0, 0, size, size};
    const HBRUSH background = CreateSolidBrush(mask);
    FillRect(dc, &area, background);
    DeleteObject(background);

    const auto drawDifference = [&](int row)
    {
        const HPEN linePen = CreatePen(PS_SOLID, 2, grey);
        const auto oldPen = SelectObject(dc, linePen);
        for (int y : {5, 10, 15, 20})
        {
            MoveToEx(dc, 5, y, nullptr);
            LineTo(dc, 19, y);
        }
        SelectObject(dc, oldPen);
        DeleteObject(linePen);
        const HBRUSH change = CreateSolidBrush(green);
        RECT marker{4, row - 2, 20, row + 2};
        FillRect(dc, &marker, change);
        DeleteObject(change);
    };

    switch (icon)
    {
    case CompareToolbarIcon::chooseRevision:
    {
        drawDocument(dc, 2, 3, blue);
        drawDocument(dc, 11, 6, orange);
        const HBRUSH brush = CreateSolidBrush(green);
        POINT triangle[] = {{8, 18}, {14, 18}, {11, 22}};
        const auto old = SelectObject(dc, brush);
        Polygon(dc, triangle, static_cast<int>(std::size(triangle)));
        SelectObject(dc, old); DeleteObject(brush);
        break;
    }
    case CompareToolbarIcon::nextDifference:
        drawDifference(10); drawArrow(dc, 12, 14, 0, 1, blue); break;
    case CompareToolbarIcon::previousDifference:
        drawDifference(15); drawArrow(dc, 12, 10, 0, -1, blue); break;
    case CompareToolbarIcon::firstDifference:
        drawDifference(5); drawArrow(dc, 12, 13, 0, -1, blue); break;
    case CompareToolbarIcon::currentDifference:
        drawDifference(10);
        Ellipse(dc, 9, 7, 15, 13);
        break;
    case CompareToolbarIcon::lastDifference:
        drawDifference(20); drawArrow(dc, 12, 12, 0, 1, blue); break;
    case CompareToolbarIcon::firstRevision:
        drawDocument(dc, 8, 4, orange);
        drawArrow(dc, 7, 12, -1, 0, blue);
        MoveToEx(dc, 2, 5, nullptr); LineTo(dc, 2, 19); break;
    case CompareToolbarIcon::previousRevision:
        drawDocument(dc, 9, 4, orange); drawArrow(dc, 7, 12, -1, 0, blue); break;
    case CompareToolbarIcon::nextRevision:
        drawDocument(dc, 4, 4, orange); drawArrow(dc, 17, 12, 1, 0, blue); break;
    case CompareToolbarIcon::lastRevision:
        drawDocument(dc, 4, 4, orange);
        drawArrow(dc, 17, 12, 1, 0, blue);
        MoveToEx(dc, 22, 5, nullptr); LineTo(dc, 22, 19); break;
    case CompareToolbarIcon::options:
    {
        const HPEN pen = CreatePen(PS_SOLID, 2, darkBlue);
        const HBRUSH brush = CreateSolidBrush(RGB(220, 235, 248));
        const auto oldPen = SelectObject(dc, pen);
        const auto oldBrush = SelectObject(dc, brush);
        Ellipse(dc, 5, 5, 19, 19);
        Ellipse(dc, 9, 9, 15, 15);
        for (int i = 0; i < 8; ++i)
        {
            const double angle = i * 3.14159265358979323846 / 4.0;
            MoveToEx(dc, 12 + static_cast<int>(7 * std::cos(angle)),
                12 + static_cast<int>(7 * std::sin(angle)), nullptr);
            LineTo(dc, 12 + static_cast<int>(10 * std::cos(angle)),
                12 + static_cast<int>(10 * std::sin(angle)));
        }
        SelectObject(dc, oldBrush); SelectObject(dc, oldPen);
        DeleteObject(brush); DeleteObject(pen);
        break;
    }
    case CompareToolbarIcon::refresh:
    {
        const HPEN pen = CreatePen(PS_SOLID, 3, green);
        const auto oldPen = SelectObject(dc, pen);
        Arc(dc, 4, 4, 20, 20, 18, 7, 6, 17);
        SelectObject(dc, oldPen); DeleteObject(pen);
        drawArrow(dc, 17, 7, 1, 0, blue);
        drawArrow(dc, 7, 17, -1, 0, green);
        break;
    }
    default: break;
    }
    SelectObject(dc, previousBitmap);
    DeleteDC(dc);
    return bitmap;
}

bool configureComparisonToolbar(HWND toolbar, HINSTANCE instance, HIMAGELIST& images)
{
    SendMessageW(toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    images = ImageList_Create(24, 24, ILC_COLOR32 | ILC_MASK,
        static_cast<int>(CompareToolbarIcon::count), 0);
    if (!images)
        return false;
    for (int index = 0; index < static_cast<int>(CompareToolbarIcon::count); ++index)
    {
        const HBITMAP bitmap = createCompareToolbarBitmap(
            static_cast<CompareToolbarIcon>(index));
        ImageList_AddMasked(images, bitmap, RGB(255, 0, 255));
        DeleteObject(bitmap);
    }
    SendMessageW(toolbar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(images));
    SetPropW(toolbar, L"NppHistoryComparisonImageCount",
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(CompareToolbarIcon::count)));
    SetPropW(toolbar, L"NppHistoryComparisonImageSize",
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(24)));
    SendMessageW(toolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(24, 24));
    SendMessageW(toolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(30, 30));
    std::vector<TBBUTTON> buttons;
    const auto addButton = [&](int image, bool enabled)
    {
        TBBUTTON button{};
        button.iBitmap = image;
        button.idCommand = ID_COMPARE_TOOL_FIRST + image;
        button.fsState = enabled ? TBSTATE_ENABLED : 0;
        button.fsStyle = BTNS_BUTTON;
        buttons.push_back(button);
    };
    const auto separator = [&]()
    {
        TBBUTTON button{};
        button.fsStyle = BTNS_SEP;
        button.iBitmap = 5;
        buttons.push_back(button);
    };
    addButton(0, true);
    separator();
    addButton(1, true);
    addButton(2, true);
    addButton(3, true);
    addButton(4, true);
    addButton(5, true);
    separator();
    addButton(6, true);
    addButton(7, true);
    addButton(8, true);
    addButton(9, true);
    separator();
    addButton(10, true);
    addButton(11, true);
    SendMessageW(toolbar, TB_ADDBUTTONS, buttons.size(), reinterpret_cast<LPARAM>(buttons.data()));
    SendMessageW(toolbar, TB_AUTOSIZE, 0, 0);
    const HWND nativeTooltip = reinterpret_cast<HWND>(
        SendMessageW(toolbar, TB_GETTOOLTIPS, 0, 0));
    if (nativeTooltip)
        SendMessageW(nativeTooltip, TTM_ACTIVATE, FALSE, 0);
    const HWND tooltip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"STATIC", nullptr, WS_POPUP | WS_BORDER | SS_LEFT | SS_CENTERIMAGE,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        GetParent(toolbar), nullptr, instance, nullptr);
    if (!tooltip)
        return false;
    SendMessageW(tooltip, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), FALSE);
    SetPropW(toolbar, L"NppHistoryHoverTooltip", tooltip);
    SetPropW(toolbar, L"NppHistoryHoverCommand", nullptr);
    SetWindowSubclass(toolbar, comparisonToolbarSubclass, 1, 0);
    return true;
}

const wchar_t* toolbarHint(int image)
{
    static const wchar_t* hints[] = {
        L"Choose revision to compare with the current editor content.",
        L"Next difference: scroll to the next block of changed lines.",
        L"Previous difference: scroll to the previous block of changed lines.",
        L"First difference: scroll to the first block of changed lines.",
        L"Current difference: return to the selected difference without changing revisions.",
        L"Last difference: scroll to the last block of changed lines.",
        L"First revision: compare with the first revision in the history list.",
        L"Previous revision: compare with the preceding revision in the history list.",
        L"Next revision: compare with the following revision in the history list.",
        L"Last revision: compare with the last revision in the history list.",
        L"Comparison options: choose which whitespace, blank-line, case and line-ending differences to ignore.",
        L"Refresh comparison: recalculate differences against the current editor content. No revision is created."
    };
    return image >= 0 && image < static_cast<int>(std::size(hints)) ? hints[image]
        : L"NppHistory comparison command";
}

bool isDifference(const DiffRow& row)
{
    return row.currentKind == DiffKind::removed || row.currentKind == DiffKind::changed
        || row.revisionKind == DiffKind::added || row.revisionKind == DiffKind::changed;
}

COLORREF markerColour(DiffKind kind)
{
    switch (kind)
    {
    case DiffKind::removed: return RGB(220, 38, 38);
    case DiffKind::added: return RGB(28, 158, 68);
    case DiffKind::changed: return RGB(40, 105, 210);
    default: return RGB(150, 150, 150);
    }
}
}

void HistoryPanel::renderComparison(HWND dialog, CompareContext& context)
{
    const int revisionIndex = context.revisionIndex;
    if (revisionIndex < 0 || revisionIndex >= static_cast<int>(_revisions.size()))
        return;
    context.currentText = currentSourceText();
    context.revisionBytes = _store->readRevision(_revisions[revisionIndex]);
    context.revisionText = decodeText(context.revisionBytes);
    context.rows = makeSideBySideDiff(context.currentText, context.revisionText, context.options);
    const auto& rows = context.rows;
    int maximumLine = 1;
    for (const auto& row : rows)
        maximumLine = (std::max)({maximumLine, row.currentLineNumber, row.revisionLineNumber});
    context.lineNumberWidth = static_cast<int>(std::to_wstring(maximumLine).size());
    context.differenceRows.clear();
    bool previousDifferent = false;
    for (int row = 0; row < static_cast<int>(rows.size()); ++row)
    {
        const bool different = isDifference(rows[row]);
        if (different && !previousDifferent)
            context.differenceRows.push_back(row);
        previousDifferent = different;
    }
    context.currentDifference = context.differenceRows.empty() ? -1 : 0;
    const HWND left = GetDlgItem(dialog, IDC_COMPARE_LEFT);
    const HWND right = GetDlgItem(dialog, IDC_COMPARE_RIGHT);
    const RenderedSide leftRendered = renderSide(rows, true);
    const RenderedSide rightRendered = renderSide(rows, false);
    int view = 0;
    SendMessageW(_nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&view));
    const HWND sourceEditor = view == 1 ? _nppData._scintillaSecondHandle : _nppData._scintillaMainHandle;
    SendMessageW(left, WM_SETREDRAW, FALSE, 0);
    SendMessageW(right, WM_SETREDRAW, FALSE, 0);
    configureScintilla(left, sourceEditor, context.lineNumberWidth);
    configureScintilla(right, sourceEditor, context.lineNumberWidth);
    for (const HWND editor : {left, right})
        SendMessageW(editor, SCI_SETREADONLY, FALSE, 0);
    SendMessageW(left, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(leftRendered.utf8.c_str()));
    SendMessageW(right, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(rightRendered.utf8.c_str()));
    std::wstring extension = _currentFile.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
    const bool codeSyntax = extension == L".c" || extension == L".cc" || extension == L".cpp"
        || extension == L".cxx" || extension == L".h" || extension == L".hpp"
        || extension == L".java" || extension == L".js" || extension == L".ts"
        || extension == L".cs";
    styleText(left, leftRendered.utf8, codeSyntax);
    styleText(right, rightRendered.utf8, codeSyntax);
    for (const HWND editor : {left, right})
    {
        SendMessageW(editor, SCI_MARKERDELETEALL, static_cast<WPARAM>(-1), 0);
        SendMessageW(editor, SCI_SETINDICATORCURRENT, 0, 0);
    }
    int selectedStart = -1;
    int selectedEnd = -1;
    if (context.currentDifference >= 0 && context.currentDifference < static_cast<int>(context.differenceRows.size()))
    {
        selectedStart = context.differenceRows[context.currentDifference];
        selectedEnd = selectedStart + 1;
        while (selectedEnd < static_cast<int>(rows.size()) && isDifference(rows[selectedEnd])) ++selectedEnd;
    }
    for (int rowIndex = 0; rowIndex < static_cast<int>(rows.size()); ++rowIndex)
    {
        const auto& row = rows[rowIndex];
        const bool selected = rowIndex >= selectedStart && rowIndex < selectedEnd;
        for (const auto& side : {std::pair<HWND, bool>{left, true}, {right, false}})
        {
            const DiffKind kind = side.second ? row.currentKind : row.revisionKind;
            if (kind == DiffKind::unchanged)
                continue;
            const int marker = selected ? 2 : (kind == DiffKind::empty ? 1 : 0);
            SendMessageW(side.first, SCI_MARKERADD, rowIndex, marker);
        }
        const auto addSpans = [&](HWND editor, const std::wstring& line,
            const std::vector<DiffSpan>& spans, int lineStart)
        {
            for (const auto& span : spans)
            {
                const int start = lineStart + static_cast<int>(wideToUtf8(line.substr(0, span.start)).size());
                const int length = static_cast<int>(wideToUtf8(line.substr(span.start, span.length)).size());
                if (length > 0)
                    SendMessageW(editor, SCI_INDICATORFILLRANGE, start, length);
            }
        };
        addSpans(left, row.currentLine, row.currentSpans, leftRendered.lineStarts[rowIndex]);
        addSpans(right, row.revisionLine, row.revisionSpans, rightRendered.lineStarts[rowIndex]);
    }
    for (const HWND editor : {left, right})
    {
        SendMessageW(editor, SCI_SETREADONLY, TRUE, 0);
        SendMessageW(editor, SCI_GOTOLINE, 0, 0);
    }
    SetDlgItemTextW(dialog, IDC_COMPARE_LEFT_HEADER,
        _currentFile.filename().wstring().c_str());
    const std::wstring revisionHeader = _currentFile.filename().wstring() + L" \x2014 "
        + _revisions[revisionIndex].timestamp + L" - " + _revisions[revisionIndex].reason;
    SetDlgItemTextW(dialog, IDC_COMPARE_RIGHT_HEADER, revisionHeader.c_str());
    const std::wstring title = _currentFile.filename().wstring() + L" \x2194 "
        + _revisions[revisionIndex].timestamp + L" - " + _revisions[revisionIndex].reason
        + L" \x2014 NppHistory";
    SetWindowTextW(dialog, title.c_str());
    SendMessageW(left, WM_SETREDRAW, TRUE, 0);
    SendMessageW(right, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(left, nullptr, TRUE);
    InvalidateRect(right, nullptr, TRUE);
    configureComparisonScroll(dialog, context);
    InvalidateRect(GetDlgItem(dialog, IDC_COMPARE_LEFT_MARKERS), nullptr, TRUE);
    InvalidateRect(GetDlgItem(dialog, IDC_COMPARE_RIGHT_MARKERS), nullptr, TRUE);
    InvalidateRect(GetDlgItem(dialog, IDC_COMPARE_LOCATION_MAP), nullptr, FALSE);
    updateComparisonNavigation(dialog, context);
    updateComparisonStatus(dialog, context);
    InvalidateRect(GetDlgItem(dialog, IDC_COMPARE_LEFT_HEADER), nullptr, TRUE);
    InvalidateRect(GetDlgItem(dialog, IDC_COMPARE_RIGHT_HEADER), nullptr, TRUE);
}

void HistoryPanel::configureComparisonScroll(HWND dialog, CompareContext& context)
{
    const HWND left = GetDlgItem(dialog, IDC_COMPARE_LEFT);
    context.visibleLines = (std::max)(1,
        static_cast<int>(SendMessageW(left, SCI_LINESONSCREEN, 0, 0)));
    context.lineHeight = (std::max)(1,
        static_cast<int>(SendMessageW(left, SCI_TEXTHEIGHT, 0, 0)));

    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = (std::max)(0, static_cast<int>(context.rows.size()) - 1);
    info.nPage = static_cast<UINT>(context.visibleLines);
    const int maximumTop = (std::max)(0, info.nMax - static_cast<int>(info.nPage) + 1);
    context.topLine = (std::min)(context.topLine, maximumTop);
    info.nPos = context.topLine;
    SetScrollInfo(GetDlgItem(dialog, IDC_COMPARE_SCROLL), SB_CTL, &info, TRUE);
    scrollComparisonTo(dialog, context, context.topLine);
}

void HistoryPanel::scrollComparisonTo(HWND dialog, CompareContext& context, int topLine)
{
    const int maximumTop = (std::max)(0,
        static_cast<int>(context.rows.size()) - context.visibleLines);
    topLine = (std::max)(0, (std::min)(topLine, maximumTop));
    context.synchronizingScroll = true;
    for (const int control : {IDC_COMPARE_LEFT, IDC_COMPARE_RIGHT})
    {
        const HWND editor = GetDlgItem(dialog, control);
        SendMessageW(editor, SCI_SETFIRSTVISIBLELINE, topLine, 0);
    }
    context.synchronizingScroll = false;
    context.topLine = topLine;
    SetScrollPos(GetDlgItem(dialog, IDC_COMPARE_SCROLL), SB_CTL, topLine, TRUE);
    InvalidateRect(GetDlgItem(dialog, IDC_COMPARE_LEFT_MARKERS), nullptr, TRUE);
    InvalidateRect(GetDlgItem(dialog, IDC_COMPARE_RIGHT_MARKERS), nullptr, TRUE);
    InvalidateRect(GetDlgItem(dialog, IDC_COMPARE_LOCATION_MAP), nullptr, FALSE);
}

void HistoryPanel::updateComparisonNavigation(HWND dialog, CompareContext& context)
{
    const bool available = !context.differenceRows.empty();
    EnableWindow(GetDlgItem(dialog, IDC_COMPARE_PREVIOUS), available);
    EnableWindow(GetDlgItem(dialog, IDC_COMPARE_NEXT), available);
    std::wstring status = L"No differences";
    if (available)
    {
        const int selected = (std::max)(0, context.currentDifference);
        status = L"Difference " + std::to_wstring(selected + 1) + L" of "
            + std::to_wstring(context.differenceRows.size());
    }
    SetDlgItemTextW(dialog, IDC_COMPARE_STATUS, status.c_str());
    SetDlgItemTextW(dialog, IDC_COMPARE_OVERALL_STATUS, status.c_str());
    RedrawWindow(GetDlgItem(dialog, IDC_COMPARE_OVERALL_STATUS), nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
}

void HistoryPanel::updateComparisonStatus(HWND dialog, CompareContext& context)
{
    const auto eolName = [](const std::wstring& value)
    {
        const bool crlf = value.find(L"\r\n") != std::wstring::npos;
        std::wstring withoutCrlf = value;
        std::size_t at = 0;
        while ((at = withoutCrlf.find(L"\r\n", at)) != std::wstring::npos)
            withoutCrlf.erase(at, 2);
        const bool lf = withoutCrlf.find(L'\n') != std::wstring::npos;
        const bool cr = withoutCrlf.find(L'\r') != std::wstring::npos;
        const int kinds = (crlf ? 1 : 0) + (lf ? 1 : 0) + (cr ? 1 : 0);
        if (kinds > 1) return std::wstring(L"Mixed");
        if (crlf) return std::wstring(L"Windows");
        if (lf) return std::wstring(L"Unix");
        if (cr) return std::wstring(L"Mac");
        return std::wstring(L"None");
    };
    const auto encodingName = [](const std::vector<std::uint8_t>& bytes)
    {
        if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf)
            return std::wstring(L"UTF-8 BOM");
        if (bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe)
            return std::wstring(L"UTF-16 LE");
        if (bytes.size() >= 2 && bytes[0] == 0xfe && bytes[1] == 0xff)
            return std::wstring(L"UTF-16 BE");
        return std::wstring(L"UTF-8");
    };
    const auto setStatus = [&](int editorId, int statusId, const std::wstring& text,
        const std::wstring& encoding)
    {
        const HWND editor = GetDlgItem(dialog, editorId);
        const int position = static_cast<int>(SendMessageW(editor, SCI_GETCURRENTPOS, 0, 0));
        const int line = static_cast<int>(SendMessageW(editor, SCI_LINEFROMPOSITION, position, 0));
        const int column = static_cast<int>(SendMessageW(editor, SCI_GETCOLUMN, position, 0));
        const int lineStart = static_cast<int>(SendMessageW(editor, SCI_POSITIONFROMLINE, line, 0));
        const int lineEnd = static_cast<int>(SendMessageW(editor, SCI_GETLINEENDPOSITION, line, 0));
        std::wostringstream value;
        value << L"  Ln: " << line + 1 << L"   Col: " << column + 1 << L"   Ch: "
            << position - lineStart + 1 << L"/" << (std::max)(0, lineEnd - lineStart)
            << L"                         " << encoding << L"        " << eolName(text);
        SetDlgItemTextW(dialog, statusId, value.str().c_str());
        RedrawWindow(GetDlgItem(dialog, statusId), nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    };
    setStatus(IDC_COMPARE_LEFT, IDC_COMPARE_LEFT_STATUS, context.currentText, L"UTF-8");
    setStatus(IDC_COMPARE_RIGHT, IDC_COMPARE_RIGHT_STATUS, context.revisionText,
        encodingName(context.revisionBytes));
}

void HistoryPanel::navigateDifference(HWND dialog, CompareContext& context, int direction)
{
    if (context.differenceRows.empty())
        return;
    const int count = static_cast<int>(context.differenceRows.size());
    if (context.currentDifference < 0)
        context.currentDifference = direction < 0 ? count - 1 : 0;
    else
        context.currentDifference = (context.currentDifference + direction + count) % count;
    const int row = context.differenceRows[context.currentDifference];
    int selectedEnd = row + 1;
    while (selectedEnd < static_cast<int>(context.rows.size()) && isDifference(context.rows[selectedEnd]))
        ++selectedEnd;
    for (const int control : {IDC_COMPARE_LEFT, IDC_COMPARE_RIGHT})
        SendDlgItemMessageW(dialog, control, SCI_MARKERDELETEALL, static_cast<WPARAM>(-1), 0);
    for (int rowIndex = 0; rowIndex < static_cast<int>(context.rows.size()); ++rowIndex)
    {
        const auto& item = context.rows[rowIndex];
        for (const auto& side : {std::pair<int, DiffKind>{IDC_COMPARE_LEFT, item.currentKind},
            {IDC_COMPARE_RIGHT, item.revisionKind}})
        {
            if (side.second == DiffKind::unchanged) continue;
            const int marker = rowIndex >= row && rowIndex < selectedEnd ? 2
                : (side.second == DiffKind::empty ? 1 : 0);
            SendDlgItemMessageW(dialog, side.first, SCI_MARKERADD, rowIndex, marker);
        }
    }
    scrollComparisonTo(dialog, context, row - context.visibleLines / 3);
    updateComparisonNavigation(dialog, context);
    InvalidateRect(GetDlgItem(dialog, IDC_COMPARE_LOCATION_MAP), nullptr, FALSE);
}

void HistoryPanel::showRevisionPicker(HWND dialog, CompareContext& context, POINT anchor,
    UINT alignment)
{
    HMENU revisions = CreatePopupMenu();
    for (int index = 0; index < static_cast<int>(_revisions.size()); ++index)
    {
        const auto& revision = _revisions[index];
        const std::wstring label = revision.timestamp + L" - " + revision.reason;
        AppendMenuW(revisions, MF_STRING | (index == context.revisionIndex ? MF_CHECKED : 0),
            4000 + index, label.c_str());
    }
    const int selected = TrackPopupMenu(revisions,
        alignment | TPM_TOPALIGN | TPM_RETURNCMD,
        anchor.x, anchor.y, 0, dialog, nullptr);
    DestroyMenu(revisions);
    if (selected >= 4000 && selected < 4000 + static_cast<int>(_revisions.size()))
    {
        context.revisionIndex = selected - 4000;
        SendDlgItemMessageW(dialog, IDC_COMPARE_REVISION, CB_SETCURSEL,
            context.revisionIndex, 0);
        context.topLine = 0;
        renderComparison(dialog, context);
    }
}

void HistoryPanel::restoreSelected()
{
    const int index = selectedIndex();
    if (index < 0 || index >= static_cast<int>(_revisions.size()))
    {
        centeredMessageBox(_nppData._nppHandle, L"Select a revision to restore.", L"NppHistory", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const RevisionInfo selected = _revisions[index];
    const bool retainSafetyRevision = _settings
        && _settings->shouldCreateRevision(RevisionTrigger::beforeRestore)
        && !_settings->isHistoryExcluded(_currentFile);
    const wchar_t* confirmation = retainSafetyRevision
        ? L"Restore this revision? The current saved file will be retained as a safety revision."
        : L"Restore this revision? History is not configured to create a safety revision first.";
    if (centeredMessageBox(_nppData._nppHandle, confirmation,
        L"NppHistory", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;

    int view = 0;
    SendMessageW(_nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&view));
    const HWND editor = view == 1 ? _nppData._scintillaSecondHandle : _nppData._scintillaMainHandle;
    const bool currentEditsModified = SendMessageW(editor, SCI_GETMODIFY, 0, 0) != 0;
    if (currentEditsModified && _prepareRestoreSaveCallback)
        _prepareRestoreSaveCallback();
    if (currentEditsModified && SendMessageW(_nppData._nppHandle, NPPM_SAVEFILE, 0,
        reinterpret_cast<LPARAM>(_currentFile.c_str())) == FALSE)
    {
        if (_cancelRestoreSaveCallback)
            _cancelRestoreSaveCallback();
        centeredMessageBox(_nppData._nppHandle, L"The current edits could not be saved, so the restore was cancelled.",
            L"NppHistory", MB_OK | MB_ICONERROR);
        pluginLogger().write(LogLevel::error, L"Restore failed",
            L"Current edits could not be saved: " + _currentFile.wstring());
        actionStatus().show(L"Restore failed");
        return;
    }
    if (retainSafetyRevision)
    {
        if (_store->captureFile(_currentFile, L"Before restore", true))
            pluginLogger().write(LogLevel::informational, L"Revision created",
                L"Before restore: " + _currentFile.wstring());
    }
    if (!_store->restoreRevision(selected, _currentFile))
    {
        centeredMessageBox(_nppData._nppHandle, L"The revision could not be restored.", L"NppHistory", MB_OK | MB_ICONERROR);
        pluginLogger().write(LogLevel::error, L"Restore failed", _currentFile.wstring());
        actionStatus().show(L"Restore failed");
        return;
    }
    pluginLogger().write(LogLevel::informational, L"Restore",
        _currentFile.wstring() + L" | " + selected.timestamp + L" | " + selected.reason);
    const bool reloaded = SendMessageW(_nppData._nppHandle, NPPM_RELOADFILE, FALSE,
        reinterpret_cast<LPARAM>(_currentFile.c_str())) != FALSE;
    refresh(_currentFile);
    actionStatus().show(reloaded ? L"Revision restored" : L"Restored; reload failed");
}

void HistoryPanel::layout()
{
    RECT area{};
    GetClientRect(_dialog, &area);
    const int margin = 8;
    MoveWindow(GetDlgItem(_dialog, IDC_CURRENT_FILE), margin, margin, area.right - margin * 2, 22, TRUE);
    std::vector<int> buttonIds;
    for (const Command command : commandOrder)
    {
        const int row = static_cast<int>(command);
        const bool visible = !_settings || _settings->commandVisible(
            static_cast<Command>(row), CommandSurface::pane);
        ShowWindow(GetDlgItem(_dialog, commands[row].paneControl), visible ? SW_SHOW : SW_HIDE);
        if (visible) buttonIds.push_back(commands[row].paneControl);
    }
    std::vector<int> minimumWidths;
    minimumWidths.reserve(std::size(buttonIds));
    const HWND firstButton = GetDlgItem(_dialog, IDC_CAPTURE);
    HDC dc = GetDC(firstButton);
    HGDIOBJ previousFont = SelectObject(dc,
        reinterpret_cast<HFONT>(SendMessageW(firstButton, WM_GETFONT, 0, 0)));
    for (const int id : buttonIds)
    {
        wchar_t label[64]{};
        GetWindowTextW(GetDlgItem(_dialog, id), label, static_cast<int>(std::size(label)));
        SIZE measured{};
        GetTextExtentPoint32W(dc, label, static_cast<int>(wcslen(label)), &measured);
        minimumWidths.push_back((std::max)(58, static_cast<int>(measured.cx) + 22));
    }
    SelectObject(dc, previousFont);
    ReleaseDC(firstButton, dc);

    const int gap = 5;
    const int available = (std::max)(1, static_cast<int>(area.right) - margin * 2);
    const int widestMinimum = minimumWidths.empty() ? 58
        : *std::max_element(minimumWidths.begin(), minimumWidths.end());
    int columns = 1;
    for (int candidate = static_cast<int>(std::size(buttonIds)); candidate >= 1; --candidate)
    {
        if ((available - (candidate - 1) * gap) / candidate >= widestMinimum)
        {
            columns = candidate;
            break;
        }
    }
    const int uniformWidth = (available - (columns - 1) * gap) / columns;
    std::vector<std::vector<int>> rows;
    for (int index = 0; index < static_cast<int>(std::size(buttonIds)); ++index)
    {
        if (index % columns == 0)
            rows.emplace_back();
        rows.back().push_back(index);
    }
    const int buttonHeight = 24;
    const int buttonsHeight = rows.empty() ? 0 : static_cast<int>(rows.size()) * buttonHeight
        + (static_cast<int>(rows.size()) - 1) * gap;
    SetPropW(_dialog, L"NppHistoryResponsiveButtonRows",
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(rows.size())));
    const int buttonTop = area.bottom - margin - buttonsHeight;
    const bool showStatus = IsWindowVisible(GetDlgItem(_dialog, IDC_SAVE_FILE_FIRST)) != FALSE;
    int warningHeight = 0;
    if (showStatus)
    {
        const HWND status = GetDlgItem(_dialog, IDC_SAVE_FILE_FIRST);
        wchar_t text[256]{};
        GetWindowTextW(status, text, static_cast<int>(std::size(text)));
        HDC statusDc = GetDC(status);
        const HGDIOBJ oldFont = SelectObject(statusDc,
            reinterpret_cast<HFONT>(SendMessageW(status, WM_GETFONT, 0, 0)));
        RECT measured{0, 0, available, 0};
        DrawTextW(statusDc, text, -1, &measured,
            DT_CALCRECT | DT_WORDBREAK | DT_CENTER | DT_NOPREFIX);
        SelectObject(statusDc, oldFont);
        ReleaseDC(status, statusDc);
        warningHeight = (std::max)(20,
            static_cast<int>(measured.bottom - measured.top + 6));
    }
    const int warningTop = buttonTop - warningHeight;
    const int listTop = 35;
    MoveWindow(GetDlgItem(_dialog, IDC_REVISIONS), margin, listTop, available,
        (std::max)(0, warningTop - gap - listTop), TRUE);
    if (showStatus)
        MoveWindow(GetDlgItem(_dialog, IDC_SAVE_FILE_FIRST), margin, warningTop,
            available, warningHeight, TRUE);
    int y = buttonTop;
    for (const auto& row : rows)
    {
        int x = margin;
        for (const int index : row)
        {
            MoveWindow(GetDlgItem(_dialog, buttonIds[index]), x, y,
                uniformWidth, buttonHeight, TRUE);
            x += uniformWidth + gap;
        }
        y += buttonHeight + gap;
    }
}

void HistoryPanel::updateButtonTooltip()
{
    if (!_buttonTooltip || !_dialog)
        return;
    const int buttonIds[] = {IDC_CAPTURE, IDC_REFRESH, IDC_COMPARE, IDC_RESTORE,
        IDC_PANEL_SETTINGS, IDC_PANEL_ABOUT, IDC_PANEL_HISTORY};
    POINT cursor{};
    GetCursorPos(&cursor);
    int hovered = 0;
    for (const int id : buttonIds)
    {
        RECT bounds{};
        GetWindowRect(GetDlgItem(_dialog, id), &bounds);
        if (IsWindowVisible(GetDlgItem(_dialog, id)) && PtInRect(&bounds, cursor))
        {
            hovered = id;
            break;
        }
    }
    if (hovered != _tooltipButton)
    {
        if (_tooltipButton)
        {
            TOOLINFOW oldTool{sizeof(oldTool)};
            oldTool.hwnd = _dialog;
            oldTool.uId = static_cast<UINT_PTR>(_tooltipButton);
            SendMessageW(_buttonTooltip, TTM_TRACKACTIVATE, FALSE,
                reinterpret_cast<LPARAM>(&oldTool));
            RemovePropW(_dialog, L"NppHistoryPanelTooltipActive");
        }
        _tooltipButton = hovered;
        _tooltipHoverStarted = GetTickCount64();
    }
    if (!_tooltipButton || GetTickCount64() - _tooltipHoverStarted < 400)
        return;
    RECT bounds{};
    GetWindowRect(GetDlgItem(_dialog, _tooltipButton), &bounds);
    TOOLINFOW tool{sizeof(tool)};
    tool.hwnd = _dialog;
    tool.uId = static_cast<UINT_PTR>(_tooltipButton);
    SendMessageW(_buttonTooltip, TTM_TRACKPOSITION, 0,
        MAKELPARAM(bounds.left, bounds.bottom + 2));
    SendMessageW(_buttonTooltip, TTM_TRACKACTIVATE, TRUE,
        reinterpret_cast<LPARAM>(&tool));
    SetPropW(_dialog, L"NppHistoryPanelTooltipActive",
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(_tooltipButton)));
}

void HistoryPanel::configureButtonIcons()
{
    const int buttonIds[] = {IDC_CAPTURE, IDC_REFRESH, IDC_COMPARE, IDC_RESTORE,
        IDC_PANEL_SETTINGS, IDC_PANEL_ABOUT, IDC_PANEL_HISTORY};
    const int iconIds[] = {IDI_CAPTURE, IDI_REFRESH, IDI_COMPARE, IDI_RESTORE,
        IDI_SETTINGS, IDI_ABOUT, IDI_NPPHISTORY};
    const wchar_t* hints[] = {
        L"Create a new revision of the current file now.",
        L"Reload this file's revision list from history storage.",
        L"Compare the current file with the selected revision.",
        L"Replace the current file with the selected revision.",
        L"Open NppHistory settings.",
        L"Show NppHistory version and author information.",
        L"Show the revision history pane for the current file."
    };
    _buttonTooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        _dialog, nullptr, _instance, nullptr);
    if (_buttonTooltip)
    {
        SendMessageW(_buttonTooltip, TTM_SETMAXTIPWIDTH, 0, 360);
        SetTimer(_dialog, panelTooltipTimer, 100, nullptr);
        SetPropW(_dialog, L"NppHistoryPanelButtonTooltipWindow", _buttonTooltip);
    }
    int tooltipsAdded = 0;
    for (int index = 0; index < static_cast<int>(std::size(buttonIds)); ++index)
    {
        const HWND button = GetDlgItem(_dialog, buttonIds[index]);
        _buttonImages[index] = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 1, 1);
        const HICON icon = static_cast<HICON>(LoadImageW(_instance,
            MAKEINTRESOURCEW(iconIds[index]), IMAGE_ICON, 16, 16, LR_SHARED));
        if (!button || !_buttonImages[index] || !icon)
            continue;
        ImageList_AddIcon(_buttonImages[index], icon);
        SetWindowLongPtrW(button, GWL_STYLE,
            GetWindowLongPtrW(button, GWL_STYLE) | BS_OWNERDRAW);
        SetWindowSubclass(button, panelButtonSubclass, 1,
            reinterpret_cast<DWORD_PTR>(this));
        if (_buttonTooltip)
        {
            TOOLINFOW tool{sizeof(tool)};
            tool.uFlags = TTF_TRACK | TTF_ABSOLUTE;
            tool.hwnd = _dialog;
            tool.uId = static_cast<UINT_PTR>(buttonIds[index]);
            tool.lpszText = const_cast<wchar_t*>(hints[index]);
            if (SendMessageW(_buttonTooltip, TTM_ADDTOOLW, 0,
                reinterpret_cast<LPARAM>(&tool)))
                ++tooltipsAdded;
        }
    }
    SetPropW(_dialog, L"NppHistoryPanelButtonIconsReady", reinterpret_cast<HANDLE>(2));
    SetPropW(_dialog, L"NppHistoryPanelButtonHoverReady", reinterpret_cast<HANDLE>(7));
    SetPropW(_dialog, L"NppHistoryPanelButtonTooltipsReady",
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(tooltipsAdded)));
}

LRESULT CALLBACK HistoryPanel::panelButtonSubclass(HWND button, UINT message, WPARAM wParam,
    LPARAM lParam, UINT_PTR subclassId, DWORD_PTR referenceData)
{
    auto* panel = reinterpret_cast<HistoryPanel*>(referenceData);
    if (panel && message == WM_MOUSEMOVE)
    {
        if (panel->_hotButton != button)
        {
            if (panel->_hotButton)
                InvalidateRect(panel->_hotButton, nullptr, TRUE);
            panel->_hotButton = button;
            SetPropW(panel->_dialog, L"NppHistoryPanelHotButton",
                reinterpret_cast<HANDLE>(static_cast<INT_PTR>(GetDlgCtrlID(button))));
            InvalidateRect(button, nullptr, TRUE);
        }
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, button, 0};
        TrackMouseEvent(&tracking);
    }
    else if (panel && (message == WM_MOUSELEAVE
        || (message == WM_ENABLE && wParam == FALSE)))
    {
        if (panel->_hotButton == button)
        {
            panel->_hotButton = nullptr;
            RemovePropW(panel->_dialog, L"NppHistoryPanelHotButton");
            InvalidateRect(button, nullptr, TRUE);
        }
    }
    else if (message == WM_NCDESTROY)
    {
        if (panel && panel->_hotButton == button)
            panel->_hotButton = nullptr;
        RemoveWindowSubclass(button, panelButtonSubclass, subclassId);
    }
    return DefSubclassProc(button, message, wParam, lParam);
}

INT_PTR CALLBACK HistoryPanel::dialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* panel = reinterpret_cast<HistoryPanel*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        panel = reinterpret_cast<HistoryPanel*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        // CreateDialogParam sends WM_INITDIALOG before returning the HWND to create().
        // Store it now so initialization helpers address this dialog's controls.
        panel->_dialog = dialog;
        const HWND list = GetDlgItem(dialog, IDC_REVISIONS);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        LVCOLUMNW column{LVCF_TEXT | LVCF_WIDTH};
        column.cx = 118; column.pszText = const_cast<wchar_t*>(L"Time"); ListView_InsertColumn(list, 0, &column);
        column.cx = 150; column.pszText = const_cast<wchar_t*>(L"Comment"); ListView_InsertColumn(list, 1, &column);
        column.cx = 72; column.pszText = const_cast<wchar_t*>(L"Size"); ListView_InsertColumn(list, 2, &column);
        panel->configureButtonIcons();
        panel->layout();
        return TRUE;
    }
    if (!panel)
        return FALSE;
    if (message == WM_TIMER && wParam == panelTooltipTimer)
    {
        panel->updateButtonTooltip();
        return TRUE;
    }
    if (message == WM_SIZE) { panel->layout(); return TRUE; }
    if (message == WM_NOTIFY)
    {
        const auto* notification = reinterpret_cast<NMHDR*>(lParam);
        if (notification && notification->hwndFrom == panel->_nppData._nppHandle)
        {
            const UINT code = LOWORD(notification->code);
            if (code == dockingSwitchIn)
                panel->_opened = true;
            else if (code == dockingSwitchOff || code == dockingClose)
                panel->_opened = false;
            if ((code == dockingSwitchIn || code == dockingSwitchOff || code == dockingClose)
                && panel->_stateChangedCallback)
                panel->_stateChangedCallback();
        }
    }
    if (message == WM_CTLCOLORSTATIC
        && GetDlgCtrlID(reinterpret_cast<HWND>(lParam)) == IDC_SAVE_FILE_FIRST)
    {
        const HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, RGB(200, 0, 0));
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
    }
    if (message == WM_DRAWITEM)
    {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        const int buttonIds[] = {IDC_CAPTURE, IDC_REFRESH, IDC_COMPARE, IDC_RESTORE,
            IDC_PANEL_SETTINGS, IDC_PANEL_ABOUT, IDC_PANEL_HISTORY};
        for (int index = 0; index < static_cast<int>(std::size(buttonIds)); ++index)
        {
            if (item->CtlID != static_cast<UINT>(buttonIds[index]))
                continue;

            RECT bounds = item->rcItem;
            const bool hot = panel->_hotButton == item->hwndItem
                && !(item->itemState & ODS_DISABLED);
            if (hot)
            {
                const HBRUSH background = CreateSolidBrush((item->itemState & ODS_SELECTED)
                    ? RGB(204, 228, 247) : RGB(227, 242, 253));
                const HBRUSH border = CreateSolidBrush(RGB(0, 120, 215));
                FillRect(item->hDC, &bounds, background);
                FrameRect(item->hDC, &bounds, border);
                DeleteObject(border);
                DeleteObject(background);
                InflateRect(&bounds, -1, -1);
            }
            else
            {
                FillRect(item->hDC, &bounds, GetSysColorBrush(COLOR_BTNFACE));
                DrawEdge(item->hDC, &bounds,
                    (item->itemState & ODS_SELECTED) ? EDGE_SUNKEN : EDGE_RAISED,
                    BF_RECT | BF_ADJUST);
            }

            wchar_t label[64]{};
            GetWindowTextW(item->hwndItem, label, static_cast<int>(std::size(label)));
            HFONT font = reinterpret_cast<HFONT>(SendMessageW(item->hwndItem, WM_GETFONT, 0, 0));
            const HGDIOBJ oldFont = font ? SelectObject(item->hDC, font) : nullptr;
            SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, GetSysColor(
                (item->itemState & ODS_DISABLED) ? COLOR_GRAYTEXT : COLOR_BTNTEXT));

            RECT textBounds{0, 0, 0, 0};
            DrawTextW(item->hDC, label, -1, &textBounds, DT_SINGLELINE | DT_CALCRECT);
            const int iconWidth = panel->_buttonImages[index] ? 16 : 0;
            const int spacing = iconWidth ? 5 : 0;
            const int contentWidth = iconWidth + spacing + (textBounds.right - textBounds.left);
            int x = item->rcItem.left + ((item->rcItem.right - item->rcItem.left) - contentWidth) / 2;
            int y = item->rcItem.top + ((item->rcItem.bottom - item->rcItem.top) - 16) / 2;
            if (item->itemState & ODS_SELECTED) { ++x; ++y; }
            if (panel->_buttonImages[index])
                ImageList_Draw(panel->_buttonImages[index], 0, item->hDC, x, y, ILD_TRANSPARENT);
            RECT labelBounds{x + iconWidth + spacing, item->rcItem.top,
                item->rcItem.right - 3, item->rcItem.bottom};
            if (item->itemState & ODS_SELECTED)
                OffsetRect(&labelBounds, 1, 1);
            DrawTextW(item->hDC, label, -1, &labelBounds,
                DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
            if (item->itemState & ODS_FOCUS)
            {
                RECT focus = item->rcItem;
                InflateRect(&focus, -3, -3);
                DrawFocusRect(item->hDC, &focus);
            }
            if (oldFont)
                SelectObject(item->hDC, oldFont);
            return TRUE;
        }
    }
    if (message == WM_COMMAND)
    {
        for (const auto& command : commands)
            if (LOWORD(wParam) == command.paneControl
                && !IsWindowEnabled(GetDlgItem(dialog, command.paneControl))) return TRUE;
        if (LOWORD(wParam) == IDC_PANEL_HISTORY)
        {
            if (panel->_settings)
                SendMessageW(panel->_nppData._nppHandle, WM_COMMAND,
                    panel->_settings->hotkeyCommandIds[static_cast<int>(Command::history)], 0);
            SetFocus(GetDlgItem(dialog, IDC_REVISIONS));
            return TRUE;
        }
        if (LOWORD(wParam) == ID_REVISION_DELETE)
        {
            panel->deleteSelected();
            return TRUE;
        }
        if (LOWORD(wParam) == ID_REVISION_EDIT)
        {
            panel->editSelectedComment();
            return TRUE;
        }
        if (LOWORD(wParam) == ID_REVISION_COMPARE)
        {
            panel->compareSelected();
            return TRUE;
        }
        if (LOWORD(wParam) == ID_REVISION_RESTORE)
        {
            panel->restoreSelected();
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_REFRESH)
        {
            pluginLogger().write(LogLevel::debug, L"Button click", L"Refresh");
            panel->refresh(panel->_currentFile);
            pluginLogger().write(LogLevel::informational, L"Refresh", panel->_currentFile.wstring());
            actionStatus().show(L"History refreshed");
        }
        if (LOWORD(wParam) == IDC_COMPARE)
        {
            pluginLogger().write(LogLevel::debug, L"Button click", L"Compare");
            panel->compareSelected();
        }
        if (LOWORD(wParam) == IDC_RESTORE)
        {
            pluginLogger().write(LogLevel::debug, L"Button click", L"Restore");
            panel->restoreSelected();
        }
        if (LOWORD(wParam) == IDC_CAPTURE)
        {
            if (panel->_captureCallback) panel->_captureCallback();
        }
        if (LOWORD(wParam) == IDC_PANEL_SETTINGS)
        {
            if (panel->_settingsCallback) panel->_settingsCallback();
        }
        if (LOWORD(wParam) == IDC_PANEL_ABOUT)
        {
            if (panel->_aboutCallback) panel->_aboutCallback();
        }
        return TRUE;
    }
    if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(lParam)->idFrom == IDC_REVISIONS
        && reinterpret_cast<NMHDR*>(lParam)->code == LVN_ITEMCHANGED)
    {
        panel->updateActionButtons();
        return FALSE;
    }
    if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(lParam)->idFrom == IDC_REVISIONS
        && reinterpret_cast<NMHDR*>(lParam)->code == NM_RCLICK)
    {
        const auto* activation = reinterpret_cast<NMITEMACTIVATE*>(lParam);
        if (activation->iItem >= 0)
            panel->showRevisionActions(activation->iItem, activation->ptAction);
        return TRUE;
    }
    if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(lParam)->idFrom == IDC_REVISIONS
        && reinterpret_cast<NMHDR*>(lParam)->code == NM_DBLCLK)
    {
        panel->compareSelected();
        return TRUE;
    }
    return FALSE;
}

INT_PTR CALLBACK HistoryPanel::editCommentProc(HWND dialog, UINT message,
    WPARAM wParam, LPARAM lParam)
{
    auto* context = reinterpret_cast<EditCommentContext*>(
        GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        context = reinterpret_cast<EditCommentContext*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        const HICON icon = LoadIconW(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog,
            GWLP_HINSTANCE)), MAKEINTRESOURCEW(IDI_NPPHISTORY));
        SendMessageW(dialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        SetDlgItemTextW(dialog, IDC_EDIT_COMMENT, context->comment.c_str());
        addControlTooltip(dialog, IDC_EDIT_COMMENT, L"Enter the comment displayed beside this revision. The revision content is not changed.");
        addControlTooltip(dialog, IDOK, L"Save the revised comment and close this window.");
        addControlTooltip(dialog, IDCANCEL, L"Discard comment edits and close this window.");
        SendDlgItemMessageW(dialog, IDC_EDIT_COMMENT, EM_SETSEL, 0, -1);
        SetFocus(GetDlgItem(dialog, IDC_EDIT_COMMENT));
        centerWindowOnOwner(dialog, GetParent(dialog));
        return FALSE;
    }
    if (!context)
        return FALSE;
    if (message == WM_COMMAND && LOWORD(wParam) == IDOK)
    {
        wchar_t comment[2048]{};
        GetDlgItemTextW(dialog, IDC_EDIT_COMMENT, comment,
            static_cast<int>(std::size(comment)));
        context->comment = comment;
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

INT_PTR CALLBACK HistoryPanel::compareProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* context = reinterpret_cast<CompareContext*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        addControlTooltip(dialog, IDC_COMPARE_LEFT, L"Current editor content, shown read-only for comparison.");
        addControlTooltip(dialog, IDC_COMPARE_RIGHT, L"Selected stored revision, shown read-only for comparison.");
        context = reinterpret_cast<CompareContext*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        const HICON icon = LoadIconW(context->panel->_instance, MAKEINTRESOURCEW(IDI_NPPHISTORY));
        SendMessageW(dialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        SendMessageW(dialog, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
        const HWND combo = GetDlgItem(dialog, IDC_COMPARE_REVISION);
        for (const auto& revision : context->panel->_revisions)
        {
            const std::wstring label = revision.timestamp + L" - " + revision.reason;
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        SendMessageW(combo, CB_SETCURSEL, context->revisionIndex, 0);
        const bool tooltipsReady = configureComparisonToolbar(
            GetDlgItem(dialog, IDC_COMPARE_TOOLBAR), context->panel->_instance,
            context->toolbarImages);
        SetPropW(dialog, L"NppHistoryToolbarTooltipsReady",
            reinterpret_cast<HANDLE>(static_cast<INT_PTR>(tooltipsReady ? 1 : 0)));
        for (const int control : {IDC_COMPARE_LEFT, IDC_COMPARE_RIGHT})
        {
            const HWND editor = GetDlgItem(dialog, control);
            SetWindowSubclass(editor, comparisonEditSubclass, 1,
                reinterpret_cast<DWORD_PTR>(dialog));
        }
        SetWindowSubclass(GetDlgItem(dialog, IDC_COMPARE_LOCATION_MAP), comparisonMarkerSubclass, 1,
            reinterpret_cast<DWORD_PTR>(dialog));
        for (const int control : {IDC_COMPARE_LEFT_HEADER, IDC_COMPARE_RIGHT_HEADER})
            SetWindowSubclass(GetDlgItem(dialog, control), comparisonHeaderSubclass, 1,
                reinterpret_cast<DWORD_PTR>(dialog));
        CheckDlgButton(dialog, IDC_COMPARE_IGNORE_WHITESPACE,
            context->options.ignoreWhitespace ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_COMPARE_IGNORE_BLANK,
            context->options.ignoreBlankLines ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_COMPARE_IGNORE_CASE,
            context->options.ignoreCase ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_COMPARE_IGNORE_EOL,
            context->options.ignoreLineEndings ? BST_CHECKED : BST_UNCHECKED);
        fitWindowWithinOwner(dialog, context->panel->_nppData._nppHandle,
            96, 220, 760, 460);
        context->panel->renderComparison(dialog, *context);
        SendMessageW(dialog, WM_SIZE, 0, 0);
        centerWindowOnOwner(dialog, context->panel->_nppData._nppHandle);
        return TRUE;
    }
    if (!context)
        return FALSE;
    if (message == WM_COMMAND && LOWORD(wParam) >= ID_COMPARE_TOOL_FIRST
        && LOWORD(wParam) <= ID_COMPARE_TOOL_LAST)
    {
        const int image = LOWORD(wParam) - ID_COMPARE_TOOL_FIRST;
        pluginLogger().write(LogLevel::debug, L"Button click",
            std::wstring(L"Compare: ") + toolbarHint(image));
        if (image == 0)
        {
            const HWND toolbar = GetDlgItem(dialog, IDC_COMPARE_TOOLBAR);
            RECT button{};
            SendMessageW(toolbar, TB_GETRECT, ID_COMPARE_TOOL_FIRST + image,
                reinterpret_cast<LPARAM>(&button));
            MapWindowPoints(toolbar, HWND_DESKTOP, reinterpret_cast<POINT*>(&button), 2);
            context->panel->showRevisionPicker(dialog, *context,
                POINT{button.left, button.bottom}, TPM_LEFTALIGN);
        }
        else if (image == 1) context->panel->navigateDifference(dialog, *context, 1);
        else if (image == 2) context->panel->navigateDifference(dialog, *context, -1);
        else if (image == 3 && !context->differenceRows.empty())
        {
            context->currentDifference = 0;
            context->panel->navigateDifference(dialog, *context, 0);
        }
        else if (image == 4 && !context->differenceRows.empty())
            context->panel->navigateDifference(dialog, *context, 0);
        else if (image == 5 && !context->differenceRows.empty())
        {
            context->currentDifference = static_cast<int>(context->differenceRows.size()) - 1;
            context->panel->navigateDifference(dialog, *context, 0);
        }
        else if (image >= 6 && image <= 9 && !context->panel->_revisions.empty())
        {
            int revision = context->revisionIndex;
            if (image == 6) revision = 0;
            else if (image == 7) revision = (std::max)(0, revision - 1);
            else if (image == 8) revision = (std::min)(
                static_cast<int>(context->panel->_revisions.size()) - 1, revision + 1);
            else revision = static_cast<int>(context->panel->_revisions.size()) - 1;
            if (revision != context->revisionIndex)
            {
                context->revisionIndex = revision;
                SendDlgItemMessageW(dialog, IDC_COMPARE_REVISION, CB_SETCURSEL, revision, 0);
                context->topLine = 0;
                context->panel->renderComparison(dialog, *context);
            }
        }
        else if (image == 10)
        {
            HMENU options = CreatePopupMenu();
            AppendMenuW(options, MF_STRING | (context->options.ignoreWhitespace ? MF_CHECKED : 0),
                5001, L"Ignore whitespace");
            AppendMenuW(options, MF_STRING | (context->options.ignoreBlankLines ? MF_CHECKED : 0),
                5002, L"Ignore blank lines");
            AppendMenuW(options, MF_STRING | (context->options.ignoreCase ? MF_CHECKED : 0),
                5003, L"Ignore case");
            AppendMenuW(options, MF_STRING | (context->options.ignoreLineEndings ? MF_CHECKED : 0),
                5004, L"Ignore line endings");
            POINT point{};
            GetCursorPos(&point);
            const int selected = TrackPopupMenu(options,
                TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD,
                point.x, point.y, 0, dialog, nullptr);
            DestroyMenu(options);
            if (selected >= 5001 && selected <= 5004)
            {
                bool* option = selected == 5001 ? &context->options.ignoreWhitespace
                    : selected == 5002 ? &context->options.ignoreBlankLines
                    : selected == 5003 ? &context->options.ignoreCase
                    : &context->options.ignoreLineEndings;
                const bool previous = *option;
                if (selected == 5001) context->options.ignoreWhitespace = !context->options.ignoreWhitespace;
                if (selected == 5002) context->options.ignoreBlankLines = !context->options.ignoreBlankLines;
                if (selected == 5003) context->options.ignoreCase = !context->options.ignoreCase;
                if (selected == 5004) context->options.ignoreLineEndings = !context->options.ignoreLineEndings;
                pluginLogger().write(LogLevel::debug, L"Option change",
                    std::wstring(L"Compare option ") + std::to_wstring(selected)
                    + L": " + (previous ? L"true" : L"false") + L" -> "
                    + (*option ? L"true" : L"false"));
                context->topLine = 0;
                context->panel->renderComparison(dialog, *context);
            }
        }
        else if (image == 11) context->panel->renderComparison(dialog, *context);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDC_COMPARE_LOCATION_CLOSE
        && HIWORD(wParam) == STN_CLICKED)
    {
        SendMessageW(dialog, WM_SETREDRAW, FALSE, 0);
        context->locationVisible = false;
        ShowWindow(GetDlgItem(dialog, IDC_COMPARE_LOCATION_LABEL), SW_HIDE);
        ShowWindow(GetDlgItem(dialog, IDC_COMPARE_LOCATION_CLOSE), SW_HIDE);
        ShowWindow(GetDlgItem(dialog, IDC_COMPARE_LOCATION_MAP), SW_HIDE);
        SendMessageW(dialog, WM_SIZE, 0, 0);
        SendMessageW(dialog, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(dialog, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDC_COMPARE_RIGHT_HEADER
        && HIWORD(wParam) == STN_CLICKED)
    {
        RECT header{};
        GetWindowRect(reinterpret_cast<HWND>(lParam), &header);
        context->panel->showRevisionPicker(dialog, *context,
            POINT{header.right, header.bottom}, TPM_RIGHTALIGN);
        return TRUE;
    }
    if (message == WM_SIZE)
    {
        SendMessageW(dialog, WM_SETREDRAW, FALSE, 0);
        RECT area{}; GetClientRect(dialog, &area);
        constexpr int margin = 5;
        constexpr int toolbarHeight = 42;
        constexpr int headerHeight = 28;
        constexpr int paneStatusHeight = 25;
        constexpr int overallStatusHeight = 23;
        const int locationWidth = context->locationVisible ? 120 : 0;
        const int headerTop = toolbarHeight;
        const int paneTop = headerTop + headerHeight;
        const int paneStatusTop = (std::max)(paneTop + 50,
            static_cast<int>(area.bottom) - overallStatusHeight - paneStatusHeight);
        const int paneHeight = paneStatusTop - paneTop;
        const int leftX = margin + locationWidth;
        const int contentWidth = (std::max)(160, static_cast<int>(area.right) - leftX - margin);
        const int leftWidth = contentWidth / 2;
        const int rightX = leftX + leftWidth + 2;
        const int rightWidth = area.right - rightX - margin;
        const auto place = [&](int control, int x, int y, int width, int height)
        {
            SetWindowPos(GetDlgItem(dialog, control), nullptr,
                x, y, (std::max)(1, width), (std::max)(1, height),
                SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOCOPYBITS);
        };
        place(IDC_COMPARE_TOOLBAR, margin, 0, area.right - margin * 2, toolbarHeight);
        ShowWindow(GetDlgItem(dialog, IDC_COMPARE_LOCATION_LABEL), context->locationVisible ? SW_SHOW : SW_HIDE);
        ShowWindow(GetDlgItem(dialog, IDC_COMPARE_LOCATION_CLOSE), context->locationVisible ? SW_SHOW : SW_HIDE);
        ShowWindow(GetDlgItem(dialog, IDC_COMPARE_LOCATION_MAP), context->locationVisible ? SW_SHOW : SW_HIDE);
        if (context->locationVisible)
        {
            place(IDC_COMPARE_LOCATION_LABEL, margin, headerTop,
                locationWidth - 24, headerHeight);
            place(IDC_COMPARE_LOCATION_CLOSE, locationWidth - 20,
                headerTop, 18, headerHeight);
            place(IDC_COMPARE_LOCATION_MAP, margin, paneTop,
                locationWidth - 8, paneStatusTop + paneStatusHeight - paneTop);
        }
        place(IDC_COMPARE_LEFT_HEADER, leftX, headerTop, leftWidth, headerHeight);
        place(IDC_COMPARE_RIGHT_HEADER, rightX, headerTop, rightWidth, headerHeight);
        place(IDC_COMPARE_LEFT, leftX, paneTop, leftWidth, paneHeight);
        place(IDC_COMPARE_RIGHT, rightX, paneTop, rightWidth, paneHeight);
        place(IDC_COMPARE_LEFT_STATUS, leftX, paneStatusTop, leftWidth, paneStatusHeight);
        place(IDC_COMPARE_RIGHT_STATUS, rightX, paneStatusTop, rightWidth, paneStatusHeight);
        place(IDC_COMPARE_OVERALL_STATUS, margin, area.bottom - overallStatusHeight,
            area.right - margin * 2, overallStatusHeight);
        context->panel->configureComparisonScroll(dialog, *context);
        SendMessageW(dialog, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(dialog, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE
            | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
        return TRUE;
    }
    if (message == WM_COMMAND && (LOWORD(wParam) == IDC_COMPARE_PREVIOUS
        || LOWORD(wParam) == IDC_COMPARE_NEXT) && HIWORD(wParam) == BN_CLICKED)
    {
        context->panel->navigateDifference(dialog, *context,
            LOWORD(wParam) == IDC_COMPARE_PREVIOUS ? -1 : 1);
        return TRUE;
    }
    if (message == WM_COMMAND && (LOWORD(wParam) == IDC_COMPARE_IGNORE_WHITESPACE
        || LOWORD(wParam) == IDC_COMPARE_IGNORE_BLANK
        || LOWORD(wParam) == IDC_COMPARE_IGNORE_CASE
        || LOWORD(wParam) == IDC_COMPARE_IGNORE_EOL) && HIWORD(wParam) == BN_CLICKED)
    {
        const CompareOptions previous = context->options;
        context->options.ignoreWhitespace = IsDlgButtonChecked(dialog,
            IDC_COMPARE_IGNORE_WHITESPACE) == BST_CHECKED;
        context->options.ignoreBlankLines = IsDlgButtonChecked(dialog,
            IDC_COMPARE_IGNORE_BLANK) == BST_CHECKED;
        context->options.ignoreCase = IsDlgButtonChecked(dialog,
            IDC_COMPARE_IGNORE_CASE) == BST_CHECKED;
        context->options.ignoreLineEndings = IsDlgButtonChecked(dialog,
            IDC_COMPARE_IGNORE_EOL) == BST_CHECKED;
        const bool before[] = {previous.ignoreWhitespace, previous.ignoreBlankLines,
            previous.ignoreCase, previous.ignoreLineEndings};
        const bool after[] = {context->options.ignoreWhitespace, context->options.ignoreBlankLines,
            context->options.ignoreCase, context->options.ignoreLineEndings};
        const wchar_t* names[] = {L"Ignore whitespace", L"Ignore blank lines",
            L"Ignore case", L"Ignore line endings"};
        for (int index = 0; index < 4; ++index)
            if (before[index] != after[index])
                pluginLogger().write(LogLevel::debug, L"Option change",
                    std::wstring(L"Compare ") + names[index] + L": "
                    + (before[index] ? L"true" : L"false") + L" -> "
                    + (after[index] ? L"true" : L"false"));
        context->topLine = 0;
        context->panel->renderComparison(dialog, *context);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDC_COMPARE_REVISION
        && HIWORD(wParam) == CBN_SELCHANGE)
    {
        const int previous = context->revisionIndex;
        context->revisionIndex = static_cast<int>(SendDlgItemMessageW(dialog,
            IDC_COMPARE_REVISION, CB_GETCURSEL, 0, 0));
        pluginLogger().write(LogLevel::debug, L"Option change",
            L"Compare revision: " + std::to_wstring(previous) + L" -> "
            + std::to_wstring(context->revisionIndex));
        context->topLine = 0;
        context->panel->renderComparison(dialog, *context);
        return TRUE;
    }
    if (message == WM_VSCROLL && reinterpret_cast<HWND>(lParam)
        == GetDlgItem(dialog, IDC_COMPARE_SCROLL))
    {
        SCROLLINFO info{};
        info.cbSize = sizeof(info);
        info.fMask = SIF_ALL;
        GetScrollInfo(reinterpret_cast<HWND>(lParam), SB_CTL, &info);
        int position = context->topLine;
        switch (LOWORD(wParam))
        {
        case SB_LINEUP: --position; break;
        case SB_LINEDOWN: ++position; break;
        case SB_PAGEUP: position -= context->visibleLines; break;
        case SB_PAGEDOWN: position += context->visibleLines; break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK: position = info.nTrackPos; break;
        case SB_TOP: position = 0; break;
        case SB_BOTTOM: position = static_cast<int>(context->rows.size()); break;
        default: return TRUE;
        }
        context->panel->scrollComparisonTo(dialog, *context, position);
        return TRUE;
    }
    if (message == comparisonWheelMessage || message == WM_MOUSEWHEEL)
    {
        const int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        const int lines = (std::max)(1, std::abs(wheelDelta) / WHEEL_DELTA * 3);
        context->panel->scrollComparisonTo(dialog, *context,
            context->topLine + (wheelDelta > 0 ? -lines : lines));
        return TRUE;
    }
    if (message == WM_NOTIFY)
    {
        const auto* notification = reinterpret_cast<SCNotification*>(lParam);
        if (notification->nmhdr.idFrom == IDC_COMPARE_TOOLBAR
            && notification->nmhdr.code == TBN_GETINFOTIPW)
        {
            auto* info = reinterpret_cast<NMTBGETINFOTIPW*>(lParam);
            const int image = info->iItem - ID_COMPARE_TOOL_FIRST;
            wcsncpy_s(info->pszText, info->cchTextMax, toolbarHint(image), _TRUNCATE);
            return TRUE;
        }
        if (notification->nmhdr.code == TTN_GETDISPINFOW)
        {
            auto* info = reinterpret_cast<NMTTDISPINFOW*>(lParam);
            int command = static_cast<int>(notification->nmhdr.idFrom);
            if (command < ID_COMPARE_TOOL_FIRST || command > ID_COMPARE_TOOL_LAST)
            {
                const HWND toolbar = GetDlgItem(dialog, IDC_COMPARE_TOOLBAR);
                POINT cursor{};
                GetCursorPos(&cursor);
                ScreenToClient(toolbar, &cursor);
                const int buttonIndex = static_cast<int>(SendMessageW(toolbar,
                    TB_HITTEST, 0, reinterpret_cast<LPARAM>(&cursor)));
                TBBUTTON button{};
                if (buttonIndex >= 0 && SendMessageW(toolbar, TB_GETBUTTON,
                    buttonIndex, reinterpret_cast<LPARAM>(&button)))
                    command = button.idCommand;
            }
            if (command >= ID_COMPARE_TOOL_FIRST && command <= ID_COMPARE_TOOL_LAST)
            {
                wcsncpy_s(info->szText,
                    toolbarHint(command - ID_COMPARE_TOOL_FIRST), _TRUNCATE);
                info->lpszText = info->szText;
                return TRUE;
            }
        }
        if ((notification->nmhdr.idFrom == IDC_COMPARE_LEFT
            || notification->nmhdr.idFrom == IDC_COMPARE_RIGHT)
            && notification->nmhdr.code == SCN_UPDATEUI)
        {
            context->panel->updateComparisonStatus(dialog, *context);
            if (!context->synchronizingScroll && (notification->updated
                & (SC_UPDATE_V_SCROLL | SC_UPDATE_H_SCROLL)) != 0)
            {
                context->synchronizingScroll = true;
                const HWND source = reinterpret_cast<HWND>(notification->nmhdr.hwndFrom);
                const HWND target = GetDlgItem(dialog, notification->nmhdr.idFrom == IDC_COMPARE_LEFT
                    ? IDC_COMPARE_RIGHT : IDC_COMPARE_LEFT);
                context->topLine = static_cast<int>(SendMessageW(source, SCI_GETFIRSTVISIBLELINE, 0, 0));
                context->horizontalOffset = static_cast<int>(SendMessageW(source, SCI_GETXOFFSET, 0, 0));
                SendMessageW(target, SCI_SETFIRSTVISIBLELINE, context->topLine, 0);
                SendMessageW(target, SCI_SETXOFFSET, context->horizontalOffset, 0);
                SetScrollPos(GetDlgItem(dialog, IDC_COMPARE_SCROLL), SB_CTL, context->topLine, TRUE);
                context->synchronizingScroll = false;
                InvalidateRect(GetDlgItem(dialog, IDC_COMPARE_LOCATION_MAP), nullptr, FALSE);
            }
            return TRUE;
        }
    }
    if (message == comparisonMarkerClickMessage)
    {
        const HWND marker = GetDlgItem(dialog, static_cast<int>(wParam));
        RECT area{};
        GetClientRect(marker, &area);
        if (!context->differenceRows.empty() && area.bottom > area.top)
        {
            const int targetRow = HIWORD(lParam) * (std::max)(0,
                static_cast<int>(context->rows.size()) - 1) / (area.bottom - area.top);
            int closest = 0;
            int distance = std::abs(context->differenceRows[0] - targetRow);
            for (int index = 1; index < static_cast<int>(context->differenceRows.size()); ++index)
            {
                const int candidate = std::abs(context->differenceRows[index] - targetRow);
                if (candidate < distance)
                {
                    closest = index;
                    distance = candidate;
                }
            }
            context->currentDifference = closest;
            context->panel->navigateDifference(dialog, *context, 0);
        }
        return TRUE;
    }
    if (message == WM_CTLCOLORSTATIC)
    {
        const int control = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
        if (control == IDC_COMPARE_LOCATION_LABEL || control == IDC_COMPARE_LOCATION_CLOSE)
        {
            static HBRUSH headerBrush = CreateSolidBrush(RGB(215, 226, 238));
            SetBkColor(reinterpret_cast<HDC>(wParam), RGB(215, 226, 238));
            SetTextColor(reinterpret_cast<HDC>(wParam), RGB(20, 20, 20));
            return reinterpret_cast<INT_PTR>(headerBrush);
        }
        if (control == IDC_COMPARE_LEFT_STATUS || control == IDC_COMPARE_RIGHT_STATUS
            || control == IDC_COMPARE_OVERALL_STATUS)
        {
            SetBkColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_3DFACE));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_3DFACE));
        }
    }
    if (message == WM_DRAWITEM && (wParam == IDC_COMPARE_LEFT_HEADER
        || wParam == IDC_COMPARE_RIGHT_HEADER))
    {
        const auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        const int savedDc = SaveDC(draw->hDC);
        IntersectClipRect(draw->hDC, draw->rcItem.left, draw->rcItem.top,
            draw->rcItem.right, draw->rcItem.bottom);
        FillRect(draw->hDC, &draw->rcItem, GetSysColorBrush(COLOR_3DFACE));
        RECT box = draw->rcItem;
        InflateRect(&box, -2, -2);
        HBRUSH fill = CreateSolidBrush(RGB(210, 225, 239));
        HPEN outline = CreatePen(PS_SOLID, 1, RGB(185, 205, 224));
        HGDIOBJ oldBrush = SelectObject(draw->hDC, fill);
        HGDIOBJ oldPen = SelectObject(draw->hDC, outline);
        RoundRect(draw->hDC, box.left, box.top, box.right, box.bottom, 8, 8);
        SelectObject(draw->hDC, oldPen);
        SelectObject(draw->hDC, oldBrush);
        DeleteObject(outline);
        DeleteObject(fill);
        wchar_t textValue[2048]{};
        GetWindowTextW(draw->hwndItem, textValue, static_cast<int>(std::size(textValue)));
        NONCLIENTMETRICSW metrics{sizeof(metrics)};
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
        metrics.lfStatusFont.lfWeight = FW_BOLD;
        HFONT font = CreateFontIndirectW(&metrics.lfStatusFont);
        HGDIOBJ oldFont = SelectObject(draw->hDC, font);
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, RGB(15, 15, 15));
        RECT textBox = box;
        InflateRect(&textBox, -9, 0);
        UINT textAlignment = DT_CENTER;
        if (wParam == IDC_COMPARE_LEFT_HEADER)
        {
            RECT measured{0, 0, textBox.right - textBox.left - 22, textBox.bottom - textBox.top};
            DrawTextW(draw->hDC, textValue, -1, &measured,
                DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
            const int textWidth = (std::min)(measured.right - measured.left,
                textBox.right - textBox.left - 22);
            const int groupWidth = textWidth + 22;
            const int groupLeft = textBox.left
                + (textBox.right - textBox.left - groupWidth) / 2;
            const HICON statusIcon = static_cast<HICON>(LoadImageW(context->panel->_instance,
                MAKEINTRESOURCEW(context->differenceRows.empty()
                    ? IDI_WINMERGE_EQUAL_TEXT : IDI_WINMERGE_DIFFERENT_TEXT),
                IMAGE_ICON, 16, 16, LR_SHARED));
            DrawIconEx(draw->hDC, groupLeft, (box.top + box.bottom - 16) / 2,
                statusIcon, 16, 16, 0, nullptr, DI_NORMAL);
            textBox.left = groupLeft + 22;
            textBox.right = textBox.left + textWidth;
            textAlignment = DT_LEFT;
        }
        else
            textBox.right -= 24;
        DrawTextW(draw->hDC, textValue, -1, &textBox,
            textAlignment | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (wParam == IDC_COMPARE_RIGHT_HEADER)
        {
            HPEN menuPen = CreatePen(PS_SOLID, 1, RGB(45, 45, 45));
            HGDIOBJ previousPen = SelectObject(draw->hDC, menuPen);
            const int right = box.right - 8;
            const int left = right - 10;
            const int middle = (box.top + box.bottom) / 2;
            for (const int offset : {-3, 0, 3})
            {
                MoveToEx(draw->hDC, left, middle + offset, nullptr);
                LineTo(draw->hDC, right, middle + offset);
            }
            SelectObject(draw->hDC, previousPen);
            DeleteObject(menuPen);
        }
        SelectObject(draw->hDC, oldFont);
        DeleteObject(font);
        RestoreDC(draw->hDC, savedDc);
        return TRUE;
    }
    if (message == WM_DRAWITEM && wParam == IDC_COMPARE_LOCATION_MAP)
    {
        const auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        const int width = draw->rcItem.right - draw->rcItem.left;
        const int height = draw->rcItem.bottom - draw->rcItem.top;
        HDC bufferDc = CreateCompatibleDC(draw->hDC);
        HBITMAP bufferBitmap = CreateCompatibleBitmap(draw->hDC,
            (std::max)(1, width), (std::max)(1, height));
        HGDIOBJ previousBitmap = bufferDc && bufferBitmap
            ? SelectObject(bufferDc, bufferBitmap) : nullptr;
        HDC paintDc = previousBitmap ? bufferDc : draw->hDC;
        if (previousBitmap)
            SetWindowOrgEx(paintDc, draw->rcItem.left, draw->rcItem.top, nullptr);
        HBRUSH background = CreateSolidBrush(RGB(232, 236, 242));
        FillRect(paintDc, &draw->rcItem, background);
        DeleteObject(background);
        const int top = draw->rcItem.top + 7;
        const int bottom = draw->rcItem.bottom - 7;
        const int barWidth = (std::max)(10, width / 4);
        RECT bars[2] = {
            {draw->rcItem.left + width / 8, top, draw->rcItem.left + width / 8 + barWidth, bottom},
            {draw->rcItem.right - width / 8 - barWidth, top,
                draw->rcItem.right - width / 8, bottom}
        };
        for (const RECT& bar : bars)
        {
            FillRect(paintDc, &bar, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            FrameRect(paintDc, &bar, GetSysColorBrush(COLOR_3DSHADOW));
        }
        const int mapHeight = (std::max)(1, bottom - top - 2);
        const int denominator = (std::max)(1, static_cast<int>(context->rows.size()));
        for (int row = 0; row < static_cast<int>(context->rows.size()); ++row)
        {
            for (int side = 0; side < 2; ++side)
            {
                const DiffKind kind = side == 0 ? context->rows[row].currentKind
                    : context->rows[row].revisionKind;
                if (kind == DiffKind::unchanged) continue;
                const int y = top + 1 + row * mapHeight / denominator;
                RECT block{bars[side].left + 1, y, bars[side].right - 1,
                    y + (std::max)(2, mapHeight / denominator)};
                const COLORREF locationColour = kind == DiffKind::empty
                    ? RGB(192, 192, 192) : RGB(239, 203, 5);
                HBRUSH brush = CreateSolidBrush(locationColour);
                FillRect(paintDc, &block, brush);
                DeleteObject(brush);
            }
        }
        const int viewTop = top + 1 + context->topLine * mapHeight / denominator;
        const int viewBottom = top + 1 + (context->topLine + context->visibleLines)
            * mapHeight / denominator;
        HPEN viewPen = CreatePen(PS_SOLID, 2, RGB(115, 115, 115));
        HGDIOBJ oldPen = SelectObject(paintDc, viewPen);
        HGDIOBJ oldBrush = SelectObject(paintDc, GetStockObject(NULL_BRUSH));
        for (const RECT& bar : bars)
            Rectangle(paintDc, bar.left - 2, viewTop, bar.right + 2,
                (std::max)(viewTop + 4, (std::min)(bottom, viewBottom)));
        SelectObject(paintDc, oldBrush);
        SelectObject(paintDc, oldPen);
        DeleteObject(viewPen);
        if (previousBitmap)
        {
            BitBlt(draw->hDC, draw->rcItem.left, draw->rcItem.top, width, height,
                paintDc, draw->rcItem.left, draw->rcItem.top, SRCCOPY);
            SelectObject(bufferDc, previousBitmap);
        }
        if (bufferBitmap) DeleteObject(bufferBitmap);
        if (bufferDc) DeleteDC(bufferDc);
        return TRUE;
    }
    if (message == WM_DRAWITEM && (wParam == IDC_COMPARE_LEFT_MARKERS
        || wParam == IDC_COMPARE_RIGHT_MARKERS))
    {
        const auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        FillRect(draw->hDC, &draw->rcItem, GetSysColorBrush(COLOR_3DFACE));
        const bool leftSide = wParam == IDC_COMPARE_LEFT_MARKERS;
        const int height = draw->rcItem.bottom - draw->rcItem.top;
        const int denominator = (std::max)(1, static_cast<int>(context->rows.size()) - 1);
        for (int row = 0; row < static_cast<int>(context->rows.size()); ++row)
        {
            const DiffKind kind = leftSide ? context->rows[row].currentKind
                : context->rows[row].revisionKind;
            const bool changed = leftSide
                ? (kind == DiffKind::removed || kind == DiffKind::changed)
                : (kind == DiffKind::added || kind == DiffKind::changed);
            if (!changed)
                continue;
            const int y = draw->rcItem.top + row * (std::max)(1, height - 2) / denominator;
            RECT tick{leftSide ? draw->rcItem.left : draw->rcItem.right - 3,
                y, leftSide ? draw->rcItem.left + 3 : draw->rcItem.right, y + 2};
            HBRUSH brush = CreateSolidBrush(markerColour(kind));
            FillRect(draw->hDC, &tick, brush);
            DeleteObject(brush);
        }
        const int firstRow = (std::max)(0, context->topLine);
        const int lastRow = (std::min)(static_cast<int>(context->rows.size()),
            firstRow + context->visibleLines + 1);
        for (int row = firstRow; row < lastRow; ++row)
        {
            const DiffKind kind = leftSide ? context->rows[row].currentKind
                : context->rows[row].revisionKind;
            const bool changed = leftSide
                ? (kind == DiffKind::removed || kind == DiffKind::changed)
                : (kind == DiffKind::added || kind == DiffKind::changed);
            if (!changed)
                continue;
            const int y = draw->rcItem.top + (row - context->topLine) * context->lineHeight
                + context->lineHeight / 2;
            if (y < draw->rcItem.top || y >= draw->rcItem.bottom)
                continue;
            const COLORREF colour = markerColour(kind);
            HBRUSH dotBrush = CreateSolidBrush(colour);
            HPEN dotPen = CreatePen(PS_SOLID, 1, RGB(GetRValue(colour) / 2,
                GetGValue(colour) / 2, GetBValue(colour) / 2));
            HGDIOBJ previousBrush = SelectObject(draw->hDC, dotBrush);
            HGDIOBJ previousPen = SelectObject(draw->hDC, dotPen);
            const int radius = 2;
            const int center = (draw->rcItem.left + draw->rcItem.right) / 2;
            Ellipse(draw->hDC, center - radius, y - radius, center + radius + 1, y + radius + 1);
            SelectObject(draw->hDC, previousPen);
            SelectObject(draw->hDC, previousBrush);
            DeleteObject(dotPen);
            DeleteObject(dotBrush);
        }
        return TRUE;
    }
    if (message == WM_COMMAND && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL))
    {
        EndDialog(dialog, LOWORD(wParam));
        return TRUE;
    }
    if (message == WM_DESTROY)
    {
        RemovePropW(dialog, L"NppHistoryToolbarTooltipsReady");
        RemovePropW(GetDlgItem(dialog, IDC_COMPARE_TOOLBAR),
            L"NppHistoryComparisonImageCount");
        RemovePropW(GetDlgItem(dialog, IDC_COMPARE_TOOLBAR),
            L"NppHistoryComparisonImageSize");
        if (context->toolbarImages) ImageList_Destroy(context->toolbarImages);
        context->toolbarImages = nullptr;
        return TRUE;
    }
    return FALSE;
}
}
