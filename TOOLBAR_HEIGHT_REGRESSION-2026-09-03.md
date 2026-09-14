# Four-pixel native toolbar regression

## Replacement correction — 3 September 2026

Installed UAT **failed** for the height-recovery build below. A user recording and live geometry sampling showed the toolbar alternating between 4 and 30 pixels while a `Toolbar layout recovered` warning was written every second. The timer was repairing a symptom and another layout participant immediately collapsed it again, causing the visible jumping.

The failure is now reproducible in a disposable Notepad++ instance containing NppHistory, Customize Toolbar and NppMenuSearch. Message tracing identified the sequence: NppHistory left toolbar choices that were disabled in Settings as hidden trailing native buttons; Customize Toolbar then measured the trailing hidden item and applied 4-pixel child-height values to the main rebar band. The trace contained window geometry and module offsets only, was not retained in the product, and did not inspect document content.

The replacement correction removes unselected NppHistory commands from the native toolbar instead of marking them hidden. Enabling a command in Settings reinserts its saved native button immediately in the toolbar's current host/customized order. The implementation preserves text, image index, button state and size, refreshes its snapshots when the toolbar is rebuilt, and releases its state when the toolbar window is destroyed. It performs no toolbar/rebar height repair and emits no recurring recovery warning.

Verification:

- The pre-correction DLL fails the repeated three-plugin host-layout test at a 4-pixel minimum toolbar height.
- The replacement build passes all nine icon/plugin geometry cases; the three-plugin case remains 34 pixels high for 30-pixel buttons through repeated host layouts.
- The focused mixed-placement test passes **109 checks** with NppHistory, Customize Toolbar and NppMenuSearch together, including repeated host layouts and live command removal/reinsertion.
- The core/native suite passes **1,243 checks, 0 failures**, including ten repeated remove/reinsert cycles, idempotence, command ordering, tooltip text, enabled state, size/image preservation and same-window customization rebuilds.
- The same-process hotkey hook no longer supplies an unnecessary module handle, which prevented the isolated copied-plugin test from loading the hook (`ERROR_MOD_NOT_FOUND`).

Review artifact: `build/x64/Release/NppHistory.dll`, version **0.2.0.25**. It has not been installed or released. Visual UAT is still required after installation.

## Installation update — 3 September 2026, 16:20 BST

> Historical record: the installed recovery described in this section failed UAT and is superseded by the replacement correction above.

Installed with user approval after verifying Notepad++ was closed. The DLL at `C:/iCloud/iCloudDrive/Filing/N/Notepad++/plugins/NppHistory/NppHistory.dll` now matches tested SHA-256 `084461D1B107CC75D6C69EF4259DFB04E6A212F78A8C476AF177FE6E84603122`; version remains **0.2.0.25**. The previous DLL was backed up and hash-verified at `build/installed-backups/20260903-162015-toolbar-height-recovery/NppHistory.dll` (SHA-256 `8029FAD791C1C31A5532F0F727F0AD15CBE61BD602E018799907A314B35FF1B0`). No settings, documents or history data were changed. Notepad++ was not launched; installed visual UAT remains pending. This supersedes the not-installed status below. No release was published.

## Observed state and verification boundary

The user reported the whole toolbar becoming blank again after accepting the menu-icon correction. Read-only diagnostics of the running Notepad++ 8.9.8 instance found 47 toolbar buttons, a valid image-list handle, and an enabled native New button. The child toolbar was **4 pixels high**, although its buttons were **30 pixels high** and its rebar band recorded **34 pixels** for minimum/current/maximum child height. This is clipping/layout inconsistency, not missing icon assets.

Customize Toolbar 5.3 and NppMenuSearch were both loaded, including `_CustomizeToolbar.dll` in the underscore-prefixed folder. Isolated tests with and without these plugins did not reliably reproduce the initiating event. An early compatibility probe found temporarily undersized geometry and Customize Toolbar changed command ordering; settled geometry tests subsequently passed. Neither plugin has been established as the cause. No third-party plugin or live user configuration was changed.

