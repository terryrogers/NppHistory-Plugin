# Native menu icon rendering regression

## Report and correction

The supplied screenshots show disabled command icons switching from an etched/outlined appearance to a pale appearance after hover. This affects Plugins > NppHistory and both document/tab-bar context menus.

Inspection found that the common command bitmap producer flattened icons onto a solid `COLOR_MENU` background using a device-dependent bitmap. The document and tab-bar context menus copy those same bitmap handles from the Plugins menu. The revision-actions popup had a separate implementation with the same issue. This is a likely cause of inconsistent native disabled/theme rendering; the exact hover transition has not yet been visually reproduced in an isolated test.

Both producers now use one transparent, 32-bit premultiplied-alpha DIB renderer. It reconstructs coverage from black/white icon renders, preserving alpha artwork, masked legacy icons and opaque black pixels without embedding a theme background. Windows continues to own enabled/disabled and hover styling. There are no command-state, shortcut, placement or toolbar changes.

Pixel format reference: [Microsoft alpha blending documentation](https://learn.microsoft.com/en-us/windows/win32/gdi/alpha-blending).

## Verification

- Release x64 plugin and test builds succeeded.
- Core suite: **1,186 checks, 0 failures** (63 new menu-bitmap checks).
- Tests cover invalid input, alpha and legacy-mask fixtures, transparent pixels, opaque black, fractional alpha, caller ownership, all nine actual command/revision menu icon assets, native menu attachment and preservation through disabled-to-enabled state changes.
- Focused isolated Notepad++ toolbar/pane suite: **95 checks passed**, including toolbar bounds and pane wrapping/non-overlap. This is a regression check, not proof of menu hover appearance.
- Full command-context native suite was not claimed as passing; its previously documented tab-popup probe limitation remains outside this correction.

Review DLL: `build/x64/Release/NppHistory.dll`, version **0.2.0.25**.

SHA-256: `8029FAD791C1C31A5532F0F727F0AD15CBE61BD602E018799907A314B35FF1B0`.

**Not installed or released.** User settings, documents and history were not changed. No OpenProject project is associated with this repository.

## Installed-environment UAT required

After explicitly approved installation with Notepad++ closed:

1. Open a new, unsaved document. Open Plugins > NppHistory; compare the disabled Capture/Compare/Restore/Refresh icons before and after moving the pointer over each item. They should remain consistently subdued, with no etched-to-faded transition or solid background square. History/Settings/About should remain coloured.
2. Repeat in the document right-click menu and tab-bar right-click menu, using whichever inline/submenu arrangement is enabled in Settings.
3. Open a saved, non-excluded test file with history. Select a revision and confirm enabled icons retain their artwork as menu highlights move between commands.
4. Right-click a revision; check Delete/Edit/Compare/Restore icons against both normal and highlighted backgrounds.
5. Repeat in dark mode if used. Report the menu, command, state and theme for any remaining visual failure.
