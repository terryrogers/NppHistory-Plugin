# NppHistory 0.2.0 beta 23 verification report

## Executive result

**Overall status: PASS.**

The Release x64 plugin and direct tests rebuilt with 0 errors and 0 warnings. The direct suite completed 263 behavioural checks with 0 failures, all six required Notepad++ exports were present, and an isolated portable Notepad++ instance completed the live workflow.

| Result | Value |
|---|---:|
| Direct behavioural checks | 263 passed / 0 failed |
| Plugin/test build | Passed, 0 warnings / 0 errors |
| Required DLL exports | 6 of 6 present |
| Live Notepad++ verification | Passed |
| DLL version | 0.2.0-beta.23 |
| DLL SHA-256 | `04AB8894D4E4759934CC0C167BBDCEE8BE945499C60A894682E8DC3E33DD4B9D` |

## Non-modal Check now

The live test opens Settings, selects Updates and clicks **Check now**. It verifies that:

- the Settings window remains open;
- the status changes immediately to `Checking...`;
- the button is disabled while the request is running;
- the status refreshes with the result and local successful-check time;
- the button becomes enabled again;
- no `NppHistory Update Check` result dialog is created; and
- the successful timestamp and status persist after Settings closes.

The live request completed with `Status: Up to date`. Deterministic direct tests continue to cover update access and response error mappings.

## Comparison toolbar icons

The old WinMerge bitmap strip has been removed. NppHistory now creates twelve native 24 x 24 toolbar glyphs for revision selection, difference navigation, revision navigation, comparison options and refresh. The live test verifies the image count, dimensions, all 15 toolbar entries including separators, command navigation, and tooltip behavior. The rendered comparison screenshot was inspected and shows the blue, orange, green and grey NppHistory palette without the previous oversized WinMerge artwork.

## Verification boundaries

- Automatic update-available prompts are retained and were not triggered because the installed development version is current.
- Deliberately breaking workstation DNS, proxy, firewall or TLS configuration was not performed.
- The existing destructive revision actions remain part of manual UAT.

## Evidence

Run `.\tests\full_verification.ps1`. Evidence is written to `build\verification-beta23`; live screenshots are under `build\runtime-autosave-test`.
