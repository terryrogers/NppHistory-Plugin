# NppHistory post-beta-24 update and logging verification

## Scope

This report covers the working tree after beta 24, with emphasis on update checking, logging configuration and audit events, revision comment editing, revision deletion and revision restoration. It does not claim that these post-release changes are present in the already-published beta 24 asset.

## Result

**Status: PASS after one implementation correction and two test-harness corrections.**

| Area | Automated evidence | Status |
|---|---|---|
| Direct behavioural suite | 282 checks, 0 failures | PASS |
| Plugin and test builds | Release x64, 0 warnings and 0 errors | PASS |
| Required Notepad++ exports | 6 of 6 | PASS |
| Isolated live Notepad++ workflow | Fresh portable test directory and fresh log on every run | PASS |
| Automatic update check | Real GitHub request and completion/failure audit path | PASS |
| Manual update check | In-window Checking state, no result popup, dynamic latest-version status | PASS |
| Update access errors | Rate limit, repository, proxy, HTTP, timeout, DNS, connection, TLS, oversized and unknown classifications | PASS (direct) |
| Logging levels | Error, Warning, Informational, Debug and disabled thresholds | PASS |
| Log rollover | Overwrite, archive rotation and retention limit | PASS |
| Revision comment update | Real dialog, metadata change, Informational and Debug audit entries | PASS |
| Revision deletion | Real confirmation, revision removal, list refresh and detailed Informational audit entry | PASS |
| Revision restoration | Real confirmation, safety-revision preservation, file restore/reload and Informational audit entry | PASS |

Final verified DLL SHA-256:

`7DCAFD05085B3C785BD4E8AD565EDD92AF4AB68367C377AE7900915DB3F9D18B`

## Findings and corrections

1. The deletion implementation already requested an Informational `Revision deleted` record, but the previous live suite never selected Delete or confirmed its dialog. The new live test proves the record is written with the source file, revision timestamp and comment.
2. Restore logging occurred after asking Notepad++ to reload the file. The file restored successfully, but the later audit call could be skipped during that transition. The audit record now occurs immediately after the successful disk restore and before the editor reload.
3. The update test expected beta 20 literally. It now reads the live GitHub Releases feed and validates the appropriate current/update-available status, so publishing a later release does not make the test stale.
4. The live test previously reused one log file. Every run now uses a new isolated directory, preventing old records from causing false passes.

## Verified logging behaviour

Informational logging was observed for file saves, revision creation, Capture, Compare, revision comment updates, revision deletion, Restore, settings changes, and automatic/manual update-check completion. Debug logging was observed for button/control actions and before/after option values. Warning update-check failures are accepted as the correct outcome when the public feed is inaccessible.

## Remaining manual boundaries

- Selecting a custom log file through the native file picker.
- Testing real proxy authentication, DNS failure, TLS interception and GitHub rate limiting without changing the workstation network.
- Accepting the automatic “update available” prompt and launching the browser.
- Installed-plugin UAT outside the isolated portable Notepad++ instance.

## Evidence

Run `./tests/full_verification.ps1`. The summary and build logs are written under `build/verification-post-beta24`; each live run reports its unique `build/runtime-autosave-test-*` evidence directory.
