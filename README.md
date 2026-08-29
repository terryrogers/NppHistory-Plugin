# NppHistory beta

NppHistory is an open-source Notepad++ plugin that automatically saves ordinary files and keeps a local, browsable revision history.

## Beta features

- Captures the existing on-disk file immediately before every Notepad++ save.
- Captures the successfully saved result as the new history baseline.
- Suppresses consecutive revisions with identical content.
- Default autosave mode: save 30 seconds after the last text change.
- Optional autosave triggers when Notepad++ loses focus, at a configurable minute interval, when the active file tab changes, and when Notepad++ exits.
- Lets autosave target either the current file or all open files.
- Enforces a minimum of 10 seconds for the after-edit trigger.
- Provides a dockable history panel with revision times formatted from the computer's Windows regional settings, editable comment, byte size, a dock-tab icon and responsive, icon-labelled Capture, Refresh, Compare, Restore, Settings and About buttons.
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
- Provides the four-command Plugins > NppHistory menu: Show History, Capture Now, Settings and About.
- Can add icon buttons for Capture, Compare and Restore to the main Notepad++ toolbar.
- Centres comparison, settings and About windows on Notepad++; the comparison window also fits itself to smaller editor windows so the main tab area remains visible.
- Includes a native About window showing the plugin icon, linked author, version, architecture, release date and licence. When populated, the embedded ISO release date is displayed using the computer's Windows regional date format.

## Install the x64 beta

1. Close Notepad++.
2. Create a folder named `NppHistory` inside the Notepad++ `plugins` folder.
3. Copy `NppHistory.dll` into that folder, producing `plugins\NppHistory\NppHistory.dll`.
4. Start 64-bit Notepad++ and open **Plugins > NppHistory**.

For a standard machine-wide installation, the destination is normally:

```text
C:\Program Files\Notepad++\plugins\NppHistory\NppHistory.dll
```

For a portable installation, use the `plugins` folder beside `notepad++.exe`.

## Configuration

Open **Plugins > NppHistory > Settings**. Settings are grouped into three tabs:

- **General** controls optional Capture, Compare and Restore buttons on the main Notepad++ toolbar. Toolbar changes take effect after restarting Notepad++. The Update panel stores an enabled option and daily, weekly or monthly frequency for a future update checker; this beta does not contact an update service.
- **Auto Save** independently enables automatic saving, with **After editing stops** selected by default at 30 seconds. Optional triggers cover Notepad++ losing focus, timed intervals in minutes, file-tab changes and Notepad++ exit. Autosave can apply to the current file only or all open files. After-edit values below 10 seconds are normalized to 10 seconds.
- **History** independently enables revision history and selects whether revisions are created before saves, after saves and before restores. It also selects either the default hidden `.npphistory` folder beside each text file or a common custom history root.

The settings and internal catalogue are stored through Notepad++'s plugin configuration API. A normal installed copy typically places them beneath:

```text
%APPDATA%\Notepad++\plugins\Config\NppHistory\
```

The catalogue is named `catalog.db`. It maps each stable file identity to its current file and history paths. History contents stay in the adjacent hidden folder or selected custom root.

When a tracked file moves:

1. A rename performed by Notepad++ supplies the exact old and new paths, so the history bucket moves immediately.
2. A file moved externally and later reopened can be recognized from its last-saved SHA-256 content hash, provided exactly one missing catalogue record matches.
3. If an open file disappears, NppHistory retains the old association and warns you. When Notepad++ later supplies a new path through Save As, rename, or reopening, relocation is attempted automatically.
4. If the old history is missing or matching would be ambiguous, NppHistory alerts you instead of guessing.

Changing the configured history location migrates the active file immediately. Other tracked files migrate the next time they are opened.

## Revision semantics

On a save, NppHistory performs this sequence:

1. `Before save`: copy the current file from disk into history.
2. Notepad++ writes the edited document.
3. `Saved`: copy the confirmed new file into history.

This makes the pre-edit disk version available for rollback and establishes the new saved content as the next baseline. Hash-based duplicate suppression prevents the same bytes from being stored twice in succession.

## Beta limitations

- This build targets 64-bit Notepad++ on Windows.
- A new untitled tab must be saved once manually so that it has a file path.
- History retention is unlimited in this beta; automatic pruning is not implemented yet.
- History is local and is not encrypted by NppHistory.
- Binary files can be versioned and restored, but the comparison viewer is intended for text.
- Cloud synchronization is outside this plugin. With adjacent storage, a cloud-synced note folder will normally also contain its hidden `.npphistory` folder, subject to the synchronization provider's hidden-file behavior.

## Verification

The source package includes direct core tests, live Notepad++ integration tests, a full-verification runner and a function-by-function evidence matrix under `tests`. The current beta verification report is `TEST_REPORT-0.2.0-beta.20.md`.

## Build and test

Open a Visual Studio developer environment and run:

```powershell
msbuild .\vs.proj\NppHistory.vcxproj /m /p:Configuration=Release /p:Platform=x64
msbuild .\vs.proj\NppHistory.Tests.vcxproj /m /p:Configuration=Release /p:Platform=x64
.\build\tests\NppHistory.Tests.exe
.\tests\runtime_smoke.ps1
```

The runtime smoke test uses an isolated portable copy of an installed 64-bit Notepad++ and verifies a real 10-second autosave, adjacent hidden storage, the internal catalogue, save revisions, forced pane capture, pane and main-toolbar controls, the tabbed settings and About windows, and the WinMerge-style comparison viewer including synchronized scrolling, native line numbers, Location Pane rendering, toolbar navigation, file headers and pane status information. The plugin binary is written to `build\x64\Release\NppHistory.dll`.

The comparison workflow was implemented after reviewing WinMerge's open-source location view, merge view, file header, status bar, syntax-color and diff-color implementations. The toolbar bitmap is derived from WinMerge's GPL-2.0-or-later artwork; see `THIRD_PARTY_NOTICES.md`. NppHistory's comparison and history engines remain independently implemented.

## License

NppHistory is licensed under GPL-3.0. The bundled Notepad++ and Scintilla interface headers retain their upstream notices.
