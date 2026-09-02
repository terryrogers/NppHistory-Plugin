#include "HistoryPanel.h"
#include "DocumentTabIndicators.h"
#include "HistoryCatalog.h"
#include "HistoryStore.h"
#include "Logger.h"
#include "PluginInterface.h"
#include "Settings.h"
#include "Utilities.h"
#include "UpdateChecker.h"
#include "UpdateInstaller.h"
#include "Version.h"
#include "resource.h"

#include <array>
#include <atomic>
#include <commctrl.h>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <optional>
#include <process.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <windows.h>

namespace fs = std::filesystem;
using namespace npphistory;

namespace
{
constexpr wchar_t pluginName[] = L"NppHistory";
constexpr UINT timerPeriodMilliseconds = 1000;
constexpr ULONGLONG startupUpdateDelayMilliseconds = 90ULL * 1000ULL;
constexpr ULONGLONG resumeUpdateDelayMilliseconds = 30ULL * 1000ULL;
constexpr ULONGLONG updateSchedulePeriodMilliseconds = 60ULL * 1000ULL;
constexpr UINT updateCompleteMessage = WM_APP + 240;
constexpr UINT installCompleteMessage = WM_APP + 243;

enum MenuIndex { captureIndex, compareIndex, historyIndex, settingsIndex, aboutIndex,
    restoreIndex, refreshIndex, menuCount };
// Keep exported indices stable for existing Notepad++ shortcut configuration.
constexpr int commandMenuIndices[] = {captureIndex, compareIndex, restoreIndex,
    refreshIndex, settingsIndex, aboutIndex, historyIndex};

struct DirtyState
{
    ULONGLONG lastEditTick = 0;
    ULONGLONG periodicBaseTick = 0;
};

HINSTANCE moduleInstance = nullptr;
NppData nppData{};
FuncItem menuItems[menuCount]{};
Settings settings;
HistoryStore historyStore;
HistoryCatalog historyCatalog;
HistoryPanel historyPanel;
fs::path settingsFile;
fs::path pluginConfigPath;
std::unordered_map<UINT_PTR, DirtyState> dirtyBuffers;
std::unordered_map<UINT_PTR, fs::path> lastKnownPaths;
std::unordered_map<UINT_PTR, fs::path> pendingRenamePaths;
std::unordered_map<UINT_PTR, ULONGLONG> missingSince;
std::unordered_set<UINT_PTR> missingAlertsShown;
std::unordered_set<UINT_PTR> restoreSaveBuffers;
bool ready = false;
bool configurationLoaded = false;
UINT_PTR activeTimerId = 0;
UINT_PTR lastActiveBuffer = 0;
ULONGLONG lastIntervalTick = 0;
ULONGLONG automaticUpdateEligibleTick = 0;
ULONGLONG nextUpdateScheduleTick = 0;
ULONGLONG nextUpdateStatusRefreshTick = 0;
std::atomic_bool updateCheckInProgress = false;
std::atomic_bool updateShuttingDown = false;
HANDLE updateThreadHandle = nullptr;
std::atomic_bool installDownloadInProgress = false;
HANDLE installThreadHandle = nullptr;
ReleaseInfo availableUpdate;
void syncCommandStates();
bool actionAvailable(Command command);
void configureCommandMenu();
void appendDocumentContextMenu(HMENU menu);
bool documentContextPending = false;
UINT_PTR currentBuffer();

void prepareRestoreSave()
{
    restoreSaveBuffers.insert(currentBuffer());
}

void cancelRestoreSave()
{
    restoreSaveBuffers.erase(currentBuffer());
}

struct UpdateRequest
{
    HWND notifyWindow = nullptr;
    bool manual = false;
    bool includePrereleases = false;
};

struct UpdateCompletion
{
    bool manual = false;
    UpdateCheckResult result;
};

struct InstallRequest
{
    HWND notifyWindow = nullptr;
    ReleaseInfo release;
    fs::path destination;
    fs::path updaterDestination;
};

struct InstallCompletion
{
    ReleaseInfo release;
    fs::path destination;
    fs::path updaterDestination;
    bool success = false;
    std::wstring detail;
};

struct ToolbarAsset
{
    HICON icon = nullptr;
    HBITMAP bitmap = nullptr;
};

std::array<ToolbarAsset, commandCount> toolbarAssets{};
std::array<HBITMAP, commandCount> pluginMenuBitmaps{};
std::array<ShortcutKey, commandCount> commandShortcuts{};
HMENU pluginCommandMenu = nullptr;
constexpr ULONG_PTR contextMenuMarker = 0x4E504843;

fs::path pathForBuffer(UINT_PTR bufferId)
{
    if (!nppData._nppHandle || !bufferId || bufferId == static_cast<UINT_PTR>(-1))
        return {};
    const LRESULT length = SendMessageW(nppData._nppHandle, NPPM_GETFULLPATHFROMBUFFERID,
        static_cast<WPARAM>(bufferId), 0);
    if (length < 0)
        return {};
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    const LRESULT copied = SendMessageW(nppData._nppHandle, NPPM_GETFULLPATHFROMBUFFERID,
        static_cast<WPARAM>(bufferId), reinterpret_cast<LPARAM>(value.data()));
    if (copied < 0)
        return {};
    value.resize(static_cast<std::size_t>(copied));
    return fs::path(value);
}

UINT_PTR currentBuffer()
{
    return static_cast<UINT_PTR>(SendMessageW(nppData._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0));
}

fs::path currentPath()
{
    return pathForBuffer(currentBuffer());
}

std::vector<HWND> documentTabControls()
{
    std::vector<HWND> controls;
    EnumChildWindows(nppData._nppHandle, [](HWND window, LPARAM value) -> BOOL {
        if (GetParent(window) != nppData._nppHandle)
            return TRUE;
        wchar_t className[64]{};
        GetClassNameW(window, className, static_cast<int>(std::size(className)));
        if (wcscmp(className, WC_TABCONTROLW) == 0)
            reinterpret_cast<std::vector<HWND>*>(value)->push_back(window);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&controls));
    return controls;
}

void refreshDocumentTabIndicators()
{
    if (!nppData._nppHandle)
        return;
    auto controls = documentTabControls();
    int autoSaveIndicatorCount = 0;
    int historyIndicatorCount = 0;
    int excludedItemCount = 0;
    int normalItemCount = 0;
    int documentTabCount = 0;
    int reservedTabCount = 0;
    for (HWND tabs : controls)
    {
        const int count = TabCtrl_GetItemCount(tabs);
        std::unordered_map<LPARAM, unsigned> indicators;
        documentTabCount += count;
        for (int index = 0; index < count; ++index)
        {
            TCITEMW item{};
            item.mask = TCIF_PARAM;
            if (!SendMessageW(tabs, TCM_GETITEMW, index, reinterpret_cast<LPARAM>(&item))) continue;
            const UINT_PTR bufferId = static_cast<UINT_PTR>(item.lParam);
            const fs::path path = pathForBuffer(bufferId);
            const bool autoSaveExcluded = settings.autoSaveEnabled
                && !settings.externalAutoSavePluginDetected
                && settings.isAutoSaveExcluded(path);
            const bool historyExcluded = settings.historyEnabled
                && settings.isHistoryExcluded(path);
            if (autoSaveExcluded)
            {
                ++autoSaveIndicatorCount;
            }
            if (historyExcluded)
            {
                ++historyIndicatorCount;
            }
            const int indicatorCount = static_cast<int>(autoSaveExcluded)
                + static_cast<int>(historyExcluded);
            indicators[static_cast<LPARAM>(bufferId)] = (autoSaveExcluded ? 1U : 0U)
                | (historyExcluded ? 2U : 0U);
            if (indicatorCount > 0)
                ++excludedItemCount;
            else
                ++normalItemCount;
        }
        updateDocumentTabDecorations(tabs, indicators, moduleInstance);
        for (int index = 0; index < count; ++index)
        {
            DocumentTabDecorationMetrics metrics;
            if (documentTabDecorationMetrics(tabs, index, metrics)
                && metrics.reservedWidth > 0 && metrics.iconsLeft > metrics.textRight
                && metrics.iconsRight <= metrics.nativeTrailingStart)
                ++reservedTabCount;
        }
    }
    SetPropW(nppData._nppHandle, L"NppHistoryAutoSaveTabIndicatorCount",
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(autoSaveIndicatorCount)));
    SetPropW(nppData._nppHandle, L"NppHistoryHistoryTabIndicatorCount",
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(historyIndicatorCount)));
    SetPropW(nppData._nppHandle, L"NppHistoryExcludedDocumentTabCount",
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(excludedItemCount)));
    SetPropW(nppData._nppHandle, L"NppHistoryNormalDocumentTabCount",
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(normalItemCount)));
    SetPropW(nppData._nppHandle, L"NppHistoryDocumentTabCount",
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(documentTabCount)));
    SetPropW(nppData._nppHandle, L"NppHistoryReservedDocumentTabCount",
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(reservedTabCount)));
    const HICON autoSaveDisabledIcon = static_cast<HICON>(LoadImageW(moduleInstance,
        MAKEINTRESOURCEW(IDI_AUTOSAVE_DISABLED), IMAGE_ICON, 16, 16, LR_SHARED));
    const HICON historyDisabledIcon = static_cast<HICON>(LoadImageW(moduleInstance,
        MAKEINTRESOURCEW(IDI_HISTORY_DISABLED), IMAGE_ICON, 16, 16, LR_SHARED));
    SetPropW(nppData._nppHandle, L"NppHistoryTabIndicatorIconCount",
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(
            static_cast<int>(autoSaveDisabledIcon != nullptr)
            + static_cast<int>(historyDisabledIcon != nullptr))));
}

