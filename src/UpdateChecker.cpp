#include "UpdateChecker.h"

#include "Utilities.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cwctype>
#include <limits>
#include <winhttp.h>

namespace npphistory
{
namespace
{
constexpr wchar_t releaseUrlPrefix[] =
    L"https://github.com/terryrogers/NppHistory-Plugin/releases/";

bool parseNumber(std::wstring_view text, std::uint64_t& value)
{
    if (text.empty() || (text.size() > 1 && text.front() == L'0'))
        return false;
    value = 0;
    for (const wchar_t character : text)
    {
        if (character < L'0' || character > L'9')
            return false;
        const auto digit = static_cast<std::uint64_t>(character - L'0');
        if (value > ((std::numeric_limits<std::uint64_t>::max)() - digit) / 10)
            return false;
        value = value * 10 + digit;
    }
    return true;
}

bool numericIdentifier(std::wstring_view value) noexcept
{
    return !value.empty() && std::all_of(value.begin(), value.end(),
        [](wchar_t character) { return character >= L'0' && character <= L'9'; });
}

bool validIdentifier(std::wstring_view value) noexcept
{
    return !value.empty() && std::all_of(value.begin(), value.end(), [](wchar_t character) {
        return (character >= L'a' && character <= L'z')
            || (character >= L'A' && character <= L'Z')
            || (character >= L'0' && character <= L'9') || character == L'-';
    });
}

bool parseJsonString(std::string_view text, std::size_t& position, std::string& value)
{
    if (position >= text.size() || text[position] != '"')
        return false;
    ++position;
    value.clear();
    while (position < text.size())
    {
        const char character = text[position++];
        if (character == '"')
            return true;
        if (character != '\\')
        {
            value.push_back(character);
            continue;
        }
        if (position >= text.size())
            return false;
        const char escaped = text[position++];
        switch (escaped)
        {
        case '"': value.push_back('"'); break;
        case '\\': value.push_back('\\'); break;
        case '/': value.push_back('/'); break;
        case 'b': value.push_back('\b'); break;
        case 'f': value.push_back('\f'); break;
        case 'n': value.push_back('\n'); break;
        case 'r': value.push_back('\r'); break;
        case 't': value.push_back('\t'); break;
        case 'u':
        {
            if (position + 4 > text.size()) return false;
            unsigned code = 0;
            for (int index = 0; index < 4; ++index)
            {
                const char digit = text[position++];
                code *= 16;
                if (digit >= '0' && digit <= '9') code += digit - '0';
                else if (digit >= 'a' && digit <= 'f') code += digit - 'a' + 10;
                else if (digit >= 'A' && digit <= 'F') code += digit - 'A' + 10;
                else return false;
            }
            value.push_back(code <= 0x7F ? static_cast<char>(code) : '?');
            break;
        }
        default: return false;
        }
    }
    return false;
}

void skipJsonValue(std::string_view text, std::size_t& position)
{
    while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])))
        ++position;
    if (position >= text.size())
        return;
    if (text[position] == '"')
    {
        std::string ignored;
        parseJsonString(text, position, ignored);
        return;
    }
    if (text[position] == '{' || text[position] == '[')
    {
        const char open = text[position++];
        const char close = open == '{' ? '}' : ']';
        int depth = 1;
        bool quoted = false;
        bool escaped = false;
        while (position < text.size() && depth > 0)
        {
            const char character = text[position++];
            if (quoted)
            {
                if (escaped) escaped = false;
                else if (character == '\\') escaped = true;
                else if (character == '"') quoted = false;
            }
            else if (character == '"') quoted = true;
            else if (character == open) ++depth;
            else if (character == close) --depth;
        }
        return;
    }
    while (position < text.size() && text[position] != ',' && text[position] != '}')
        ++position;
}

