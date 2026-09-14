#include "Logger.h"

#include "Utilities.h"

#include <fstream>
#include <windows.h>

namespace npphistory
{
namespace
{
std::wstring timestampNow()
{
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t date[128]{};
    wchar_t clock[128]{};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &time, nullptr,
        date, static_cast<int>(std::size(date)), nullptr);
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &time, nullptr,
        clock, static_cast<int>(std::size(clock)));
    return std::wstring(date) + L" " + clock;
}

std::wstring oneLine(std::wstring_view value)
{
    std::wstring result(value);
    for (wchar_t& character : result)
        if (character == L'\r' || character == L'\n' || character == L'\t') character = L' ';
    return result;
}
}

PluginLogger& pluginLogger()
{
    static PluginLogger logger;
    return logger;
}

const wchar_t* logLevelName(LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::critical: return L"CRITICAL";
    case LogLevel::error: return L"ERROR";
    case LogLevel::warning: return L"WARNING";
    case LogLevel::informational: return L"INFO";
    case LogLevel::debug: return L"DEBUG";
    }
    return L"UNKNOWN";
}

void PluginLogger::configure(const Settings& settings,
    const std::filesystem::path& pluginConfigRoot)
{
    std::lock_guard lock(_mutex);
    _enabled = settings.loggingEnabled;
    _level = settings.logLevel;
    _path = settings.logLocationMode == LogLocationMode::customFile
        && !settings.customLogFile.empty()
        ? settings.customLogFile : pluginConfigRoot / L"NppHistory.log";
    _maximumBytes = static_cast<std::uintmax_t>((std::max)(1U,
        settings.logMaximumSizeMb)) * 1024ULL * 1024ULL;
    _rolloverMode = settings.logRolloverMode;
    _archivesToRetain = settings.logArchivesToRetain;
}

bool PluginLogger::enabled(LogLevel level) const
{
    std::lock_guard lock(_mutex);
    return _enabled && static_cast<int>(level) <= static_cast<int>(_level);
}

std::filesystem::path PluginLogger::path() const
{
    std::lock_guard lock(_mutex);
    return _path;
}

bool PluginLogger::ensureFile()
{
    std::lock_guard lock(_mutex);
    if (_path.empty()) return false;
    std::error_code error;
    if (!_path.parent_path().empty())
        std::filesystem::create_directories(_path.parent_path(), error);
    if (error) return false;
    std::ofstream stream(_path, std::ios::binary | std::ios::app);
    return stream.good();
}

bool PluginLogger::rotateIfNeeded(std::uintmax_t incomingBytes)
{
    std::error_code error;
    const auto existing = std::filesystem::exists(_path, error)
        ? std::filesystem::file_size(_path, error) : 0;
    if (error || existing + incomingBytes <= _maximumBytes)
        return !error;
    if (_rolloverMode == LogRolloverMode::overwrite || _archivesToRetain == 0)
    {
        std::ofstream truncate(_path, std::ios::binary | std::ios::trunc);
        return truncate.good();
    }
    const auto archive = [&](unsigned index) {
        return std::filesystem::path(_path.wstring() + L"." + std::to_wstring(index));
    };
    std::filesystem::remove(archive(_archivesToRetain), error);
    error.clear();
    for (unsigned index = _archivesToRetain; index > 1; --index)
    {
        const auto previous = archive(index - 1);
        if (std::filesystem::exists(previous, error))
        {
            error.clear();
            std::filesystem::rename(previous, archive(index), error);
            if (error) return false;
        }
        error.clear();
    }
    std::filesystem::rename(_path, archive(1), error);
    return !error;
}

void PluginLogger::write(LogLevel level, std::wstring_view action, std::wstring_view detail)
{
    std::lock_guard lock(_mutex);
    if (!_enabled || static_cast<int>(level) > static_cast<int>(_level) || _path.empty())
        return;
    std::wstring line = timestampNow() + L" [" + logLevelName(level) + L"] "
        + oneLine(action);
    if (!detail.empty()) line += L" | " + oneLine(detail);
    line += L"\r\n";
    const std::string bytes = wideToUtf8(line);
    std::error_code error;
    if (!_path.parent_path().empty())
        std::filesystem::create_directories(_path.parent_path(), error);
    if (error || !rotateIfNeeded(bytes.size())) return;
    std::ofstream stream(_path, std::ios::binary | std::ios::app);
    if (stream) stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
}
