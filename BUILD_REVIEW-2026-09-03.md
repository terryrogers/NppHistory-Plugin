# Latest changes: local review build

Built on 3 September 2026 from source commit `44710925d8f8c0860fbac9adac5cc9baff22ac0c`. No source changes were required to compile. This report supersedes the earlier **not compiled / core tests not run** status for the command refinements, live command settings, temporary status messages and contextual action logging. It does not supersede their outstanding native UAT requirements.

## Results

| Check | Result |
|---|---|
| Release x64 plugin rebuild | PASS; 0 warnings, 0 errors |
| Release x64 updater rebuild | PASS; 0 warnings, 0 errors |
| Release x64 core-test runner rebuild | PASS; 0 warnings, 0 errors |
| Core test execution | PASS; **1,123 checks, 0 failures** |
| Contextual feedback source audit | PASS; 21 event kinds, 21 wording fixtures, one paired status/log producer |
| Settings tooltip source audit | PASS; 79 inputs/hints, none missing or attached to non-input controls |
| Installed plugin | Unchanged; new build not installed |
| Real Notepad++ smoke tests and visual UAT | Not run for this build; user review pending |
| Restart/install updater workflow | Rebuilt only; not executed for this build |
| GitHub release | Not published |

The core runner includes the temporary-status Win32-control tests, contextual feedback/log pairing, severity filtering, Unicode and timestamp formatting fixtures, disabled/inaccessible logging, live-hotkey state tests, and existing settings/history/diff/update/logger tests. This is not a claim of full native host or visual compatibility.

Build logs are local under `build/verification-action-feedback/`: `plugin-build.log`, `tests-build.log`, and `updater-build.log`. The source-audit scripts always report their own `Compiled=False`/`RuntimeTested=False` boundary; the independent build and test results above establish what was actually executed this turn.

Core runner output:

```text
NppHistory core verification: 1123 checks, 0 failures.
All NppHistory core tests passed.
```

## Review artifacts

- `build/x64/Release/NppHistory.dll`: version **0.2.0.25**, 879,104 bytes; SHA-256 `B58F1AC880C8A79AE8AA7C3A97110BC1D731162268679FFBBEEA3C456EB77182`.
- `build/x64/Release/NppHistoryUpdater.exe`: 248,832 bytes; SHA-256 `7E4EB1F8F567234384DE4EF630037DCB5FE23C80A3F9446089C9220261D95AF6`.
- Installed DLL at `C:\iCloud\iCloudDrive\Filing\N\Notepad++\plugins\NppHistory\NppHistory.dll` remains SHA-256 `0E02F5958085D412E27A38CAD265DBBDEB8FCB19748CCB6CC9FA2CB3CB4795AF`.

Version and empty release date were deliberately retained: this is a review build, not a new published release. The hash distinguishes it from the older installed build with the same version number. Installation requires separate user approval and Notepad++ closure; no running application or user configuration was changed.