bool parseReleaseObject(std::string_view object, ReleaseInfo& release, bool& draft)
{
    if (object.empty() || object.front() != '{')
        return false;
    bool haveTag = false;
    bool haveUrl = false;
    bool haveDraft = false;
    bool havePrerelease = false;
    std::size_t position = 1;
    while (position < object.size())
    {
        while (position < object.size() && (std::isspace(static_cast<unsigned char>(object[position]))
            || object[position] == ','))
            ++position;
        if (position >= object.size() || object[position] == '}')
            break;
        std::string key;
        if (!parseJsonString(object, position, key))
            return false;
        while (position < object.size() && std::isspace(static_cast<unsigned char>(object[position])))
            ++position;
        if (position >= object.size() || object[position++] != ':')
            return false;
        while (position < object.size() && std::isspace(static_cast<unsigned char>(object[position])))
            ++position;
        if (key == "tag_name" || key == "html_url")
        {
            std::string value;
            if (!parseJsonString(object, position, value))
                return false;
            const std::wstring wide = utf8ToWide(value);
            if (wide.empty() && !value.empty())
                return false;
            if (key == "tag_name") { release.tag = wide; haveTag = true; }
            else { release.url = wide; haveUrl = true; }
        }
        else if (key == "draft" || key == "prerelease")
        {
            bool value = false;
            if (object.substr(position, 4) == "true") { value = true; position += 4; }
            else if (object.substr(position, 5) == "false") { position += 5; }
            else return false;
            if (key == "draft") { draft = value; haveDraft = true; }
            else { release.prerelease = value; havePrerelease = true; }
        }
        else
        {
            skipJsonValue(object, position);
        }
    }
    return haveTag && haveUrl && haveDraft && havePrerelease;
}

std::vector<std::string_view> topLevelObjects(std::string_view json)
{
    std::vector<std::string_view> objects;
    std::size_t position = 0;
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position])))
        ++position;
    if (position >= json.size() || json[position++] != '[')
        return objects;
    while (position < json.size())
    {
        while (position < json.size() && (std::isspace(static_cast<unsigned char>(json[position]))
            || json[position] == ','))
            ++position;
        if (position >= json.size())
            return {};
        if (json[position] == ']')
        {
            ++position;
            while (position < json.size()
                && std::isspace(static_cast<unsigned char>(json[position])))
                ++position;
            return position == json.size() ? objects : std::vector<std::string_view>{};
        }
        if (json[position] != '{')
            return {};
        const std::size_t start = position++;
        int depth = 1;
        bool quoted = false;
        bool escaped = false;
        while (position < json.size() && depth > 0)
        {
            const char character = json[position++];
            if (quoted)
            {
                if (escaped) escaped = false;
                else if (character == '\\') escaped = true;
                else if (character == '"') quoted = false;
            }
            else if (character == '"') quoted = true;
            else if (character == '{') ++depth;
            else if (character == '}') --depth;
        }
        if (depth != 0)
            return {};
        objects.emplace_back(json.substr(start, position - start));
    }
    return {};
}

std::optional<std::string> downloadReleaseJson(std::wstring& detail)
{
    HINTERNET session = WinHttpOpen(L"NppHistory/0.2 (update check)",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
    {
        detail = L"Windows could not initialise the HTTPS connection.";
        return std::nullopt;
    }
    WinHttpSetTimeouts(session, 4000, 4000, 4000, 6000);
    HINTERNET connection = WinHttpConnect(session, L"api.github.com",
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET",
        L"/repos/terryrogers/NppHistory-Plugin/releases?per_page=20", nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    const wchar_t headers[] = L"Accept: application/vnd.github+json\r\n"
        L"X-GitHub-Api-Version: 2022-11-28\r\n";
    bool success = request && WinHttpSendRequest(request, headers, static_cast<DWORD>(-1L),
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr);
    DWORD systemError = success ? ERROR_SUCCESS : GetLastError();
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (success)
    {
        const bool queried = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE
            | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
            &statusSize, WINHTTP_NO_HEADER_INDEX) != FALSE;
        if (!queried)
            systemError = GetLastError();
        success = queried && status == 200;
    }

    std::string body;
    while (success)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available))
        { systemError = GetLastError(); success = false; break; }
        if (available == 0) break;
        if (body.size() + available > 1024 * 1024)
        { systemError = ERROR_INSUFFICIENT_BUFFER; success = false; break; }
        const std::size_t offset = body.size();
        body.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(request, body.data() + offset, available, &read))
        { systemError = GetLastError(); success = false; break; }
        body.resize(offset + read);
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    if (!success)
    {
        detail = updateAccessErrorMessage(status, systemError);
        return std::nullopt;
    }
    return body;
}
}

