# Commands and hotkeys verification — 3 September 2026

This report describes the previously compiled and installed build. Later [source-only refinements](SOURCE_REFINEMENTS-2026-09-03.md) are **not covered by these PASS results** and have not been compiled, installed or exercised in Notepad++.

**Result: automated verification PASS; installed 3 September 2026; installed-environment UAT pending.**

## Scope

The command order is **Capture, Compare, Restore, History, Refresh, Settings, About** on the History pane, main toolbar, Plugins menu, document context menu and settings rows. The original five exported command indices are unchanged, preserving existing Notepad++ Shortcut Mapper references; Restore and Refresh are appended to the exported array and the visual menu is reordered independently.

**Commands & Hotkeys** renders a single visible ampersand. Each command has independent pane, toolbar and editor right-click visibility plus a hotkey checkbox/input. Plugins menu visibility is permanently on for all seven commands. The right-click panel chooses an NppHistory submenu or inline commands between two separators. No selected context commands means no plugin additions. Existing user context-menu entries are not removed or rewritten.

Pane and context placements apply on OK; toolbar/hotkey registration applies after restart. Cancel discards settings edits. Context shortcut text is copied from the native Plugins menu so it reflects the active Notepad++ assignment, not an unregistered pending shortcut.

## Automated evidence

| Behaviour | Evidence | Result |
|---|---|---|
| Shared order and defaults | Direct core assertions for every command | PASS |
| Independent placement persistence | Each of seven commands enabled/disabled on three optional surfaces; INI round-trip | PASS |
| Plugins menu cannot be hidden | Each command ignores a false visibility setting | PASS |
| Seven independent hotkeys | Core INI round-trip and live shortcut labels on all commands | PASS |
| Basic shortcut validation | Missing/duplicate selection checks, live duplicate rejection/recovery; a conflicting Ctrl+Alt+Shift+F3 test was correctly rejected | PASS |
| Shared action availability | Exhaustive 64-state matrix for seven actions, plus live selected/unselected/unsaved transitions | PASS |
| Plugins menu and context icons | Seven native bitmap entries, ordered labels and identical active shortcut text | PASS |
| Context menu layout | Real editor popup inspected in submenu and inline modes; inline bounding separators verified | PASS |
| Context changes and cleanup | Individual visibility, all-off, repeated opening without duplicates | PASS |
| Command dispatch | ID obtained from actual context popup dispatched through the host, with Refresh activity verified in the log | PASS |
| Pane placement | Immediate hide, shared order, Cancel behaviour and all-buttons-hidden case | PASS |
| Toolbar registration | All seven optional commands registered and ordered | PASS |
| Settings presentation | Rendered screenshot reviewed: literal ampersand, requested row order, aligned columns, context panel | PASS |
| Tooltips and logging | 85 settings tooltip targets, seven pane button tooltips, semantic settings activity and old/new persisted values | PASS |

Core suite: **899 checks, zero failures**. The command-specific native test passed **52 checks**. Its result and the full regression summary are recorded in `build/verification-beta25-rc/commands-tests.json` and `summary.log` respectively. The full runner rebuilds the plugin, tests and updater, tests automatic/manual update checks and isolated installer replacement/rollback, performs the existing live workflow, and runs 14 tab-layout configurations.

The first clean full run reported one failure: the disabled Refresh tooltip did not activate (`DisabledRefreshTooltipActive=0`). All other workflow assertions passed. The earlier focused run and the subsequent clean full workflow passed that same hover assertion without a plugin change; the failed observation is retained in `build/commands-first-full-runtime.log`. This intermittent automated hover observation is disclosed rather than silently omitted.

Latest workflow evidence: `build/runtime-autosave-test-6830549cdeb346dfbbc2db50288e19b0`. Latest command/settings evidence: `build/commands-8e6b1e021d8442c4b32dce711f536509`.

The completed clean full run passed all 14 tab configurations, live workflow, automatic/manual update checks, updater installation/rollback protection, revision editing/deletion/restoration and associated logging checks. All three builds reported zero warnings and errors.

Candidate identity (numeric version remains `0.2.0.25`, release date intentionally blank):

- DLL SHA-256: `0E02F5958085D412E27A38CAD265DBBDEB8FCB19748CCB6CC9FA2CB3CB4795AF`
- Updater SHA-256: `5EF22D62807FF0E86B1B0E12CCFAD7FBF080B85CE7315378006C9E1F273DE711`

The test harness now selects settings tabs using their actual native rectangles rather than hard-coded pixel coordinates. Test data, generated logs and screenshots remain in ignored `build` directories, not in the installed plugin or user history.

## Boundaries and manual UAT

This is a locally built candidate, **installed but not published**. Native popup structure and command IDs were verified automatically, but synthetic mouse/key selection in a hidden popup did not reliably generate a command. This is not counted as a passing physical-click test. Mouse selection from both context layouts remains manual UAT. The existing revision-list right-click menu is unchanged.

Hotkey validation scans available Notepad++ menu shortcut labels and detects duplicates within NppHistory. It does not claim to detect every operating-system/global shortcut or another plugin's private keyboard hooks. Context menus, toolbar buttons and shortcuts use the same command IDs and file/revision guards.

After installation and restart:

1. Open **Plugins > NppHistory > Settings > Commands & Hotkeys**. Confirm the literal `&` and seven rows in the requested order.
2. Enable all **Right-click** boxes and **Group commands in an NppHistory submenu**, then click **OK**. Right-click inside a saved document. Confirm the submenu, order, icons and shortcut labels; choose **History** and then **Refresh**.
3. Clear **Group commands in an NppHistory submenu**, click **OK**, and right-click inside the document again. Confirm the selected commands appear directly between two lines. Choose **Settings** from that menu.
4. Clear **Pane** for Capture and **Right-click** for About, then click **OK**. Confirm only those placements disappear; all seven commands remain in **Plugins > NppHistory**. Repeat a change with **Cancel** and confirm it is discarded.
5. Enable all **Toolbar** boxes and assign distinct available hotkeys. Click **OK**, restart Notepad++, then verify toolbar order, hover names and the active shortcuts in both menus. Try each shortcut, using only disposable files for Capture/Restore.
6. With the History pane open and no revision selected, confirm Compare/Restore are disabled on the pane, toolbar and both menus. Select a revision and confirm they enable. Open an unsaved document and confirm Capture/Compare/Restore/Refresh disable everywhere.
7. Close the History pane, select a saved disposable file with revisions and invoke Compare. Confirm it compares the current file with its latest revision. Restore must remain disabled until a revision is selected in the open pane.
8. Repeat the right-click checks in the second editor view and with the user's usual dark-mode/DPI settings. Confirm the existing document menu and revision-list menu still work normally.

## Authorized local installation

On 3 September 2026 at 01:01:42 BST, after the user requested installation and with no Notepad++ processes running, the tested DLL was copied to `C:\iCloud\iCloudDrive\Filing\N\Notepad++\plugins\NppHistory\NppHistory.dll`. Its installed SHA-256 was verified as `0E02F5958085D412E27A38CAD265DBBDEB8FCB19748CCB6CC9FA2CB3CB4795AF` and its file version as `0.2.0.25`.

The previous DLL was backed up to `build/installed-backups/20260903-010142-commands-hotkeys/NppHistory.dll`; its backup SHA-256 was verified as `D809B48A6CF44B8D3352B425C16BBFE60EEE1C0951A00540885D7C1260A39B07`. Settings, user history and the updater were not changed. Notepad++ was left closed. Startup, physical menu clicks and installed-environment UAT remain pending; no release was published.

No OpenProject records apply to this repository.
