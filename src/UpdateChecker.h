#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace npphistory
{
struct SemanticVersion
{
    std::uint64_t major = 0;
    std::uint64_t minor = 0;
    std::uint64_t patch = 0;
    std::vector<std::wstring> prerelease;
};

struct ReleaseInfo
{
    std::wstring tag;
    std::wstring url;
    bool prerelease = false;
};

enum class UpdateCheckStatus
{
    updateAvailable,
    upToDate,
    networkError,
    invalidResponse
};

struct UpdateCheckResult
{
    UpdateCheckStatus status = UpdateCheckStatus::networkError;
    ReleaseInfo release;
    std::wstring detail;
};

bool parseSemanticVersion(std::wstring_view text, SemanticVersion& version);
int compareSemanticVersions(const SemanticVersion& left, const SemanticVersion& right) noexcept;
std::vector<ReleaseInfo> parseGitHubReleases(std::string_view json);
std::optional<ReleaseInfo> selectNewestRelease(const std::vector<ReleaseInfo>& releases,
    bool includePrereleases);
std::optional<ReleaseInfo> selectNewestUpdate(const std::vector<ReleaseInfo>& releases,
    std::wstring_view currentVersion, bool includePrereleases);
bool elapsedFrequencyDue(std::uint64_t nowSeconds, std::uint64_t lastSeconds,
    unsigned intervalDays) noexcept;
bool trustedReleaseUrl(std::wstring_view url) noexcept;
bool shouldNotifyUpdate(std::wstring_view availableVersion,
    std::wstring_view lastNotifiedVersion, bool manual) noexcept;
std::wstring updateAccessErrorMessage(unsigned httpStatus, unsigned long systemError);
std::uint64_t currentUnixSeconds() noexcept;
UpdateCheckResult checkGitHubForUpdates(std::wstring_view currentVersion,
    bool includePrereleases);
}
