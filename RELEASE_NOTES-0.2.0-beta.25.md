# NppHistory 0.2.0 beta 25

This is a controlled prerelease candidate for 64-bit Notepad++ on Windows. It is not the final v1.0.0 release.

## Highlights

- Adds verified download, restart, plugin replacement and Notepad++ relaunch for future NppHistory updates.
- Offers **Download, restart and install**, **View release** and **Later** when an automatic or manual check finds a compatible newer release.
- Keeps update checks active during long-running Notepad++ sessions, including scheduled checks after Windows resumes from sleep.
- Adds retry backoff and a live countdown to the next automatic check without checking merely because Settings opens or gains focus.
- Expands audit logging for saves, revisions, update checks, comment changes, deletion and restoration.
- Displays prerelease builds consistently in the numeric Plugins Admin form, such as `0.2.0.25`.
- Adds CI and release-gate testing for successful updater replacement, backup creation, invalid-digest rejection and preservation of the installed DLL.
- Keeps Compare and Restore disabled until a history revision is selected.
- Renames the revision-list Bytes column to Size and displays dynamic human-readable units.
- Adds a clear pale-blue hover state to the History pane command buttons.
- Adds separate automatic-saving and revision-history wildcard exclusion lists, with distinct crossed-out Auto Save and Revision History tab icons and a wrapping **File Excluded in Settings** pane status. Each icon appears only while its feature is enabled, and indicator painting leaves Notepad++'s tab padding, captions, widths and ordering untouched.
- Disables all file-history actions for excluded History files and adds explanatory hover tooltips to every History pane button, including disabled buttons.
- Adds contextual hover help to every actionable Settings control, including disabled inputs.
- Detects the separate Notepad++ AutoSave plugin and disables NppHistory Auto Save with a red settings notice, preventing competing automatic-saving engines.
- Labels restore safety revisions explicitly as **Before restore**, including when modified editor content must first be saved, without also creating a misleading **Saved** revision.
- Uses compact `[INFO]` severity labels for informational log records.
- Keeps all five Plugins-menu icons attached if Notepad++ refreshes or rebuilds the menu.
- Records readable Settings debug actions such as `Settings tab | Logging` and `Settings control | Open Log`, without raw numeric control IDs.
- Records setting changes with readable option names and values instead of internal numbers, and emits each pane-button click once.
- Centres plugin dialogs and confirmations on the main Notepad++ window.

## Installation from beta 24 or earlier

Beta 24 does not contain the restart installer, so the first upgrade to beta 25 is manual:

1. Close Notepad++.
2. Extract the release ZIP into the Notepad++ `plugins` directory. The archive contains `NppHistory\NppHistory.dll`.
3. Replace the previous DLL when prompted.
4. Start Notepad++ and confirm **Plugins > NppHistory > About** reports version `0.2.0.25`.

After beta 25 is installed, a later compatible release can exercise the new automatic restart-and-install path.

## Safety behavior

- Both downloaded executables must match the exact GitHub release tag, asset names, sizes and SHA-256 digests.
- The external updater waits for Notepad++ to close, retains the previous DLL and verifies the replacement immediately before installation.
- Download, permission, verification, replacement and restart failures leave the installed plugin recoverable and report the outcome.
- Normal Notepad++ unsaved-file prompts remain in control of shutdown.

## Known beta limitations

- Windows x64 only.
- New untitled tabs must be saved once before file-dependent history actions are available.
- History retention is unlimited; automatic pruning is not implemented yet.
- History is local and is not encrypted by NppHistory.
- Cloud synchronization remains outside the plugin.
- Manual UAT must pass before this candidate is published.

Detailed verification evidence is recorded in `TEST_REPORT-0.2.0-beta.25.md`.
