# NppHistory 0.2.0 beta 25 release-candidate verification

## Scope and release boundary

This report covers the beta 25 candidate at numeric plugin version `0.2.0.25` and semantic prerelease version `0.2.0-beta.25`. It validates the candidate source and locally built x64 binaries. It does not claim that beta 25 has been published, manually accepted, installed over beta 24, or validated through a real beta-25-to-beta-26 automatic update.

The embedded release date is intentionally empty during candidate testing. It will be set to the actual publication date only after manual UAT approval.

## Automated result

**Status: PASS — manual UAT remains required.**

| Area | Evidence | Status |
|---|---|---|
| Core behavioural suite | 304 checks, 0 failures | PASS |
| Plugin build | Release x64 | PASS |
| Restart-installer build | Release x64 | PASS |
| Core-test build | Release x64 | PASS |
| Required Notepad++ exports | 6 of 6 | PASS |
| DLL metadata | Product/file version `0.2.0.25`, expected name and description | PASS |
| Live Notepad++ workflow | Fresh isolated portable instance under a path containing spaces | PASS |
| Automatic update scheduling | Real GitHub check, completion logging and next-check countdown | PASS |
| Manual update check | In-window checking state and correct candidate-newer-than-feed result | PASS |
| Revision capture/comment/delete/restore | Live UI and audit-log evidence | PASS |
| Comparison interface | Live rendering, navigation, shared scrolling, tooltips and pane behavior | PASS |
| Settings and About | Five tabs, dependent controls, numeric version and blank candidate release date | PASS |
| Restart-installer success | Verified replacement and previous-DLL backup | PASS |
| Restart-installer rejection | Invalid SHA-256 rejected and installed DLL preserved | PASS |

## Verified hashes

- Candidate DLL SHA-256: `2E18337B6990CD40DE570939DACF3AF60F88FC5F1FB06925E314015853FE90F4`
- Candidate updater SHA-256: `3A6D71FC1ACECE783F4F3EC54B6259511FF4B4D92DB9D354C0CD92227ADF662D`

These hashes identify this local candidate build only. The publication workflow will rebuild the release assets independently and publish its own SHA-256 manifest.

## Corrections found during candidate preparation

Moving the repository to `C:\Users\terry\Downloads\Projects\Notepad++ History Plugin` exposed two test-harness quoting defects:

1. The updater smoke test did not quote staged DLL, target, result and restart paths passed through `Start-Process`.
2. The live Notepad++ test did not quote the note path passed through `Start-Process`.

Both harnesses now exercise paths containing spaces successfully. The production restart-installer launch path already used its tested Windows command-line quoting function.

## CI and publication gates

- Normal pushes and pull requests now build the plugin, updater and core tests, run the 304-check suite, run updater replacement/rollback smoke tests and upload both binaries.
- Tagged releases repeat the core and updater tests.
- Publication rejects mismatched semantic tags, numeric plugin versions, missing/invalid publication dates and missing version-specific release notes.
- The manual ZIP is structured as `NppHistory\NppHistory.dll`.
- Automatic-update assets include the exact versioned x64 DLL, `NppHistoryUpdater.exe` and a SHA-256 manifest.

## Remaining manual UAT

The following remain release gates rather than automated claims:

- Installed beta 25 behavior in Terry Rogers' normal Notepad++ environment.
- Auto Save triggers across multiple real open files, focus changes, tab changes and shutdown.
- File rename, move, external deletion and missing-open-file reconciliation.
- Adjacent and custom history locations, including inaccessible paths.
- Restore safety with unsaved editor changes.
- Logging rollover and archive behavior through the visible Settings workflow.
- Display at the user's normal DPI and docking arrangements.
- Manual beta-24-to-beta-25 installation.
- Real automatic beta-25-to-beta-26 download, UAC/replacement, restart and result reporting.

## Reproduction

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\full_verification.ps1
```

Build logs and summaries are written to `build\verification-beta25-rc`. Each live run reports its unique `build\runtime-autosave-test-*` evidence directory.
