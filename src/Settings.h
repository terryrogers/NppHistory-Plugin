#pragma once

#include "HistoryCatalog.h"

#include <filesystem>
#include <windows.h>

namespace npphistory
{
inline constexpr UINT settingsCheckUpdateMessage = WM_APP + 241;

enum class AutoSaveScope
{
    currentFile,
    allOpenFiles
};

enum class UpdateFrequency
{
    daily,
    weekly,
    monthly
};

enum class LogLevel
{
    error,
    warning,
    informational,
    debug
};

enum class LogLocationMode
{
    pluginConfig,
    customFile
};

enum class LogRolloverMode
{
    overwrite,
    archive
};

enum class AutoSaveTrigger
{
    afterEdit,
    focusLoss,
    timedInterval,
    tabChange,
    exit
};

enum class RevisionTrigger
{
    beforeSave,
    afterSave,
    beforeRestore,
    manual
};

struct Settings
{
    bool autoSaveEnabled = true;
    bool autoSaveAfterEdit = true;
    unsigned afterEditSeconds = 30;
    bool autoSaveOnFocusLoss = false;
    bool autoSaveAtIntervals = false;
    unsigned intervalMinutes = 10;
    bool autoSaveOnTabChange = false;
    bool autoSaveOnExit = false;
    AutoSaveScope autoSaveScope = AutoSaveScope::allOpenFiles;
    bool toolbarCapture = false;
    bool toolbarCompare = false;
    bool toolbarRestore = false;
    bool autoUpdateEnabled = false;
    UpdateFrequency updateFrequency = UpdateFrequency::weekly;
    bool includePrereleaseUpdates = true;
    unsigned long long lastUpdateCheck = 0;
    unsigned long long lastUpdateAttempt = 0;
    unsigned long long nextUpdateRetry = 0;
    unsigned updateFailureCount = 0;
    std::wstring lastNotifiedVersion;
    std::wstring lastUpdateStatus;
    bool loggingEnabled = false;
    LogLevel logLevel = LogLevel::informational;
    LogLocationMode logLocationMode = LogLocationMode::pluginConfig;
    std::filesystem::path customLogFile;
    unsigned logMaximumSizeMb = 5;
    LogRolloverMode logRolloverMode = LogRolloverMode::archive;
    unsigned logArchivesToRetain = 5;
    std::filesystem::path defaultLogFile;
    bool openLogNow = false;
    bool updateInstallAvailable = false;
    bool installUpdateNow = false;
    bool historyEnabled = true;
    bool historyBeforeSave = true;
    bool historyAfterSave = true;
    bool historyBeforeRestore = true;
    HistoryLocationMode historyLocationMode = HistoryLocationMode::adjacent;
    std::filesystem::path customHistoryRoot;

    bool shouldAutoSave(AutoSaveTrigger trigger) const noexcept;
    bool shouldCreateRevision(RevisionTrigger trigger) const noexcept;
    bool afterEditDue(unsigned long long now, unsigned long long lastEdit) const noexcept;
    bool intervalDue(unsigned long long now, unsigned long long lastInterval) const noexcept;
    bool updateCheckDue(unsigned long long nowSeconds) const noexcept;
    unsigned long long nextUpdateCheckTime(unsigned long long nowSeconds) const noexcept;
    void recordUpdateSuccess(unsigned long long nowSeconds) noexcept;
    void recordUpdateFailure(unsigned long long nowSeconds) noexcept;

    void load(const std::filesystem::path& file);
    bool save(const std::filesystem::path& file) const;
    bool edit(HWND owner, HINSTANCE instance);
    void refreshUpdateStatus(bool checking = false) const;
    HWND activeDialogWindow() const noexcept;
    bool closeForUpdateInstall() const;
};
}
