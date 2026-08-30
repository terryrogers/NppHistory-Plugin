#include "TestHarness.h"
#include "UpdateChecker.h"
#include "UpdateInstaller.h"

#include <winhttp.h>

using namespace npphistory;

void runUpdateCheckerTests(TestContext& context)
{
    SemanticVersion version;
    context.expect(parseSemanticVersion(L"v1.2.3", version)
        && version.major == 1 && version.minor == 2 && version.patch == 3
        && version.prerelease.empty(), "semantic version parser accepts a v-prefixed release");
    context.expect(parseSemanticVersion(L"0.2.0-beta.21+build.7", version)
        && version.prerelease.size() == 2 && version.prerelease[0] == L"beta"
        && version.prerelease[1] == L"21", "semantic version parser accepts prerelease/build metadata");
    for (const auto invalid : {L"", L"1.2", L"1.2.3.4", L"01.2.3", L"1.02.3",
        L"1.2.03", L"1.2.3-", L"1.2.3-beta..1", L"1.2.3-01", L"1.2.x",
        L"18446744073709551616.0.0", L"1.0.0+", L"1.0.0+bad?",
        L"1.0.0+foo..bar", L"1.0.0+one+two"})
        context.expect(!parseSemanticVersion(invalid, version),
            "semantic version parser rejects malformed versions");

    const auto compare = [&](const wchar_t* left, const wchar_t* right) {
        SemanticVersion a, b;
        return parseSemanticVersion(left, a) && parseSemanticVersion(right, b)
            ? compareSemanticVersions(a, b) : 99;
    };
    context.expect(compare(L"1.0.0", L"1.0.0") == 0, "equal semantic versions compare equally");
    context.expect(compare(L"2.0.0", L"1.99.99") > 0, "major versions compare numerically");
    context.expect(compare(L"1.10.0", L"1.9.9") > 0, "minor versions compare numerically");
    context.expect(compare(L"1.0.10", L"1.0.9") > 0, "patch versions compare numerically");
    context.expect(compare(L"1.0.0", L"1.0.0-rc.1") > 0, "stable releases outrank prereleases");
    context.expect(compare(L"1.0.0-beta.11", L"1.0.0-beta.2") > 0,
        "numeric prerelease identifiers compare numerically");
    context.expect(compare(L"1.0.0-beta.184467440737095516160",
        L"1.0.0-beta.18446744073709551615") > 0,
        "arbitrarily large numeric prerelease identifiers compare by numeric magnitude");
    context.expect(compare(L"1.0.0-beta", L"1.0.0-10") > 0,
        "alphanumeric prerelease identifiers outrank numeric identifiers");
    context.expect(compare(L"1.0.0-beta.1", L"1.0.0-beta") > 0,
        "a longer equal prerelease sequence ranks later");

    const std::string json = R"json([
      {"author":{"html_url":"https://attacker.invalid/"},"tag_name":"v0.2.0-beta.22",
       "html_url":"https://github.com/terryrogers/NppHistory-Plugin/releases/tag/v0.2.0-beta.22",
       "draft":false,"prerelease":true,"body":"escaped text and { braces }"},
      {"tag_name":"v1.0.0","html_url":"https://github.com/terryrogers/NppHistory-Plugin/releases/tag/v1.0.0",
       "draft":false,"prerelease":false},
      {"tag_name":"v9.0.0","html_url":"https://github.com/terryrogers/NppHistory-Plugin/releases/tag/v9.0.0",
       "draft":true,"prerelease":false},
      {"tag_name":"v8.0.0","html_url":"https://evil.invalid/releases/tag/v8.0.0",
       "draft":false,"prerelease":false}
    ])json";
    const auto releases = parseGitHubReleases(json);
    context.expect(releases.size() == 2, "release parser keeps non-draft releases from the trusted repository only");
    context.expect(releases.size() == 2 && releases[0].tag == L"v0.2.0-beta.22"
        && releases[0].prerelease, "release parser reads prerelease metadata at the top object level");
    context.expect(releases.size() == 2 && releases[1].tag == L"v1.0.0"
        && !releases[1].prerelease, "release parser reads stable release metadata");
    context.expect(parseGitHubReleases("not-json").empty(), "release parser rejects a non-array response");
    context.expect(parseGitHubReleases(
        "[{\"tag_name\":\"v1.0.0\",\"html_url\":\"https://github.com/terryrogers/NppHistory-Plugin/releases/tag/v1.0.0\",\"draft\":false,\"prerelease\":false}] trailing").empty(),
        "release parser rejects trailing response data");
    context.expect(parseGitHubReleases("[{\"tag_name\":\"v1.0.0\"}]").empty(),
        "release parser rejects incomplete release records");

    const std::string installableJson = R"json([{
      "tag_name":"v0.3.0-beta.1",
      "html_url":"https://github.com/terryrogers/NppHistory-Plugin/releases/tag/v0.3.0-beta.1",
      "draft":false,"prerelease":true,
      "assets":[
        {"name":"NppHistory-0.3.0-beta.1-source.zip","browser_download_url":"https://example.invalid/source.zip","digest":"sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","size":90000},
        {"name":"NppHistory-0.3.0-beta.1-x64.dll","browser_download_url":"https://github.com/terryrogers/NppHistory-Plugin/releases/download/v0.3.0-beta.1/NppHistory-0.3.0-beta.1-x64.dll","digest":"sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","size":500000},
        {"name":"NppHistoryUpdater.exe","browser_download_url":"https://github.com/terryrogers/NppHistory-Plugin/releases/download/v0.3.0-beta.1/NppHistoryUpdater.exe","digest":"sha256:abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789","size":180000}
      ]
    }])json";
    const auto installableReleases = parseGitHubReleases(installableJson);
    context.expect(installableReleases.size() == 1
        && trustedUpdateAsset(installableReleases.front()),
        "release parsing selects and trusts the exact versioned x64 DLL asset");
    ReleaseInfo tampered = installableReleases.front();
    tampered.assetUrl = L"https://evil.invalid/NppHistory.dll";
    context.expect(!trustedUpdateAsset(tampered),
        "automatic installation rejects an untrusted asset host");
    tampered = installableReleases.front();
    tampered.assetDigest = L"sha256:not-a-digest";
    context.expect(!trustedUpdateAsset(tampered),
        "automatic installation rejects malformed release digests");
    tampered = installableReleases.front();
    tampered.assetName = L"NppHistory-other-x64.dll";
    context.expect(!trustedUpdateAsset(tampered),
        "automatic installation requires the asset version to match the release tag");
    tampered = installableReleases.front();
    tampered.updaterUrl = L"https://evil.invalid/NppHistoryUpdater.exe";
    context.expect(!trustedUpdateAsset(tampered),
        "automatic installation also verifies the staged updater's exact release URL");
    tampered = installableReleases.front();
    tampered.tag = L"../malicious";
    context.expect(!trustedUpdateAsset(tampered),
        "automatic installation rejects a non-semantic release tag before staging paths are formed");
    context.expect(quoteCommandLineArgument(L"C:\\Program Files\\Notepad++\\notepad++.exe")
        == L"\"C:\\Program Files\\Notepad++\\notepad++.exe\"",
        "updater command-line quoting preserves paths containing spaces");
    context.expect(quoteCommandLineArgument(L"C:\\trailing\\") == L"\"C:\\trailing\\\\\"",
        "updater command-line quoting safely doubles a trailing backslash");

    const auto betaUpdate = selectNewestUpdate(releases, L"0.2.0-beta.21", true);
    context.expect(betaUpdate && betaUpdate->tag == L"v1.0.0",
        "include-prereleases channel selects the greatest newer version");
    const auto stableOnly = selectNewestUpdate(releases, L"0.2.0-beta.21", false);
    context.expect(stableOnly && stableOnly->tag == L"v1.0.0",
        "stable channel ignores prereleases");
    const std::vector<ReleaseInfo> betaOnly{releases.front()};
    context.expect(!selectNewestUpdate(betaOnly, L"0.2.0-beta.21", false),
        "stable channel does not offer a prerelease");
    context.expect(selectNewestUpdate(betaOnly, L"0.2.0-beta.21", true).has_value(),
        "prerelease channel offers a newer beta");
    context.expect(!selectNewestUpdate(releases, L"1.0.0", true),
        "no update is selected when the installed version is newest");
    context.expect(!selectNewestUpdate(releases, L"not-a-version", true),
        "an invalid installed version fails safely without selecting an update");
    const auto newestPublished = selectNewestRelease(releases, true);
    context.expect(newestPublished && newestPublished->tag == L"v1.0.0",
        "newest-release selection reports the greatest published version independently of the installed version");
    context.expect(selectNewestRelease(betaOnly, true)->tag == L"v0.2.0-beta.22"
        && !selectNewestRelease(betaOnly, false),
        "newest-release selection respects the prerelease channel");
    const std::vector<ReleaseInfo> publishedBetas{
        {L"v0.2.0-beta.20", L"https://github.com/terryrogers/NppHistory-Plugin/releases/tag/v0.2.0-beta.20", true},
        {L"v0.2.0-beta.24", L"https://github.com/terryrogers/NppHistory-Plugin/releases/tag/v0.2.0-beta.24", true}};
    context.expect(selectNewestUpdate(publishedBetas, L"0.2.0-beta.22", true)->tag
        == L"v0.2.0-beta.24",
        "beta 22 detects a published beta 24 when prereleases are included");

    const std::string publishedBetaJson = R"json([
      {"tag_name":"v0.2.0-beta.20","html_url":"https://github.com/terryrogers/NppHistory-Plugin/releases/tag/v0.2.0-beta.20","draft":false,"prerelease":true},
      {"tag_name":"v0.2.0-beta.24","html_url":"https://github.com/terryrogers/NppHistory-Plugin/releases/tag/v0.2.0-beta.24","draft":false,"prerelease":true}
    ])json";
    const auto availableResult = evaluateReleaseJson(publishedBetaJson,
        L"0.2.0-beta.22", true);
    context.expect(availableResult.status == UpdateCheckStatus::updateAvailable
        && availableResult.release.tag == L"v0.2.0-beta.24",
        "release evaluation reports beta 24 as available to beta 22");
    const auto currentResult = evaluateReleaseJson(publishedBetaJson,
        L"0.2.0-beta.24", true);
    context.expect(currentResult.status == UpdateCheckStatus::upToDate
        && currentResult.release.tag == L"v0.2.0-beta.24",
        "up-to-date evaluation retains the latest published version for status text");
    const auto stableResult = evaluateReleaseJson(publishedBetaJson, L"0.2.0", false);
    context.expect(stableResult.status == UpdateCheckStatus::upToDate
        && stableResult.release.tag.empty(),
        "stable-only evaluation ignores prerelease releases without failing");
    for (const auto invalidJson : {std::string_view("not-json"), std::string_view("[]"),
        std::string_view("[{\"tag_name\":\"v0.2.0-beta.24\"}]")})
    {
        const auto invalidResult = evaluateReleaseJson(invalidJson, L"0.2.0-beta.24", true);
        context.expect(invalidResult.status == UpdateCheckStatus::invalidResponse
            && invalidResult.detail.find(L"no usable") != std::wstring::npos,
            "invalid or unusable release data produces a safe invalid-response result");
    }

    context.expect(elapsedFrequencyDue(100, 0, 7), "an update check with no previous timestamp is due");
    context.expect(!elapsedFrequencyDue(604899, 100, 7), "weekly check is not due one second early");
    context.expect(elapsedFrequencyDue(604900, 100, 7), "weekly check is due exactly at its threshold");
    context.expect(elapsedFrequencyDue(50, 100, 7), "clock rollback safely permits a new check");

    context.expect(trustedReleaseUrl(L"https://github.com/terryrogers/NppHistory-Plugin/releases/tag/v1.0.0"),
        "the official release URL is trusted");
    context.expect(!trustedReleaseUrl(L"http://github.com/terryrogers/NppHistory-Plugin/releases/tag/v1.0.0")
        && !trustedReleaseUrl(L"https://github.com/other/NppHistory-Plugin/releases/tag/v1.0.0")
        && !trustedReleaseUrl(L"https://evil.invalid/terryrogers/NppHistory-Plugin/releases/tag/v1.0.0"),
        "non-HTTPS, wrong-owner and wrong-host release URLs are rejected");
    context.expect(shouldNotifyUpdate(L"v1.0.0", L"", false),
        "automatic checks notify for a previously unseen version");
    context.expect(!shouldNotifyUpdate(L"v1.0.0", L"v1.0.0", false),
        "automatic checks suppress a duplicate version notification");
    context.expect(shouldNotifyUpdate(L"v1.0.0", L"v1.0.0", true),
        "manual checks reoffer the selected update even when it was previously notified");
    context.expect(!shouldNotifyUpdate(L"", L"", true),
        "an empty release tag never produces a notification");

    context.expect(updateAccessErrorMessage(403, 0).find(L"limit") != std::wstring::npos,
        "HTTP access-limit errors give retry guidance");
    context.expect(updateAccessErrorMessage(429, 0).find(L"limit") != std::wstring::npos,
        "HTTP rate-limit errors give retry guidance");
    context.expect(updateAccessErrorMessage(404, 0).find(L"could not be accessed") != std::wstring::npos,
        "missing repository errors are described");
    context.expect(updateAccessErrorMessage(407, 0).find(L"proxy") != std::wstring::npos,
        "proxy authentication errors are described");
    context.expect(updateAccessErrorMessage(503, 0).find(L"503") != std::wstring::npos,
        "unexpected HTTP errors retain their status code");
    context.expect(updateAccessErrorMessage(0, ERROR_WINHTTP_TIMEOUT).find(L"timed out") != std::wstring::npos,
        "network timeouts are described");
    context.expect(updateAccessErrorMessage(0, ERROR_WINHTTP_NAME_NOT_RESOLVED).find(L"DNS") != std::wstring::npos,
        "DNS access errors are described");
    context.expect(updateAccessErrorMessage(0, ERROR_WINHTTP_CANNOT_CONNECT).find(L"firewall") != std::wstring::npos,
        "connection failures mention firewall and proxy access");
    context.expect(updateAccessErrorMessage(0, ERROR_WINHTTP_SECURE_FAILURE).find(L"HTTPS") != std::wstring::npos,
        "TLS access errors are described");
    context.expect(updateAccessErrorMessage(0, ERROR_INSUFFICIENT_BUFFER).find(L"safely process") != std::wstring::npos,
        "oversized release responses are rejected with a bounded-data explanation");
    context.expect(updateAccessErrorMessage(0, 0).find(L"could not be accessed") != std::wstring::npos,
        "an unclassified access failure still produces a useful message");
    context.expect(updateAccessErrorMessage(0, 12345).find(L"12345") != std::wstring::npos,
        "unknown Windows access errors preserve the diagnostic code");
}
