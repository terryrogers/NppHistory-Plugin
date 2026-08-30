# Changelog

## Unreleased

- Keep Compare and Restore disabled until a revision is selected, while retaining Capture and Refresh for saved files.
- Renamed the revision-list Bytes column to Size and added human-readable dynamic B, KB, MB, GB, TB, PB and EB units.
- Added a pale-blue hover background and blue outline to enabled History pane buttons.
- Added separate multiline wildcard exclusion lists for automatic saving and revision history, with filename and full-path matching.
- Added distinct labelled **AS** (Auto Save) and **H** (History) document-tab badges in reserved tab space plus a wrapping red **File Excluded in Settings** pane message; each badge is shown only while its feature is enabled.
- Excluded History files now disable Capture, Refresh, Compare and Restore, and all six History pane buttons have explanatory tooltips that also work while a button is disabled.
- Detects an installed `AutoSave.dll`, disables NppHistory's overlapping Auto Save engine, displays an unchecked disabled master control with a red explanation, preserves the configured preference for later restoration and records a warning in the plugin log.

## 0.2.0-beta.25 (release candidate)

- Made revision action commands share one testable command route and added live dialog coverage for comment editing, deletion and restoration.
- Added detailed Informational audit records for revision deletion and restoration, with the restore record written before Notepad++ reloads the editor.
- Expanded update-result and access-error tests, logging-level and rollover tests, and automatic/manual live update verification.
- Made each live verification run use a fresh isolated directory and replaced the stale beta-20 update expectation with the current public Releases feed.
- Added automatic update scheduling while Notepad++ remains open, resume-from-sleep handling, retry backoff, and a live next-check countdown without checking merely because Settings opens or gains focus.
- Added a verified external restart installer with SHA-256 validation, protected replacement, previous-DLL backup, failure reporting, optional UAC elevation and automatic Notepad++ relaunch.
- Added install choices to automatic and manual update-available prompts while keeping up-to-date and access-error checks on the Updates page.
- Added the release workflow assets required for future automatic updates: a versioned raw x64 DLL, updater executable, manual ZIP and SHA-256 manifest.
- Removed the redundant standalone install button, spaced the three status lines and standardized user-facing beta versions as the numeric plugin form such as `0.2.0.25`.
- Added restart-installer replacement and rollback smoke testing to normal CI and release publication gates.
- Strengthened release validation so the tag, numeric plugin version, publication date and release-notes file must agree.

## 0.2.0-beta.24

- Clarified successful update checks by displaying the latest eligible published GitHub release, for example `Up to date — latest published version: 0.2.0-beta.20`.
- Kept the update-available wording concise while removing the leading `v` from displayed GitHub tags.
- Added direct coverage proving beta 22 selects a published beta 24 when prerelease versions are enabled.
- Corrected the Windows version resource so Plugins Admin and About consistently display `0.2.0.24` while the GitHub prerelease tag remains `0.2.0-beta.24`.
- Added the `NppHistory` plugin name, descriptive metadata and the beta 24 release date to the DLL resources used by Plugins Admin and About.

## 0.2.0-beta.23

- Changed **Check now** to remain inside Settings, show `Checking...`, and refresh the Updates status text without displaying a result dialog.
- Preserved automatic update-available notifications while keeping manual checks non-modal for success, failure and available-update results.
- Replaced the WinMerge bitmap-strip toolbar with twelve crisp NppHistory-native comparison icons using the plugin's blue, orange, green and grey palette.
- Removed the unused third-party toolbar bitmap while retaining the documented WinMerge comparison-status artwork.

## 0.2.0-beta.22

- Disabled Capture, Refresh, Compare and Restore for unsaved tabs and added a centered red Save File First message that collapses out of the pane after the first save.
- Reorganized Settings into General, Auto Save, History, Logging and Updates tabs, moving update configuration and its persistent status onto the Updates page.
- Added optional Error, Warning, Informational and Debug logging to the default Notepad++ plugin configuration folder or a custom log file.
- Added configurable maximum log size, overwrite/archive rollover, archive retention and an Open Log command that opens the effective log in Notepad++.
- Added Informational records for file saves, revision creation/deletion/comment updates, Capture, Refresh, Compare, Restore, settings changes and automatic/manual update checks.
- Added Warning records for update-check access failures, Error records for failed storage/UI operations and Debug records for plugin form actions and option changes with previous/new values.
- Added direct logging/rotation/settings tests and live saved/unsaved pane, five-tab Settings, logging-event and update-status verification.

