#include "TestHarness.h"
#include "Settings.h"
#include "LiveHotkeys.h"
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
        && left.autoSaveExclusions == right.autoSaveExclusions
        && left.toolbarCapture == right.toolbarCapture
        && left.toolbarCompare == right.toolbarCompare
        && left.toolbarHistory == right.toolbarHistory
        && left.hotkeyCapture == right.hotkeyCapture
        && left.hotkeyCompare == right.hotkeyCompare
        && left.hotkeyHistory == right.hotkeyHistory
        && left.autoUpdateEnabled == right.autoUpdateEnabled
        && left.updateFrequency == right.updateFrequency
        && left.includePrereleaseUpdates == right.includePrereleaseUpdates
        && left.lastUpdateCheck == right.lastUpdateCheck
        && left.lastUpdateAttempt == right.lastUpdateAttempt
        && left.nextUpdateRetry == right.nextUpdateRetry
        && left.updateFailureCount == right.updateFailureCount
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
        && left.historyExclusions == right.historyExclusions
        && left.historyLocationMode == right.historyLocationMode
        && left.customHistoryRoot == right.customHistoryRoot;
}
}

void runSettingsTests(TestContext& context)
{
    Settings liveConfig;
    for (int row = 0; row < commandCount; ++row)
        liveConfig.commandHotkey(static_cast<Command>(row)) =
            {true, true, true, true, static_cast<unsigned>(VK_F13 + row)};
    LiveHotkeys runtime;
    runtime.apply(liveConfig);
    for (int row = 0; row < commandCount; ++row)
    {
        const unsigned key = VK_F13 + row;
        context.expect(!runtime.event(key, true, false, false, true, true, true, false, false).consume,
            "hotkey ignored outside active main window or in modal/menu scope");
        context.expect(!runtime.event(key, true, false, true, true, true, true, true, false).consume,
            "Windows-key combinations are not intercepted");
        context.expect(!runtime.event(key, true, false, true, true, true, true, false, true).consume,
            "AltGr text input is not intercepted");
        context.expect(!runtime.event(key, true, false, true, true, true, false, false, false).consume,
            "modifiers must match exactly");
        const auto first = runtime.event(key, true, false, true, true, true, true, false, false);
        context.expect(first.consume && first.command == row, "one initial keydown routes the correct command");
        const auto repeat = runtime.event(key, true, true, true, true, true, true, false, false);
        context.expect(repeat.consume && repeat.command == -1, "held keys cannot repeat actions");
        const auto released = runtime.event(key, false, false, false, false, false, false, false, false);
        context.expect(released.consume && released.command == -1, "consumed keyup does not escape into a command dialog");
    }
    const unsigned beforeGeneration = runtime.generation;
    Settings draft = liveConfig;
    draft.hotkeyCapture.key = VK_F24;
    context.expect(runtime.keys[0].key == VK_F13, "editing an uncommitted draft does not change live bindings");
    runtime.apply(draft);
    context.expect(runtime.generation != beforeGeneration, "committing invalidates queued old-generation actions");
    context.expect(!runtime.event(VK_F13, true, false, true, true, true, true, false, false).consume,
        "old key no longer matches after replacement");
    context.expect(runtime.event(VK_F24, true, false, true, true, true, true, false, false).command == 0,
        "new key matches immediately after replacement");
    draft.hotkeyCapture.enabled = false;
    runtime.apply(draft);
    runtime.resetPressed();
    context.expect(!runtime.event(VK_F24, true, false, true, true, true, true, false, false).consume,
        "disabled shortcut passes through");
    context.expect(!runtime.event(999, true, false, true, true, true, true, false, false).consume,
        "invalid key codes are ignored safely");
    context.expect(!safeCommandHotkey({true, false, false, false, 'A'})
        && !safeCommandHotkey({true, false, false, true, 'A'})
        && !safeCommandHotkey({true, false, true, false, 'F'})
        && !safeCommandHotkey({true, false, true, false, VK_F4})
        && !safeCommandHotkey({true, true, true, false, VK_DELETE}),
        "typing, host mnemonics and system combinations are rejected");
    context.expect(safeCommandHotkey({true, false, false, false, VK_F8})
        && safeCommandHotkey({true, true, true, false, 'C'}), "function keys and modified commands are allowed");
    context.expect(commandHotkeyText({true, true, true, true, VK_F24}) == L"Ctrl+Alt+Shift+F24"
        && commandHotkeyText({false, true, true, false, 'C'}).empty(), "menu suffix describes the active binding");
    TestDirectory directory(L"settings");
    std::wstring order;
    for (const Command command : commandOrder)
        order += std::wstring(commands[static_cast<int>(command)].name) + L"|";
    context.expect(order == L"Capture|Compare|Restore|History|Refresh|Settings|About|",
        "all command surfaces share the requested order");
    Settings placements;
    for (const Command command : commandOrder)
    {
        context.expect(placements.commandVisible(command, CommandSurface::pane) == (command != Command::history)
            && placements.commandVisible(command, CommandSurface::plugins)
            && !placements.commandVisible(command, CommandSurface::toolbar)
            && !placements.commandVisible(command, CommandSurface::context)
            && !placements.commandVisible(command, CommandSurface::tabContext),
            "all seven commands have safe default placements");
        placements.setCommandVisible(command, CommandSurface::plugins, false);
        context.expect(placements.commandVisible(command, CommandSurface::plugins),
            "all seven Plugins menu items are permanently available");
        for (const CommandSurface surface : {CommandSurface::pane, CommandSurface::toolbar,
            CommandSurface::context, CommandSurface::tabContext})
        {
            placements.setCommandVisible(command, surface, true);
            context.expect(placements.commandVisible(command, surface) == !placementLocked(command, surface),
                "optional placement enables except locked History Pane entry");
            placements.setCommandVisible(command, surface, false);
            context.expect(!placements.commandVisible(command, surface), "placement can be disabled");
            placements.setCommandVisible(command, surface, surface != CommandSurface::pane);
        }
        placements.commandHotkey(command) = {true, true, true, true,
            static_cast<unsigned>(VK_F1 + static_cast<int>(command))};
    }
    placements.contextSubmenu = false;
    const auto commandsPath = directory.path() / L"commands.ini";
    context.expect(placements.save(commandsPath), "seven-command placements and shortcuts save");
    Settings placementReload;
    placementReload.load(commandsPath);
    context.expect(!placementReload.contextSubmenu, "inline context mode persists");
    for (const Command command : commandOrder)
    {
        context.expect(placementReload.commandHotkey(command) == placements.commandHotkey(command),
            "each command shortcut round-trips independently");
        context.expect(!placementReload.commandVisible(command, CommandSurface::pane)
            && placementReload.commandVisible(command, CommandSurface::toolbar)
            && placementReload.commandVisible(command, CommandSurface::context)
            && placementReload.commandVisible(command, CommandSurface::tabContext)
            && placementReload.commandVisible(command, CommandSurface::plugins),
            "each command placement round-trips independently");
    }
    // Legacy settings must not re-enable the self-opening History command in its pane.
    WritePrivateProfileStringW(L"NppHistory", L"PaneHistory", L"1", commandsPath.c_str());
    placementReload.load(commandsPath);
    context.expect(!placementReload.commandVisible(Command::history, CommandSurface::pane),
        "legacy PaneHistory=1 is ignored");
    placementReload.setCommandVisible(Command::capture, CommandSurface::tabContext, false);
    context.expect(placementReload.commandVisible(Command::capture, CommandSurface::context),
        "tab context selection is independent of document context selection");
    context.expect(placementSurfaces[1] == CommandSurface::tabContext,
        "former Plugins settings column edits tab context instead");
    for (int bits = 0; bits < 64; ++bits)
    {
        const bool saved = bits & 1, excluded = bits & 2, enabled = bits & 4,
            revisions = bits & 8, selected = bits & 16, visible = bits & 32;
        context.expect(commandAvailable(Command::capture, saved, excluded, enabled, revisions, selected, visible)
            == (saved && !excluded && enabled), "Capture state matrix");
        context.expect(commandAvailable(Command::compare, saved, excluded, enabled, revisions, selected, visible)
            == (saved && !excluded && revisions && (!visible || selected)), "Compare state matrix");
        context.expect(commandAvailable(Command::restore, saved, excluded, enabled, revisions, selected, visible)
            == (saved && !excluded && visible && selected), "Restore state matrix");
        context.expect(commandAvailable(Command::refresh, saved, excluded, enabled, revisions, selected, visible)
            == (saved && !excluded), "Refresh state matrix");
        for (const Command command : {Command::history, Command::settings, Command::about})
            context.expect(commandAvailable(command, saved, excluded, enabled, revisions, selected, visible),
                "navigation commands remain available in every file state");
    }
    context.expect(autoSaveScopeDisplayName(AutoSaveScope::currentFile) == L"Current file only"
        && autoSaveScopeDisplayName(AutoSaveScope::allOpenFiles) == L"All open files",
        "Auto Save scope log names describe both choices");
    context.expect(updateFrequencyDisplayName(UpdateFrequency::daily) == L"Daily"
        && updateFrequencyDisplayName(UpdateFrequency::weekly) == L"Weekly"
        && updateFrequencyDisplayName(UpdateFrequency::monthly) == L"Monthly",
        "update-frequency log names describe every schedule");
    context.expect(logLevelDisplayName(LogLevel::critical) == L"Critical"
        && logLevelDisplayName(LogLevel::error) == L"Error"
        && logLevelDisplayName(LogLevel::warning) == L"Warning"
        && logLevelDisplayName(LogLevel::informational) == L"Informational"
        && logLevelDisplayName(LogLevel::debug) == L"Debug",
        "log-level names describe every severity");
    context.expect(logLocationDisplayName(LogLocationMode::pluginConfig)
            == L"Notepad++ plugin configuration folder"
        && logLocationDisplayName(LogLocationMode::customFile) == L"Custom file",
        "log-location names describe both storage choices");
    context.expect(logRolloverDisplayName(LogRolloverMode::overwrite)
            == L"Overwrite current log"
        && logRolloverDisplayName(LogRolloverMode::archive) == L"Create archives",
        "log-rollover names describe both policies");
    context.expect(historyLocationDisplayName(HistoryLocationMode::adjacent)
            == L"Hidden .npphistory folder beside each file"
        && historyLocationDisplayName(HistoryLocationMode::customRoot) == L"Common folder",
        "history-location names describe both storage choices");
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
    configured.autoSaveExclusions = L"*.log\r\nmyfile*.com";
    configured.toolbarCapture = true;
    configured.toolbarCompare = true;
    configured.toolbarHistory = true;
    configured.hotkeyCapture = {true, true, false, true, 'C'};
    configured.hotkeyCompare = {true, true, true, false, 'M'};
    configured.hotkeyHistory = {true, false, true, true, VK_F8};
    configured.autoUpdateEnabled = true;
    configured.updateFrequency = UpdateFrequency::monthly;
    configured.includePrereleaseUpdates = false;
    configured.lastUpdateCheck = 123456789ULL;
    configured.lastUpdateAttempt = 123456999ULL;
    configured.nextUpdateRetry = 123457899ULL;
    configured.updateFailureCount = 2;
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
    configured.historyExclusions = L"*.tmp\r\nprivate-?.txt";
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
    context.expect(configured.autoSaveExclusions == roundTrip.autoSaveExclusions,
        "AutoSaveExclusions round-trips a multiline wildcard list");
    context.expect(configured.toolbarCapture == roundTrip.toolbarCapture,
        "ToolbarCapture round-trips");
    context.expect(configured.toolbarCompare == roundTrip.toolbarCompare,
        "ToolbarCompare round-trips");
    context.expect(configured.toolbarHistory == roundTrip.toolbarHistory,
        "ToolbarHistory round-trips");
    context.expect(configured.hotkeyCapture == roundTrip.hotkeyCapture,
        "Capture hotkey round-trips modifiers, key and enabled state");
    context.expect(configured.hotkeyCompare == roundTrip.hotkeyCompare,
        "Compare hotkey round-trips modifiers, key and enabled state");
    context.expect(configured.hotkeyHistory == roundTrip.hotkeyHistory,
        "History hotkey round-trips modifiers, key and enabled state");
    context.expect(configured.autoUpdateEnabled == roundTrip.autoUpdateEnabled,
        "AutoUpdateEnabled round-trips");
    context.expect(configured.updateFrequency == roundTrip.updateFrequency,
        "UpdateFrequency round-trips");
    context.expect(configured.includePrereleaseUpdates == roundTrip.includePrereleaseUpdates,
        "IncludePrereleaseUpdates round-trips");
    context.expect(configured.lastUpdateCheck == roundTrip.lastUpdateCheck,
        "LastUpdateCheck round-trips a 64-bit value");
    context.expect(configured.lastUpdateAttempt == roundTrip.lastUpdateAttempt,
        "LastUpdateAttempt round-trips a 64-bit value");
    context.expect(configured.nextUpdateRetry == roundTrip.nextUpdateRetry,
        "NextUpdateRetry round-trips a 64-bit value");
    context.expect(configured.updateFailureCount == roundTrip.updateFailureCount,
        "UpdateFailureCount round-trips");
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
    context.expect(configured.historyExclusions == roundTrip.historyExclusions,
        "HistoryExclusions round-trips a multiline wildcard list");
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

    Settings exclusions;
    exclusions.autoSaveExclusions = L"*.log\r\nmyfile*.com";
    exclusions.historyExclusions = L"*.tmp";
    context.expect(exclusions.isAutoSaveExcluded(L"C:\\Notes\\PLUGIN.LOG"),
        "auto-save exclusions are case-insensitive");
    context.expect(exclusions.isAutoSaveExcluded(L"C:\\Notes\\myfile.com")
        && exclusions.isAutoSaveExcluded(L"C:\\Notes\\myfiles.com"),
        "auto-save exclusions support a star matching zero or more characters");
    context.expect(!exclusions.isAutoSaveExcluded(L"C:\\Notes\\other.com"),
        "auto-save exclusions remain anchored to the complete filename");
    context.expect(!exclusions.isHistoryExcluded(L"C:\\Notes\\PLUGIN.LOG")
        && exclusions.isHistoryExcluded(L"C:\\Notes\\cache.tmp"),
        "auto-save and revision-history exclusions are independent");

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

    const auto toolbarMigrationPath = directory.path() / L"toolbar-migration.ini";
    const std::string toolbarMigrationText =
        "[NppHistory]\r\nToolbarRestore=1\r\n";
    writeAllBytesAtomic(toolbarMigrationPath, std::vector<std::uint8_t>(
        toolbarMigrationText.begin(), toolbarMigrationText.end()));
    Settings toolbarMigration;
    toolbarMigration.load(toolbarMigrationPath);
    context.expect(toolbarMigration.toolbarHistory,
        "legacy ToolbarRestore preference migrates to ToolbarHistory");

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
    policy.autoSaveEnabled = true;
    policy.externalAutoSavePluginDetected = true;
    for (const auto trigger : {AutoSaveTrigger::afterEdit, AutoSaveTrigger::focusLoss,
        AutoSaveTrigger::timedInterval, AutoSaveTrigger::tabChange, AutoSaveTrigger::exit})
        context.expect(!policy.shouldAutoSave(trigger),
            "an installed AutoSave plugin disables every NppHistory Auto Save trigger");
    policy.externalAutoSavePluginDetected = false;

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

    policy.autoUpdateEnabled = true;
    policy.updateFrequency = UpdateFrequency::daily;
    policy.lastUpdateCheck = 100;
    policy.nextUpdateRetry = 0;
    context.expect(policy.nextUpdateCheckTime(200) == 86'500,
        "nextUpdateCheckTime reports the daily wall-clock deadline");
    context.expect(policy.nextUpdateCheckTime(86'500) == 86'500,
        "nextUpdateCheckTime reports an overdue check as due now");
    policy.autoUpdateEnabled = false;
    context.expect(policy.nextUpdateCheckTime(200) == 0,
        "nextUpdateCheckTime reports no deadline when automatic checks are disabled");
    policy.autoUpdateEnabled = true;
    policy.lastUpdateCheck = 200;
    policy.nextUpdateRetry = 1'100;
    context.expect(policy.nextUpdateCheckTime(300) == 86'600,
        "a manual failure does not move a future automatic deadline earlier");
    policy.lastUpdateCheck = 100;
    policy.nextUpdateRetry = 0;
    policy.recordUpdateFailure(86'500);
    context.expect(policy.updateFailureCount == 1 && policy.nextUpdateRetry == 87'400,
        "the first update failure schedules a 15-minute retry");
    context.expect(!policy.updateCheckDue(87'399) && policy.updateCheckDue(87'400),
        "the retry deadline suppresses automatic checks until it is reached");
    policy.recordUpdateFailure(87'400);
    context.expect(policy.updateFailureCount == 2 && policy.nextUpdateRetry == 91'000,
        "the second update failure schedules a one-hour retry");
    policy.recordUpdateFailure(91'000);
    context.expect(policy.updateFailureCount == 3 && policy.nextUpdateRetry == 112'600,
        "later update failures use the capped six-hour retry");
    policy.recordUpdateSuccess(112'600);
    context.expect(policy.lastUpdateCheck == 112'600 && policy.lastUpdateAttempt == 112'600
        && policy.nextUpdateRetry == 0 && policy.updateFailureCount == 0,
        "a successful update check resets retry state");

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
