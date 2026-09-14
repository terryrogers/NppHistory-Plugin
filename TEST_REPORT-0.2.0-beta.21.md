# NppHistory 0.2.0 beta 21 verification report

## Executive result

**Overall status: PASS with the update-available prompt and browser launch retained for manual UAT.**

The Release x64 plugin and tests were rebuilt with 0 errors and 0 warnings. The direct suite completed 245 behavioural checks with 0 failures, all six required Notepad++ plugin exports were present, and a fresh portable Notepad++ process completed the live workflow. The live updater reached the public GitHub Releases service, reported that beta 21 was newer than the currently published beta 20 release, and persisted the successful-check timestamp.

| Result | Value |
|---|---:|
| Core behavioural checks | 245 passed / 0 failed |
| Clean plugin build | Passed, 0 warnings / 0 errors |
| Clean test build | Passed, 0 warnings / 0 errors |
| Required DLL exports | 6 of 6 present |
| Live Notepad++ verification | Passed |
| Live GitHub access | Passed |
| DLL file/product version | 0.2.0-beta.21 |
| DLL SHA-256 | `E55B5C396EB9ED9E0DD88AC3CBBF9118CB78F15DC949BAD395B49A8E452ADFB5` |

## Implemented update behaviour

- Update checks run in a worker thread so the Notepad++ interface remains responsive.
- Automatic checks support Daily, Weekly and Monthly frequency settings.
- A manual **Check now...** command is available in Settings > General.
- Stable-only and prerelease-inclusive channels are supported. Prereleases are enabled by default while NppHistory itself is in beta.
- GitHub release tags are compared as semantic versions, including prerelease ordering and arbitrarily large numeric prerelease identifiers.
- Drafts, malformed releases and release links outside the official `terryrogers/NppHistory-Plugin` repository are rejected.
- Automatic notifications for the same available version are shown once; manual checks may show the result again.
- A successful check records `LastUpdateCheck`. Failed access attempts do not record success, allowing a later automatic retry.
- The checker does not download, replace or execute files. If an update is accepted, Windows opens the trusted official GitHub release page.

## Unable-to-access behaviour

Manual checks show an actionable warning and explicitly state that no files were downloaded or changed. Automatic access failures are quiet so offline startup does not interrupt the user; because the check is not marked successful, it is eligible to retry later.

Direct tests verify specific handling for:

| Failure | User-facing handling | Status |
|---|---|---|
| HTTP 403 or 429 | GitHub refusal/access-limit guidance and retry later | PASS |
| HTTP 404 | Repository or Releases service could not be accessed | PASS |
| HTTP 407 | Proxy authentication required | PASS |
| Other HTTP status | Exact status code retained with retry guidance | PASS |
| Request timeout | Internet connection and retry guidance | PASS |
| DNS resolution failure | DNS and internet-access guidance | PASS |
| Connection failure | Firewall, proxy and internet-access guidance | PASS |
| HTTPS/TLS failure | System clock, TLS inspection and certificate guidance | PASS |
| Oversized response | Response rejected as unsafe to process | PASS |
| Other Windows networking error | Diagnostic Windows error number retained | PASS |
| Malformed or unusable JSON | Response rejected without offering an update | PASS |
| Browser cannot open release URL | Separate warning instructs the user to visit the repository manually | Implemented; manual UAT |

## Direct update-check coverage

The direct suite covers valid and invalid semantic versions; stable and prerelease precedence; numeric overflow; build metadata; GitHub response parsing; nested fields; drafts; incomplete records; trailing response data; trusted URL restrictions; channel selection; newest-version selection; exact scheduling thresholds; system clock rollback; duplicate notification suppression; manual notification policy; and all access-error mappings listed above.

## Live Notepad++ coverage

The isolated live test verifies the Settings controls and their enabled/disabled states, performs **Check now...** against GitHub, recognizes the up-to-date result, confirms that a successful timestamp is persisted, and then completes the existing Auto Save, History, comparison, toolbar, docking and About-window regression workflow.

## Verification boundaries

- The published repository currently contains beta 20 while this build identifies as beta 21, so the live test exercises the “newest selected-channel version” result. The update-available prompt cannot be exercised live until a newer release exists or a test endpoint is introduced.
- Opening the release page is intentionally retained for manual UAT because it launches the user's external browser.
- Individual network failures are deterministic direct tests. Deliberately breaking the workstation's DNS, proxy, firewall or TLS configuration was not appropriate for the live test.
- Automatic startup timing and quiet failure/retry policy are directly tested at the scheduling/policy layer; a multi-day live test was not performed.

## Repeatability and evidence

Run:

```powershell
.\tests\full_verification.ps1
```

Evidence is written under `build/verification-beta21`, including clean build logs, direct-test output, export inspection, live-runtime results and the final DLL hash. Live screenshots are under `build/runtime-autosave-test`.