## 0.2.0-beta.21

- Implemented non-blocking update checks against the public `terryrogers/NppHistory-Plugin` GitHub Releases feed.
- Added daily, weekly and monthly automatic scheduling, an Include prerelease versions channel option and a manual Check now command.
- Added semantic-version ordering, including stable/prerelease precedence and numeric prerelease identifiers.
- Suppressed repeat automatic notifications for the same release while preserving explicit manual checks.
- Restricted browser navigation to HTTPS release URLs under the official repository and retained notification-only behaviour: NppHistory never downloads or replaces its DLL.
- Added specific handling for offline/DNS, connection, proxy authentication, TLS, timeout, GitHub rate-limit, HTTP, oversized and malformed-response failures. Automatic failures remain silent and retry on a later launch; manual failures explain that no files were downloaded or changed.
- Added persistent last-check and last-notified state using the existing Unicode settings file.
- Expanded direct core coverage from 180 to 245 checks and added a live background GitHub update check to the isolated Notepad++ verification.

## 0.2.0-beta.20

- Embedded the beta's 2026-08-30 publication date for locale-aware display in the About window and DLL metadata.
- Replaced the original compact core smoke test with separate utility, settings/policy, diff, history-store and history-catalog suites totaling 180 behavioural checks.
- Added direct configuration-policy coverage for every Auto Save and History trigger, including exact timer boundaries and master-switch overrides.
- Added invalid, Unicode, empty, duplicate, large-document, migration, ambiguity and failure-path coverage.
- Fixed Unicode custom history paths being corrupted when Windows created or retained an ANSI INI file; Settings now migrates the INI to UTF-16 before saving.
- Prevented restore, comment editing and deletion from accepting missing revision files.
- Expanded the live Notepad++ test to verify General options and Auto Save/History dependent-control enablement.
- Added a repeatable full-verification runner, function coverage matrix and detailed verification report.

## 0.2.0-beta.19

- Standardized the General, Auto Save and History pages to the same outer and inner spacing grid.
- Balanced the vertical gap above the OK and Cancel buttons with the gap below them.
- Aligned the bottom edges of each page's content panels and preserved identical OK and Cancel button dimensions.
- Increased the Auto Save what panel's lower breathing room and matched it on the General and History pages.
- Added rendered live Notepad++ verification of all three settings pages.

## 0.2.0-beta.18

- Changed the History-pane Revision Actions menu from left-click to the conventional right-click action.
- Added distinct Delete and Edit artwork and icon-labelled all four Revision Actions commands.
- Separated automatic saving from revision history with independent enable switches.
- Added History controls for creating revisions before saves, after saves and before restores.
- Tightened the History location panel and clarified its adjacent and common-folder choices.
- Added persistence and live Notepad++ coverage for the independent Auto Save and History settings and the right-click menu.

## 0.2.0-beta.17

- Changed revision timestamps to use the current Windows user's short-date and short-time regional formats, including the computer's 12/24-hour preference.
- Added locale-aware display of the embedded ISO release date in the About window while preserving an empty release date for unreleased builds.
- Added core coverage for locale-aware release-date formatting.

## 0.2.0-beta.16

- Restored the complete bottom border around the Auto Save what group.
- Renamed the History-pane Reason column to Comment and widened it while tightening the Time column.
- Changed revision timestamps to compact `dd/MM/yyyy h:mtt` display formatting.
- Added a left-click Revision Actions menu to every history item with Delete, Edit, Compare and Restore commands.
- Added a compact Edit Revision Comment dialog that updates revision metadata without changing its captured content.
- Added confirmed revision deletion and recalculation of duplicate-suppression state from the newest remaining revision.
- Added core coverage for timestamp formatting, comment editing and deletion, plus live Notepad++ coverage for the Revision Actions popup.

## 0.2.0-beta.15

- Removed the grey rectangular backdrops behind labels, checkboxes and radio buttons on all Settings pages.
- Reduced the Settings window and tightened the General, Auto Save and History layouts while retaining the same controls.
- Changed the responsive History-pane layout to use one uniform button width across every visible row.
- Added live runtime verification that all six History-pane buttons have identical widths.

## 0.2.0-beta.14

