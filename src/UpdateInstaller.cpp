#include "UpdateInstaller.h"

#include "Utilities.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>
#include <winhttp.h>

namespace npphistory
{
namespace
{
bool downloadFile(const std::wstring& url, const std::wstring& digest,
    std::uint64_t expectedSize, const std::filesystem::path& destination,
    std::wstring& detail)
{
    URL_COMPONENTSW components{sizeof(components)};
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &components)
        || components.nScheme != INTERNET_SCHEME_HTTPS)
    {
        detail = L"The update download address is invalid.";
        return false;
    }
    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength)
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);

    HINTERNET session = WinHttpOpen(L"NppHistory/0.2 (update installer)",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
    {
        detail = L"Windows could not initialise the HTTPS update download.";
        return false;
    }
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 30000);
    HINTERNET connection = WinHttpConnect(session, host.c_str(), components.nPort, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    bool success = request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr);
    DWORD systemError = success ? ERROR_SUCCESS : GetLastError();
    DWORD status = 0, statusSize = sizeof(status);
    if (success)
    {
        const bool queried = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE
            | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
            &statusSize, WINHTTP_NO_HEADER_INDEX) != FALSE;
        if (!queried)
            systemError = GetLastError();
        success = queried && status == 200;
    }
    std::vector<std::uint8_t> bytes;
    while (success)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available))
        { systemError = GetLastError(); success = false; break; }
        if (available == 0)
            break;
        if (bytes.size() + available > expectedSize
            || bytes.size() + available > 32ULL * 1024ULL * 1024ULL)
        { systemError = ERROR_INSUFFICIENT_BUFFER; success = false; break; }
        const std::size_t offset = bytes.size();
        bytes.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(request, bytes.data() + offset, available, &read))
        { systemError = GetLastError(); success = false; break; }
        bytes.resize(offset + read);
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    if (!success)
    {
        detail = updateAccessErrorMessage(status, systemError);
        return false;
    }
    if (bytes.size() != expectedSize)
    {
        detail = L"The downloaded update size does not match its release metadata.";
        return false;
    }
    std::string expected = wideToUtf8(digest.substr(7));
    std::transform(expected.begin(), expected.end(), expected.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (sha256Hex(bytes) != expected)
    {
        detail = L"The downloaded update failed SHA-256 verification and was discarded.";
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error || !writeAllBytesAtomic(destination, bytes))
    {
        detail = L"The verified update could not be written to the staging folder.";
        return false;
    }
    return true;
}
}

bool downloadVerifiedUpdatePackage(const ReleaseInfo& release,
    const std::filesystem::path& dllDestination,
    const std::filesystem::path& updaterDestination, std::wstring& detail)
{
    if (!trustedUpdateAsset(release))
    {
        detail = L"This release does not contain a trusted x64 installation package.";
        return false;
    }
    if (!downloadFile(release.assetUrl, release.assetDigest, release.assetSize,
        dllDestination, detail))
        return false;
    if (!downloadFile(release.updaterUrl, release.updaterDigest, release.updaterSize,
        updaterDestination, detail))
    {
        std::error_code ignored;
        std::filesystem::remove(dllDestination, ignored);
        return false;
    }
    return true;
}

std::wstring quoteCommandLineArgument(const std::wstring& value)
{
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t character : value)
    {
        if (character == L'\\')
        {
            ++slashes;
            continue;
        }
        if (character == L'\"')
        {
            result.append(slashes * 2 + 1, L'\\');
            result.push_back(L'\"');
            slashes = 0;
            continue;
        }
        result.append(slashes, L'\\');
        slashes = 0;
        result.push_back(character);
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}
}
