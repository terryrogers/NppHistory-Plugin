# Temporary action messages

Status: **source-only; not compiled, installed or runtime-tested**. The existing no-compilation instruction remains in force. Existing DLLs and user configuration are unchanged.

The initial wording and comparison-closure behaviour below are superseded by [contextual action messages and paired logging](SOURCE_ACTION_FEEDBACK-2026-09-03.md). The five-second timing, native ownership and layout-preservation rules remain unchanged.

## Behaviour

- Borrow the leftmost native status field (normally document type) for approximately five seconds. Prefix feedback with `NppHistory:`. Do not add, resize or repartition status fields; cursor position, size, encoding, line endings and typing mode remain native.
- Show concise outcomes for Capture, Refresh, comparison closure, Restore, revision deletion/comment edits, Settings OK, file saves, automatic saves and completed update checks. Comparison feedback says `Comparison closed`, not that a comparison succeeded merely because its window was created. Cancellations and unavailable commands do not produce success messages.
- Save-triggered revision feedback is reported only when a revision was actually created. Automatic-save completion replaces the save notification with `Auto saved` or `Auto save failed`. Compound actions and rapid/bulk actions coalesce to the latest outcome, not a queued notification history. The log remains the detailed record.
- Preserve existing modal confirmations and errors. Failures have outcome-specific feedback; a restored file whose editor reload fails is distinguished from a fully reloaded restore. Messages contain no filenames or document contents and work with logging disabled.
- Wait until the action's current notifications finish before displaying. New messages replace older messages and restart the timeout without losing the original native text. Once displayed, another native/plugin write to this field immediately takes precedence; expiry never restores stale text over it.
- Clear on document activation, native status-layout/simple-mode changes and shutdown. Skip owner-drawn and simple-mode fields; do not force a hidden status bar visible. Reattach if the native status control is recreated. UI-thread-only access and exception containment keep feedback failures from interrupting the action.
- Long text may be clipped by the native field on narrow windows; the plugin deliberately does not change Notepad++'s layout. Actual appearance remains a UAT requirement.

## Prepared automated coverage

`tests/test_temporary_status.cpp` uses a hidden native status control to check queued display, prefix, preserved style, latest-message coalescing, replacement restoration, cancellation before display, timer expiry and stale-timer protection. It also covers Unicode/ANSI host writes, unrelated-field updates, unchanged field count/widths, simple/owner-drawn modes, native layout changes, shutdown, missing control and reattachment after recreation. Registered in the core test project. **Not executed.**

Source-only checks completed: both project XML files parsed and all referenced source paths exist; Settings tooltip audit PASS (79 inputs, 79 targets, no missing/non-input targets); patch whitespace validation PASS. None substitutes for compilation or native execution.

The existing build DLL and installed DLL were rehashed and both remain `0E02F5958085D412E27A38CAD265DBBDEB8FCB19748CCB6CC9FA2CB3CB4795AF` (SHA-256), unchanged from before this batch.

## UAT after an authorised build

1. Note the normal leftmost status text. Capture a revision: expect `NppHistory: Revision captured`, then normal text after about five seconds. Trigger Refresh before expiry: expect replacement and a fresh timeout, then the same normal baseline.
2. Verify Capture/Refresh from pane, toolbar, Plugins menu, enabled context menus and hotkeys. Compare should report `Comparison closed` after its window closes. Confirm Restore, comment edit and deletion show the correct outcome; cancel each confirmation and check there is no false success.
3. Save an edited file with History enabled, then disabled/excluded; only an actually created revision should be announced. Test autosave separately without the conflicting AutoSave plugin. Force a save/access failure on a disposable file: no success message.
4. Click Settings OK, then repeat with Cancel. Check update results for up-to-date, available and offline/failure cases; existing update prompts must remain functional. Simulate settings write failure: expect `Settings save failed` rather than a saved claim.
5. During a message, change document/language, move the caret, switch encoding/line-ending display and open menus. Native first-field text takes priority; unrelated fields keep updating. No obsolete language text may return after expiry.
6. Repeat with logging disabled, light/dark themes, narrow/wide windows, high DPI and hidden/re-enabled status bar. Verify legibility, no changed field widths and no forced visibility.
7. Close/reopen Notepad++ during feedback and run the full existing regression suite. Verify clean shutdown and no status subclass/timer left in an unloaded DLL.

GitHub synchronization must skip CI compilation for this batch; no release or installation is authorised.
