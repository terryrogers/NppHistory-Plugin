#include "Utilities.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace npphistory
{
std::vector<std::uint8_t> readAllBytes(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};

    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length < 0)
        return {};
    input.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty())
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty())
        return {};
    return bytes;
}

std::wstring formatFileSize(std::uintmax_t bytes)
{
    static constexpr const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB", L"PB", L"EB"};
    if (bytes < 1024)
        return std::to_wstring(bytes) + L" B";

    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(units))
    {
        value /= 1024.0;
        ++unit;
    }

    std::wostringstream output;
    output << std::fixed << std::setprecision(1) << value;
    std::wstring formatted = output.str();
    if (formatted.size() >= 2
        && formatted.compare(formatted.size() - 2, 2, L".0") == 0)
        formatted.resize(formatted.size() - 2);
    return formatted + L" " + units[unit];
}

bool writeAllBytesAtomic(const fs::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    if (error)
        return false;

    const fs::path temporary = path.parent_path() /
        (L".npph-" + std::to_wstring(::GetCurrentProcessId()) + L"-"
            + std::to_wstring(::GetTickCount64()) + L".tmp");

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            return false;
        if (!bytes.empty())
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output)
        {
            fs::remove(temporary, error);
            return false;
        }
    }

    if (!::MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        fs::remove(temporary, error);
        return false;
    }
    return true;
}

std::string sha256Hex(const std::vector<std::uint8_t>& bytes)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD resultLength = 0;
    std::vector<std::uint8_t> object;
    std::array<std::uint8_t, 32> digest{};

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("BCryptOpenAlgorithmProvider failed");

    const auto closeAlgorithm = [&]() { if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); };
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0) < 0)
    {
        closeAlgorithm();
        throw std::runtime_error("BCryptGetProperty failed");
    }

    object.resize(objectLength);
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) < 0)
    {
        closeAlgorithm();
        throw std::runtime_error("BCryptCreateHash failed");
    }

    if (!bytes.empty() && BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), 0) < 0)
    {
        BCryptDestroyHash(hash);
        closeAlgorithm();
        throw std::runtime_error("BCryptHashData failed");
    }

    if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
    {
        BCryptDestroyHash(hash);
        closeAlgorithm();
        throw std::runtime_error("BCryptFinishHash failed");
    }

    BCryptDestroyHash(hash);
    closeAlgorithm();

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto value : digest)
        output << std::setw(2) << static_cast<unsigned int>(value);
    return output.str();
}

std::string sha256Hex(const std::wstring& text)
{
    const auto utf8 = wideToUtf8(text);
    return sha256Hex(std::vector<std::uint8_t>(utf8.begin(), utf8.end()));
}

std::wstring normalizePath(const fs::path& path)
{
    std::error_code error;
    auto absolute = fs::absolute(path, error).lexically_normal().wstring();
    if (error)
        absolute = path.lexically_normal().wstring();
    std::replace(absolute.begin(), absolute.end(), L'/', L'\\');
    if (!absolute.empty())
        ::CharLowerBuffW(absolute.data(), static_cast<DWORD>(absolute.size()));
    return absolute;
}

std::string wideToUtf8(const std::wstring& text)
{
    if (text.empty())
        return {};
    const int length = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0)
        return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
    return result;
}

std::wstring utf8ToWide(const std::string& text)
{
    if (text.empty())
        return {};
    const int length = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0)
        return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

std::wstring decodeText(const std::vector<std::uint8_t>& bytes)
{
    if (bytes.empty())
        return {};

    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE)
    {
        const auto characterCount = (bytes.size() - 2) / sizeof(wchar_t);
        return std::wstring(reinterpret_cast<const wchar_t*>(bytes.data() + 2), characterCount);
    }

    std::size_t offset = 0;
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
        offset = 3;

    const char* source = reinterpret_cast<const char*>(bytes.data() + offset);
    const int sourceLength = static_cast<int>(bytes.size() - offset);
    int length = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, sourceLength, nullptr, 0);
    UINT codePage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (length <= 0)
    {
        codePage = CP_ACP;
        flags = 0;
        length = ::MultiByteToWideChar(codePage, flags, source, sourceLength, nullptr, 0);
    }
    if (length <= 0)
        return L"[NppHistory could not decode this revision as text.]";
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    ::MultiByteToWideChar(codePage, flags, source, sourceLength, result.data(), length);
    return result;
}

