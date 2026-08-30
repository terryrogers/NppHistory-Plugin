#pragma once

#include "HistoryStore.h"
#include "PluginInterface.h"
#include "Settings.h"
#include "TextDiff.h"

#include <filesystem>
#include <array>
#include <vector>
#include <windows.h>
#include <commctrl.h>

namespace npphistory
{
class HistoryPanel
{
public:
    bool create(HINSTANCE instance, const NppData& nppData, HistoryStore& store,
        const Settings& settings,
        int commandId, PFUNCPLUGINCMD captureCallback, PFUNCPLUGINCMD settingsCallback,
        PFUNCPLUGINCMD aboutCallback);
    void show();
    void refresh(const std::filesystem::path& file);
    void compare() { compareSelected(); }
    void restore() { restoreSelected(); }
    HWND handle() const noexcept { return _dialog; }

private:
    struct CompareContext
    {
        HistoryPanel* panel = nullptr;
        int revisionIndex = 0;
        bool synchronizingScroll = false;
        int topLine = 0;
        int visibleLines = 1;
        int lineHeight = 1;
        int lineNumberWidth = 1;
        int currentDifference = -1;
        int horizontalOffset = 0;
        HIMAGELIST toolbarImages = nullptr;
        bool locationVisible = true;
        std::wstring currentText;
        std::wstring revisionText;
        std::vector<std::uint8_t> revisionBytes;
        CompareOptions options;
        std::vector<DiffRow> rows;
        std::vector<int> differenceRows;
    };

    struct EditCommentContext
    {
        std::wstring comment;
    };

    static INT_PTR CALLBACK dialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK compareProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK editCommentProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK panelButtonSubclass(HWND button, UINT message, WPARAM wParam,
        LPARAM lParam, UINT_PTR subclassId, DWORD_PTR referenceData);
    void layout();
    void configureButtonIcons();
    void updateButtonTooltip();
    void updateActionButtons();
    void showRevisionActions(int index, POINT anchor);
    void editSelectedComment();
    void deleteSelected();
    void compareSelected();
    void renderComparison(HWND dialog, CompareContext& context);
    void configureComparisonScroll(HWND dialog, CompareContext& context);
    void scrollComparisonTo(HWND dialog, CompareContext& context, int topLine);
    void navigateDifference(HWND dialog, CompareContext& context, int direction);
    void showRevisionPicker(HWND dialog, CompareContext& context, POINT anchor, UINT alignment);
    void updateComparisonNavigation(HWND dialog, CompareContext& context);
    void updateComparisonStatus(HWND dialog, CompareContext& context);
    std::wstring currentSourceText() const;
    void restoreSelected();
    int selectedIndex() const;

    HINSTANCE _instance = nullptr;
    NppData _nppData{};
    HistoryStore* _store = nullptr;
    const Settings* _settings = nullptr;
    HWND _dialog = nullptr;
    HWND _buttonTooltip = nullptr;
    int _tooltipButton = 0;
    unsigned long long _tooltipHoverStarted = 0;
    std::filesystem::path _currentFile;
    std::vector<RevisionInfo> _revisions;
    bool _fileSaved = false;
    HWND _hotButton = nullptr;
    PFUNCPLUGINCMD _captureCallback = nullptr;
    PFUNCPLUGINCMD _settingsCallback = nullptr;
    PFUNCPLUGINCMD _aboutCallback = nullptr;
    std::array<HIMAGELIST, 6> _buttonImages{};
};
}
