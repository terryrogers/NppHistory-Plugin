# NppHistory 0.2.0 beta 25 release-candidate verification

## Scope and release boundary

This report covers the beta 25 candidate at numeric plugin version `0.2.0.25` and semantic prerelease version `0.2.0-beta.25`. It validates the candidate source and locally built x64 binaries. It does not claim that beta 25 has been published, manually accepted, installed over beta 24, or validated through a real beta-25-to-beta-26 automatic update.

The embedded release date is intentionally empty during candidate testing. It will be set to the actual publication date only after manual UAT approval.

## Automated result

**Status: PASS — full local verification rerun 2 September 2026; installed-environment manual UAT remains required.**

| Area | Evidence | Status |
|---|---|---|
| Core behavioural suite | 370 checks, 0 failures | PASS |
| Plugin build | Release x64 | PASS |
| Restart-installer build | Release x64 | PASS |
| Core-test build | Release x64 | PASS |
| Required Notepad++ exports | 6 of 6 | PASS |
| DLL metadata | Product/file version `0.2.0.25`, expected name and description | PASS |
| Live Notepad++ workflow | Fresh isolated portable instance under a path containing spaces | PASS |
| Automatic update scheduling | Completion or access-error logging and next-check countdown | PASS |
| Manual update check | In-window checking state with success and access-error handling | PASS |
| History pane selection state | Compare and Restore disabled until a revision is selected | PASS |
| Shared Compare command state | Pane, Plugins menu and main toolbar disable without a selection, re-enable with a selection, and closed-pane Compare uses the latest revision | PASS — automated and manual UAT |
| Plugin command icons | Five live menu bitmap handles are verified and missing assignments are automatically reapplied during command-state refresh | PASS — automated; corrected build awaits repeat manual UAT |
| Revision size display | Dynamic B, KB, MB and GB formatting with direct boundary checks | PASS |
| History pane button hover | All six buttons register and clear pointer hover state | PASS |
| Independent exclusions | Multiline wildcard persistence, matching, Auto Save suppression, manual-save allowance and History suppression | PASS |
| Exclusion indicators | Supplied icons, feature gating, wrapping pane status, positive reserved geometry; native-control identity/rename/reorder/orientation tests and eight real horizontal/vertical/small/large/light/dark combinations verify separate filename/icon/pin/close space | PASS for tested configurations |
| Excluded-file actions and help | Capture, Refresh, Compare and Restore disabled; all six pane tooltips registered and disabled Refresh tooltip visibly activated | PASS |
| Settings contextual help | 51 targets registered; disabled interval input selected and tooltip popup visibly activated | PASS |
| External AutoSave conflict | Direct absent/conventional/recursive discovery, all-trigger suppression, hidden non-conflict notice and installed-plugin UAT | PASS |
| Revision capture/comment/delete/restore | Live unsaved-edit restore creates a forced `Before restore` safety revision containing the unsaved text; UI and audit-log evidence | PASS — automated; corrected build awaits repeat manual UAT |
| Log severity labels | Emitted informational records use `[INFO]`; ERROR, WARNING and DEBUG remain unchanged | PASS |
| Settings debug records | Friendly tab/control names are emitted for genuine user actions; raw numeric IDs and focus/init noise are rejected | PASS |
| Settings value records | Enum values use readable choices, booleans use Enabled/Disabled, empty values use `(not set)`, and the INFO summary includes the changed-option count | PASS |
| Button-click records | Capture, Settings and About each produce one Debug click record per invocation | PASS |
| Popup positioning | Edit Comment, Delete, Restore, Compare, Settings and About are live-verified centrally against the main Notepad++ window; all plugin message boxes use the same explicit centring helper | PASS |
| Comparison interface | Live rendering, navigation, shared scrolling, tooltips and pane behavior | PASS |
| Settings and About | Five tabs, dependent controls, numeric version and blank candidate release date | PASS |
| Restart-installer success | Verified replacement and previous-DLL backup | PASS |
| Restart-installer rejection | Invalid SHA-256 rejected and installed DLL preserved | PASS |

## Verified hashes

- Candidate DLL SHA-256: `D809B48A6CF44B8D3352B425C16BBFE60EEE1C0951A00540885D7C1260A39B07`
- Candidate updater SHA-256: `DDE133C95FCC1C4F7B6B644CBB1AB259CDA151A96C7915BAC958B15E7D7B273E`

These hashes identify this local candidate build only. The publication workflow will rebuild the release assets independently and publish its own SHA-256 manifest.

## Corrections found during candidate preparation

Moving the repository to `C:\Users\terry\Downloads\Projects\Notepad++ History Plugin` exposed two test-harness quoting defects:

1. The updater smoke test did not quote staged DLL, target, result and restart paths passed through `Start-Process`.
2. The live Notepad++ test did not quote the note path passed through `Start-Process`.

Both harnesses now exercise paths containing spaces successfully. The production restart-installer launch path already used its tested Windows command-line quoting function.

