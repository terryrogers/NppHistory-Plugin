# NppHistory Architecture

## System Context

**Confirmed:** NppHistory is a native C++17, Win32, x64 Notepad++ plugin. It is built with Visual Studio/MSBuild and uses Notepad++ plugin messages, Scintilla controls, Windows resources and filesystem APIs. `NppHistoryUpdater.exe` is a separate restart-time replacement helper.

## Major Components

- `Settings`: persisted autosave, history, logging, update, command-placement and shortcut policy.
- `HistoryStore`: revision creation, hashing, duplicate suppression, migration, restore and filesystem safety.
- `HistoryCatalog`: stable file identities and current source/history path reconciliation.
- `HistoryPanel`: dockable revision list and user actions.
- `TextDiff`: read-only text comparison, navigation, highlighting and location map.
- `DocumentTabIndicators`: visible autosave/history exclusion state.
- `Logger`: bounded diagnostic output with configured rollover behaviour.
- `UpdateChecker` and `UpdateInstaller`: release discovery, trusted-location checks, digest verification and protected replacement.
- `ToolbarVisibility` and `TemporaryStatusBar`: host UI integration and transient feedback.

## Data Flow

1. Notepad++ supplies file and lifecycle notifications.
2. Autosave policy may request a Notepad++ save for eligible documents.
3. History policy captures the on-disk pre-save content, then the confirmed saved result.
4. SHA-256 duplicate suppression prevents consecutive identical revisions.
5. The catalogue maintains stable identity and storage-path mappings.
6. The panel reads revision metadata and invokes compare, comment, delete or restore actions.
7. Restore first retains the current saved file as a safety revision.

## Storage And Privacy

**Confirmed:** The default store is a hidden `.npphistory` folder adjacent to the source file. A configured common root stores buckets beneath its hidden `.npphistory` child. Settings and `catalog.db` normally live in the Notepad++ plugin configuration area. History is local and unencrypted by NppHistory; the user's storage or synchronisation choices determine whether it leaves the machine.

## Safety Boundaries

- Migration removes a source only after verified success and leaves failed sources available for retry.
- Ambiguous moved-file matches are reported rather than guessed.
- Update installation accepts trusted GitHub release locations, verifies SHA-256 data, retains a previous DLL and reports recoverable failures.
- Generated binaries, local catalogues, settings, logs and user history are excluded from Git.

## Verification Layers

**Confirmed:** Direct native tests, PowerShell smoke tests, updater/runtime isolation and GitHub Actions cover different layers. Installed Notepad++ behaviour, UI compatibility, migration against user data and restart survival remain distinct manual evidence boundaries.
