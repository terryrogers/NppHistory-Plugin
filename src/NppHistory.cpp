#include "HistoryPanel.h"
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

enum MenuIndex { showHistoryIndex, captureIndex, settingsIndex, aboutIndex,
    toolbarCompareIndex, toolbarRestoreIndex, menuCount };

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

std::array<ToolbarAsset, 3> toolbarAssets{};

fs::path pathForBuffer(UINT_PTR bufferId)
{
    if (!nppData._nppHandle || !bufferId)
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
        MessageBoxW(nppData._nppHandle, message.c_str(), pluginName, MB_OK | MB_ICONWARNING);
    }
    else if (result.historyMissing)
    {
        const std::wstring message = L"NppHistory detected that this file moved, but its recorded history folder no longer exists.\n\nExpected location: "
            + result.previousHistoryPath.wstring() + L"\n\nA new history will be started at the current location.";
        MessageBoxW(nppData._nppHandle, message.c_str(), pluginName, MB_OK | MB_ICONWARNING);
    }
    else if (result.ambiguousMatch)
    {
        MessageBoxW(nppData._nppHandle,
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
            MessageBoxW(nppData._nppHandle, message.c_str(), pluginName, MB_OK | MB_ICONWARNING);
        }
    }
}

void refreshPanel()
{
    if (ready)
        historyPanel.refresh(currentPath());
}

void saveBuffer(UINT_PTR bufferId)
{
    const fs::path path = pathForBuffer(bufferId);
    if (!isSavableFile(path))
    {
        dirtyBuffers.erase(bufferId);
        return;
    }
    SendMessageW(nppData._nppHandle, NPPM_SAVEFILE, 0, reinterpret_cast<LPARAM>(path.c_str()));
}