HWND currentEditor()
{
    int view = 0;
    SendMessageW(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&view));
    return view == 1 ? nppData._scintillaSecondHandle : nppData._scintillaMainHandle;
}

bool isSavableFile(const fs::path& path)
{
    std::error_code error;
    if (path.empty() || !fs::is_regular_file(path, error))
        return false;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY) == 0;
}

bool existingFile(const fs::path& path)
{
    std::error_code error;
    return !path.empty() && fs::is_regular_file(path, error);
}

void showReconcileAlert(const ReconcileResult& result)
{
    if (result.moveFailed)
    {
        const std::wstring message = L"NppHistory found the previous history folder but could not move it.\n\nPrevious: "
            + result.previousHistoryPath.wstring() + L"\n\nHistory will continue using the previous location until this is resolved.";
        centeredMessageBox(nppData._nppHandle, message.c_str(), pluginName, MB_OK | MB_ICONWARNING);
    }
    else if (result.historyMissing)
    {
        const std::wstring message = L"NppHistory detected that this file moved, but its recorded history folder no longer exists.\n\nExpected location: "
            + result.previousHistoryPath.wstring() + L"\n\nA new history will be started at the current location.";
        centeredMessageBox(nppData._nppHandle, message.c_str(), pluginName, MB_OK | MB_ICONWARNING);
    }
    else if (result.ambiguousMatch)
    {
        centeredMessageBox(nppData._nppHandle,
            L"NppHistory found more than one missing catalogue entry with identical content, so it could not safely identify the moved file. A new history record was created.",
            pluginName, MB_OK | MB_ICONWARNING);
    }
}

void reconcileFile(UINT_PTR bufferId, const fs::path& path,
    const std::optional<fs::path>& previousPath = std::nullopt)
{
    if (!existingFile(path))
        return;
    const auto result = historyCatalog.reconcile(path, previousPath);
    showReconcileAlert(result);
    lastKnownPaths[bufferId] = path;
    missingSince.erase(bufferId);
    missingAlertsShown.erase(bufferId);
}

void detectMissingBuffers()
{
    const ULONGLONG now = GetTickCount64();
    for (const auto& [bufferId, path] : lastKnownPaths)
    {
        if (existingFile(path))
        {
            missingSince.erase(bufferId);
            missingAlertsShown.erase(bufferId);
            continue;
        }
        auto [entry, inserted] = missingSince.try_emplace(bufferId, now);
        if (!inserted && now - entry->second >= 2000 && missingAlertsShown.insert(bufferId).second)
        {
            const fs::path history = historyCatalog.historyPathFor(path);
            const std::wstring message = L"Notepad++ has an open file that no longer exists at:\n\n"
                + path.wstring() + L"\n\nNppHistory has retained its history at:\n\n"
                + (history.empty() ? L"(no recorded history location)" : history.wstring())
                + L"\n\nIf the file is saved or reopened at a new location, NppHistory will try to move and reconnect its history automatically.";
            centeredMessageBox(nppData._nppHandle, message.c_str(), pluginName, MB_OK | MB_ICONWARNING);
        }
    }
}

void refreshPanel()
{
    if (ready)
    {
        refreshDocumentTabIndicators();
        historyPanel.refresh(currentPath());
        syncCommandStates();
    }
}

void saveBuffer(UINT_PTR bufferId)
{
    const fs::path path = pathForBuffer(bufferId);
    if (!isSavableFile(path))
    {
        dirtyBuffers.erase(bufferId);
        return;
    }
    if (settings.isAutoSaveExcluded(path))
        return;
    SendMessageW(nppData._nppHandle, NPPM_SAVEFILE, 0, reinterpret_cast<LPARAM>(path.c_str()));
}

void saveConfiguredScope(UINT_PTR preferredBuffer = 0)
{
    if (!settings.autoSaveEnabled || settings.externalAutoSavePluginDetected
        || dirtyBuffers.empty())
        return;
    if (settings.autoSaveScope == AutoSaveScope::allOpenFiles)
    {
        std::vector<UINT_PTR> buffers;
        buffers.reserve(dirtyBuffers.size());
        for (const auto& [bufferId, state] : dirtyBuffers)
        {
            static_cast<void>(state);
            buffers.push_back(bufferId);
        }
        for (const UINT_PTR bufferId : buffers)
            saveBuffer(bufferId);
    }
    else
        saveBuffer(preferredBuffer ? preferredBuffer : currentBuffer());
}

unsigned __stdcall updateThreadProc(void* parameter)
{
    std::unique_ptr<UpdateRequest> request(static_cast<UpdateRequest*>(parameter));
    auto completion = std::make_unique<UpdateCompletion>();
    completion->manual = request->manual;
    completion->result = checkGitHubForUpdates(NPPHISTORY_VERSION_SEMVER_W,
        request->includePrereleases);
    if (updateShuttingDown || !PostMessageW(request->notifyWindow, updateCompleteMessage, 0,
        reinterpret_cast<LPARAM>(completion.get())))
    {
        updateCheckInProgress = false;
        return 0;
    }
    completion.release();
    return 0;
}

void startUpdateCheck(bool manual, std::optional<bool> includePrereleases = std::nullopt)
{
    bool expected = false;
    if (!updateCheckInProgress.compare_exchange_strong(expected, true))
    {
        settings.refreshUpdateStatus(true);
        return;
    }
    if (updateThreadHandle)
    {
        CloseHandle(updateThreadHandle);
        updateThreadHandle = nullptr;
    }
    auto request = std::make_unique<UpdateRequest>();
    request->notifyWindow = nppData._nppHandle;
    request->manual = manual;
    request->includePrereleases = includePrereleases.value_or(settings.includePrereleaseUpdates);
    pluginLogger().write(LogLevel::informational,
        manual ? L"Manual update check started" : L"Automatic update check started",
        request->includePrereleases ? L"Prereleases included" : L"Stable releases only");
    const uintptr_t thread = _beginthreadex(nullptr, 0, updateThreadProc, request.get(), 0, nullptr);
    if (!thread)
    {
        updateCheckInProgress = false;
        settings.recordUpdateFailure(currentUnixSeconds());
        pluginLogger().write(LogLevel::error, L"Update check could not start",
            L"The background update-check thread could not be created");
        settings.lastUpdateStatus = L"Last check failed: update check could not be started";
        settings.save(settingsFile);
        settings.refreshUpdateStatus(false);
        return;
    }
    request.release();
    updateThreadHandle = reinterpret_cast<HANDLE>(thread);
}

