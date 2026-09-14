#include "TestHarness.h"
#include "ActionFeedback.h"
#include "Logger.h"
#include "TemporaryStatusBar.h"
#include "Utilities.h"

using namespace npphistory;

void runActionFeedbackTests(TestContext& context)
{
    struct Example { ActionEvent event; const wchar_t* message; LogLevel level; };
    const auto info = LogLevel::informational;
    const auto warning = LogLevel::warning;
    const auto critical = LogLevel::critical;
    const Example examples[]{
        {ActionEvent::captured, L"Test.log Revision Captured.", info},
        {ActionEvent::captureFailed, L"Revision Capture for Test.log Failed!", critical},
        {ActionEvent::refreshed, L"History for Test.log Refreshed.", info},
        {ActionEvent::comparisonOpened, L"Test.log Comparison View Opened.", info},
        {ActionEvent::comparisonFailed, L"Comparison View for Test.log Failed to Open!", warning},
        {ActionEvent::restored, L"Restored R123 Revision for Test.log.", info},
        {ActionEvent::restoreFailed, L"Restore of R123 Revision for Test.log Failed!", critical},
        {ActionEvent::reloadFailed, L"Restored R123 Revision for Test.log; File Reload Failed!", critical},
        {ActionEvent::deleted, L"Test.log Revision R123 Deleted.", info},
        {ActionEvent::deleteFailed, L"Deletion of Test.log Revision R123 Failed!", warning},
        {ActionEvent::commentUpdated, L"Test.log Revision R123 Comment Updated.", info},
        {ActionEvent::commentFailed, L"Test.log Revision R123 Comment Update Failed!", warning},
        {ActionEvent::settingsSaved, L"Settings Saved.", info},
        {ActionEvent::settingsFailed, L"Settings Save Failed!", critical},
        {ActionEvent::fileSaved, L"Test.log Saved.", info},
        {ActionEvent::savedWithRevision, L"Test.log Saved; Revision Created.", info},
        {ActionEvent::autoSaved, L"Test.log Automatically Saved.", info},
        {ActionEvent::autoSaveFailed, L"Automatic Save for Test.log Failed!", critical},
        {ActionEvent::updateAvailable, L"Manual Update Check: Update Available (Version 0.2.0.25).", info},
        {ActionEvent::upToDate, L"Manual Update Check: Up to Date (Version 0.2.0.25).", info},
        {ActionEvent::updateFailed, L"Manual Update Check Failed!", warning}
    };
    TestDirectory directory(L"action-feedback");
    Settings settings;
    settings.loggingEnabled = true;
    settings.logLevel = LogLevel::debug;
    auto& logger = pluginLogger();
    logger.configure(settings, directory.path());
    const ActionContext subject{L"C:\\Example\\Test.log", L"R123", L"0.2.0.25", false, L"Diagnostic Context"};
    for (const auto& example : examples)
    {
        const auto feedback = makeActionFeedback(example.event, subject);
        context.expect(feedback.message == example.message, "Action wording/case/punctuation matches specification");
        context.expect(feedback.level == example.level, "Action uses the specified INFO/WARNING/CRITICAL severity");
        reportAction(example.event, subject);
    }
    const auto log = decodeText(readAllBytes(logger.path()));
    for (const auto& example : examples)
    {
        const std::wstring expected = L"[" + std::wstring(logLevelName(example.level)) + L"] "
            + example.message + L" | File: C:\\Example\\Test.log | Revision: R123 | Version: 0.2.0.25 | Detail: Diagnostic Context";
        const auto position = log.find(expected);
        context.expect(position != std::wstring::npos, "Every temporary action has matching contextual log text");
        context.expect(position != std::wstring::npos && log.find(expected, position + 1) == std::wstring::npos,
            "One reportAction call emits exactly one corresponding log event");
    }
    ActionContext automatic = subject;
    automatic.automatic = true;
    for (auto event : {ActionEvent::updateAvailable, ActionEvent::upToDate, ActionEvent::updateFailed})
        context.expect(makeActionFeedback(event, automatic).message.find(L"Automatic Update Check") == 0,
            "Automatic update results are explicitly distinguished from manual checks");
    context.expect(makeActionFeedback(ActionEvent::upToDate).message == L"Manual Update Check: Up to Date.",
        "Missing published version omits empty version parentheses");
    context.expect(makeActionFeedback(ActionEvent::restored).message == L"Restored Selected Revision for Current File.",
        "Missing filename/revision has an intelligible fallback");
    context.expect(makeActionFeedback(ActionEvent::captured, {L"C:\\Notes\\caf\u00e9.log"}).message
        == L"caf\u00e9.log Revision Captured.", "Filename spelling and Unicode preserved without a full path in status");
    context.expect(makeActionFeedback(ActionEvent::commentUpdated, {L"Test.log", L"03/09/2026 9:05 AM"}).message
        == L"Test.log Revision 03/09/2026 9:05 AM Comment Updated.", "Revision uses the supplied localized timestamp unchanged");
    reportAction(ActionEvent::commentUpdated, {L"Test.log", L"R1\r\n[INFO] Forged", {}, false, L"Details\tHere"});
    const auto sanitized = decodeText(readAllBytes(logger.path()));
    context.expect(sanitized.find(L"Revision: R1  [INFO] Forged") != std::wstring::npos
        && sanitized.find(L"\r\n[INFO] Forged") == std::wstring::npos
        && sanitized.find(L"Detail: Details Here") != std::wstring::npos, "Context cannot inject additional log lines");

    for (auto threshold : {LogLevel::error, LogLevel::warning, LogLevel::informational, LogLevel::debug})
    {
        settings.logLevel = threshold;
        logger.configure(settings, directory.path());
        context.expect(logger.enabled(LogLevel::critical), "Critical is included by every enabled existing log threshold");
    }
    context.expect(static_cast<int>(LogLevel::error) == 0 && static_cast<int>(LogLevel::warning) == 1
        && static_cast<int>(LogLevel::informational) == 2 && static_cast<int>(LogLevel::debug) == 3,
        "Adding Critical does not reinterpret persisted logging settings");
    settings.logLevel = LogLevel::warning;
    logger.configure(settings, directory.path());
    const auto beforeFiltered = readAllBytes(logger.path());
    reportAction(ActionEvent::refreshed, {L"Filtered.log"});
    context.expect(readAllBytes(logger.path()) == beforeFiltered, "INFO action logging respects a Warning threshold");

    settings.loggingEnabled = false;
    logger.configure(settings, directory.path());
    const auto beforeDisabled = readAllBytes(logger.path());
    reportAction(ActionEvent::autoSaveFailed, {L"Disabled.log"});
    context.expect(readAllBytes(logger.path()) == beforeDisabled && !logger.enabled(LogLevel::critical),
        "Even Critical action messages respect disabled logging");

    // The paired reporter must still deliver UI feedback when logging is disabled.
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    const HWND owner = CreateWindowExW(0, L"STATIC", L"Feedback test", WS_OVERLAPPED,
        0, 0, 800, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    const HWND bar = owner ? CreateWindowExW(0, STATUSCLASSNAMEW, nullptr, WS_CHILD,
        0, 0, 800, 20, owner, nullptr, GetModuleHandleW(nullptr), nullptr) : nullptr;
    context.expect(owner && bar, "Hidden action-feedback status fixture created");
    if (bar)
    {
        const int edge = -1;
        SendMessageW(bar, SB_SETPARTS, 1, reinterpret_cast<LPARAM>(&edge));
        actionStatus().initialize(owner);
        const auto display = [&] {
            MSG message{};
            while (PeekMessageW(&message, bar, TemporaryStatusBar::showMessage,
                TemporaryStatusBar::showMessage, PM_REMOVE)) DispatchMessageW(&message);
            wchar_t text[256]{};
            SendMessageW(bar, SB_GETTEXTW, 0, reinterpret_cast<LPARAM>(text));
            return std::wstring(text);
        };
        reportAction(ActionEvent::captured, {L"Test.log"});
        context.expect(display() == L"NppHistory: Test.log Revision Captured.",
            "Contextual status feedback works with logging disabled");
        // Use a real directory as a log-file path: logging cannot write a file there.
        settings.loggingEnabled = true;
        settings.logLevel = LogLevel::debug;
        settings.logLocationMode = LogLocationMode::customFile;
        settings.customLogFile = directory.path();
        logger.configure(settings, directory.path());
        reportAction(ActionEvent::autoSaveFailed, {L"Test.log"});
        context.expect(display() == L"NppHistory: Automatic Save for Test.log Failed!",
            "Inaccessible log output does not suppress failure feedback");
        actionStatus().shutdown();
    }
    if (owner) DestroyWindow(owner);
    settings.loggingEnabled = false;
    logger.configure(settings, directory.path());
}
