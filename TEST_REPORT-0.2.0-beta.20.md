# NppHistory 0.2.0 beta 20 verification report

## Executive result

**Overall status: PASS with explicitly documented manual-UAT boundaries.**

The Release x64 plugin and test executable were rebuilt from source with **0 errors and 0 warnings**. The direct core suite completed **180 behavioural checks with 0 failures**. A fresh isolated portable Notepad++ instance loaded the rebuilt DLL and completed the automated live workflow successfully. All six required Notepad++ plugin exports were present.

This report does not call every test a unit test. Pure logic and storage code is tested directly. Native dialog, Scintilla, docking, notification and toolbar behaviour is tested in a real Notepad++ process. A few destructive/modal user paths are tested at their underlying logic layer but remain manual-UAT items because automating their confirmation clicks would be brittle and could conceal UI defects.

| Result | Value |
|---|---:|
| Core behavioural checks | 180 passed / 0 failed |
| Clean plugin build | Passed, 0 warnings / 0 errors |
| Clean test build | Passed, 0 warnings / 0 errors |
| Required DLL exports | 6 of 6 present |
| Live Notepad++ verification | Passed |
| DLL file/product version | 0.2.0-beta.20 |
| DLL SHA-256 | `64CAE9B42B47C5E23AD0818CBF1BEBE52CD3AFCBBEAC7740685F1D0797F621CF` |

## Test levels used

- **Direct unit/core:** invokes a function with controlled input and checks its return value or filesystem result.
- **Indirect unit/core:** exercises a private helper through its public operation and verifies the externally observable result.
- **Isolated Win32:** creates temporary native windows and checks sizing/position behaviour without Notepad++.
- **Live integration:** loads the real DLL in an isolated portable Notepad++ process and inspects or operates the actual UI and files.
- **ABI inspection:** confirms the DLL exposes the required Notepad++ entry points.

The detailed function inventory and evidence level are in `tests/FUNCTION_TEST_MATRIX.md`.

## Defects found and corrected during this round

1. **Unicode custom history roots were corrupted in an ANSI INI file.** A path containing `Ω` was reloaded as `O`. Settings now creates or migrates the INI to UTF-16 before using the Windows profile API. The suite verifies both a new Unicode path and migration of an existing ANSI INI.
2. **Revision mutations accepted stale entries.** Restore, Edit Comment and Delete could proceed against a `RevisionInfo` whose data or metadata file had disappeared. These operations now reject missing revision/metadata files, and direct failure-path checks pass.
3. A blocked-migration fixture initially represented a movable directory rather than a real conflict. The fixture was corrected to place an actual blocking file at the destination; catalogue reconciliation now has meaningful success, missing, ambiguous and blocked cases.

## Configuration-option status

### General

| Option | Persistence | Behaviour/UI evidence | Status |
|---|---|---|---|
| Main toolbar: Capture | Direct round-trip | Registered in live Notepad++; icon and command present | PASS |
| Main toolbar: Compare | Direct round-trip | Registered in live Notepad++; opens comparison workflow | PASS |
| Main toolbar: Restore | Direct round-trip | Registered in live Notepad++; command present | PASS; confirmation/overwrite remains manual UAT |
| Enable automatic update checks | Direct round-trip | Checkbox and panel verified live | PASS as stored placeholder |
| Frequency: Daily | Direct round-trip | Choice accepted | PASS as stored placeholder |
| Frequency: Weekly | Default and direct round-trip | Displayed live | PASS as stored placeholder |
| Frequency: Monthly | Direct round-trip | Choice accepted | PASS as stored placeholder |

The update checker itself is intentionally not implemented, so no network/update behaviour is claimed.

### Auto Save

| Option | Direct policy/persistence evidence | Live evidence | Status |
|---|---|---|---|
| Enable automatic saving | Enabled and disabled; master override of every trigger | Disables/re-enables dependent controls | PASS |
| After editing stops | Enabled/disabled; exact threshold and pre-threshold checks; backward tick safety | Real edit was saved after the configured 10 seconds | PASS |
| Seconds | Round-trip; values below 10 normalize to 10 | Field and dependent state verified | PASS |
| Notepad++ loses focus | Enabled/disabled policy and persistence | Checkbox verified | PASS at policy/wiring level; actual focus-loss event remains manual UAT |
| Timed intervals | Enabled/disabled; exact threshold and pre-threshold checks; backward tick safety | Minutes field enables only with trigger | PASS at policy/wiring level; a full timed live save remains manual UAT |
| Interval minutes | Round-trip; values below 1 normalize to 1 | Field enablement verified | PASS |
| File tab changes | Enabled/disabled policy and persistence | Checkbox verified | PASS at policy/wiring level; actual tab-switch save remains manual UAT |
| Notepad++ exits | Enabled/disabled policy and persistence | Checkbox verified | PASS at policy/wiring level; shutdown-save remains manual UAT |
| Current file only | Direct round-trip | Radio option verified | PASS |
| All open files | Default/persistence | Radio option verified; live run used one open test document | PASS at selection level; multi-file live save remains manual UAT |

