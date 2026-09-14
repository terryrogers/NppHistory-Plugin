# Apply toolbar and hotkey changes on OK

Status: **source-only; not compiled, installed or runtime-tested**. This follows the command-placement refinements and supersedes their restart explanation. The installed DLL and existing build output remain unchanged.

## Behaviour implemented

- All seven toolbar icons are registered once at startup. Settings controls their visibility dynamically, keeping the existing command order and enabled/disabled states. OK immediately shows/hides buttons; Cancel leaves them alone. Icon-set/dark-mode notifications and the existing timer reapply visibility after native toolbar reconstruction, without registering duplicate buttons.
- NppHistory owns a replaceable shortcut table. A hook restricted to the Notepad++ UI thread examines removed keyboard messages before normal accelerator processing. It is never installed globally and does not register system-wide hotkeys.
- OK replaces the whole binding table and refreshes Plugins/document/tab menu shortcut suffixes. The previous key stops matching; the new one starts matching. Editing a Settings draft has no live effect. Cancel does not replace the table.
- Shortcuts require the active, enabled main Notepad++ window. Modal dialogs, modeless top-level dialogs, open menus, Windows-key combinations and AltGr typing are excluded. Ordinary typing, Shift-only typing, Alt-letter menu mnemonics and reserved system combinations are rejected. Held keys dispatch at most once per press.
- Commands are queued outside the keyboard hook. Before execution, the handler rechecks the table generation, current document identity, focus scope and action availability. A queued shortcut cannot act on a different document or survive a settings replacement.
- Old Notepad++ native registrations are removed and read back before enabling the live table, preventing the obsolete combination or two handlers from firing the same action. Failure is logged and live shortcuts are marked unavailable; Settings retries setup on reopening and prevents enabling unavailable bindings.
- Valid edits from Notepad++ Shortcut Mapper are copied after that dialog closes, persisted in NppHistory, and then removed from the native accelerator table to keep one handler. Duplicate/unsafe assignments are rejected with a warning and previous NppHistory settings retained. **Commands & Hotkeys is the authoritative shortcut configuration**; native Shortcut Mapper may show those entries as unassigned once imported.
- Existing menu-visible conflict checks remain basic: they do not enumerate every Scintilla key binding, another plugin's private handler, or external applications' shortcuts. Full compatibility is not claimed.

## Verification boundary

- Read-only Settings tooltip source audit: PASS, 79 input/button targets, no missing or non-input targets.
- Updated PowerShell smoke scripts parse successfully; whitespace checks pass.
- Unit cases cover the pure keyboard state machine, replacement/disable, draft isolation, exact modifiers, focus/AltGr/Windows exclusions, repeats/releases, reserved keys and shortcut display text. **Not run.**
- Native smoke cases now check immediate toolbar hide/show, menu suffix replacement and Cancel, all within one process. They distinguish seven registered commands from the configured visible subset. **Not run.**
- No compiler, test executable, Notepad++ automation, installation or release was run for this batch. GitHub synchronization skips CI compilation.

## Required checks after an authorised build

1. Run core and native regression suites. Test toolbar selections from all-off through all-on, preserving order and no duplicates. Toggle icon size, dark/light themes and DPI; choices must persist.
2. Assign a free Capture shortcut, click OK and physically press it: exactly one revision/action. Reassign it and prove the old combination no longer captures, while the new one does. Disable it and verify no capture. Repeat for the other six commands where applicable.
3. Change a shortcut and toolbar selection, then Cancel: both original behaviours must remain. Verify menu suffixes on Plugins and both context menus after each successful OK.
4. Hold a shortcut down: no repeated capture/restore or repeated dialogs. Check modified/unsaved/excluded files and Compare/Restore without a revision selection: disabled actions must not run.
5. Press configured keys inside Settings/hotkey inputs, Find, About, comment-edit and update dialogs, open menus and another application. These must not launch plugin commands. Verify normal typing, AltGr characters and Windows shortcuts remain unaffected.
6. Change file focus or Settings while an action is queued; confirm stale actions are discarded. Close Notepad++ and verify the thread hook is removed cleanly.
7. Exercise native Shortcut Mapper changes (valid, duplicate and reserved), then reopen NppHistory Settings. Confirm import/rejection, one action per press, correct displayed/persisted values and clean restart behaviour.
8. Simulate hook/removal and settings-file write failures before release. Confirm clear logs and no duplicate/falsely active shortcuts. Persistence failure may leave session settings active but cannot be treated as a saved configuration.

Implementation references: [Microsoft WH_GETMESSAGE callback](https://learn.microsoft.com/en-us/windows/win32/winmsg/getmsgproc), [toolbar show/hide message](https://learn.microsoft.com/en-us/windows/win32/controls/tb-hidebutton), and the bundled Notepad++ plugin API definitions.