void saveConfiguredScope(UINT_PTR preferredBuffer = 0)
{
    if (!settings.autoSaveEnabled || dirtyBuffers.empty())
        return;
    if (settings.autoSaveScope == AutoSaveScope::allOpenFiles)
        SendMessageW(nppData._nppHandle, NPPM_SAVEALLFILES, 0, 0);
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
        MessageBoxW(nppData._nppHandle,
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
        MessageBoxW(nppData._nppHandle,
            L"This release does not contain the verified x64 asset required for automatic installation. You can still install it from the release page.",
            L"NppHistory Update", MB_OK | MB_ICONINFORMATION);
        openReleasePage(release);
        return;
    }
    bool expected = false;
    if (!installDownloadInProgress.compare_exchange_strong(expected, true))
    {
        MessageBoxW(nppData._nppHandle, L"The update is already being downloaded.",
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
        MessageBoxW(nppData._nppHandle,
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
        MessageBoxW(nppData._nppHandle, completion->detail.c_str(),
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
        MessageBoxW(nppData._nppHandle, detail.c_str(), L"NppHistory Update",
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
        MessageBoxW(nppData._nppHandle, detail.c_str(), L"NppHistory Update",
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
        const HWND settingsDialog = completion->manual
            ? settings.activeDialogWindow() : nullptr;
        dialog.hwndParent = settingsDialog ? settingsDialog : nppData._nppHandle;
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
    pluginLogger().write(LogLevel::debug, L"Button click", L"Show History");
    historyPanel.refresh(currentPath());
    historyPanel.show();
}

void captureNow()
{
    pluginLogger().write(LogLevel::debug, L"Button click", L"Capture Now");
    if (!settings.shouldCreateRevision(RevisionTrigger::manual))
    {
        MessageBoxW(nppData._nppHandle,
            L"Enable revision history in NppHistory Settings before capturing a revision.",
            pluginName, MB_OK | MB_ICONINFORMATION);
        return;
    }
    const fs::path path = currentPath();
    if (!isSavableFile(path))
    {
        MessageBoxW(nppData._nppHandle, L"Save this note to a file before capturing a revision.",
            pluginName, MB_OK | MB_ICONINFORMATION);
        return;
    }
    reconcileFile(currentBuffer(), path);
    if (SendMessageW(currentEditor(), SCI_GETMODIFY, 0, 0) != 0)
    {
        if (SendMessageW(nppData._nppHandle, NPPM_SAVEFILE, 0,
            reinterpret_cast<LPARAM>(path.c_str())) == FALSE)
        {
            MessageBoxW(nppData._nppHandle, L"The current note could not be saved.",
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

void compareFromToolbar()
{
    pluginLogger().write(LogLevel::debug, L"Button click", L"Compare Selected Revision");
    showHistory();
    historyPanel.compare();
}

void restoreFromToolbar()
{
    pluginLogger().write(LogLevel::debug, L"Button click", L"Restore Selected Revision");
    showHistory();
    historyPanel.restore();
}

std::wstring boolText(bool value) { return value ? L"true" : L"false"; }

void logSettingsChanges(const Settings& previous, const Settings& current)
{
    bool changed = false;
    const auto change = [&](std::wstring_view name, const std::wstring& before,
        const std::wstring& after)
    {
        if (before == after) return;
        changed = true;
        pluginLogger().write(LogLevel::debug, L"Setting change",
            std::wstring(name) + L": " + before + L" -> " + after);
    };
    const auto boolean = [&](std::wstring_view name, bool before, bool after) {
        change(name, boolText(before), boolText(after));
    };
    boolean(L"Toolbar Capture", previous.toolbarCapture, current.toolbarCapture);
    boolean(L"Toolbar Compare", previous.toolbarCompare, current.toolbarCompare);
    boolean(L"Toolbar Restore", previous.toolbarRestore, current.toolbarRestore);
    boolean(L"Auto Save enabled", previous.autoSaveEnabled, current.autoSaveEnabled);
    boolean(L"After editing stops", previous.autoSaveAfterEdit, current.autoSaveAfterEdit);
    change(L"After edit seconds", std::to_wstring(previous.afterEditSeconds), std::to_wstring(current.afterEditSeconds));
    boolean(L"Save on focus loss", previous.autoSaveOnFocusLoss, current.autoSaveOnFocusLoss);
    boolean(L"Save at intervals", previous.autoSaveAtIntervals, current.autoSaveAtIntervals);
    change(L"Interval minutes", std::to_wstring(previous.intervalMinutes), std::to_wstring(current.intervalMinutes));
    boolean(L"Save on tab change", previous.autoSaveOnTabChange, current.autoSaveOnTabChange);
    boolean(L"Save on exit", previous.autoSaveOnExit, current.autoSaveOnExit);
    change(L"Auto Save scope", std::to_wstring(static_cast<int>(previous.autoSaveScope)), std::to_wstring(static_cast<int>(current.autoSaveScope)));
    boolean(L"History enabled", previous.historyEnabled, current.historyEnabled);
    boolean(L"Revision before save", previous.historyBeforeSave, current.historyBeforeSave);
    boolean(L"Revision after save", previous.historyAfterSave, current.historyAfterSave);
    boolean(L"Revision before restore", previous.historyBeforeRestore, current.historyBeforeRestore);
    change(L"History location", std::to_wstring(static_cast<int>(previous.historyLocationMode)), std::to_wstring(static_cast<int>(current.historyLocationMode)));
    change(L"Custom history root", previous.customHistoryRoot.wstring(), current.customHistoryRoot.wstring());
    boolean(L"Logging enabled", previous.loggingEnabled, current.loggingEnabled);
    change(L"Log level", std::to_wstring(static_cast<int>(previous.logLevel)), std::to_wstring(static_cast<int>(current.logLevel)));
    change(L"Log location", std::to_wstring(static_cast<int>(previous.logLocationMode)), std::to_wstring(static_cast<int>(current.logLocationMode)));
    change(L"Custom log file", previous.customLogFile.wstring(), current.customLogFile.wstring());
    change(L"Log maximum size MB", std::to_wstring(previous.logMaximumSizeMb), std::to_wstring(current.logMaximumSizeMb));
    change(L"Log rollover", std::to_wstring(static_cast<int>(previous.logRolloverMode)), std::to_wstring(static_cast<int>(current.logRolloverMode)));
    change(L"Log archives", std::to_wstring(previous.logArchivesToRetain), std::to_wstring(current.logArchivesToRetain));
    boolean(L"Automatic update checks", previous.autoUpdateEnabled, current.autoUpdateEnabled);
    change(L"Update frequency", std::to_wstring(static_cast<int>(previous.updateFrequency)), std::to_wstring(static_cast<int>(current.updateFrequency)));
    boolean(L"Include prereleases", previous.includePrereleaseUpdates, current.includePrereleaseUpdates);
    if (changed) pluginLogger().write(LogLevel::informational, L"Settings changed");
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
                    MessageBoxW(nppData._nppHandle, L"The log file could not be opened in Notepad++.",
                        pluginName, MB_OK | MB_ICONERROR);
            }
            else
                MessageBoxW(nppData._nppHandle, L"The log file could not be created or accessed.",
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
    settings.defaultLogFile = pluginConfigPath / L"NppHistory.log";
    pluginLogger().configure(settings, pluginConfigPath);
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

void registerConfiguredToolbarButtons()
{
    ensureConfigurationLoaded();
    const bool enabled[] = {settings.toolbarCapture, settings.toolbarCompare,
        settings.toolbarRestore};
    const int commands[] = {captureIndex, toolbarCompareIndex, toolbarRestoreIndex};
    const int resources[] = {IDI_CAPTURE, IDI_COMPARE, IDI_RESTORE};
    int registered = 0;
    for (int index = 0; index < 3; ++index)
    {
        if (!enabled[index])
            continue;
        toolbarAssets[index].icon = static_cast<HICON>(LoadImageW(moduleInstance,
            MAKEINTRESOURCEW(resources[index]), IMAGE_ICON, 16, 16, LR_SHARED));
        toolbarAssets[index].bitmap = createToolbarBitmap(toolbarAssets[index].icon);
        toolbarIconsWithDarkMode icons{toolbarAssets[index].bitmap,
            toolbarAssets[index].icon, toolbarAssets[index].icon};
        if (SendMessageW(nppData._nppHandle, NPPM_ADDTOOLBARICON_FORDARKMODE,
            menuItems[commands[index]]._cmdID, reinterpret_cast<LPARAM>(&icons)) != FALSE)
            ++registered;
    }
    SetPropW(nppData._nppHandle, L"NppHistoryToolbarButtonsRegistered",
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(registered + 1)));
}

bool removeMenuCommand(HMENU menu, UINT command)
{
    const int count = GetMenuItemCount(menu);
    for (int index = 0; index < count; ++index)
    {
        const HMENU child = GetSubMenu(menu, index);
        if (child && removeMenuCommand(child, command))
            return true;
    }
    return DeleteMenu(menu, command, MF_BYCOMMAND) != FALSE;
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
    MessageBoxW(nppData._nppHandle, message.c_str(),
        installed ? L"NppHistory Updated" : L"NppHistory Update Failed",
        MB_OK | (installed ? MB_ICONINFORMATION : MB_ICONERROR));
}

void initialise()
{
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES
        | ICC_LINK_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    ensureConfigurationLoaded();
    historyCatalog.configure(pluginConfigPath / L"catalog.db", settings.historyLocationMode,
        settings.customHistoryRoot, pluginConfigPath / L"history");
    historyStore.setCatalog(&historyCatalog);
    reconcileFile(currentBuffer(), currentPath());

    historyPanel.create(moduleInstance, nppData, historyStore, settings,
        menuItems[showHistoryIndex]._cmdID, captureNow, editSettings, showAbout);
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
    const HMENU mainMenu = GetMenu(nppData._nppHandle);
    removeMenuCommand(mainMenu, menuItems[toolbarCompareIndex]._cmdID);
    removeMenuCommand(mainMenu, menuItems[toolbarRestoreIndex]._cmdID);
    DrawMenuBar(nppData._nppHandle);
    ready = true;
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
    setMenuItem(showHistoryIndex, L"Show History", showHistory);
    setMenuItem(captureIndex, L"Capture Now", captureNow);
    setMenuItem(settingsIndex, L"Settings", editSettings);
    setMenuItem(aboutIndex, L"About", showAbout);
    setMenuItem(toolbarCompareIndex, L"Compare Selected Revision", compareFromToolbar);
    setMenuItem(toolbarRestoreIndex, L"Restore Selected Revision", restoreFromToolbar);
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
        if (settings.shouldCreateRevision(RevisionTrigger::beforeSave) && isSavableFile(path))
        {
            if (historyStore.captureFile(path, L"Before save"))
                pluginLogger().write(LogLevel::informational, L"Revision created",
                    L"Before save: " + path.wstring());
        }
    }
    else if (code == NPPN_FILESAVED)
    {
        const fs::path path = pathForBuffer(bufferId);
        const auto previous = lastKnownPaths.find(bufferId);
        if (previous != lastKnownPaths.end() && normalizePath(previous->second) != normalizePath(path))
            reconcileFile(bufferId, path, previous->second);
        else
            reconcileFile(bufferId, path);
        pluginLogger().write(LogLevel::informational, L"File saved", path.wstring());
        if (settings.shouldCreateRevision(RevisionTrigger::afterSave) && !path.empty())
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
        lastKnownPaths.erase(bufferId);
        pendingRenamePaths.erase(bufferId);
        missingSince.erase(bufferId);
        missingAlertsShown.erase(bufferId);
    }
    else if (code == NPPN_SHUTDOWN)
    {
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
