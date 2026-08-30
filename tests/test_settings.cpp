#include "TestHarness.h"
#include "Settings.h"
#include "Utilities.h"

using namespace npphistory;

namespace
{
bool sameSettings(const Settings& left, const Settings& right)
{
    return left.autoSaveEnabled == right.autoSaveEnabled
        && left.autoSaveAfterEdit == right.autoSaveAfterEdit
        && left.afterEditSeconds == right.afterEditSeconds
        && left.autoSaveOnFocusLoss == right.autoSaveOnFocusLoss
        && left.autoSaveAtIntervals == right.autoSaveAtIntervals
        && left.intervalMinutes == right.intervalMinutes
        && left.autoSaveOnTabChange == right.autoSaveOnTabChange
        && left.autoSaveOnExit == right.autoSaveOnExit
        && left.autoSaveScope == right.autoSaveScope
        && left.toolbarCapture == right.toolbarCapture
        && left.toolbarCompare == right.toolbarCompare
        && left.toolbarRestore == right.toolbarRestore
        && left.autoUpdateEnabled == right.autoUpdateEnabled
        && left.updateFrequency == right.updateFrequency
        && left.includePrereleaseUpdates == right.includePrereleaseUpdates
        && left.lastUpdateCheck == right.lastUpdateCheck
        && left.lastNotifiedVersion == right.lastNotifiedVersion
        && left.lastUpdateStatus == right.lastUpdateStatus
        && left.loggingEnabled == right.loggingEnabled
        && left.logLevel == right.logLevel
        && left.logLocationMode == right.logLocationMode
        && left.customLogFile == right.customLogFile
        && left.logMaximumSizeMb == right.logMaximumSizeMb
        && left.logRolloverMode == right.logRolloverMode
        && left.logArchivesToRetain == right.logArchivesToRetain
        && left.historyEnabled == right.historyEnabled
        && left.historyBeforeSave == right.historyBeforeSave
        && left.historyAfterSave == right.historyAfterSave
        && left.historyBeforeRestore == right.historyBeforeRestore
        && left.historyLocationMode == right.historyLocationMode
        && left.customHistoryRoot == right.customHistoryRoot;
}
}

