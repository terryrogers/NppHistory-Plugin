# Four-pixel native toolbar regression

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
4. If the toolbar briefly collapses again, wait two seconds. It should recover; when logging is enabled at Warning or a more detailed level, check for `Toolbar layout recovered`.
5. Report Pass or the action that still leaves it blank. A recurrence is useful evidence for tracing the initiating host/plugin event further.
