#include "TestHarness.h"
#include "Utilities.h"

#include <chrono>
#include <commctrl.h>
#include <cstdlib>
#include <regex>

namespace fs = std::filesystem;
using namespace npphistory;

namespace
{
void runMenuBitmapTests(TestContext& context)
{
    context.expect(createMenuIconBitmap(nullptr) == nullptr, "menu bitmap rejects a null icon");
    const HICON systemIcon = LoadIconW(nullptr, IDI_APPLICATION);
    context.expect(createMenuIconBitmap(systemIcon, 0) == nullptr
        && createMenuIconBitmap(systemIcon, -1) == nullptr
        && createMenuIconBitmap(systemIcon, 257) == nullptr,
        "menu bitmap rejects invalid or excessive dimensions");
    for (bool legacy : {false, true})
    {
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), 2, -2, 1, 32, BI_RGB};
        std::uint32_t* pixels = nullptr;
        HBITMAP colour = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS,
            reinterpret_cast<void**>(&pixels), nullptr, 0);
        context.expect(colour != nullptr, "synthetic icon colour bitmap created");
        if (!colour) continue;
        pixels[0] = 0;
        pixels[1] = legacy ? 0 : 0xFF000000; // Opaque black must not become transparent.
        pixels[2] = legacy ? 0x00FF0000 : 0x80FF0000; // Icon input uses straight-alpha red.
        pixels[3] = legacy ? 0x000000FF : 0xFF0000FF;
        const BYTE maskBits[] = {static_cast<BYTE>(legacy ? 0x80 : 0), 0, 0, 0};
        HBITMAP mask = CreateBitmap(2, 2, 1, 1, maskBits);
        ICONINFO iconInfo{};
        iconInfo.fIcon = TRUE;
        iconInfo.hbmColor = colour;
        iconInfo.hbmMask = mask;
        HICON icon = CreateIconIndirect(&iconInfo);
        context.expect(icon != nullptr, "synthetic alpha or legacy mask icon created");
        HBITMAP result = createMenuIconBitmap(icon, 2);
        DIBSECTION dib{};
        const bool valid = result && GetObjectW(result, sizeof(dib), &dib) == sizeof(dib);
        context.expect(valid && dib.dsBm.bmBitsPixel == 32 && dib.dsBm.bmWidth == 2
            && dib.dsBm.bmHeight == 2,
            "menu image is a 32-bit DIB, not an opaque compatible bitmap");
        if (valid)
        {
            const auto* actual = static_cast<const std::uint32_t*>(dib.dsBm.bmBits);
            context.expect(actual[0] == 0, "transparent menu pixels have no baked background");
            context.expect(actual[1] == 0xFF000000, "opaque black icon pixels are preserved");
            context.expect(actual[2] == (legacy ? 0xFFFF0000 : 0x80800000),
                "menu bitmap preserves masked colours and fractional premultiplied alpha");
            context.expect(actual[3] == 0xFF0000FF, "opaque blue icon pixels are preserved");
        }
        ICONINFO stillOwned{};
        context.expect(icon && GetIconInfo(icon, &stillOwned), "conversion does not destroy caller's icon");
        if (stillOwned.hbmColor) DeleteObject(stillOwned.hbmColor);
        if (stillOwned.hbmMask) DeleteObject(stillOwned.hbmMask);
        if (result) DeleteObject(result);
        if (icon) DestroyIcon(icon);
        if (mask) DeleteObject(mask);
        DeleteObject(colour);
    }

    wchar_t executable[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    const auto resources = fs::path(executable).parent_path().parent_path().parent_path() / L"src" / L"res";
    for (const wchar_t* file : {L"Capture.ico", L"Compare.ico", L"Restore.ico", L"NppHistory.ico",
        L"Refresh.ico", L"Settings.ico", L"About.ico", L"Delete.ico", L"Edit.ico"})
    {
        HICON icon = static_cast<HICON>(LoadImageW(nullptr, (resources / file).c_str(),
            IMAGE_ICON, 16, 16, LR_LOADFROMFILE));
        context.expect(icon != nullptr, "actual command icon fixture loads");
        HBITMAP bitmap = createMenuIconBitmap(icon);
        DIBSECTION dib{};
        const bool valid = bitmap && GetObjectW(bitmap, sizeof(dib), &dib) == sizeof(dib);
        context.expect(valid && dib.dsBm.bmBitsPixel == 32 && dib.dsBm.bmWidth == 16,
            "actual command icon converted to alpha menu bitmap");
        if (valid)
        {
            const auto* pixels = static_cast<const std::uint32_t*>(dib.dsBm.bmBits);
            bool transparent = false, visible = false, premultiplied = true;
            for (int i = 0; i < 256; ++i)
            {
                const auto alpha = pixels[i] >> 24;
                transparent |= alpha == 0;
                visible |= alpha != 0;
                for (int shift : {0, 8, 16})
                    premultiplied &= ((pixels[i] >> shift) & 255) <= alpha;
            }
            context.expect(transparent && visible && premultiplied,
                "actual menu icon has visible artwork and valid transparent/premultiplied pixels");
            HMENU menu = CreatePopupMenu();
            MENUITEMINFOW item{sizeof(item)};
            item.fMask = MIIM_ID | MIIM_BITMAP | MIIM_STATE;
            item.wID = 1;
            item.hbmpItem = bitmap;
            item.fState = MFS_DISABLED;
            context.expect(InsertMenuItemW(menu, 0, TRUE, &item) != FALSE,
                "alpha bitmap attaches to a disabled native menu item");
            EnableMenuItem(menu, 1, MF_BYCOMMAND | MF_ENABLED);
            MENUITEMINFOW readback{sizeof(readback)};
            readback.fMask = MIIM_BITMAP | MIIM_STATE;
            context.expect(GetMenuItemInfoW(menu, 1, FALSE, &readback)
                && readback.hbmpItem == bitmap && !(readback.fState & MFS_DISABLED),
                "native menu state changes preserve the same alpha bitmap");
            DestroyMenu(menu);
        }
        if (bitmap) DeleteObject(bitmap);
        if (icon) DestroyIcon(icon);
    }
}
}

