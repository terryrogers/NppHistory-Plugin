# NppHistory beta

NppHistory is an open-source Notepad++ plugin that automatically saves ordinary files and keeps a local, browsable revision history.

Latest local review build (3 September 2026): the failed periodic toolbar-height repair has been removed. Disabled NppHistory toolbar commands are now removed and restored live instead of being left as hidden trailing buttons, preventing Customize Toolbar from collapsing the toolbar to four pixels. The repeatable NppHistory + Customize Toolbar + NppMenuSearch resize regression passes, together with 1,243 core/native checks and 109 focused compatibility checks. This build is not installed; the currently installed recovery build failed UAT. See [toolbar-height regression report](TOOLBAR_HEIGHT_REGRESSION-2026-09-03.md). The prior [menu-icon correction](MENU_ICON_REGRESSION-2026-09-03.md) was accepted in visual UAT.

Follow-up toolbar/pane repaint correction: rebuilt plugin, 1,123 core checks and 95 focused native layout checks pass; isolated renders inspected. Installed and hash-verified on 3 September 2026 with the previous DLL backed up. User-environment visual confirmation remains pending. See [regression report and latest DLL hash](TOOLBAR_PANE_REGRESSION-2026-09-03.md).

## Beta features

- Pending native review: temporary action messages use the leftmost Notepad++ status field for about five seconds, then restore its normal text. A newer action replaces the previous message; a native update to that field takes precedence. Other status fields and their widths remain unchanged. Feedback works independently of plugin logging. See [status-message verification boundary](SOURCE_STATUS_MESSAGES-2026-09-03.md).
- These messages now include the filename and, where applicable, the revision's localized timestamp. Each event has matching INFO/WARNING/CRITICAL log wording plus full-path diagnostic context. Logging remains subject to its enabled switch and threshold; Critical is included at every enabled level. See [complete message and severity list](SOURCE_ACTION_FEEDBACK-2026-09-03.md).
- Captures the existing on-disk file immediately before every Notepad++ save.
- Captures the successfully saved result as the new history baseline.
- Suppresses consecutive revisions with identical content.
- Default autosave mode: save 30 seconds after the last text change.
- Optional autosave triggers when Notepad++ loses focus, at a configurable minute interval, when the active file tab changes, and when Notepad++ exits.
- Lets autosave target either the current file or all open files.
- Enforces a minimum of 10 seconds for the after-edit trigger.
- Provides a dockable history panel with revision times formatted from the computer's Windows regional settings, editable comment, human-readable dynamic file size, a dock-tab icon and configurable, responsive icon buttons with clear hover feedback.
- Opens an icon-labelled Revision Actions menu by right-clicking a history item, with Delete, Edit, Compare and Restore commands.
- Stores history in a hidden `.npphistory` folder beside the text file by default.
- Supports a configurable common history root instead.
- Maintains an internal catalogue of file paths, history paths, stable IDs, and last-saved hashes.
- Moves history automatically when a tracked file is renamed or reliably recognized at a new location.
- Warns when a recorded history folder is missing or a moved-file match is ambiguous.
- Shows current source and the selected revision in a source-informed WinMerge-style comparison viewer.
- Uses native Scintilla text panes with line-number margins and syntax-colored comments, keywords, strings, and numbers.
- Includes a two-column Location Pane with proportional difference blocks and visible-area outlines.
- Shows a concise filename header above and line/column, encoding, and line-ending information below each pane, with WinMerge's equal/different text-file status icon beside the current filename.
- Uses the standard Windows title bar, a unique NppHistory icon, current filename and revision identity, and native window controls; double-clicking it or either file header maximizes or restores the window.
- Provides tooltip-style hover explanations for every toolbar command applicable to read-only history comparison.
- Aligns corresponding lines with blank placeholders and displays original line numbers in fixed gutters.
- Gives each pane a WinMerge-positioned vertical scrollbar while keeping both panes synchronized.
- Uses WinMerge's gold comparison, grey placeholder, and selected-red line palette.
- Highlights the exact changed characters within paired lines using a stronger inline indicator.
- Shows the full-document difference map in a double-buffered two-column WinMerge-style Location Pane.
- Lets you click the Location Pane or use the WinMerge-style toolbar to navigate difference groups, including returning to the currently selected difference after scrolling away.
- Can ignore whitespace, blank-line-only differences, letter case, and line-ending differences.
- Opens the revision selector beneath the Open toolbar button or beneath the right-hand file header.
- Supports revision selection and navigation from a streamlined WinMerge-style toolbar without inapplicable merge or editing commands.
- Restores a revision after first retaining the current saved file as a safety revision.
- Supports forced manual revision capture, including when the file matches the latest revision.
- Uses one order throughout: Capture, Compare, Restore, History, Refresh, Settings, About. All seven commands always appear in **Plugins > NppHistory**, with their active keyboard shortcuts.
- Independently shows each command on the History pane, main toolbar and/or document editor's right-click menu. Right-click commands can be grouped in an NppHistory submenu or shown inline between separators, with icons and active shortcuts.
- Keeps file/revision actions enabled or disabled consistently across surfaces. Restore requires a selected revision in the open History pane.
- Compares the current file with its newest saved revision when Compare is invoked while the History pane is closed.
- Centres comparison, settings and About windows on Notepad++; the comparison window also fits itself to smaller editor windows so the main tab area remains visible.
- Includes a native About window showing the plugin icon, linked author, version, architecture, release date and licence. When populated, the embedded ISO release date is displayed using the computer's Windows regional date format.
- Checks for updates while Notepad++ remains open and can download a verified x64 release, restart Notepad++, install it with rollback protection and reopen the editor.

