# Commands and tooltips — source-only refinements

Status: source changes prepared; **not compiled, not installed, no native/runtime tests run**. The installed 0.2.0.25 DLL and existing build output are unchanged. Earlier automated PASS reports apply only to the previous build.

## Changes

- Removed the Plugins settings column. Plugins > NppHistory still always contains all seven commands.
- Added independent per-command tab-bar right-click menu selections, defaulting off, stored as `TabContextCapture`, etc. Existing document-menu settings are preserved.
- Renamed Pane to History Pane. History is always unchecked/disabled there, including when old settings contain `PaneHistory=1`.
- Both document and tab-bar menus use the existing submenu-versus-separators preference, shared command ordering, icons, native shortcut labels and action availability checks.
- Tab context integration lets Notepad++ select the clicked tab and activate the correct split view. It does not alter tab geometry, captions or selection itself. Plugin entries are removed after tracking finishes because the host also reuses its tab menu for Document List.
- Removed six inappropriate tooltip targets: tab strip, hotkey note/status, Auto Save conflict notice, effective log-path label and update status. All 79 Settings inputs/buttons have explanations. Registration and hit-testing reject labels and group boxes even if accidentally added later.
- Added hints for Edit Comment input/Save/Cancel, About OK and read-only comparison text fields. Reviewed comparison navigation descriptions and clarified that Before save captures the existing file on disk, not unsaved editor text. Existing pane-button hints remain; passive labels, headers, artwork and status text have no hints. Off-screen compatibility controls are not user-facing and have no new tooltip registrations.

## Why toolbar/hotkey changes currently require restart

**Superseded by the subsequent [live-command source changes](SOURCE_LIVE_COMMANDS-2026-09-03.md).** The following describes the previously installed implementation, not the current source.

This is the plugin's current registration lifecycle, not a general inability to change Windows controls live.

- Toolbar commands are registered in response to Notepad++'s startup `NPPN_TBMODIFICATION` notification via `registerConfiguredToolbarButtons`. Saving Settings does not currently reconcile additions/removals with the live host toolbar.
- Shortcut structures are populated during `setInfo` and handed to Notepad++ through the plugin command array. Settings saves next-start preferences; it does not rebuild the host's active accelerator table. Notepad++ exposes shortcut query/removal messages in the bundled API, but no equivalent general setter used by this implementation. Existing shortcuts can also be managed through Notepad++'s own Shortcut Mapper.
- History Pane and right-click placements are read dynamically, so these apply on OK without a restart.

Reference inspected for native tab selection: [Notepad++ TabBar.cpp, WM_RBUTTONDOWN](https://github.com/notepad-plus-plus/notepad-plus-plus/blob/master/PowerEditor/src/WinControls/TabBar/TabBar.cpp). Popup creation was checked against the local upstream `NppNotification.cpp` reference. No tab-sizing code was changed.

## Checks performed without compilation

- Read-only Settings source audit: **PASS**, 79 input/button controls, 79 tooltip targets, no missing inputs and no non-input targets.
- PowerShell syntax-only parsing: **PASS** for both updated native smoke scripts and the source audit; `git diff --check` passed.
- Built and installed DLL hashes remain `0E02F5958085D412E27A38CAD265DBBDEB8FCB19748CCB6CC9FA2CB3CB4795AF`. No executable or DLL was rebuilt or replaced.
- Reviewed settings persistence, semantic change logging, native menu ownership and surface-specific filtering.
- Updated core and native smoke-test expectations; these tests remain **unexecuted** for this batch.

## Required after the remaining alterations and an authorised build

1. Compile DLL and tests, then run core, runtime, command-menu and tab-layout suites. Recheck six visible pane buttons and 79 Settings tooltip targets.
2. Verify the compact, two-line column headings at normal and high DPI. History Pane / History must be cleared and disabled; all seven Plugins menu entries remain available.
3. Enable different document-menu and tab-menu commands; save/reopen Settings. Check icons, active shortcuts and common ordering in each, in submenu and inline modes. Cancel must discard changes.
4. Right-click inactive saved, unsaved and excluded tabs in both split views and both tab orientations. Confirm the target document is correct and Capture/Compare/Restore/Refresh enablement matches the pane. Actually invoke each applicable command; popup inspection alone is insufficient.
5. Open/close submenus repeatedly, move between sibling submenus, then open Document List and editor context menus. No duplicates, emptied submenus or leaked tab-only commands should appear. Clear all tab-menu choices and verify native menu contents remain intact.
6. Hover every visible Settings input/button, including disabled fields. Hover labels, status text, headings and tab strip: no tooltip. Check comment-editor, About and comparison controls too.
7. Check Debug records identify tab-bar placement changes by command and readable previous/new values. Recheck existing menu/hotkey behaviour after restart.

No release/version change, deployment or UAT pass is claimed.
