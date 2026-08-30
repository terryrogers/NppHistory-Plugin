# NppHistory 0.2.0 beta 24 verification report

## Executive result

**Overall status: PASS and ready for controlled prerelease publication.**

The Release x64 plugin and direct tests rebuilt with 0 errors and 0 warnings. The direct suite completed 266 behavioural checks with 0 failures, all six required Notepad++ exports were present, and an isolated portable Notepad++ instance completed the live workflow.

| Result | Value |
|---|---:|
| Direct behavioural checks | 266 passed / 0 failed |
| Plugin/test build | Passed, 0 warnings / 0 errors |
| Required DLL exports | 6 of 6 present |
| Live Notepad++ verification | Passed |
| DLL version | 0.2.0-beta.24 |
| DLL SHA-256 | `1C534ADDCEFE53D12C5BC186CDC025908A9057FCDBBF957744242DCD4ED82167` |

## Published-version status

The updater now retains the newest release eligible for the selected stable/prerelease channel even when the installed version is already current. The live test verified the exact status:

`Up to date — latest published version: 0.2.0-beta.20`

This was the real latest GitHub prerelease at test time. The status and local successful-check time were displayed in Settings and persisted without a result dialog.

Direct tests additionally prove that an installed `0.2.0-beta.22` selects a published `0.2.0-beta.24` when prerelease versions are included, while the stable-only channel ignores prereleases.

## Regression verification

The live workflow also passed autosave, before/after-save history, saved and unsaved pane states, responsive buttons, logging, all Settings pages, About metadata, and the complete side-by-side comparison workflow including the beta 23 native icon toolbar.

## Verification boundaries

- The post-publication Releases feed is verified separately after GitHub publishes beta 24.
- Destructive revision actions remain part of manual UAT.
- Deliberate DNS, proxy, firewall and TLS failures were not introduced on the workstation.

## Evidence

Run `.\tests\full_verification.ps1`. Evidence is written to `build\verification-beta24`; live screenshots are under `build\runtime-autosave-test`.