fs::path moduleFilePath()
{
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(moduleInstance, path.data(),
        static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
        return {};
    path.resize(length);
    return path;
}

void openReleasePage(const ReleaseInfo& release)
{
    if (!trustedReleaseUrl(release.url))
        return;
    const auto opened = reinterpret_cast<INT_PTR>(ShellExecuteW(nppData._nppHandle,
        L"open", release.url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (opened <= 32)
        centeredMessageBox(nppData._nppHandle,
            L"Windows could not open the release page. Visit the NppHistory-Plugin repository on GitHub manually.",
            L"NppHistory Update", MB_OK | MB_ICONWARNING);
}

unsigned __stdcall installThreadProc(void* parameter)
{
    std::unique_ptr<InstallRequest> request(static_cast<InstallRequest*>(parameter));
    auto completion = std::make_unique<InstallCompletion>();
    completion->release = request->release;
    completion->destination = request->destination;
    completion->updaterDestination = request->updaterDestination;
    completion->success = downloadVerifiedUpdatePackage(request->release,
        request->destination, request->updaterDestination, completion->detail);
    if (updateShuttingDown || !PostMessageW(request->notifyWindow, installCompleteMessage, 0,
        reinterpret_cast<LPARAM>(completion.get())))
    {
        installDownloadInProgress = false;
        return 0;
    }
    completion.release();
    return 0;
}

void beginUpdateInstall(const ReleaseInfo& release)
{
    if (!trustedUpdateAsset(release))
    {
        centeredMessageBox(nppData._nppHandle,
            L"This release does not contain the verified x64 asset required for automatic installation. You can still install it from the release page.",
            L"NppHistory Update", MB_OK | MB_ICONINFORMATION);
        openReleasePage(release);
        return;
    }
    bool expected = false;
    if (!installDownloadInProgress.compare_exchange_strong(expected, true))
    {
        centeredMessageBox(nppData._nppHandle, L"The update is already being downloaded.",
            L"NppHistory Update", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (installThreadHandle)
    {
        CloseHandle(installThreadHandle);
        installThreadHandle = nullptr;
    }
    auto request = std::make_unique<InstallRequest>();
    request->notifyWindow = nppData._nppHandle;
    request->release = release;
    request->destination = pluginConfigPath / L"updates" / release.tag
        / L"NppHistory.pending.dll";
    request->updaterDestination = request->destination.parent_path()
        / L"NppHistoryUpdater.pending.exe";
    const std::wstring shownVersion = displayVersion(release.tag);
    settings.lastUpdateStatus = L"Downloading and verifying " + shownVersion + L"...";
    settings.refreshUpdateStatus(false);
    pluginLogger().write(LogLevel::informational, L"Update download started", shownVersion);
    const uintptr_t thread = _beginthreadex(nullptr, 0, installThreadProc, request.get(), 0, nullptr);
    if (!thread)
    {
        installDownloadInProgress = false;
        settings.lastUpdateStatus = L"Update installation could not start";
        settings.refreshUpdateStatus(false);
        centeredMessageBox(nppData._nppHandle,
            L"The background update download could not be started.", L"NppHistory Update",
            MB_OK | MB_ICONERROR);
        return;
    }
    request.release();
    installThreadHandle = reinterpret_cast<HANDLE>(thread);
}

bool directoryWritable(const fs::path& directory)
{
    const fs::path probe = directory / (L"NppHistory-access-"
        + std::to_wstring(GetCurrentProcessId()) + L".tmp");
    HANDLE file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    CloseHandle(file);
    DeleteFileW(probe.c_str());
    return true;
}

void handleInstallCompletion(std::unique_ptr<InstallCompletion> completion)
{
    installDownloadInProgress = false;
    if (installThreadHandle)
    {
        CloseHandle(installThreadHandle);
        installThreadHandle = nullptr;
    }
    if (!completion->success)
    {
        settings.lastUpdateStatus = L"Update installation failed: " + completion->detail;
        settings.refreshUpdateStatus(false);
        pluginLogger().write(LogLevel::error, L"Update download failed", completion->detail);
        centeredMessageBox(nppData._nppHandle, completion->detail.c_str(),
            L"NppHistory Update", MB_OK | MB_ICONERROR);
        return;
    }
    const fs::path pluginDll = moduleFilePath();
    const fs::path updater = completion->updaterDestination;
    std::wstring notepadPath(32768, L'\0');
    const DWORD notepadLength = GetModuleFileNameW(nullptr, notepadPath.data(),
        static_cast<DWORD>(notepadPath.size()));
    if (pluginDll.empty() || !fs::is_regular_file(updater) || notepadLength == 0
        || notepadLength >= notepadPath.size())
    {
        const std::wstring detail = L"The verified restart installer is missing from the staging folder. The existing plugin was not changed.";
        settings.lastUpdateStatus = L"Update installation failed: staged updater missing";
        settings.refreshUpdateStatus(false);
        pluginLogger().write(LogLevel::error, L"Update launch failed", detail);
        centeredMessageBox(nppData._nppHandle, detail.c_str(), L"NppHistory Update",
            MB_OK | MB_ICONERROR);
        return;
    }
    notepadPath.resize(notepadLength);
    const fs::path result = pluginConfigPath / L"update-result.ini";
    DeleteFileW(result.c_str());
    std::wstring parameters = L"--wait-pid " + quoteCommandLineArgument(
        std::to_wstring(GetCurrentProcessId()))
        + L" --source " + quoteCommandLineArgument(completion->destination.wstring())
        + L" --target " + quoteCommandLineArgument(pluginDll.wstring())
        + L" --restart " + quoteCommandLineArgument(notepadPath)
        + L" --result " + quoteCommandLineArgument(result.wstring())
        + L" --version " + quoteCommandLineArgument(displayVersion(completion->release.tag))
        + L" --sha256 " + quoteCommandLineArgument(completion->release.assetDigest.substr(7));
    SHELLEXECUTEINFOW execute{sizeof(execute)};
    execute.fMask = SEE_MASK_NOCLOSEPROCESS;
    execute.hwnd = nppData._nppHandle;
    execute.lpVerb = directoryWritable(pluginDll.parent_path()) ? L"open" : L"runas";
    execute.lpFile = updater.c_str();
    execute.lpParameters = parameters.c_str();
    execute.lpDirectory = updater.parent_path().c_str();
    execute.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&execute))
    {
        const DWORD error = GetLastError();
        const std::wstring detail = error == ERROR_CANCELLED
            ? L"Administrator approval was cancelled. The existing plugin was not changed."
            : L"The restart installer could not be launched. Windows error "
                + std::to_wstring(error) + L".";
        settings.lastUpdateStatus = L"Update installation cancelled or failed";
        settings.refreshUpdateStatus(false);
        pluginLogger().write(LogLevel::error, L"Update launch failed", detail);
        centeredMessageBox(nppData._nppHandle, detail.c_str(), L"NppHistory Update",
            MB_OK | MB_ICONERROR);
        return;
    }
    if (execute.hProcess)
        CloseHandle(execute.hProcess);
    pluginLogger().write(LogLevel::informational, L"Restart installer launched",
        displayVersion(completion->release.tag));
    PostMessageW(nppData._nppHandle, WM_CLOSE, 0, 0);
}

void handleUpdateCompletion(std::unique_ptr<UpdateCompletion> completion)
{
    updateCheckInProgress = false;
    if (updateThreadHandle)
    {
        CloseHandle(updateThreadHandle);
        updateThreadHandle = nullptr;
    }
    const bool successful = completion->result.status == UpdateCheckStatus::updateAvailable
        || completion->result.status == UpdateCheckStatus::upToDate;
    if (successful)
    {
        settings.recordUpdateSuccess(currentUnixSeconds());
        const std::wstring publishedVersion = displayVersion(completion->result.release.tag);
        settings.lastUpdateStatus = completion->result.status == UpdateCheckStatus::updateAvailable
            ? L"Update available: " + publishedVersion
            : (publishedVersion.empty() ? L"Up to date"
                : L"Up to date \u2014 latest published version: " + publishedVersion);
        if (!settings.save(settingsFile))
            pluginLogger().write(LogLevel::error, L"Settings save failed", settingsFile.wstring());
        pluginLogger().write(LogLevel::informational,
            completion->manual ? L"Manual update check completed" : L"Automatic update check completed",
            settings.lastUpdateStatus);
    }
    else
    {
        settings.recordUpdateFailure(currentUnixSeconds());
        settings.lastUpdateStatus = L"Last check failed: " + completion->result.detail;
        if (!settings.save(settingsFile))
            pluginLogger().write(LogLevel::error, L"Settings save failed", settingsFile.wstring());
        pluginLogger().write(LogLevel::warning, L"Update check failure",
            completion->result.detail);
    }
    if (completion->result.status == UpdateCheckStatus::updateAvailable)
        availableUpdate = completion->result.release;
    else if (completion->result.status == UpdateCheckStatus::upToDate)
        availableUpdate = {};
    settings.refreshUpdateStatus(false);
    if (completion->result.status == UpdateCheckStatus::updateAvailable)
    {
        const auto& release = completion->result.release;
        if (!shouldNotifyUpdate(release.tag, settings.lastNotifiedVersion,
            completion->manual))
            return;
        if (!completion->manual)
        {
            settings.lastNotifiedVersion = release.tag;
            if (!settings.save(settingsFile))
                pluginLogger().write(LogLevel::error, L"Settings save failed", settingsFile.wstring());
        }
        const bool installable = trustedUpdateAsset(release);
        const std::wstring content = L"Available version: " + displayVersion(release.tag)
            + L"\nInstalled version: " + NPPHISTORY_VERSION_TEXT_W
            + (installable
                ? L"\n\nNppHistory can download the verified x64 update, restart Notepad++, install it and reopen Notepad++."
                : L"\n\nThis release can be viewed and installed manually from GitHub.");
        const TASKDIALOG_BUTTON buttons[] = {
            {1001, L"Download, restart and install"},
            {1002, L"View release"},
            {IDCANCEL, L"Later"}};
        TASKDIALOGCONFIG dialog{sizeof(dialog)};
        dialog.hwndParent = nppData._nppHandle;
        dialog.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
        dialog.pszWindowTitle = L"NppHistory Update Available";
        dialog.pszMainIcon = TD_INFORMATION_ICON;
        dialog.pszMainInstruction = L"A newer NppHistory version is available";
        dialog.pszContent = content.c_str();
        dialog.pButtons = installable ? buttons : buttons + 1;
        dialog.cButtons = installable ? 3U : 2U;
        dialog.nDefaultButton = installable ? 1001 : 1002;
        int selected = IDCANCEL;
        if (SUCCEEDED(TaskDialogIndirect(&dialog, &selected, nullptr, nullptr)))
        {
            if (selected == 1001)
            {
                if (!completion->manual || !settings.closeForUpdateInstall())
                    beginUpdateInstall(release);
            }
            else if (selected == 1002)
                openReleasePage(release);
        }
    }
}

LRESULT CALLBACK mainWindowSubclass(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR)
{
    if (ready && message == WM_CONTEXTMENU
        && (reinterpret_cast<HWND>(wParam) == nppData._scintillaMainHandle
            || reinterpret_cast<HWND>(wParam) == nppData._scintillaSecondHandle))
    {
        // Let Notepad++ build its normal editor menu and switch the active split view.
        const bool previous = documentContextPending;
        documentContextPending = true;
        const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        documentContextPending = previous;
        return result;
    }
    if (ready && message == WM_INITMENUPOPUP && documentContextPending)
    {
        documentContextPending = false; // Only the root editor popup, never its child menus.
        const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        appendDocumentContextMenu(reinterpret_cast<HMENU>(wParam));
        return result;
    }
    if (ready && message == WM_INITMENUPOPUP
        && reinterpret_cast<HMENU>(wParam) == pluginCommandMenu)
    {
        const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        configureCommandMenu();
        syncCommandStates();
        return result;
    }
    if (message == installCompleteMessage)
    {
        std::unique_ptr<InstallCompletion> completion(
            reinterpret_cast<InstallCompletion*>(lParam));
        if (completion && ready)
            handleInstallCompletion(std::move(completion));
        return 0;
    }
    if (message == updateCompleteMessage)
    {
        std::unique_ptr<UpdateCompletion> completion(
            reinterpret_cast<UpdateCompletion*>(lParam));
        if (completion && ready)
            handleUpdateCompletion(std::move(completion));
        return 0;
    }
    if (message == settingsCheckUpdateMessage)
    {
        startUpdateCheck(true, wParam != FALSE);
        return 0;
    }
    if (message == WM_POWERBROADCAST && ready
        && (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND))
    {
        const ULONGLONG now = GetTickCount64();
        automaticUpdateEligibleTick = now + resumeUpdateDelayMilliseconds;
        nextUpdateScheduleTick = automaticUpdateEligibleTick;
        return TRUE;
    }
    if (message == WM_ACTIVATEAPP && wParam == FALSE && ready
        && settings.shouldAutoSave(AutoSaveTrigger::focusLoss))
        saveConfiguredScope();
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(window, mainWindowSubclass, 1);
    return DefSubclassProc(window, message, wParam, lParam);
}

void CALLBACK timerProc(HWND, UINT, UINT_PTR, DWORD)
{
    try
    {
        detectMissingBuffers();
        syncCommandStates();
        const ULONGLONG now = GetTickCount64();
        if (now >= nextUpdateStatusRefreshTick)
        {
            nextUpdateStatusRefreshTick = now + 60ULL * 1000ULL;
            settings.refreshUpdateStatus(false);
        }
        if (settings.autoUpdateEnabled && now >= automaticUpdateEligibleTick
            && now >= nextUpdateScheduleTick)
        {
            nextUpdateScheduleTick = now + updateSchedulePeriodMilliseconds;
            if (!updateCheckInProgress && settings.updateCheckDue(currentUnixSeconds()))
                startUpdateCheck(false);
        }
        if (!settings.autoSaveEnabled)
            return;
        if (settings.shouldAutoSave(AutoSaveTrigger::afterEdit) && !dirtyBuffers.empty())
        {
            std::vector<UINT_PTR> due;
            for (const auto& [bufferId, state] : dirtyBuffers)
            {
                if (settings.afterEditDue(now, state.lastEditTick))
                    due.push_back(bufferId);
            }
            for (const UINT_PTR bufferId : due)
                saveBuffer(bufferId);
        }
        if (!dirtyBuffers.empty() && settings.intervalDue(now, lastIntervalTick))
        {
            lastIntervalTick = now;
            saveConfiguredScope();
        }
    }
    catch (...) {}
}

void showHistory()
{
    pluginLogger().write(LogLevel::debug, L"Button click", L"History");
    historyPanel.refresh(currentPath());
    historyPanel.show();
}

void captureNow()
{
    pluginLogger().write(LogLevel::debug, L"Button click", L"Capture");
    if (!settings.shouldCreateRevision(RevisionTrigger::manual))
    {
        centeredMessageBox(nppData._nppHandle,
            L"Enable revision history in NppHistory Settings before capturing a revision.",
            pluginName, MB_OK | MB_ICONINFORMATION);
        return;
    }
    const fs::path path = currentPath();
    if (!isSavableFile(path))
    {
        centeredMessageBox(nppData._nppHandle, L"Save this note to a file before capturing a revision.",
            pluginName, MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (settings.isHistoryExcluded(path))
    {
        centeredMessageBox(nppData._nppHandle,
            L"Revision history is disabled for this file by an exclusion pattern.",
            pluginName, MB_OK | MB_ICONINFORMATION);
        return;
    }
    reconcileFile(currentBuffer(), path);
    if (SendMessageW(currentEditor(), SCI_GETMODIFY, 0, 0) != 0)
    {
        if (SendMessageW(nppData._nppHandle, NPPM_SAVEFILE, 0,
            reinterpret_cast<LPARAM>(path.c_str())) == FALSE)
        {
            centeredMessageBox(nppData._nppHandle, L"The current note could not be saved.",
                pluginName, MB_OK | MB_ICONERROR);
            pluginLogger().write(LogLevel::error, L"Capture failed",
                L"The current note could not be saved: " + path.wstring());
            return;
        }
    }
    if (historyStore.captureFile(path, L"Manual capture", true))
        pluginLogger().write(LogLevel::informational, L"Capture", path.wstring());
    else
        pluginLogger().write(LogLevel::error, L"Capture failed", path.wstring());
    refreshPanel();
}

void compareCurrent()
{
    if (!actionAvailable(Command::compare)) return;
    pluginLogger().write(LogLevel::debug, L"Button click", L"Compare");
    if (!historyPanel.visible())
        historyPanel.refresh(currentPath());
    historyPanel.compare();
}

void restoreCurrent()
{
    if (!actionAvailable(Command::restore)) return;
    pluginLogger().write(LogLevel::debug, L"Button click", L"Restore");
    historyPanel.restore();
}

void refreshCurrent()
{
    if (!actionAvailable(Command::refresh)) return;
    pluginLogger().write(LogLevel::debug, L"Button click", L"Refresh");
    historyPanel.refresh(currentPath());
    pluginLogger().write(LogLevel::informational, L"Refresh", currentPath().wstring());
}

std::wstring boolText(bool value) { return value ? L"Enabled" : L"Disabled"; }

void logSettingsChanges(const Settings& previous, const Settings& current)
{
    unsigned changedCount = 0;
    const auto displayValue = [](const std::wstring& value) {
        if (value.empty())
            return std::wstring(L"(not set)");
        std::wstring display = value;
        for (wchar_t& character : display)
        {
            if (character == L'\r' || character == L'\n')
                character = L' ';
        }
        return display;
    };
    const auto change = [&](std::wstring_view name, const std::wstring& before,
        const std::wstring& after)
    {
        if (before == after) return;
        ++changedCount;
        pluginLogger().write(LogLevel::debug, L"Setting change",
            std::wstring(name) + L": " + displayValue(before) + L" -> "
                + displayValue(after));
    };
    const auto boolean = [&](std::wstring_view name, bool before, bool after) {
        change(name, boolText(before), boolText(after));
    };
    boolean(L"Context menu submenu", previous.contextSubmenu, current.contextSubmenu);
    for (int row = 0; row < commandCount; ++row)
    {
        const wchar_t* surfaces[] = {L"Pane", L"Plugins menu", L"Toolbar", L"Document context menu"};
        for (int column = 0; column < 4; ++column)
            boolean(std::wstring(surfaces[column]) + L": " + commands[row].name,
                previous.commandVisible(static_cast<Command>(row), static_cast<CommandSurface>(column)),
                current.commandVisible(static_cast<Command>(row), static_cast<CommandSurface>(column)));
    }
    const auto hotkeyValue = [](const HotkeySetting& value) {
        if (!value.enabled) return std::wstring(L"disabled");
        std::wstring text;
        if (value.ctrl) text += L"Ctrl+";
        if (value.alt) text += L"Alt+";
        if (value.shift) text += L"Shift+";
        if (value.key >= VK_F1 && value.key <= VK_F12)
            text += L"F" + std::to_wstring(value.key - VK_F1 + 1);
        else text += static_cast<wchar_t>(value.key);
        return text;
    };
    const auto hotkey = [&](std::wstring_view name, const HotkeySetting& before,
        const HotkeySetting& after) {
        if (!(before == after))
            change(name, hotkeyValue(before), hotkeyValue(after));
    };
    for (const Command command : commandOrder)
        hotkey(std::wstring(commands[static_cast<int>(command)].name) + L" hotkey",
            previous.commandHotkey(command), current.commandHotkey(command));
    boolean(L"Auto Save enabled", previous.autoSaveEnabled, current.autoSaveEnabled);
    boolean(L"After editing stops", previous.autoSaveAfterEdit, current.autoSaveAfterEdit);
    change(L"After edit seconds", std::to_wstring(previous.afterEditSeconds), std::to_wstring(current.afterEditSeconds));
    boolean(L"Save on focus loss", previous.autoSaveOnFocusLoss, current.autoSaveOnFocusLoss);
    boolean(L"Save at intervals", previous.autoSaveAtIntervals, current.autoSaveAtIntervals);
    change(L"Interval minutes", std::to_wstring(previous.intervalMinutes), std::to_wstring(current.intervalMinutes));
    boolean(L"Save on tab change", previous.autoSaveOnTabChange, current.autoSaveOnTabChange);
    boolean(L"Save on exit", previous.autoSaveOnExit, current.autoSaveOnExit);
    change(L"Auto Save scope", autoSaveScopeDisplayName(previous.autoSaveScope),
        autoSaveScopeDisplayName(current.autoSaveScope));
    change(L"Auto Save exclusions", previous.autoSaveExclusions, current.autoSaveExclusions);
    boolean(L"History enabled", previous.historyEnabled, current.historyEnabled);
    boolean(L"Revision before save", previous.historyBeforeSave, current.historyBeforeSave);
    boolean(L"Revision after save", previous.historyAfterSave, current.historyAfterSave);
    boolean(L"Revision before restore", previous.historyBeforeRestore, current.historyBeforeRestore);
    change(L"History location", historyLocationDisplayName(previous.historyLocationMode),
        historyLocationDisplayName(current.historyLocationMode));
    change(L"Custom history root", previous.customHistoryRoot.wstring(), current.customHistoryRoot.wstring());
    change(L"History exclusions", previous.historyExclusions, current.historyExclusions);
    boolean(L"Logging enabled", previous.loggingEnabled, current.loggingEnabled);
    change(L"Log level", logLevelDisplayName(previous.logLevel),
        logLevelDisplayName(current.logLevel));
    change(L"Log location", logLocationDisplayName(previous.logLocationMode),
        logLocationDisplayName(current.logLocationMode));
    change(L"Custom log file", previous.customLogFile.wstring(), current.customLogFile.wstring());
    change(L"Log maximum size MB", std::to_wstring(previous.logMaximumSizeMb), std::to_wstring(current.logMaximumSizeMb));
    change(L"Log rollover", logRolloverDisplayName(previous.logRolloverMode),
        logRolloverDisplayName(current.logRolloverMode));
    change(L"Log archives", std::to_wstring(previous.logArchivesToRetain), std::to_wstring(current.logArchivesToRetain));
    boolean(L"Automatic update checks", previous.autoUpdateEnabled, current.autoUpdateEnabled);
    change(L"Update frequency", updateFrequencyDisplayName(previous.updateFrequency),
        updateFrequencyDisplayName(current.updateFrequency));
    boolean(L"Include prereleases", previous.includePrereleaseUpdates, current.includePrereleaseUpdates);
    if (changedCount > 0)
        pluginLogger().write(LogLevel::informational, L"Settings changed",
            std::to_wstring(changedCount) + L" option(s) updated");
}

void editSettings()
{
    pluginLogger().write(LogLevel::debug, L"Button click", L"Settings");
    const Settings previous = settings;
    settings.defaultLogFile = pluginConfigPath / L"NppHistory.log";
    if (settings.edit(nppData._nppHandle, moduleInstance))
    {
        const bool openLog = settings.openLogNow;
        const bool installUpdate = settings.installUpdateNow;
        settings.openLogNow = false;
        settings.installUpdateNow = false;
        pluginLogger().configure(settings, pluginConfigPath);
        logSettingsChanges(previous, settings);
        configureCommandMenu();
        if (!settings.save(settingsFile))
            pluginLogger().write(LogLevel::error, L"Settings save failed", settingsFile.wstring());
        historyCatalog.configure(pluginConfigPath / L"catalog.db", settings.historyLocationMode,
            settings.customHistoryRoot, pluginConfigPath / L"history");
        reconcileFile(currentBuffer(), currentPath());
        refreshPanel();
        const ULONGLONG now = GetTickCount64();
        lastIntervalTick = now;
        automaticUpdateEligibleTick = now + resumeUpdateDelayMilliseconds;
        nextUpdateScheduleTick = automaticUpdateEligibleTick;
        nextUpdateStatusRefreshTick = now;
        for (auto& [bufferId, state] : dirtyBuffers)
        {
            (void)bufferId;
            state.lastEditTick = now;
        }
        if (openLog)
        {
            if (pluginLogger().ensureFile())
            {
                const auto logPath = pluginLogger().path();
                if (SendMessageW(nppData._nppHandle, NPPM_DOOPEN, 0,
                    reinterpret_cast<LPARAM>(logPath.c_str())) == FALSE)
                    centeredMessageBox(nppData._nppHandle, L"The log file could not be opened in Notepad++.",
                        pluginName, MB_OK | MB_ICONERROR);
            }
            else
                centeredMessageBox(nppData._nppHandle, L"The log file could not be created or accessed.",
                    pluginName, MB_OK | MB_ICONERROR);
        }
        if (installUpdate)
            beginUpdateInstall(availableUpdate);
    }
}

INT_PTR CALLBACK aboutProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_INITDIALOG)
    {
        const HICON icon = LoadIconW(moduleInstance, MAKEINTRESOURCEW(IDI_NPPHISTORY));
        SendMessageW(dialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        SendMessageW(dialog, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
        SetDlgItemTextW(dialog, IDC_ABOUT_VERSION,
            (std::wstring(L"Version: ") + NPPHISTORY_VERSION_TEXT_W + L"  x64").c_str());
        SetDlgItemTextW(dialog, IDC_ABOUT_RELEASE_DATE,
            (std::wstring(L"Release Date: ")
                + localDateDisplay(NPPHISTORY_RELEASE_DATE_W)).c_str());
        centerWindowOnOwner(dialog, nppData._nppHandle);
        return TRUE;
    }
    if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(lParam)->idFrom == IDC_ABOUT_AUTHOR
        && (reinterpret_cast<NMHDR*>(lParam)->code == NM_CLICK
            || reinterpret_cast<NMHDR*>(lParam)->code == NM_RETURN))
    {
        const auto* link = reinterpret_cast<NMLINK*>(lParam);
        ShellExecuteW(dialog, L"open", link->item.szUrl, nullptr, nullptr, SW_SHOWNORMAL);
        return TRUE;
    }
    if (message == WM_COMMAND && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL))
    {
        EndDialog(dialog, LOWORD(wParam));
        return TRUE;
    }
    return FALSE;
}

void showAbout()
{
    pluginLogger().write(LogLevel::debug, L"Button click", L"About");
    DialogBoxParamW(moduleInstance, MAKEINTRESOURCEW(IDD_ABOUT), nppData._nppHandle,
        aboutProc, 0);
}

void setMenuItem(int index, const wchar_t* name, PFUNCPLUGINCMD callback)
{
    wcscpy_s(menuItems[index]._itemName, name);
    menuItems[index]._pFunc = callback;
}

void ensureConfigurationLoaded()
{
    if (configurationLoaded)
        return;
    const int configLength = static_cast<int>(SendMessageW(nppData._nppHandle,
        NPPM_GETPLUGINSCONFIGDIR, 0, 0));
    std::wstring config(static_cast<std::size_t>((std::max)(0, configLength)) + 1, L'\0');
    if (configLength > 0)
        SendMessageW(nppData._nppHandle, NPPM_GETPLUGINSCONFIGDIR,
            static_cast<WPARAM>(config.size()), reinterpret_cast<LPARAM>(config.data()));
    config.resize(wcslen(config.c_str()));
    pluginConfigPath = fs::path(config) / pluginName;
    settingsFile = pluginConfigPath / L"NppHistory.ini";
    settings.load(settingsFile);
    std::array<wchar_t, 32768> modulePath{};
    const DWORD modulePathLength = GetModuleFileNameW(moduleInstance, modulePath.data(),
        static_cast<DWORD>(modulePath.size()));
    if (modulePathLength > 0 && modulePathLength < modulePath.size())
    {
        const fs::path pluginsRoot = fs::path(modulePath.data()).parent_path().parent_path();
        settings.externalAutoSavePluginPath = findExternalAutoSavePlugin(pluginsRoot);
    }
    settings.externalAutoSavePluginDetected = !settings.externalAutoSavePluginPath.empty()
        || GetModuleHandleW(L"AutoSave.dll") != nullptr;
    settings.defaultLogFile = pluginConfigPath / L"NppHistory.log";
    pluginLogger().configure(settings, pluginConfigPath);
    if (settings.externalAutoSavePluginDetected)
        pluginLogger().write(LogLevel::warning, L"NppHistory Auto Save disabled",
            settings.externalAutoSavePluginPath.empty() ? L"AutoSave.dll is loaded."
                : L"AutoSave.dll is installed at "
                    + settings.externalAutoSavePluginPath.wstring());
    configurationLoaded = true;
}

HBITMAP createToolbarBitmap(HICON icon)
{
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, 16, 16);
    HGDIOBJ previous = SelectObject(memory, bitmap);
    RECT area{0, 0, 16, 16};
    HBRUSH background = CreateSolidBrush(RGB(255, 0, 255));
    FillRect(memory, &area, background);
    DrawIconEx(memory, 0, 0, icon, 16, 16, 0, nullptr, DI_NORMAL);
    DeleteObject(background);
    SelectObject(memory, previous);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return bitmap;
}

void setToolbarCommandEnabled(int commandId, bool enabled)
{
    const std::pair<int, bool> state{commandId, enabled};
    EnumChildWindows(nppData._nppHandle, [](HWND window, LPARAM value) -> BOOL {
        wchar_t className[64]{};
        GetClassNameW(window, className, static_cast<int>(std::size(className)));
        if (wcscmp(className, TOOLBARCLASSNAMEW) != 0)
            return TRUE;
        const auto* state = reinterpret_cast<const std::pair<int, bool>*>(value);
        if (SendMessageW(window, TB_COMMANDTOINDEX, state->first, 0) >= 0)
        {
            SendMessageW(window, TB_ENABLEBUTTON, state->first,
                MAKELPARAM(state->second ? TRUE : FALSE, 0));
            LRESULT buttonState = SendMessageW(window, TB_GETSTATE, state->first, 0);
            if (buttonState >= 0)
            {
                if (state->second)
                    buttonState |= TBSTATE_ENABLED;
                else
                    buttonState &= ~TBSTATE_ENABLED;
                SendMessageW(window, TB_SETSTATE, state->first,
                    MAKELPARAM(buttonState, 0));
            }
            InvalidateRect(window, nullptr, FALSE);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&state));
}

HBITMAP createPluginMenuBitmap(int resource)
{
    const HICON icon = static_cast<HICON>(LoadImageW(moduleInstance,
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

void configurePluginMenuIcons()
{
    if (!nppData._nppHandle)
        return;
    const auto findContainingMenu = [](HMENU root, int target, const auto& self) -> HMENU
    {
        const int count = GetMenuItemCount(root);
        for (int item = 0; item < count; ++item)
        {
            if (GetMenuItemID(root, item) == static_cast<UINT>(target))
                return root;
            if (const HMENU child = GetSubMenu(root, item))
                if (const HMENU found = self(child, target, self))
                    return found;
        }
        return nullptr;
    };
    const auto& indices = commandMenuIndices;
    int configured = 0;
    bool changed = false;
    for (int index = 0; index < static_cast<int>(std::size(indices)); ++index)
    {
        const int command = menuItems[indices[index]]._cmdID;
        const HMENU menu = findContainingMenu(GetMenu(nppData._nppHandle), command,
            findContainingMenu);
        if (!menu)
            continue;
        if (!pluginMenuBitmaps[index])
            pluginMenuBitmaps[index] = createPluginMenuBitmap(commands[index].icon);
        if (!pluginMenuBitmaps[index])
            continue;
        MENUITEMINFOW current{sizeof(current)};
        current.fMask = MIIM_BITMAP;
        GetMenuItemInfoW(menu, command, FALSE, &current);
        if (current.hbmpItem != pluginMenuBitmaps[index])
        {
            MENUITEMINFOW item{sizeof(item)};
            item.fMask = MIIM_BITMAP;
            item.hbmpItem = pluginMenuBitmaps[index];
            if (SetMenuItemInfoW(menu, command, FALSE, &item))
                changed = true;
        }
        current = MENUITEMINFOW{sizeof(current)};
        current.fMask = MIIM_BITMAP;
        if (GetMenuItemInfoW(menu, command, FALSE, &current)
            && current.hbmpItem == pluginMenuBitmaps[index])
            ++configured;
    }
    SetPropW(nppData._nppHandle, L"NppHistoryPluginMenuIconsReady",
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(configured + 1)));
    if (changed)
        DrawMenuBar(nppData._nppHandle);
}

void setCommandEnabled(int index, bool enabled)
{
    const int command = menuItems[index]._cmdID;
    if (!command || !nppData._nppHandle)
        return;
    const auto findContainingMenu = [](HMENU root, int target, const auto& self) -> HMENU
    {
        const int count = GetMenuItemCount(root);
        for (int item = 0; item < count; ++item)
        {
            if (GetMenuItemID(root, item) == static_cast<UINT>(target))
                return root;
            if (const HMENU child = GetSubMenu(root, item))
                if (const HMENU found = self(child, target, self))
                    return found;
        }
        return nullptr;
    };
    const HMENU commandMenu = findContainingMenu(GetMenu(nppData._nppHandle), command,
        findContainingMenu);
    if (commandMenu)
        EnableMenuItem(commandMenu, command,
            MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
    setToolbarCommandEnabled(command, enabled);
}

void syncCommandStates()
{
    if (!ready || !nppData._nppHandle)
        return;
    const bool captureEnabled = actionAvailable(Command::capture);
    const bool compareEnabled = actionAvailable(Command::compare);
    for (int row = 0; row < commandCount; ++row)
        setCommandEnabled(commandMenuIndices[row], actionAvailable(static_cast<Command>(row)));
    configurePluginMenuIcons();
    SetPropW(nppData._nppHandle, L"NppHistoryCaptureCommandEnabled",
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(captureEnabled ? 2 : 1)));
    SetPropW(nppData._nppHandle, L"NppHistoryCompareCommandEnabled",
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(compareEnabled ? 2 : 1)));
}

bool actionAvailable(Command command)
{
    const fs::path path = currentPath();
    return commandAvailable(command, isSavableFile(path), settings.isHistoryExcluded(path),
        settings.historyEnabled, historyPanel.hasRevisions(), historyPanel.hasSelectedRevision(),
        historyPanel.visible());
}

HMENU findCommandMenu(HMENU menu, UINT id)
{
    for (int index = 0; index < GetMenuItemCount(menu); ++index)
    {
        if (GetMenuItemID(menu, index) == id) return menu;
        if (const HMENU child = GetSubMenu(menu, index))
            if (const HMENU result = findCommandMenu(child, id)) return result;
    }
    return nullptr;
}

void configureCommandMenu()
{
    pluginCommandMenu = findCommandMenu(GetMenu(nppData._nppHandle), menuItems[captureIndex]._cmdID);
    if (!pluginCommandMenu) return;
    // Reorder native items without changing exported indices/IDs or the shortcut
    // strings maintained by Notepad++ (including Shortcut Mapper changes).
    std::array<MENUITEMINFOW, commandCount> items{};
    std::array<std::array<wchar_t, 256>, commandCount> labels{};
    for (int row = 0; row < commandCount; ++row)
    {
        auto& item = items[row];
        item.cbSize = sizeof(item);
        item.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE | MIIM_FTYPE | MIIM_BITMAP | MIIM_DATA;
        item.dwTypeData = labels[row].data();
        item.cch = static_cast<UINT>(labels[row].size());
        if (!GetMenuItemInfoW(pluginCommandMenu, menuItems[commandMenuIndices[row]]._cmdID, FALSE, &item))
            return;
    }
    for (int row = 0; row < commandCount; ++row)
        RemoveMenu(pluginCommandMenu, items[row].wID, MF_BYCOMMAND);
    int position = 0;
    for (const Command command : commandOrder)
        InsertMenuItemW(pluginCommandMenu, position++, TRUE, &items[static_cast<int>(command)]);
}

void appendDocumentContextMenu(HMENU menu)
{
    if (!menu || !pluginCommandMenu) return;
    // The host may reuse its popup. Remove only entries tagged by this plugin.
    for (int index = GetMenuItemCount(menu) - 1; index >= 0; --index)
    {
        MENUITEMINFOW item{sizeof(item)};
        item.fMask = MIIM_DATA | MIIM_SUBMENU;
        if (GetMenuItemInfoW(menu, index, TRUE, &item) && item.dwItemData == contextMenuMarker)
        {
            RemoveMenu(menu, index, MF_BYPOSITION);
            if (item.hSubMenu) DestroyMenu(item.hSubMenu);
        }
    }
    bool any = false;
    for (const Command command : commandOrder)
        any = any || settings.commandVisible(command, CommandSurface::context);
    if (!any) return;
    syncCommandStates();
    const HMENU target = settings.contextSubmenu ? CreatePopupMenu() : menu;
    if (!target) return;
    const auto separator = [&]() {
        MENUITEMINFOW item{sizeof(item)};
        item.fMask = MIIM_FTYPE | MIIM_DATA;
        item.fType = MFT_SEPARATOR;
        item.dwItemData = contextMenuMarker;
        InsertMenuItemW(menu, GetMenuItemCount(menu), TRUE, &item);
    };
    if (!settings.contextSubmenu) separator();
    for (const Command command : commandOrder)
    {
        if (!settings.commandVisible(command, CommandSurface::context)) continue;
        const int row = static_cast<int>(command);
        wchar_t label[256]{};
        MENUITEMINFOW item{sizeof(item)};
        item.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE | MIIM_BITMAP | MIIM_DATA;
        item.dwTypeData = label;
        item.cch = static_cast<UINT>(std::size(label));
        if (!GetMenuItemInfoW(pluginCommandMenu, menuItems[commandMenuIndices[row]]._cmdID, FALSE, &item))
            continue;
        item.dwItemData = contextMenuMarker;
        InsertMenuItemW(target, GetMenuItemCount(target), TRUE, &item);
    }
    if (settings.contextSubmenu)
    {
        MENUITEMINFOW item{sizeof(item)};
        item.fMask = MIIM_SUBMENU | MIIM_STRING | MIIM_DATA;
        item.hSubMenu = target;
        item.dwTypeData = const_cast<wchar_t*>(pluginName);
        item.dwItemData = contextMenuMarker;
        if (!InsertMenuItemW(menu, GetMenuItemCount(menu), TRUE, &item)) DestroyMenu(target);
    }
    else separator();
}

void registerConfiguredToolbarButtons()
{
    ensureConfigurationLoaded();
    int registered = 0;
    for (const Command command : commandOrder)
    {
        const int index = static_cast<int>(command);
        if (!settings.commandVisible(static_cast<Command>(index), CommandSurface::toolbar))
            continue;
        toolbarAssets[index].icon = static_cast<HICON>(LoadImageW(moduleInstance,
            MAKEINTRESOURCEW(commands[index].icon), IMAGE_ICON, 16, 16, LR_SHARED));
        toolbarAssets[index].bitmap = createToolbarBitmap(toolbarAssets[index].icon);
        toolbarIconsWithDarkMode icons{toolbarAssets[index].bitmap,
            toolbarAssets[index].icon, toolbarAssets[index].icon};
        if (SendMessageW(nppData._nppHandle, NPPM_ADDTOOLBARICON_FORDARKMODE,
            menuItems[commandMenuIndices[index]]._cmdID, reinterpret_cast<LPARAM>(&icons)) != FALSE)
            ++registered;
    }
    SetPropW(nppData._nppHandle, L"NppHistoryToolbarButtonsRegistered",
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(registered + 1)));
    syncCommandStates();
}

void showPendingUpdateResult()
{
    const fs::path result = pluginConfigPath / L"update-result.ini";
    if (!fs::is_regular_file(result))
        return;
    wchar_t status[64]{}, detail[1024]{}, version[128]{};
    GetPrivateProfileStringW(L"Update", L"Status", L"", status,
        static_cast<DWORD>(std::size(status)), result.c_str());
    GetPrivateProfileStringW(L"Update", L"Detail", L"", detail,
        static_cast<DWORD>(std::size(detail)), result.c_str());
    GetPrivateProfileStringW(L"Update", L"Version", L"", version,
        static_cast<DWORD>(std::size(version)), result.c_str());
    DeleteFileW(result.c_str());
    if (!status[0])
        return;
    const bool success = wcscmp(status, L"success") == 0;
    const bool installed = success || wcscmp(status, L"installed_restart_failed") == 0;
    std::wstring message = detail[0] ? detail : (installed
        ? L"NppHistory was updated successfully." : L"The NppHistory update failed.");
    if (version[0])
        message += L"\n\nVersion: " + std::wstring(version);
    pluginLogger().write(installed ? LogLevel::informational : LogLevel::error,
        installed ? L"Update installed" : L"Update installation failed", message);
    centeredMessageBox(nppData._nppHandle, message.c_str(),
        installed ? L"NppHistory Updated" : L"NppHistory Update Failed",
        MB_OK | (installed ? MB_ICONINFORMATION : MB_ICONERROR));
}

void initialise()
{
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES
        | ICC_LINK_CLASS | ICC_STANDARD_CLASSES | ICC_HOTKEY_CLASS};
    InitCommonControlsEx(&controls);
    ensureConfigurationLoaded();
    historyCatalog.configure(pluginConfigPath / L"catalog.db", settings.historyLocationMode,
        settings.customHistoryRoot, pluginConfigPath / L"history");
    historyStore.setCatalog(&historyCatalog);
    reconcileFile(currentBuffer(), currentPath());

    for (int row = 0; row < commandCount; ++row)
        settings.hotkeyCommandIds[row] = menuItems[commandMenuIndices[row]]._cmdID;
    historyPanel.create(moduleInstance, nppData, historyStore, settings,
        menuItems[historyIndex]._cmdID, captureNow, editSettings, showAbout,
        syncCommandStates, prepareRestoreSave, cancelRestoreSave);
    historyPanel.refresh(currentPath());
    SendMessageW(nppData._nppHandle, NPPM_ADDSCNMODIFIEDFLAGS, 0,
        SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT);
    activeTimerId = SetTimer(nullptr, 0, timerPeriodMilliseconds, timerProc);
    lastActiveBuffer = currentBuffer();
    const ULONGLONG now = GetTickCount64();
    lastIntervalTick = now;
    automaticUpdateEligibleTick = now + startupUpdateDelayMilliseconds;
    nextUpdateScheduleTick = automaticUpdateEligibleTick;
    nextUpdateStatusRefreshTick = now;
    SetWindowSubclass(nppData._nppHandle, mainWindowSubclass, 1, 0);
    ready = true;
    configureCommandMenu();
    refreshDocumentTabIndicators();
    configurePluginMenuIcons();
    syncCommandStates();
    updateShuttingDown = false;
    showPendingUpdateResult();
}
}

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        moduleInstance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) void setInfo(NppData data)
{
    nppData = data;
    ensureConfigurationLoaded();
    const auto shortcut = [](ShortcutKey& target, const HotkeySetting& source) {
        target._isCtrl = source.ctrl;
        target._isAlt = source.alt;
        target._isShift = source.shift;
        target._key = static_cast<UCHAR>(source.key);
    };
    setMenuItem(captureIndex, L"Capture", captureNow);
    setMenuItem(compareIndex, L"Compare", compareCurrent);
    setMenuItem(historyIndex, L"History", showHistory);
    setMenuItem(settingsIndex, L"Settings", editSettings);
    setMenuItem(aboutIndex, L"About", showAbout);
    setMenuItem(restoreIndex, L"Restore", restoreCurrent);
    setMenuItem(refreshIndex, L"Refresh", refreshCurrent);
    for (int row = 0; row < commandCount; ++row)
    {
        const auto& key = settings.commandHotkey(static_cast<Command>(row));
        shortcut(commandShortcuts[row], key);
        menuItems[commandMenuIndices[row]]._pShKey = key.enabled ? &commandShortcuts[row] : nullptr;
    }
}