bool parseSemanticVersion(std::wstring_view text, SemanticVersion& version)
{
    version = {};
    if (!text.empty() && (text.front() == L'v' || text.front() == L'V'))
        text.remove_prefix(1);
    const auto build = text.find(L'+');
    if (build != std::wstring_view::npos)
    {
        if (text.find(L'+', build + 1) != std::wstring_view::npos)
            return false;
        std::wstring_view metadata = text.substr(build + 1);
        while (!metadata.empty())
        {
            const auto separator = metadata.find(L'.');
            if (!validIdentifier(metadata.substr(0, separator)))
                return false;
            if (separator == std::wstring_view::npos)
                break;
            metadata.remove_prefix(separator + 1);
        }
        if (metadata.empty())
            return false;
        text = text.substr(0, build);
    }
    const auto dash = text.find(L'-');
    const auto core = text.substr(0, dash);
    const auto first = core.find(L'.');
    const auto second = first == std::wstring_view::npos ? first : core.find(L'.', first + 1);
    if (first == std::wstring_view::npos || second == std::wstring_view::npos
        || core.find(L'.', second + 1) != std::wstring_view::npos
        || !parseNumber(core.substr(0, first), version.major)
        || !parseNumber(core.substr(first + 1, second - first - 1), version.minor)
        || !parseNumber(core.substr(second + 1), version.patch))
        return false;
    if (dash == std::wstring_view::npos)
        return true;
    std::wstring_view prerelease = text.substr(dash + 1);
    while (!prerelease.empty())
    {
        const auto separator = prerelease.find(L'.');
        const auto identifier = prerelease.substr(0, separator);
        if (!validIdentifier(identifier)
            || (numericIdentifier(identifier) && identifier.size() > 1 && identifier.front() == L'0'))
            return false;
        version.prerelease.emplace_back(identifier);
        if (separator == std::wstring_view::npos)
            break;
        prerelease.remove_prefix(separator + 1);
    }
    return !version.prerelease.empty();
}

int compareSemanticVersions(const SemanticVersion& left, const SemanticVersion& right) noexcept
{
    const auto compareNumber = [](std::uint64_t a, std::uint64_t b) { return a < b ? -1 : a > b ? 1 : 0; };
    if (const int value = compareNumber(left.major, right.major)) return value;
    if (const int value = compareNumber(left.minor, right.minor)) return value;
    if (const int value = compareNumber(left.patch, right.patch)) return value;
    if (left.prerelease.empty() || right.prerelease.empty())
        return left.prerelease.empty() == right.prerelease.empty() ? 0
            : left.prerelease.empty() ? 1 : -1;
    const std::size_t common = (std::min)(left.prerelease.size(), right.prerelease.size());
    for (std::size_t index = 0; index < common; ++index)
    {
        const auto& a = left.prerelease[index];
        const auto& b = right.prerelease[index];
        const bool aNumeric = numericIdentifier(a);
        const bool bNumeric = numericIdentifier(b);
        if (aNumeric && bNumeric)
        {
            if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
            if (a != b) return a < b ? -1 : 1;
        }
        else if (aNumeric != bNumeric) return aNumeric ? -1 : 1;
        else if (a != b) return a < b ? -1 : 1;
    }
    return compareNumber(left.prerelease.size(), right.prerelease.size());
}

std::vector<ReleaseInfo> parseGitHubReleases(std::string_view json)
{
    std::vector<ReleaseInfo> releases;
    for (const auto object : topLevelObjects(json))
    {
        ReleaseInfo release;
        bool draft = false;
        if (parseReleaseObject(object, release, draft) && !draft
            && trustedReleaseUrl(release.url))
            releases.push_back(std::move(release));
    }
    return releases;
}