void runSettingsTests(TestContext& context)
{
    TestDirectory directory(L"settings");
    const auto missingPath = directory.path() / L"missing.ini";
    Settings defaults;
    Settings loadedDefaults;
    loadedDefaults.load(missingPath);
    context.expect(sameSettings(defaults, loadedDefaults),
        "Settings::load applies every documented default when no INI exists");

    Settings configured;
    configured.autoSaveEnabled = false;
    configured.autoSaveAfterEdit = false;
    configured.afterEditSeconds = 44;
    configured.autoSaveOnFocusLoss = true;
    configured.autoSaveAtIntervals = true;
    configured.intervalMinutes = 17;
    configured.autoSaveOnTabChange = true;
    configured.autoSaveOnExit = true;
    configured.autoSaveScope = AutoSaveScope::currentFile;
    configured.toolbarCapture = true;
    configured.toolbarCompare = true;
    configured.toolbarRestore = true;
    configured.autoUpdateEnabled = true;
    configured.updateFrequency = UpdateFrequency::monthly;
    configured.includePrereleaseUpdates = false;
    configured.lastUpdateCheck = 123456789ULL;
    configured.lastNotifiedVersion = L"v0.2.0-beta.20";
    configured.lastUpdateStatus = L"Up to date";
    configured.loggingEnabled = true;
    configured.logLevel = LogLevel::debug;
    configured.logLocationMode = LogLocationMode::customFile;
    configured.customLogFile = directory.path() / L"Logs Ω" / L"NppHistory.log";
    configured.logMaximumSizeMb = 12;
    configured.logRolloverMode = LogRolloverMode::overwrite;
    configured.logArchivesToRetain = 8;
    configured.historyEnabled = false;
    configured.historyBeforeSave = false;
    configured.historyAfterSave = false;
    configured.historyBeforeRestore = false;
    configured.historyLocationMode = HistoryLocationMode::customRoot;
    configured.customHistoryRoot = directory.path() / L"Histories \u03A9";
    const auto configuredPath = directory.path() / L"nested" / L"NppHistory.ini";
    context.expect(configured.save(configuredPath),
        "Settings::save creates the configuration directory");
    Settings roundTrip;
    roundTrip.load(configuredPath);
    context.expect(configured.autoSaveEnabled == roundTrip.autoSaveEnabled,
        "AutoSaveEnabled round-trips");
    context.expect(configured.autoSaveAfterEdit == roundTrip.autoSaveAfterEdit,
        "AutoSaveAfterEdit round-trips");
    context.expect(configured.afterEditSeconds == roundTrip.afterEditSeconds,
        "AfterEditSeconds round-trips");
    context.expect(configured.autoSaveOnFocusLoss == roundTrip.autoSaveOnFocusLoss,
        "AutoSaveOnFocusLoss round-trips");
    context.expect(configured.autoSaveAtIntervals == roundTrip.autoSaveAtIntervals,
        "AutoSaveAtIntervals round-trips");
    context.expect(configured.intervalMinutes == roundTrip.intervalMinutes,
        "IntervalMinutes round-trips");
    context.expect(configured.autoSaveOnTabChange == roundTrip.autoSaveOnTabChange,
        "AutoSaveOnTabChange round-trips");
    context.expect(configured.autoSaveOnExit == roundTrip.autoSaveOnExit,
        "AutoSaveOnExit round-trips");
    context.expect(configured.autoSaveScope == roundTrip.autoSaveScope,
        "AutoSaveScope round-trips");
    context.expect(configured.toolbarCapture == roundTrip.toolbarCapture,
        "ToolbarCapture round-trips");
    context.expect(configured.toolbarCompare == roundTrip.toolbarCompare,
        "ToolbarCompare round-trips");
    context.expect(configured.toolbarRestore == roundTrip.toolbarRestore,
        "ToolbarRestore round-trips");
    context.expect(configured.autoUpdateEnabled == roundTrip.autoUpdateEnabled,
        "AutoUpdateEnabled round-trips");
    context.expect(configured.updateFrequency == roundTrip.updateFrequency,
        "UpdateFrequency round-trips");
    context.expect(configured.includePrereleaseUpdates == roundTrip.includePrereleaseUpdates,
        "IncludePrereleaseUpdates round-trips");
    context.expect(configured.lastUpdateCheck == roundTrip.lastUpdateCheck,
        "LastUpdateCheck round-trips a 64-bit value");
    context.expect(configured.lastNotifiedVersion == roundTrip.lastNotifiedVersion,
        "LastNotifiedVersion round-trips");
    context.expect(configured.lastUpdateStatus == roundTrip.lastUpdateStatus,
        "LastUpdateStatus round-trips");
    context.expect(configured.loggingEnabled == roundTrip.loggingEnabled,
        "LoggingEnabled round-trips");
    context.expect(configured.logLevel == roundTrip.logLevel,
        "LogLevel round-trips");
    context.expect(configured.logLocationMode == roundTrip.logLocationMode,
        "LogLocationMode round-trips");
    context.expect(configured.customLogFile == roundTrip.customLogFile,
        "CustomLogFile round-trips a Unicode path");
    context.expect(configured.logMaximumSizeMb == roundTrip.logMaximumSizeMb,
        "LogMaximumSizeMb round-trips");
    context.expect(configured.logRolloverMode == roundTrip.logRolloverMode,
        "LogRolloverMode round-trips");
    context.expect(configured.logArchivesToRetain == roundTrip.logArchivesToRetain,
        "LogArchivesToRetain round-trips");
    context.expect(configured.historyEnabled == roundTrip.historyEnabled,
        "HistoryEnabled round-trips");
    context.expect(configured.historyBeforeSave == roundTrip.historyBeforeSave,
        "HistoryBeforeSave round-trips");
    context.expect(configured.historyAfterSave == roundTrip.historyAfterSave,
        "HistoryAfterSave round-trips");
    context.expect(configured.historyBeforeRestore == roundTrip.historyBeforeRestore,
        "HistoryBeforeRestore round-trips");
    context.expect(configured.historyLocationMode == roundTrip.historyLocationMode,
        "HistoryLocationMode round-trips");
    context.expect(configured.customHistoryRoot == roundTrip.customHistoryRoot,
        "CustomHistoryRoot round-trips Unicode paths");
    if (configured.customHistoryRoot != roundTrip.customHistoryRoot)
        std::cerr << "  expected CustomHistoryRoot: "
            << wideToUtf8(configured.customHistoryRoot.wstring())
            << " (" << configured.customHistoryRoot.wstring().size() << ")\n"
            << "  actual CustomHistoryRoot: "
            << wideToUtf8(roundTrip.customHistoryRoot.wstring())
            << " (" << roundTrip.customHistoryRoot.wstring().size() << ")\n";

    for (const auto frequency : {UpdateFrequency::daily, UpdateFrequency::weekly,
        UpdateFrequency::monthly})
    {
        configured.updateFrequency = frequency;
        context.expect(configured.save(configuredPath),
            "Settings::save accepts every update-frequency choice");
        Settings frequencyReload;
        frequencyReload.load(configuredPath);
        context.expect(frequencyReload.updateFrequency == frequency,
            "UpdateFrequency preserves daily, weekly and monthly choices");
    }

    const auto ansiPath = directory.path() / L"ansi-to-unicode.ini";
    const std::string ansiText = "[NppHistory]\r\nEnabled=0\r\n";
    writeAllBytesAtomic(ansiPath,
        std::vector<std::uint8_t>(ansiText.begin(), ansiText.end()));
    Settings migrated;
    migrated.load(ansiPath);
    migrated.customHistoryRoot = directory.path() / L"Unicode \u03A9";
    context.expect(migrated.save(ansiPath),
        "Settings::save migrates an existing ANSI INI to Unicode");
    const auto migratedBytes = readAllBytes(ansiPath);
    Settings migratedReload;
    migratedReload.load(ansiPath);
    context.expect(migratedBytes.size() >= 2 && migratedBytes[0] == 0xFF
        && migratedBytes[1] == 0xFE
        && migratedReload.customHistoryRoot == migrated.customHistoryRoot,
        "ANSI migration writes a UTF-16 BOM and preserves new Unicode values");

    Settings policy;
    policy.autoSaveEnabled = true;
    policy.autoSaveAfterEdit = true;
    policy.autoSaveOnFocusLoss = true;
    policy.autoSaveAtIntervals = true;
    policy.autoSaveOnTabChange = true;
    policy.autoSaveOnExit = true;
    for (const auto trigger : {AutoSaveTrigger::afterEdit, AutoSaveTrigger::focusLoss,
        AutoSaveTrigger::timedInterval, AutoSaveTrigger::tabChange, AutoSaveTrigger::exit})
        context.expect(policy.shouldAutoSave(trigger),
            "Settings::shouldAutoSave enables each selected trigger");
    policy.autoSaveAfterEdit = false;
    policy.autoSaveOnFocusLoss = false;
    policy.autoSaveAtIntervals = false;
    policy.autoSaveOnTabChange = false;
    policy.autoSaveOnExit = false;
    for (const auto trigger : {AutoSaveTrigger::afterEdit, AutoSaveTrigger::focusLoss,
        AutoSaveTrigger::timedInterval, AutoSaveTrigger::tabChange, AutoSaveTrigger::exit})
        context.expect(!policy.shouldAutoSave(trigger),
            "Settings::shouldAutoSave disables each cleared trigger");
    policy.autoSaveAfterEdit = true;
    policy.autoSaveOnFocusLoss = true;
    policy.autoSaveAtIntervals = true;
    policy.autoSaveOnTabChange = true;
    policy.autoSaveOnExit = true;
    policy.autoSaveEnabled = false;
    for (const auto trigger : {AutoSaveTrigger::afterEdit, AutoSaveTrigger::focusLoss,
        AutoSaveTrigger::timedInterval, AutoSaveTrigger::tabChange, AutoSaveTrigger::exit})
        context.expect(!policy.shouldAutoSave(trigger),
            "the Auto Save master switch overrides every trigger");

    policy.historyEnabled = true;
    policy.historyBeforeSave = true;
    policy.historyAfterSave = true;
    policy.historyBeforeRestore = true;
    for (const auto trigger : {RevisionTrigger::beforeSave, RevisionTrigger::afterSave,
        RevisionTrigger::beforeRestore, RevisionTrigger::manual})
        context.expect(policy.shouldCreateRevision(trigger),
            "Settings::shouldCreateRevision enables each selected revision path");
    policy.historyBeforeSave = false;
    policy.historyAfterSave = false;
    policy.historyBeforeRestore = false;
    context.expect(!policy.shouldCreateRevision(RevisionTrigger::beforeSave)
        && !policy.shouldCreateRevision(RevisionTrigger::afterSave)
        && !policy.shouldCreateRevision(RevisionTrigger::beforeRestore)
        && policy.shouldCreateRevision(RevisionTrigger::manual),
        "manual capture remains available while automatic history triggers are cleared");
    policy.historyEnabled = false;
    for (const auto trigger : {RevisionTrigger::beforeSave, RevisionTrigger::afterSave,
        RevisionTrigger::beforeRestore, RevisionTrigger::manual})
        context.expect(!policy.shouldCreateRevision(trigger),
            "the History master switch overrides every revision path");

    policy.autoSaveEnabled = true;
    policy.autoSaveAfterEdit = true;
    policy.afterEditSeconds = 10;
    context.expect(!policy.afterEditDue(9'999, 0),
        "afterEditDue is false immediately before its threshold");
    context.expect(policy.afterEditDue(10'000, 0),
        "afterEditDue is true exactly at its threshold");
    context.expect(!policy.afterEditDue(5, 10),
        "afterEditDue safely handles a backwards tick value");
    policy.autoSaveAfterEdit = false;
    context.expect(!policy.afterEditDue(20'000, 0),
        "afterEditDue respects the trigger checkbox");

    policy.autoSaveAtIntervals = true;
    policy.intervalMinutes = 2;
    context.expect(!policy.intervalDue(119'999, 0),
        "intervalDue is false immediately before its threshold");
    context.expect(policy.intervalDue(120'000, 0),
        "intervalDue is true exactly at its threshold");
    context.expect(!policy.intervalDue(5, 10),
        "intervalDue safely handles a backwards tick value");
    policy.autoSaveAtIntervals = false;
    context.expect(!policy.intervalDue(240'000, 0),
        "intervalDue respects the interval checkbox");

    policy.autoUpdateEnabled = true;
    policy.updateFrequency = UpdateFrequency::daily;
    policy.lastUpdateCheck = 100;
    context.expect(!policy.updateCheckDue(86'499),
        "daily update check is not due one second before its threshold");
    context.expect(policy.updateCheckDue(86'500),
        "daily update check is due exactly at its threshold");
    policy.updateFrequency = UpdateFrequency::weekly;
    context.expect(!policy.updateCheckDue(604'899) && policy.updateCheckDue(604'900),
        "weekly update frequency uses seven days");
    policy.updateFrequency = UpdateFrequency::monthly;
    context.expect(!policy.updateCheckDue(2'592'099) && policy.updateCheckDue(2'592'100),
        "monthly update frequency uses thirty days");
    policy.autoUpdateEnabled = false;
    context.expect(!policy.updateCheckDue(9'999'999),
        "disabled automatic updates never become due");

    const auto normalizedPath = directory.path() / L"normalized.ini";
    WritePrivateProfileStringW(L"NppHistory", L"AfterEditSeconds", L"0", normalizedPath.c_str());
    WritePrivateProfileStringW(L"NppHistory", L"IntervalMinutes", L"0", normalizedPath.c_str());
    WritePrivateProfileStringW(L"NppHistory", L"UpdateFrequency", L"99", normalizedPath.c_str());
    WritePrivateProfileStringW(L"NppHistory", L"LogMaximumSizeMb", L"9999", normalizedPath.c_str());
    WritePrivateProfileStringW(L"NppHistory", L"LogArchivesToRetain", L"-4", normalizedPath.c_str());
    Settings normalized;
    normalized.load(normalizedPath);
    context.expect(normalized.afterEditSeconds == 10,
        "Settings::load enforces the 10-second after-edit minimum");
    context.expect(normalized.intervalMinutes == 1,
        "Settings::load enforces the one-minute interval minimum");
    context.expect(normalized.updateFrequency == UpdateFrequency::monthly,
        "Settings::load clamps an out-of-range update frequency");
    context.expect(normalized.logMaximumSizeMb == 1024,
        "Settings::load clamps an excessive log size limit");
    context.expect(normalized.logArchivesToRetain == 0,
        "Settings::load clamps a negative archive retention count");

    const auto legacyPath = directory.path() / L"legacy.ini";
    WritePrivateProfileStringW(L"NppHistory", L"Enabled", L"0", legacyPath.c_str());
    WritePrivateProfileStringW(L"NppHistory", L"Mode", L"1", legacyPath.c_str());
    WritePrivateProfileStringW(L"NppHistory", L"PeriodicSeconds", L"120", legacyPath.c_str());
    Settings legacy;
    legacy.load(legacyPath);
    context.expect(!legacy.autoSaveEnabled,
        "Settings::load migrates the legacy Enabled key");
    context.expect(!legacy.autoSaveAfterEdit && legacy.autoSaveAtIntervals,
        "Settings::load migrates legacy periodic mode");
    context.expect(legacy.intervalMinutes == 2,
        "Settings::load converts legacy periodic seconds to minutes");
}
