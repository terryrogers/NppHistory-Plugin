#include "TestHarness.h"
#include "Logger.h"
#include "Utilities.h"

using namespace npphistory;

void runLoggerTests(TestContext& context)
{
    TestDirectory directory(L"logger");
    Settings settings;
    settings.loggingEnabled = true;
    settings.logLevel = LogLevel::informational;
    settings.logLocationMode = LogLocationMode::pluginConfig;
    settings.logMaximumSizeMb = 1;
    settings.logRolloverMode = LogRolloverMode::archive;
    settings.logArchivesToRetain = 2;
    auto& logger = pluginLogger();
    logger.configure(settings, directory.path());
    context.expect(std::wstring(logLevelName(LogLevel::critical)) == L"CRITICAL"
        && std::wstring(logLevelName(LogLevel::error)) == L"ERROR"
        && std::wstring(logLevelName(LogLevel::warning)) == L"WARNING"
        && std::wstring(logLevelName(LogLevel::informational)) == L"INFO"
        && std::wstring(logLevelName(LogLevel::debug)) == L"DEBUG",
        "logLevelName labels every configured severity");
    context.expect(logger.enabled(LogLevel::error) && logger.enabled(LogLevel::warning)
        && logger.enabled(LogLevel::informational) && !logger.enabled(LogLevel::debug),
        "informational logging includes error, warning and informational events but filters debug");
    context.expect(logger.path() == directory.path() / L"NppHistory.log",
        "PluginLogger uses the Notepad++ plugin configuration directory by default");
    context.expect(logger.ensureFile(), "PluginLogger creates its log file and parent directory");
    logger.write(LogLevel::informational, L"Capture", L"note.txt");
    logger.write(LogLevel::critical, L"Critical test event", L"note.txt");
    logger.write(LogLevel::informational, L"Revision deleted",
        L"note.txt | 2026-08-30T01:02:03Z | Manual capture");
    logger.write(LogLevel::debug, L"Hidden debug event");
    const auto firstText = decodeText(readAllBytes(logger.path()));
    context.expect(firstText.find(L"[CRITICAL] Critical test event | note.txt") != std::wstring::npos,
        "PluginLogger writes Critical without relabeling it as Error");
    context.expect(firstText.find(L"[INFO] Capture | note.txt") != std::wstring::npos,
        "PluginLogger writes informational actions and detail");
    context.expect(firstText.find(L"[INFO] Revision deleted | note.txt | 2026-08-30T01:02:03Z | Manual capture")
        != std::wstring::npos, "PluginLogger records revision deletion audit detail");
    context.expect(firstText.find(L"Hidden debug event") == std::wstring::npos,
        "PluginLogger filters events below the configured verbosity");

    settings.logLevel = LogLevel::debug;
    logger.configure(settings, directory.path());
    context.expect(logger.enabled(LogLevel::debug),
        "debug logging enables every severity");
    logger.write(LogLevel::debug, L"Option change", L"Enabled: false -> true\nnext");
    const auto debugText = decodeText(readAllBytes(logger.path()));
    context.expect(debugText.find(L"[DEBUG] Option change | Enabled: false -> true next")
        != std::wstring::npos, "PluginLogger records debug changes and sanitizes line breaks");

    const std::wstring large(1'100'000, L'x');
    logger.write(LogLevel::error, L"Large", large);
    context.expect(std::filesystem::exists(logger.path().wstring() + L".1"),
        "PluginLogger archives the current log when the size limit is exceeded");

    logger.write(LogLevel::error, L"Rotate two", large);
    logger.write(LogLevel::error, L"Rotate three", large);
    context.expect(std::filesystem::exists(logger.path().wstring() + L".1")
        && std::filesystem::exists(logger.path().wstring() + L".2")
        && !std::filesystem::exists(logger.path().wstring() + L".3"),
        "PluginLogger retains only the configured number of archives");

    settings.logLocationMode = LogLocationMode::customFile;
    settings.customLogFile = directory.path() / L"custom" / L"plugin.log";
    settings.logRolloverMode = LogRolloverMode::overwrite;
    logger.configure(settings, directory.path());
    logger.write(LogLevel::warning, L"Update check failure", L"offline");
    context.expect(logger.path() == settings.customLogFile
        && std::filesystem::exists(settings.customLogFile),
        "PluginLogger writes to a configured custom log file");

    const std::wstring overwritePayload(1'100'000, L'y');
    logger.write(LogLevel::error, L"Overwrite one", overwritePayload);
    logger.write(LogLevel::error, L"Overwrite two", overwritePayload);
    const auto overwrittenText = decodeText(readAllBytes(settings.customLogFile));
    context.expect(overwrittenText.find(L"Overwrite two") != std::wstring::npos
        && overwrittenText.find(L"Overwrite one") == std::wstring::npos
        && !std::filesystem::exists(settings.customLogFile.wstring() + L".1"),
        "overwrite rollover truncates the current log without creating an archive");

    settings.loggingEnabled = false;
    logger.configure(settings, directory.path());
    context.expect(!logger.enabled(LogLevel::error),
        "disabled logging rejects every severity");
    const auto before = std::filesystem::file_size(settings.customLogFile);
    logger.write(LogLevel::error, L"Disabled event");
    context.expect(std::filesystem::file_size(settings.customLogFile) == before,
        "PluginLogger writes nothing when logging is disabled");
}