std::optional<ReleaseInfo> selectNewestUpdate(const std::vector<ReleaseInfo>& releases,
    std::wstring_view currentVersion, bool includePrereleases)
{
    SemanticVersion current;
    if (!parseSemanticVersion(currentVersion, current))
        return std::nullopt;
    std::optional<ReleaseInfo> selected;
    SemanticVersion selectedVersion;
    for (const auto& release : releases)
    {
        if (release.prerelease && !includePrereleases)
            continue;
        SemanticVersion candidate;
        if (!parseSemanticVersion(release.tag, candidate)
            || compareSemanticVersions(candidate, current) <= 0)
            continue;
        if (!selected || compareSemanticVersions(candidate, selectedVersion) > 0)
        {
            selected = release;
            selectedVersion = std::move(candidate);
        }
    }
    return selected;
}

bool elapsedFrequencyDue(std::uint64_t nowSeconds, std::uint64_t lastSeconds,
    unsigned intervalDays) noexcept
{
    if (lastSeconds == 0 || nowSeconds < lastSeconds)
        return true;
    return nowSeconds - lastSeconds >= static_cast<std::uint64_t>(intervalDays) * 86400ULL;
}

bool trustedReleaseUrl(std::wstring_view url) noexcept
{
    return url.size() > std::size(releaseUrlPrefix) - 1
        && url.substr(0, std::size(releaseUrlPrefix) - 1) == releaseUrlPrefix;
}

bool shouldNotifyUpdate(std::wstring_view availableVersion,
    std::wstring_view lastNotifiedVersion, bool manual) noexcept
{
    return !availableVersion.empty()
        && (manual || availableVersion != lastNotifiedVersion);
}

std::wstring updateAccessErrorMessage(unsigned httpStatus, unsigned long systemError)
{
    if (httpStatus == 403 || httpStatus == 429)
        return L"GitHub temporarily refused the request or its access limit was reached. Try again later.";
    if (httpStatus == 404)
        return L"The NppHistory GitHub repository or Releases service could not be accessed.";
    if (httpStatus == 407)
        return L"The configured network proxy requires authentication.";
    if (httpStatus != 0 && httpStatus != 200)
        return L"GitHub returned HTTP " + std::to_wstring(httpStatus) + L". Try again later.";
    switch (systemError)
    {
    case ERROR_WINHTTP_TIMEOUT:
        return L"The connection to GitHub timed out. Check the internet connection and try again.";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        return L"The GitHub address could not be resolved. Check DNS and internet access.";
    case ERROR_WINHTTP_CANNOT_CONNECT:
        return L"A connection to GitHub could not be established. Check firewall, proxy and internet access.";
    case ERROR_WINHTTP_SECURE_FAILURE:
        return L"Windows rejected the secure HTTPS connection to GitHub. Check the system clock, TLS inspection and certificate settings.";
    case ERROR_INSUFFICIENT_BUFFER:
        return L"GitHub returned more update data than NppHistory can safely process.";
    default:
        break;
    }
    if (systemError)
        return L"The GitHub Releases service could not be accessed (Windows error "
            + std::to_wstring(systemError) + L").";
    return L"The GitHub Releases service could not be accessed.";
}

std::uint64_t currentUnixSeconds() noexcept
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

UpdateCheckResult checkGitHubForUpdates(std::wstring_view currentVersion,
    bool includePrereleases)
{
    std::wstring detail;
    const auto json = downloadReleaseJson(detail);
    if (!json)
        return {UpdateCheckStatus::networkError, {}, std::move(detail)};
    const auto releases = parseGitHubReleases(*json);
    if (releases.empty())
        return {UpdateCheckStatus::invalidResponse, {},
            L"GitHub returned no usable NppHistory releases."};
    const auto update = selectNewestUpdate(releases, currentVersion, includePrereleases);
    return update ? UpdateCheckResult{UpdateCheckStatus::updateAvailable, *update, {}}
        : UpdateCheckResult{UpdateCheckStatus::upToDate, {}, {}};
}
}
