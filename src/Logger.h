#pragma once

#include "Settings.h"

#include <filesystem>
#include <mutex>
#include <string_view>

namespace npphistory
{
class PluginLogger
{
public:
    void configure(const Settings& settings, const std::filesystem::path& pluginConfigRoot);
    void write(LogLevel level, std::wstring_view action, std::wstring_view detail = {});
    bool ensureFile();
    std::filesystem::path path() const;
    bool enabled(LogLevel level) const;

private:
    bool rotateIfNeeded(std::uintmax_t incomingBytes);

    mutable std::mutex _mutex;
    bool _enabled = false;
    LogLevel _level = LogLevel::informational;
    std::filesystem::path _path;
    std::uintmax_t _maximumBytes = 5ULL * 1024ULL * 1024ULL;
    LogRolloverMode _rolloverMode = LogRolloverMode::archive;
    unsigned _archivesToRetain = 5;
};

PluginLogger& pluginLogger();
const wchar_t* logLevelName(LogLevel level) noexcept;
}