- Rebuilt Settings around General, Auto Save and History tabs.
- Added autosave triggers for editing inactivity, Notepad++ losing focus, timed minute intervals, file-tab changes and Notepad++ exit, with current-file or all-open-files scope.
- Added General > Toolbar options for optional Capture, Compare and Restore buttons on the main Notepad++ toolbar.
- Added an Update settings placeholder with an enable option and daily, weekly or monthly frequency; update checking is intentionally not implemented yet.
- Added distinct icons to all six responsive History-pane buttons and to the three optional main-toolbar commands.
- Fixed the History-pane Capture button and made an explicit capture create a Manual capture revision even when its contents match the latest revision.
- Centred the About heading, changed the author to a clickable Terry Rogers website link, and added a Release Date field.
- Added a separately maintained release-date value to the DLL version resource; it remains blank until this beta receives a release date.
- Extended native and runtime coverage for settings persistence, tabs, autosave controls, toolbar registration, pane icons, the About link and forced manual captures.

## 0.2.0-beta.13

- Centred the comparison, settings and About windows on the main Notepad++ window.
- Made the comparison window shrink to the available Notepad++ size before centring so the main file-tab area remains visible on smaller windows.
- Added the NppHistory icon to the dock registration and to the comparison, settings and About window captions.
- Expanded the docked history pane to Capture, Refresh, Compare, Restore, Settings and About buttons.
- Made pane buttons measure their labels, share available row width and wrap across additional rows as the pane narrows.
- Changed Plugins > NppHistory to exactly Show History, Capture Now, Settings and About.
- Added a native JSTool-inspired About dialog with the NppHistory icon, author, version, architecture, licence and summary.
- Extended runtime validation with rendered panel and About screenshots plus checks for responsive rows, menu names, icons and window centring.

## 0.2.0-beta.12

- Replaced the unreliable common-controls toolbar hints with an independent hover popup for every visible command button.
- Changed toolbar hit detection to use the buttons' displayed rectangles, fixing missing hints on the comparison toolbar.
- Made the source-header status icon accurately distinguish equal files from different files using WinMerge's corresponding status artwork.
- Corrected the runtime hover regression so it exercises the toolbar without passing invalid cross-process pointer data.

## 0.2.0-beta.11

- Fixed toolbar hover hints by assigning text directly to every native tooltip tool and retaining notification-based text as a fallback.
- Added a runtime assertion that reads back the native tooltip-control text rather than only counting registered hover regions.
- Double-buffered the Location Pane so scroll and difference-navigation updates are composed off-screen and displayed in one operation.
- Stopped requesting background erasure for routine Location Pane updates, removing the visible erase-then-redraw flash.

## 0.2.0-beta.10

- Eliminated stale copied pixels during Location Pane collapse and resizing by using no-copy child positioning followed by an opaque full-window redraw.
- Made the pane and overall status fields repaint opaque backgrounds so shorter status text cannot leave repeated characters behind.
- Removed the three-line menu indicator from the left filename header and anchored a separately drawn indicator at the far right of the revision header.
- Added WinMerge's different-text-file status icon immediately to the left of the current filename.
- Added a unique multi-resolution NppHistory document/history icon to the native window caption.
- Changed the window caption to include the current filename, revision timestamp and revision reason.
- Changed Open so its revision picker is positioned immediately beneath the Open toolbar button; the right revision header continues to open the same picker beneath that header.
- Extended the rendered runtime regression to capture the comparison after Location Pane collapse and verify the caption metadata and icon.

## 0.2.0-beta.9

- Restored the Current Difference toolbar command between First and Last Difference.
- Made Current Difference return both panes to the already-selected difference after manual scrolling, without changing the selection.
- Added a runtime regression check confirming that the command re-centres the panes while the Location Pane remains stable and toolbar hints continue working.

## 0.2.0-beta.8

- Replaced the custom title strip and window buttons with the standard Windows caption, minimize, maximize/restore and close controls.
- Removed the File, Edit, View, Merge and other decorative menu labels, plus the duplicate file-comparison tab.
- Reduced the toolbar to the commands applicable to read-only history comparison: revision selection, difference navigation, revision navigation, options and refresh.
- Removed the ambiguous Current Difference command that could trigger Location Pane flicker and interrupt toolbar hints.
- Changed both pane headers to show concise source filenames; the revision header also shows its capture time and reason without exposing an internal history path.
- Made Location Pane closure a single redraw-safe layout operation so the headers, editors and status fields resize together without stale or overlapping UI.
- Extended the runtime test to verify the native window frame, concise headers, reduced toolbar hints and Location Pane collapse geometry.