## Install the x64 beta

1. Close Notepad++.
2. Create a folder named `NppHistory` inside the Notepad++ `plugins` folder.
3. Copy `NppHistory.dll` into that folder, producing `plugins\NppHistory\NppHistory.dll`. The restart installer is downloaded and verified only when it is needed.
4. Start 64-bit Notepad++ and open **Plugins > NppHistory**.

For a standard machine-wide installation, the destination is normally:

```text
C:\Program Files\Notepad++\plugins\NppHistory\NppHistory.dll
```

For a portable installation, use the `plugins` folder beside `notepad++.exe`.

## Configuration

Open **Plugins > NppHistory > Settings**. Settings are grouped into five tabs. Every actionable Settings control has a contextual tooltip explaining its purpose, units, scope and dependencies, including when that control is disabled:

- **Commands & Hotkeys** provides a row for each of the seven commands. Select **History Pane**, **Tab bar menu**, **Toolbar**, and **Document menu** independently. All seven commands always appear in **Plugins > NppHistory**, without a settings column. **History** is unchecked and disabled for **History Pane**, because its purpose is to open that pane. Enable a hotkey and press its complete combination in the adjacent field. Missing keys, duplicate plugin shortcuts and conflicts with visible Notepad++ menu shortcuts are rejected. This is basic validation, not an exhaustive check of Windows/global or other applications' shortcuts. All placements and hotkey changes apply on **OK**, without restarting Notepad++. Shortcuts operate only while the main Notepad++ window is active, not inside modal dialogs or menus. **Cancel** discards edits.
- In **Document and tab bar right-click menus**, select **Group commands in an NppHistory submenu** for a submenu; clear it for inline commands between separator lines. The two menus have independent command selections but share this grouping choice. With no commands checked for a menu, nothing is added there. Defaults show the six eligible pane commands, no toolbar/context commands, and no enabled plugin hotkeys; existing toolbar/hotkey preferences are retained when upgrading. The new tab-menu selections default off.
- Tooltips are restricted to buttons (including checkboxes/radio buttons) and input/text controls. Labels, group headings, status text, icons and the settings tab strip have none. See [source-only refinement status](SOURCE_REFINEMENTS-2026-09-03.md): these latest changes have not yet been compiled or installed.
- **Auto Save** independently enables automatic saving, with **After editing stops** selected by default at 30 seconds. Optional triggers cover Notepad++ losing focus, timed intervals in minutes, file-tab changes and Notepad++ exit. Autosave can apply to the current file only or all open files. After-edit values below 10 seconds are normalized to 10 seconds. Its own multiline exclusion list prevents plugin-initiated saving of matching files. If the separate Notepad++ `AutoSave.dll` plugin is installed, NppHistory disables its Auto Save engine and displays the reason in red, avoiding two automatic-saving systems acting on the same documents; the saved NppHistory settings are retained for use if that plugin is later removed.
- **History** independently enables revision history and selects whether revisions are created before saves, after saves and before restores. It also selects either the default hidden `.npphistory` folder beside each text file or a common custom history root. Its separate multiline exclusion list prevents new revisions for matching files.
- **Logging** optionally records Error, Warning, Informational or Debug events. The log can use the standard Notepad++ plugin configuration folder or a custom file, can be opened directly in Notepad++, and supports a maximum size, overwrite or archive rollover, and configurable archive retention.
- **Updates** checks the public NppHistory GitHub Releases feed daily, weekly or monthly, optionally includes prereleases, supports an immediate manual check and displays the latest check status and next scheduled time. **Check now** stays on the Updates page, shows `Checking...`, refreshes the status text and, when a newer release is found, offers the available update actions. Checks continue while Notepad++ remains open and after Windows resumes from sleep. For a compatible release, **Download, restart and install** downloads the exact versioned x64 DLL, verifies its GitHub SHA-256 digest, starts the external updater, closes Notepad++, backs up and replaces the DLL, and reopens Notepad++. Permission, network, verification, replacement and restart failures retain or report the recoverable state.

The settings and internal catalogue are stored through Notepad++'s plugin configuration API. A normal installed copy typically places them beneath:

```text
%APPDATA%\Notepad++\plugins\Config\NppHistory\
```

The catalogue is named `catalog.db`. The default log is `NppHistory.log`. The catalogue maps each stable file identity to its current file and history paths. History contents stay in the adjacent hidden folder or selected custom root.