### History

| Option | Direct policy/storage evidence | Live evidence | Status |
|---|---|---|---|
| Enable revision history | Enabled/disabled; master override of all revision paths | Disables/re-enables dependent controls | PASS |
| Before a file is saved | Enabled/disabled policy | A real `Before save` revision was created | PASS |
| After a file is saved | Enabled/disabled policy | A real `Saved` revision was created | PASS |
| Before restoring a revision | Enabled/disabled policy; storage restore safety | Checkbox verified | PASS at policy/storage level; modal restore remains manual UAT |
| Manual Capture | Remains available if automatic revision triggers are clear; blocked by History master | Pane Capture created a forced `Manual capture` revision | PASS |
| Hidden `.npphistory` beside each file | Path derivation, hidden-root operation, capture and catalogue tests | Real adjacent hidden history folder created | PASS |
| Common folder | Unicode persistence; desired path and migration tests | Controls enable correctly | PASS in core/UI; installed-plugin custom-root migration remains manual UAT |

## Core behaviour by subsystem

### Utilities — PASS

Covered successful, empty and replacement atomic writes; missing reads; standard SHA-256 vectors for empty input and `abc`; wide-string hashing; UTF-8 conversion and invalid input; path normalization; UTF-8 BOM, UTF-16 and ANSI decoding; UTC revision stamps; Windows-local timestamp and release-date formatting; invalid dates; and native window centring/fitting.

### Settings and policy — PASS

Covered every documented default, every persisted field in a non-default configuration, all three update frequencies, Unicode paths, ANSI-to-Unicode migration, all five Auto Save triggers, all four revision paths, both master switches, exact timing boundaries, tick reversal, numeric clamping, and migration of legacy `Enabled`, `Mode` and `PeriodicSeconds` keys.

### Diff engine — PASS

Covered empty and identical input; additions, removals and modifications; source/revision line numbers; inline changed spans; ignore whitespace, blank lines, case and line endings; repeated blank-line alignment; side-by-side and unified output; and both large-document fallback thresholds. The live viewer additionally verified difference navigation, current-difference selection, synchronized scrolling, line numbers, status colours and Location Pane interaction.

### History store — PASS

Covered unconfigured/missing input; root and bucket selection; standard, duplicate, forced and changed captures; metadata parsing and ordering; revision reading/restoration; invalid/missing revision rejection; comment newline sanitization; orphan and malformed metadata; empty content; deletion, latest-hash recalculation and final-hash removal; and operation without a catalogue.

### History catalogue — PASS

Covered configuration, load/save, stable IDs, adjacent/custom desired paths, unknown-record capture, persistence/reload, malformed database tolerance, explicit moves, content-hash matching, location migration, missing old history, ambiguous matches, blocked moves and legacy-record migration.

## Live Notepad++ behaviour

The isolated live run passed the following:

- plugin DLL discovery and Plugins > NppHistory menu contents;
- real 10-second after-edit autosave;
- before-save and after-save revision creation;
- hidden adjacent history and catalogue creation;
- all six History-pane buttons, icons, consistent widths and responsive wrapping;
- forced manual Capture and revision-list refresh;
- right-click Revision Actions menu and command icons;
- three optional main-toolbar commands and dock icon;
- comparison-window centring, caption, native frame, file/revision headers and status icon;
- comparison toolbar, hover hints, header double-click maximize/restore, Location Pane collapse, synchronized scroll, difference navigation, Current Difference, revision picker and comparison palette;
- General, Auto Save and History tabs, labels, option visibility, dependent-control state, form icon and centring;
- About content, icon and centring.

## Important verification boundaries

These items are not reported as failed; they are the remaining areas where an automated test would have to drive a confirmation dialog, external website, shutdown, focus transition or long-lived multi-file workflow:

- confirm Restore through its overwrite prompt and inspect the reopened editor;
- confirm Delete through its prompt and inspect the row removal;
- submit Edit Comment through its modal dialog (the underlying metadata update is directly tested);
- click the Terry Rogers hyperlink and verify the external browser target;
- fire real focus-loss, tab-change, exit and multi-file autosave events;
- run a live custom-root migration from an already populated installed plugin;
- perform extended visual UAT at non-default DPI, themes and multiple monitor layouts.

## Repeatability and evidence

Run:

```powershell
.\tests\full_verification.ps1
```

The runner rebuilds both targets, executes the core suite, inspects the DLL exports, runs the isolated live Notepad++ test, and records its evidence under `build/verification-beta20`:

- `plugin-build.log`
- `tests-build.log`
- `core-tests.log`
- `dll-exports.log`
- `runtime-tests.log`
- `summary.log`

Screenshots from the live run are under `build/runtime-autosave-test` for the History pane, comparison window, all three settings tabs and About window.

## Release assessment

Beta 20 is suitable for continued beta/UAT use based on clean compilation, 180 passing core checks and the passing live Notepad++ workflow. It should not be described as exhaustively proven on every Windows display configuration or every modal/destructive interaction until the manual-UAT items above have been completed.
