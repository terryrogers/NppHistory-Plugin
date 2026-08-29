#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>

namespace npphistory
{
std::vector<std::uint8_t> readAllBytes(const std::filesystem::path& path);
bool writeAllBytesAtomic(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes);
std::string sha256Hex(const std::vector<std::uint8_t>& bytes);
std::string sha256Hex(const std::wstring& text);
std::wstring normalizePath(const std::filesystem::path& path);
std::string wideToUtf8(const std::wstring& text);
std::wstring utf8ToWide(const std::string& text);
std::wstring decodeText(const std::vector<std::uint8_t>& bytes);
std::string utcTimestampCompact();
std::wstring localTimestampDisplay(const std::filesystem::file_time_type& value);
std::wstring localDateDisplay(std::wstring_view isoDate);
void centerWindowOnOwner(HWND window, HWND owner);
void fitWindowWithinOwner(HWND window, HWND owner, int horizontalMargin, int verticalMargin,
    int minimumWidth, int minimumHeight);
}