The first installed UAT pass also exposed that Compare and Restore were enabled for a saved file before a revision was selected. The pane now enables Capture and Refresh for saved files independently, enables Compare and Restore only when a revision is selected, and updates those actions on list selection changes. The same UAT correction renamed Bytes to Size and added dynamic human-readable units. Live regression checks cover both the unselected and selected states.

The access-error live path exposed two over-strict test assumptions: a failed first check has no last-successful timestamp, and a failed check logs a warning rather than a completion record. The live criteria now require the correctly spaced failure status and warning record when the GitHub feed is inaccessible.

Manual restore-safety UAT exposed that saving modified editor content during Restore triggered the ordinary after-save revision first. Duplicate suppression then prevented the intended `Before restore` revision, leaving only `Saved`. Restore-initiated saves are now distinguished from ordinary saves, their normal after-save capture is suppressed, and one forced safety revision is created with the exact `Before restore` comment. The live test now modifies the editor without saving and verifies both that comment and the unsaved marker inside the stored revision.

Manual menu UAT also exposed that Notepad++ could later discard the initially assigned Plugins-menu bitmaps. Command-state refresh now checks all five live menu entries and reapplies only missing bitmap assignments. The live test inspects the actual five `hbmpItem` handles rather than relying solely on an internal readiness marker.

Manual log review exposed that Settings debug records included raw Windows control IDs and initialization/focus traffic. Settings logging now records only genuine clicks, tab changes and dropdown selections, and maps each event to a stable friendly name. The live test verifies representative tab, checkbox and OK records and rejects any numeric Settings-control record.

A second log review exposed internal enum numbers and duplicate pane-button records. Setting-change output now uses readable choices and option counts, while each pane action has one Debug click route.

Further installed UAT showed that paint-only indicators still overlapped filenames. The new implementation reserves native length separately for each affected tab using an internal display-only suffix. Native callers still receive the canonical filename and unchanged buffer pointer; shared tab padding/minimum width and tab order are not changed. A dedicated native-control suite adds 30 checks across both orientations, including rename, reorder, repeated refresh, orientation round trips, removal and detach. The isolated real-Notepad++ harness compares feature-off and feature-on geometry, verifies the actual native orientation and reserved slots, cycles document activation, and creates/closes a new tab. Small/large and light/dark combinations are tested in both orientations. At the tested small-tab configuration, one icon adds 36 px, two add 54 px, and ordinary tabs add 0 px along the text axis. These are observed values, not fixed layout constants.

Vertical tabs now reserve space along the bottom-to-top text axis. Icons stay upright, stacked after the filename and before pin/close. Fixed-width styles still safely omit indicators rather than paint over text or native controls; the pane notice remains. Multiple-monitor DPI transitions and the user's installed environment remain UAT gates.

A popup-position audit found that assigning the main Notepad++ owner was insufficient to centre standard Windows message boxes. NppHistory now explicitly positions every plugin message box when it activates. The live workflow measures Edit Comment, Delete, Restore, Compare, Settings and About centres against the main Notepad++ window.

## CI and publication gates

- Normal pushes and pull requests now build the plugin, updater and core tests, run the 370-check suite, run updater replacement/rollback smoke tests and upload both binaries.
- Tagged releases repeat the core and updater tests.
- Publication rejects mismatched semantic tags, numeric plugin versions, missing/invalid publication dates and missing version-specific release notes.
- The manual ZIP is structured as `NppHistory\NppHistory.dll`.
- Automatic-update assets include the exact versioned x64 DLL, `NppHistoryUpdater.exe` and a SHA-256 manifest.

## Remaining manual UAT

Completed in Terry Rogers' installed Notepad++ environment on 30 August 2026:

- With the History pane open and no revision selected, pane Compare/Restore, the Plugins menu Compare command and the main-toolbar Compare command were disabled.
- Selecting a revision enabled all corresponding Compare controls and pane Restore.
- Closing the History pane left Compare enabled and successfully compared the current file with its latest revision.
- An earlier verified beta 25 candidate was installed with a recoverable backup; the newer candidate identified by the hashes above still requires installed-environment UAT.

The following remain release gates rather than automated claims:

- Installed beta 25 behavior in Terry Rogers' normal Notepad++ environment.
- Auto Save triggers across multiple real open files, focus changes, tab changes and shutdown.
- File rename, move, external deletion and missing-open-file reconciliation.
- Adjacent and custom history locations, including inaccessible paths.
- Restore safety with unsaved editor changes.
- Logging rollover and archive behavior through the visible Settings workflow.
- Display at the user's normal DPI and docking arrangements.
- Real automatic beta-25-to-beta-26 download, UAC/replacement, restart and result reporting.

## Reproduction

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\full_verification.ps1
```

Build logs and summaries are written to `build\verification-beta25-rc`. The 2 September live workflow evidence is in `build\runtime-autosave-test-111c63edb52a42f590a48eb8b07ebbd1`. The eight tab-layout runs, measured lengths and screenshot directories are recorded in `build\verification-beta25-rc\tab-layout-tests.json`. All three builds completed with zero warnings and errors; 370 core checks, the live workflow, updater replacement/rollback protection and all eight tab configurations passed. All eight excluded-tab screenshots were visually reviewed. No installed DLL was replaced by this verification.
