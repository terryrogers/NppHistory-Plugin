# NppHistory 0.2.0 beta 22 verification report

## Executive result

**Overall status: PASS with destructive revision actions and external-browser launch retained for manual UAT.**

The Release x64 plugin and direct tests rebuilt with 0 errors and 0 warnings. The direct suite completed 263 behavioural checks with 0 failures, all six required Notepad++ exports were present, and an isolated portable Notepad++ instance completed the live workflow.

| Result | Value |
|---|---:|
| Direct behavioural checks | 263 passed / 0 failed |
| Plugin/test build | Passed, 0 warnings / 0 errors |
| Required DLL exports | 6 of 6 present |
| Live Notepad++ verification | Passed |
| DLL version | 0.2.0-beta.22 |
| DLL SHA-256 | `BE81D706CCB28DECD190D065BE16C70717573C4381385432308D00EC8863E70D` |

## Saved and unsaved History pane

The live run verifies that a saved document hides the warning and enables Capture, Refresh, Compare and Restore. It then creates a real untitled Notepad++ tab and verifies that all four commands are disabled and the red, centered **Save File First** message is visible. The list expands back into the warning area when the message is absent.

## Settings and logging

The live run verifies the requested General, Auto Save, History, Logging and Updates order; each page's visibility; master/dependent control states; Settings centring; and rendered screenshots. Logging is verified at the default Notepad++ plugin configuration path with Debug selected.

Direct logging tests cover:

- default and custom log paths;
- disabled logging;
- Error, Warning, Informational and Debug threshold filtering;
- Windows-local timestamps and single-line event sanitization;
- maximum-size rollover;
- overwrite and numbered-archive modes;
- archive retention;
- creation of missing folders and log files.

The live log is required to contain successful file-save, revision-creation, Capture, Compare, settings-change, update-check start/completion and Debug button-click records. The source additionally instruments Refresh, Restore, revision deletion, revision comment changes, failed capture/compare/restore/storage/settings operations and update access failures.

## Update status

Updates now has its own page. The live workflow verifies the status label, performs a real background check against GitHub, confirms the result dialog, and confirms persistence of both the successful timestamp and `Up to date` status. Access failures remain visible on manual checks, quiet during automatic checks, and are recorded at Warning level when logging is enabled.

## Verification boundaries

- Revision deletion, comment editing and Restore confirmation remain manual UAT because they require destructive/modal choices; their storage operations are directly tested.
- The Open Log button is wired to create/access the effective log and request that Notepad++ open it; its external visible tab transition remains manual UAT.
- Deliberately breaking workstation DNS, proxy, firewall or TLS configuration was not performed. Deterministic update error mappings remain directly tested.
- Browser launch from an available-update prompt remains manual UAT.

## Evidence

Run `.\tests\full_verification.ps1`. Evidence is written to `build\verification-beta22`; live screenshots and the exercised log are under `build\runtime-autosave-test`.
