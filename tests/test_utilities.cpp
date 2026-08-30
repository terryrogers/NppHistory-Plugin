#include "TestHarness.h"
#include "Utilities.h"

#include <chrono>
#include <cstdlib>
#include <regex>

namespace fs = std::filesystem;
using namespace npphistory;

void runUtilityTests(TestContext& context)
{
    TestDirectory directory(L"utilities");
    const fs::path nested = directory.path() / L"nested" / L"bytes.bin";
    const std::vector<std::uint8_t> first{0, 1, 2, 0xFE, 0xFF};
    const std::vector<std::uint8_t> replacement{'n', 'e', 'w'};

    context.expect(readAllBytes(directory.path() / L"missing.bin").empty(),
        "readAllBytes returns empty for a missing file");
    context.expect(writeAllBytesAtomic(nested, first),
        "writeAllBytesAtomic creates parent directories and writes binary data");
    context.expect(readAllBytes(nested) == first,
        "readAllBytes preserves every binary byte");
    context.expect(writeAllBytesAtomic(nested, replacement),
        "writeAllBytesAtomic replaces an existing file");
    context.expect(readAllBytes(nested) == replacement,
        "atomic replacement exposes only the new content");
    context.expect(writeAllBytesAtomic(nested, {}),
        "writeAllBytesAtomic supports an empty file");
    context.expect(fs::file_size(nested) == 0, "empty atomic write truncates the file");

    context.expect(sha256Hex(std::vector<std::uint8_t>{})
        == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "sha256Hex bytes matches the standard empty-input vector");
    context.expect(sha256Hex(std::vector<std::uint8_t>{'a', 'b', 'c'})
        == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "sha256Hex bytes matches the standard abc vector");
    context.expect(sha256Hex(L"abc")
        == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "sha256Hex wide text hashes its UTF-8 representation");

    const std::wstring unicode = L"Terry \u03A9 \U0001F642";
    const std::string utf8 = wideToUtf8(unicode);
    context.expect(!utf8.empty() && utf8ToWide(utf8) == unicode,
        "wideToUtf8 and utf8ToWide round-trip Unicode text");
    context.expect(wideToUtf8(L"").empty() && utf8ToWide("").empty(),
        "UTF conversion functions preserve empty input");
    context.expect(utf8ToWide(std::string("\xC3\x28", 2)).empty(),
        "utf8ToWide rejects malformed UTF-8");

    const std::wstring normalized = normalizePath(directory.path() / L"Folder" / L".." / L"FILE.txt");
    context.expect(fs::path(normalized).is_absolute(), "normalizePath returns an absolute path");
    context.expect(normalized.find(L'/') == std::wstring::npos,
        "normalizePath uses Windows separators");
    context.expect(normalized.find(L"..") == std::wstring::npos,
        "normalizePath removes lexical parent components");
    std::wstring lower = normalized;
    CharLowerBuffW(lower.data(), static_cast<DWORD>(lower.size()));
    context.expect(normalized == lower, "normalizePath normalizes path case");

    context.expect(decodeText({}).empty(), "decodeText preserves empty content");
    context.expect(decodeText({'h', 'i'}) == L"hi", "decodeText reads UTF-8 without a BOM");
    context.expect(decodeText({0xEF, 0xBB, 0xBF, 'h', 'i'}) == L"hi",
        "decodeText removes a UTF-8 BOM");
    const std::wstring utf16Text = L"wide \u03A9";
    std::vector<std::uint8_t> utf16{0xFF, 0xFE};
    const auto* utf16Bytes = reinterpret_cast<const std::uint8_t*>(utf16Text.data());
    utf16.insert(utf16.end(), utf16Bytes,
        utf16Bytes + utf16Text.size() * sizeof(wchar_t));
    context.expect(decodeText(utf16) == utf16Text,
        "decodeText reads UTF-16 little-endian content");
    context.expect(!decodeText({0xE9}).empty(),
        "decodeText falls back to the Windows ANSI code page for non-UTF-8 text");

    const std::string stamp = utcTimestampCompact();
    context.expect(std::regex_match(stamp,
        std::regex(R"(^\d{8}-\d{6}-\d{3}$)")),
        "utcTimestampCompact has the documented sortable UTC shape");
    context.expect(localTimestampDisplay(fs::file_time_type::clock::now()) != L"Unknown",
        "localTimestampDisplay formats a valid file timestamp");

    SYSTEMTIME date{2026, 8, 0, 29};
    wchar_t expectedDate[128]{};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &date, nullptr,
        expectedDate, static_cast<int>(std::size(expectedDate)), nullptr);
    context.expect(localDateDisplay(L"2026-08-29") == expectedDate,
        "localDateDisplay follows the current Windows short-date format");
    context.expect(localDateDisplay(L"").empty(), "localDateDisplay preserves an empty release date");
    context.expect(localDateDisplay(L"not-a-date") == L"not-a-date",
        "localDateDisplay preserves non-ISO metadata");
    context.expect(localDateDisplay(L"2026-02-31") == L"2026-02-31",
        "localDateDisplay rejects an impossible ISO date");
    context.expect(localDateDisplay(L"1500-01-01") == L"1500-01-01",
        "localDateDisplay rejects dates outside FILETIME range");

    context.expect(formatFileSize(0) == L"0 B" && formatFileSize(1023) == L"1023 B",
        "formatFileSize keeps byte values below one kibibyte in bytes");
    context.expect(formatFileSize(1024) == L"1 KB"
        && formatFileSize(1536) == L"1.5 KB",
        "formatFileSize scales and trims kilobyte values");
    context.expect(formatFileSize(1024ULL * 1024ULL) == L"1 MB"
        && formatFileSize(5ULL * 1024ULL * 1024ULL * 1024ULL) == L"5 GB",
        "formatFileSize selects dynamic units for larger revisions");

    context.expect(wildcardMatchCaseInsensitive(L"*.log", L"NppHistory.LOG"),
        "wildcard matching is case-insensitive and supports star");
    context.expect(wildcardMatchCaseInsensitive(L"myfile*.com", L"myfile.com")
        && wildcardMatchCaseInsensitive(L"myfile*.com", L"myfiles.com"),
        "star matches zero or more filename characters");
    context.expect(wildcardMatchCaseInsensitive(L"report-????.txt", L"report-2026.txt")
        && !wildcardMatchCaseInsensitive(L"report-????.txt", L"report-26.txt"),
        "question mark matches exactly one character");
    context.expect(!wildcardMatchCaseInsensitive(L"*.log", L"notes.log.bak"),
        "wildcard matching is anchored to the complete value");
    context.expect(pathMatchesWildcardList(L"C:\\Notes\\NppHistory.LOG",
        L"\r\n *.tmp \n  *.log\t\r\n"),
        "multiline wildcard lists ignore blank rows and surrounding whitespace");
    context.expect(pathMatchesWildcardList(L"C:\\Notes\\NppHistory.log",
        L"C:/Notes/*.log"),
        "patterns containing a path separator match the normalized full path");
    context.expect(!pathMatchesWildcardList(L"C:\\Other\\NppHistory.log",
        L"C:/Notes/*.log"),
        "full-path wildcard patterns do not match a different directory");
    context.expect(!pathMatchesWildcardList(L"C:\\Notes\\NppHistory.log", L""),
        "an empty wildcard list excludes no files");

    centerWindowOnOwner(nullptr, nullptr);
    fitWindowWithinOwner(nullptr, nullptr, 0, 0, 0, 0);
    context.expect(true, "window helpers safely ignore null handles");

    const HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"owner", WS_POPUP,
        120, 120, 300, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    const HWND child = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"child", WS_POPUP,
        0, 0, 100, 80, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    centerWindowOnOwner(child, owner);
    RECT ownerRect{};
    RECT childRect{};
    GetWindowRect(owner, &ownerRect);
    GetWindowRect(child, &childRect);
    context.expect(std::abs((ownerRect.left + ownerRect.right)
        - (childRect.left + childRect.right)) <= 2
        && std::abs((ownerRect.top + ownerRect.bottom)
            - (childRect.top + childRect.bottom)) <= 2,
        "centerWindowOnOwner centers a child window on its owner");

    SetWindowPos(child, nullptr, 0, 0, 500, 400, SWP_NOMOVE | SWP_NOZORDER);
    fitWindowWithinOwner(child, owner, 20, 40, 50, 50);
    GetWindowRect(child, &childRect);
    context.expect(childRect.right - childRect.left == 280
        && childRect.bottom - childRect.top == 160,
        "fitWindowWithinOwner constrains width and height to owner margins");
    DestroyWindow(child);
    DestroyWindow(owner);
}