The correction below is **recovery from the verified invalid layout**, not a claim that the originating event has been fully identified. Installed-environment UAT remains necessary.

## Correction

- During existing toolbar synchronization, inspect only a rebar-hosted toolbar containing NppHistory commands, after host initialization.
- If a visible toolbar has visible non-separator buttons but is shorter than its current native button height, find its matching rebar band and reapply valid child-height constraints.
- Re-run only that rebar's layout using its actual client dimensions. Equal band settings alone were insufficient in the native regression fixture. Do not send `TB_AUTOSIZE` or a synthetic size to the main Notepad++ window.
- Preserve button size, order, enablement, image lists and unrelated rebar-band settings. Leave healthy, minimized, intentionally hidden and all-buttons-hidden toolbars alone.
- Emit one descriptive `[WARNING] Toolbar layout recovered` entry after a successful recovery, subject to normal logging settings. Healthy timer polls make no further geometry changes or repeat entries.

For context, [Notepad++ 8.9.8's toolbar implementation](https://github.com/notepad-plus-plus/notepad-plus-plus/blob/v8.9.8/PowerEditor/src/WinControls/ToolBar/ToolBar.cpp) derives its normal band height from toolbar button/padding metrics and lets its rebar own child layout.

## Regression evidence

- Release x64 plugin and tests build successfully. Core/native-control suite: **1,203 checks, 0 failures**.
- New native fixture deliberately creates a 4-pixel toolbar with valid band metadata. Recovery restores complete button height and preserves dimensions, order, band identity and an unrelated search-style band. Hidden and healthy cases remain unchanged.
- Installed baseline DLL (`8029FAD791C1C31A5532F0F727F0AD15CBE61BD602E018799907A314B35FF1B0`) fails the new isolated recovery assertion after deliberate clipping: `build/commands-1d8bcf01a2ea4b198ef34b5efa3a72e2`.
- Corrected build passes the same injected-fault recovery, logging and layout workflow: **104 checks** with mixed toolbar visibility and the available monitor transition; **103 checks** with all seven commands configured. The monitor test covered the one display exposed to the test process, not a multi-DPI matrix.
- Corrected evidence directories: `build/commands-c632f2c9f4fa427e99e10a95562d618b` and `build/commands-a4718be9b59944d9b75df6bd0df14b21`. The recovered-toolbar render was visually inspected.
- Settled startup matrix: **9 cases passed** in `build/toolbar-geometry-36ddf70a199a4d5daa89b72513d56d5a`. All five native icon styles were confirmed through the host API (small variants: 25-pixel toolbar / 22-pixel buttons; large variants: 41 / 38). Four optional installed-plugin combinations also fit: Customize Toolbar alone, NppMenuSearch alone, both together, and both with NppHistory. This validates those isolated startup configurations, not every interaction with the user's full plugin set. The optional plugins are copied only into disposable test folders.
- The earlier 95-check suite only compared before/after toolbar dimensions and could pass with an invalid starting height. It now requires enough height for actual native buttons at startup and after live visibility changes.
- Full document/tab-context UAT and other previously outstanding manual tests are not claimed as completed by these focused checks.

## Review artifact

`build/x64/Release/NppHistory.dll`, version **0.2.0.25**.

SHA-256: `084461D1B107CC75D6C69EF4259DFB04E6A212F78A8C476AF177FE6E84603122`.

Not installed or released. The current installation is unchanged. No OpenProject project applies.

## Manual review after approved installation

1. Reopen Notepad++ and confirm the whole toolbar, including its native buttons, is visible.
2. Open NppHistory Settings > Commands & Hotkeys. Toggle one Toolbar checkbox, click OK, and confirm only that command changes visibility without collapsing the toolbar. Restore the setting.
3. Resize/maximize/restore Notepad++; confirm the toolbar remains fully visible.
4. Leave Notepad++ open for at least ten seconds and resize it several times. The toolbar must remain stable; no `Toolbar layout recovered` warnings should be generated.
5. Report Pass, or report the first action after which the toolbar changes height or becomes blank.