## 0.2.0-beta.7

- Removed the synthetic `Current ↔ History` tab; the title strip now contains only the active file comparison tab.
- Made double-clicking the comparison tab strip or either file header maximize or restore the window.
- Added tooltip hints for every toolbar command, including explanations for disabled read-only merge actions.
- Wired Open to revision selection and Select/Next/Previous/First/Current/Last Difference to navigation.
- Wired First/Previous/Next/Last File to revision navigation.
- Wired Options to the whitespace, blank-line, case and line-ending comparison filters.
- Disabled the unsupported New command instead of leaving an enabled no-op button.
- Extended the runtime test to verify the single tab, maximize/restore behavior, tooltip registration and revision-toolbar navigation.

## 0.2.0-beta.6

- Replaced the previous NppHistory-specific comparison layout with the WinMerge frame as the sole visual specification.
- Added the WinMerge-style tab/title band, menu row, original toolbar artwork and command ordering.
- Matched the Location Pane, paired rounded file headers, two editor panes, independent synchronized scrollbars, pane status fields and overall difference counter.
- Removed the visible revision dropdown, comparison-option strip, centre scrollbar, marker lanes, legend and Close button.
- Made the right file header open the revision picker while keeping the header visually consistent with WinMerge.
- Kept history comparison read-only and disabled toolbar commands that would write or merge content.
- Corrected plain-text files being syntax-coloured as C/C++ after apostrophes.
- Added stronger WinMerge-style inline word-difference fills.

## 0.2.0-beta.5

- Replaced the RichEdit comparison controls with native Scintilla panes.
- Added a WinMerge-style two-column Location Pane with proportional difference blocks, visible-area outlines, and click-to-navigate behavior.
- Added blue-grey file headers above both panes and file information status bars below them.
- Added native line-number margins, horizontal scrolling, synchronized caret/scroll updates, and syntax-colored comments, keywords, strings, and numbers.
- Adopted WinMerge's gold, grey, and selected-red line palette plus stronger inline word-difference indicators.
- Retained the shared centre scrollbar and red, green, and blue difference markers requested for NppHistory.
- Updated the runtime test to verify native Scintilla line-number margins.

## 0.2.0-beta.4

- Rebuilt the comparison window as a native WinMerge-inspired, read-only viewer.
- Added original line-number gutters and blank placeholders for vertically aligned rows.
- Added character-level highlighting within paired changed lines.
- Added Previous and Next navigation with a difference-group counter.
- Added red, green, and blue visible-row dots and full-document location ticks.
- Made the marker lanes clickable for direct navigation.
- Added options to ignore whitespace, blank-line-only changes, case, and line endings.
- Retained the shared centre scrollbar for synchronized vertical navigation.

## 0.2.0-beta.3

- Fixed inserted lines beside repeated blank lines causing identical later lines to be shown as changed.
- Enlarged the red change dots and aligned them directly with visible highlighted rows.
- Made the marker lanes refresh whenever the synchronized comparison view scrolls.

## 0.2.0-beta.2

- Replaced the two comparison-pane vertical scrollbars with one central scrollbar.
- Made the centre scrollbar and mouse wheel keep both comparison panes on the same line.
- Added proportional red change markers immediately to the left and right of the centre scrollbar.
- Left-side markers identify current-side removals and changes; right-side markers identify revision-side additions and changes.

## 0.2.0-beta.1

- Changed the default storage to a hidden `.npphistory` folder beside each text file.
- Added a configurable common history root.
- Added the persistent `catalog.db` file/location catalogue.
- Added automatic history relocation for Notepad++ renames and content-matched external moves.
- Added missing-file detection and warnings when the recorded history cannot be located.
- Replaced the unified comparison with a synchronized side-by-side view.
- Added a revision dropdown and red, green, and blue line highlighting.

## 0.1.0-beta.1

- First beta.
- Added pre-save and post-save revision capture with duplicate suppression.
- Added configurable after-edit and periodic autosave modes.
- Added dockable history list, unified diff, guarded restore, and manual capture.