std::string utcTimestampCompact()
{
    SYSTEMTIME time{};
    ::GetSystemTime(&time);
    char buffer[32]{};
    sprintf_s(buffer, "%04u%02u%02u-%02u%02u%02u-%03u", time.wYear, time.wMonth, time.wDay,
        time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    return buffer;
}

std::wstring localTimestampDisplay(const fs::file_time_type& value)
{
    const auto systemTimePoint = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        value - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t raw = std::chrono::system_clock::to_time_t(systemTimePoint);
    std::tm local{};
    if (localtime_s(&local, &raw) != 0)
        return L"Unknown";

    SYSTEMTIME time{};
    time.wYear = static_cast<WORD>(local.tm_year + 1900);
    time.wMonth = static_cast<WORD>(local.tm_mon + 1);
    time.wDay = static_cast<WORD>(local.tm_mday);
    time.wDayOfWeek = static_cast<WORD>(local.tm_wday);
    time.wHour = static_cast<WORD>(local.tm_hour);
    time.wMinute = static_cast<WORD>(local.tm_min);
    time.wSecond = static_cast<WORD>(local.tm_sec);

    wchar_t date[128]{};
    wchar_t clock[128]{};
    if (::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &time, nullptr,
            date, static_cast<int>(std::size(date)), nullptr) <= 0
        || ::GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &time, nullptr,
            clock, static_cast<int>(std::size(clock))) <= 0)
        return L"Unknown";
    return std::wstring(date) + L" " + clock;
}

std::wstring localDateDisplay(std::wstring_view isoDate)
{
    if (isoDate.empty())
        return {};
    if (isoDate.size() != 10 || isoDate[4] != L'-' || isoDate[7] != L'-')
        return std::wstring(isoDate);

    const auto digits = [](std::wstring_view value) {
        int result = 0;
        for (const wchar_t character : value)
        {
            if (character < L'0' || character > L'9')
                return -1;
            result = result * 10 + (character - L'0');
        }
        return result;
    };
    SYSTEMTIME dateValue{};
    const int year = digits(isoDate.substr(0, 4));
    const int month = digits(isoDate.substr(5, 2));
    const int day = digits(isoDate.substr(8, 2));
    if (year < 1601 || month < 1 || month > 12 || day < 1 || day > 31)
        return std::wstring(isoDate);
    dateValue.wYear = static_cast<WORD>(year);
    dateValue.wMonth = static_cast<WORD>(month);
    dateValue.wDay = static_cast<WORD>(day);
    FILETIME validation{};
    if (!::SystemTimeToFileTime(&dateValue, &validation))
        return std::wstring(isoDate);

    wchar_t formatted[128]{};
    if (::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &dateValue, nullptr,
            formatted, static_cast<int>(std::size(formatted)), nullptr) <= 0)
        return std::wstring(isoDate);
    return formatted;
}

void centerWindowOnOwner(HWND window, HWND owner)
{
    if (!window || !owner)
        return;
    RECT windowRect{};
    RECT ownerRect{};
    if (!GetWindowRect(window, &windowRect) || !GetWindowRect(owner, &ownerRect))
        return;
    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;
    int left = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    int top = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
    const HMONITOR monitor = MonitorFromRect(&ownerRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    if (monitor && GetMonitorInfoW(monitor, &info))
    {
        const int workLeft = static_cast<int>(info.rcWork.left);
        const int workTop = static_cast<int>(info.rcWork.top);
        const int workRight = static_cast<int>(info.rcWork.right);
        const int workBottom = static_cast<int>(info.rcWork.bottom);
        left = (std::max)(workLeft, (std::min)(left, workRight - width));
        top = (std::max)(workTop, (std::min)(top, workBottom - height));
    }
    SetWindowPos(window, nullptr, left, top, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void fitWindowWithinOwner(HWND window, HWND owner, int horizontalMargin, int verticalMargin,
    int minimumWidth, int minimumHeight)
{
    if (!window || !owner)
        return;
    RECT windowRect{};
    RECT ownerRect{};
    if (!GetWindowRect(window, &windowRect) || !GetWindowRect(owner, &ownerRect))
        return;
    const int currentWidth = static_cast<int>(windowRect.right - windowRect.left);
    const int currentHeight = static_cast<int>(windowRect.bottom - windowRect.top);
    const int ownerWidth = static_cast<int>(ownerRect.right - ownerRect.left);
    const int ownerHeight = static_cast<int>(ownerRect.bottom - ownerRect.top);
    const int availableWidth = (std::max)(minimumWidth, ownerWidth - horizontalMargin);
    const int availableHeight = (std::max)(minimumHeight, ownerHeight - verticalMargin);
    SetWindowPos(window, nullptr, 0, 0,
        (std::min)(currentWidth, availableWidth),
        (std::min)(currentHeight, availableHeight),
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}
}