void runUtilityTests(TestContext& context)
{
    runMenuBitmapTests(context);
    context.expect(!isTooltipInputControl(nullptr), "null controls cannot have tooltips");
    const HWND tooltipOwner = CreateWindowExW(0, L"Static", L"Tooltip test", WS_POPUP,
        0, 0, 200, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    context.expect(tooltipOwner != nullptr, "tooltip test owner created");
    if (tooltipOwner)
    {
        struct ControlCase { const wchar_t* type; DWORD style; bool eligible; };
        const ControlCase cases[] = {
            {L"Static", SS_LEFT, false}, {L"Static", SS_NOTIFY, false},
            {L"Button", BS_GROUPBOX, false}, {L"Button", BS_PUSHBUTTON, true},
            {L"Button", BS_AUTOCHECKBOX | WS_DISABLED, true},
            {L"Button", BS_AUTORADIOBUTTON, true}, {L"Edit", ES_READONLY, true},
            {L"Edit", ES_MULTILINE, true}, {L"ComboBox", CBS_DROPDOWNLIST, true}
        };
        for (const auto& spec : cases)
        {
            const HWND control = CreateWindowExW(0, spec.type, L"", WS_CHILD | spec.style,
                0, 0, 100, 20, tooltipOwner, reinterpret_cast<HMENU>(101),
                GetModuleHandleW(nullptr), nullptr);
            context.expect(control && isTooltipInputControl(control) == spec.eligible,
                "tooltip eligibility accepts inputs/buttons and rejects labels/group boxes");
            if (control) DestroyWindow(control);
        }
        DestroyWindow(tooltipOwner);
    }
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
    context.expect(matchingPathWildcardPattern(L"C:\\Notes\\NppHistory.LOG",
        L"*.tmp\r\n  *.log  \r\n*.bak") == L"*.log",
        "matchingPathWildcardPattern reports the trimmed exclusion responsible for a match");
    context.expect(matchingPathWildcardPattern(L"C:\\Notes\\NppHistory.txt",
        L"*.tmp\r\n*.log").empty(),
        "matchingPathWildcardPattern reports no exclusion for an unmatched file");
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

    const fs::path pluginRoot = directory.path() / L"plugins";
    context.expect(findExternalAutoSavePlugin(pluginRoot).empty(),
        "AutoSave conflict detection reports no plugin in an empty plugin root");
    const fs::path autoSaveDll = pluginRoot / L"AutoSave" / L"AutoSave.dll";
    context.expect(writeAllBytesAtomic(autoSaveDll, {'M', 'Z'}),
        "AutoSave conflict test creates a conventional plugin entry");
    context.expect(findExternalAutoSavePlugin(pluginRoot) == autoSaveDll,
        "AutoSave conflict detection finds the conventional AutoSave.dll layout");
    fs::remove(autoSaveDll);
    const fs::path renamedFolderDll = pluginRoot / L"Legacy Auto Save" / L"AUTOSAVE.DLL";
    context.expect(writeAllBytesAtomic(renamedFolderDll, {'M', 'Z'})
        && findExternalAutoSavePlugin(pluginRoot) == renamedFolderDll,
        "AutoSave conflict detection is recursive and case-insensitive");

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