extern "C" __declspec(dllexport) const wchar_t* getName() { return pluginName; }

extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count)
{
    *count = menuCount;
    return menuItems;
}

void handleNotification(SCNotification* notification)
{
    if (!notification)
        return;
    const UINT code = notification->nmhdr.code;
    if (code == NPPN_TBMODIFICATION)
    {
        registerConfiguredToolbarButtons();
        return;
    }
    if (code == NPPN_READY)
    {
        initialise();
        return;
    }
    if (!ready)
        return;

    if (code == SCN_MODIFIED
        && (notification->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) != 0)
    {
        const UINT_PTR bufferId = currentBuffer();
        const ULONGLONG now = GetTickCount64();
        auto [entry, inserted] = dirtyBuffers.try_emplace(bufferId, DirtyState{now, now});
        entry->second.lastEditTick = now;
        if (inserted)
            entry->second.periodicBaseTick = now;
        return;
    }

    const UINT_PTR bufferId = static_cast<UINT_PTR>(notification->nmhdr.idFrom);
    if (code == NPPN_BUFFERACTIVATED)
    {
        if (settings.shouldAutoSave(AutoSaveTrigger::tabChange) && lastActiveBuffer
            && lastActiveBuffer != bufferId)
            saveConfiguredScope(lastActiveBuffer);
        lastActiveBuffer = bufferId;
    }
    if (code == NPPN_BEFORESHUTDOWN)
    {
        if (settings.shouldAutoSave(AutoSaveTrigger::exit))
            saveConfiguredScope(lastActiveBuffer);
        return;
    }
    if (code == NPPN_FILEBEFORESAVE)
    {
        const fs::path path = pathForBuffer(bufferId);
        if (settings.shouldCreateRevision(RevisionTrigger::beforeSave)
            && isSavableFile(path) && !settings.isHistoryExcluded(path))
        {
            if (historyStore.captureFile(path, L"Before save"))
                pluginLogger().write(LogLevel::informational, L"Revision created",
                    L"Before save: " + path.wstring());
        }
    }
    else if (code == NPPN_FILESAVED)
    {
        const fs::path path = pathForBuffer(bufferId);
        const bool restoreInitiatedSave = restoreSaveBuffers.erase(bufferId) > 0;
        const auto previous = lastKnownPaths.find(bufferId);
        if (previous != lastKnownPaths.end() && normalizePath(previous->second) != normalizePath(path))
            reconcileFile(bufferId, path, previous->second);
        else
            reconcileFile(bufferId, path);
        pluginLogger().write(LogLevel::informational, L"File saved", path.wstring());
        if (!restoreInitiatedSave
            && settings.shouldCreateRevision(RevisionTrigger::afterSave) && !path.empty()
            && !settings.isHistoryExcluded(path))
        {
            if (historyStore.captureFile(path, L"Saved"))
                pluginLogger().write(LogLevel::informational, L"Revision created",
                    L"After save: " + path.wstring());
        }
        dirtyBuffers.erase(bufferId);
        refreshPanel();
    }
    else if (code == NPPN_FILEOPENED || code == NPPN_BUFFERACTIVATED)
    {
        const fs::path path = pathForBuffer(bufferId);
        const auto previous = lastKnownPaths.find(bufferId);
        if (existingFile(path))
        {
            if (previous != lastKnownPaths.end() && normalizePath(previous->second) != normalizePath(path))
                reconcileFile(bufferId, path, previous->second);
            else
                reconcileFile(bufferId, path);
        }
        else
        {
            missingSince.try_emplace(bufferId, GetTickCount64());
        }
        refreshPanel();
    }
    else if (code == NPPN_FILEBEFORERENAME)
    {
        pendingRenamePaths[bufferId] = pathForBuffer(bufferId);
    }
    else if (code == NPPN_FILERENAMECANCEL)
    {
        pendingRenamePaths.erase(bufferId);
    }
    else if (code == NPPN_FILERENAMED)
    {
        const fs::path path = pathForBuffer(bufferId);
        const auto previous = pendingRenamePaths.find(bufferId);
        reconcileFile(bufferId, path, previous == pendingRenamePaths.end()
            ? std::optional<fs::path>{} : std::optional<fs::path>{previous->second});
        pendingRenamePaths.erase(bufferId);
        refreshPanel();
    }
    else if (code == NPPN_FILEDELETED)
    {
        missingSince[bufferId] = GetTickCount64() - 2000;
        detectMissingBuffers();
    }
    else if (code == NPPN_FILECLOSED)
    {
        dirtyBuffers.erase(bufferId);
        restoreSaveBuffers.erase(bufferId);
        lastKnownPaths.erase(bufferId);
        pendingRenamePaths.erase(bufferId);
        missingSince.erase(bufferId);
        missingAlertsShown.erase(bufferId);
    }
    else if (code == NPPN_SHUTDOWN)
    {
        for (HWND tabs : documentTabControls()) removeDocumentTabDecorations(tabs);
        updateShuttingDown = true;
        if (updateThreadHandle)
        {
            WaitForSingleObject(updateThreadHandle, 15000);
            CloseHandle(updateThreadHandle);
            updateThreadHandle = nullptr;
        }
        if (installThreadHandle)
        {
            WaitForSingleObject(installThreadHandle, 35000);
            CloseHandle(installThreadHandle);
            installThreadHandle = nullptr;
        }
        updateCheckInProgress = false;
        installDownloadInProgress = false;
        if (activeTimerId)
            KillTimer(nullptr, activeTimerId);
        activeTimerId = 0;
        RemoveWindowSubclass(nppData._nppHandle, mainWindowSubclass, 1);
        for (auto& asset : toolbarAssets)
        {
            if (asset.bitmap) DeleteObject(asset.bitmap);
            asset.bitmap = nullptr;
            asset.icon = nullptr;
        }
        for (auto& bitmap : pluginMenuBitmaps)
        {
            if (bitmap) DeleteObject(bitmap);
            bitmap = nullptr;
        }
        restoreSaveBuffers.clear();
        ready = false;
    }
}

extern "C" __declspec(dllexport) void beNotified(SCNotification* notification)
{
    try
    {
        handleNotification(notification);
    }
    catch (...)
    {
        // Never allow a history/configuration failure to escape into Notepad++.
    }
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }
extern "C" __declspec(dllexport) BOOL isUnicode() { return TRUE; }