An unsaved Notepad++ tab has no stable file path, so its History pane shows **Save File First** and disables Capture, Refresh, Compare and Restore. Saving the tab removes the message, expands the revision list into the released space and enables Capture and Refresh. Compare and Restore become available only after selecting a revision.

Exclusion patterns use Windows-style wildcards and are evaluated one per line, without regard to letter case. `*` matches zero or more characters and `?` matches one character. A pattern such as `*.log` matches filenames in any folder; `myfile*.com` matches both `myfile.com` and `myfiles.com`. A pattern containing a slash, backslash or drive colon is matched against the complete normalized path. Blank rows are ignored. Auto Save exclusions do not prevent a manual Notepad++ save, and History exclusions do not remove or block access to existing revisions. Excluded document tabs show a labelled orange **AS** badge for automatic saving and/or a labelled blue **H** badge for revision history; reserved tab space keeps these badges clear of the filename. A badge is visible only when its feature is enabled, and the History pane displays **File Excluded in Settings** in wrapping red text. A History exclusion disables Capture, Refresh, Compare and Restore.

When a tracked file moves:

1. A rename performed by Notepad++ supplies the exact old and new paths, so the history bucket moves immediately.
2. A file moved externally and later reopened can be recognized from its last-saved SHA-256 content hash, provided exactly one missing catalogue record matches.
3. If an open file disappears, NppHistory retains the old association and warns you. When Notepad++ later supplies a new path through Save As, rename, or reopening, relocation is attempted automatically.
4. If the old history is missing or matching would be ambiguous, NppHistory alerts you instead of guessing.

Changing the configured history location migrates the active file immediately. Other tracked files migrate the next time they are opened. When a common folder is selected, opening or activating a file also discovers matching older history in its adjacent `.npphistory` folder. Those revisions are safely merged into the common location without overwriting existing history; comments and timestamps are preserved. The migrated bucket and an empty `.npphistory` folder are removed only after the move succeeds. INFO logging records the file, source, destination and counts; failures are logged as WARNING and leave the source available for retry.

## Revision semantics

On a save, NppHistory performs this sequence:

1. `Before save`: copy the current file from disk into history.
2. Notepad++ writes the edited document.
3. `Saved`: copy the confirmed new file into history.

This makes the pre-edit disk version available for rollback and establishes the new saved content as the next baseline. Hash-based duplicate suppression prevents the same bytes from being stored twice in succession.

## Beta limitations

- This build targets 64-bit Notepad++ on Windows.
- A new untitled tab must be saved once manually so that it has a file path; the History pane makes this state explicit and disables file-dependent commands.
- History retention is unlimited in this beta; automatic pruning is not implemented yet.
- History is local and is not encrypted by NppHistory.
- Binary files can be versioned and restored, but the comparison viewer is intended for text.
- Cloud synchronization is outside this plugin. With adjacent storage, a cloud-synced note folder will normally also contain its hidden `.npphistory` folder, subject to the synchronization provider's hidden-file behavior.

## Verification

The source package includes direct core tests, live Notepad++ integration tests, restart-installer replacement/rollback tests, a full-verification runner and a function-by-function evidence matrix under `tests`. The current beta 25 candidate report is `TEST_REPORT-0.2.0-beta.25.md`.

The beta 25 candidate DLL, Windows properties, Plugins Admin version and About dialog use the numeric build version `0.2.0.25`. GitHub and the prerelease update channel use the semantic prerelease identifier `0.2.0-beta.25`. The About release date remains empty until the candidate is approved and published.

## Build and test

Open a Visual Studio developer environment and run:

```powershell
msbuild .\vs.proj\NppHistory.vcxproj /m /p:Configuration=Release /p:Platform=x64
msbuild .\vs.proj\NppHistoryUpdater.vcxproj /m /p:Configuration=Release /p:Platform=x64
msbuild .\vs.proj\NppHistory.Tests.vcxproj /m /p:Configuration=Release /p:Platform=x64
.\build\tests\NppHistory.Tests.exe
.\tests\runtime_smoke.ps1
```

The runtime smoke test uses an isolated portable copy of an installed 64-bit Notepad++ and verifies a real 10-second autosave, adjacent hidden storage, the internal catalogue, save revisions, saved/unsaved pane states, forced pane capture, pane and main-toolbar controls, a real background GitHub update check, updater controls, logging output, all five Settings tabs, the About window, and the WinMerge-style comparison viewer including synchronized scrolling, native line numbers, Location Pane rendering, toolbar navigation, file headers and pane status information. The plugin and restart installer are written to `build\x64\Release\NppHistory.dll` and `build\x64\Release\NppHistoryUpdater.exe`.

The comparison workflow was implemented after reviewing WinMerge's open-source location view, merge view, file header, status bar, syntax-color and diff-color implementations. NppHistory now draws its own comparison toolbar icons; the remaining comparison-status artwork is documented in `THIRD_PARTY_NOTICES.md`. NppHistory's comparison and history engines remain independently implemented.

## License

NppHistory is licensed under GPL-3.0. The bundled Notepad++ and Scintilla interface headers retain their upstream notices.
