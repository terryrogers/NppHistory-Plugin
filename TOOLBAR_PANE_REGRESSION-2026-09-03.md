# Toolbar / History pane repaint regression

## Installation update — 3 September 2026, 14:22 BST

Installed with explicit user approval after verifying Notepad++ was closed. The installed DLL now matches corrected build SHA-256 `A58027E96C530AD54209CA609FBB8E04B44AB1D983C3EDEF963CF3243EFF4674`; version remains **0.2.0.25**. Previous DLL hash `B58F1AC880C8A79AE8AA7C3A97110BC1D731162268679FFBBEEA3C456EB77182` was backed up and verified at `build/installed-backups/20260903-142234-toolbar-pane-repaint/NppHistory.dll` before replacement. No settings, user files or history were changed. Notepad++ was not launched; installed-environment visual UAT is still pending. This update supersedes the not-installed status recorded below.

## Report and investigation

The user reported a blank native toolbar and stale/overlapping-looking History pane button rows after the latest review build. No user document contents or supplied screenshot were copied into repository artifacts. Investigation and runtime checks used disposable `commands.txt` in isolated Notepad++ processes.

Source inspection found two repaint hazards:

- Toolbar enablement rewrote state and invalidated the complete toolbar for each plugin command during each periodic poll, including when nothing changed. Visibility changes separately erased/autosized the host toolbar and sent a synthetic main-window resize.
- Pane layout moved individually drawn child buttons without explicitly clearing the final parent background; old button pixels could remain in gaps after repositioning or hiding controls.

The exact blank-toolbar symptom was not reproduced in the isolated baseline. The correction addresses these observed source hazards; confirmation in the user's loaded-plugin environment remains necessary. Successful core tests alone did not exercise that host painting behaviour.

## Changes

- Update a toolbar command only when its enabled state changes. Let `TB_ENABLEBUTTON` preserve unrelated state bits; remove redundant state rewrites and unconditional full-toolbar invalidation.
- Validate command presence and handle 32-bit negative native return values correctly on x64.
- Stop sending `TB_AUTOSIZE` or synthetic main-window `WM_SIZE` during visibility synchronization. Notepad++ continues to own its toolbar/rebar bounds. Repaint the parent background together with its transparent toolbar only after visibility actually changes.
- After pane layout, invalidate/erase the full parent and its children so the final positions and gaps are repainted cleanly. Command order, configured visibility, equal button sizes and wrapping behaviour are retained.

Reference: [Microsoft toolbar autosizing documentation](https://learn.microsoft.com/en-us/windows/win32/controls/tb-autosize) and [Notepad++ toolbar implementation](https://github.com/notepad-plus-plus/notepad-plus-plus/blob/master/PowerEditor/src/WinControls/ToolBar/ToolBar.cpp). The host toolbar uses a parent-managed rebar and transparent toolbar style.

## Validation performed

- Plugin rebuilt successfully; no compilation diagnostics were reported.
- Core tests: **1,123 checks, 0 failures**.
- Focused isolated native test: `tests/commands_smoke.ps1 -LayoutOnly` — **95 checks passed**. Covers menu/toolbar registration/order, immediate toolbar hide/show, Cancel, retained toolbar position/height, six pane controls with History hidden, widths 270/145/390/270, ordering, equal sizes, bottom margin, list clearance and all pairwise non-overlap checks.
- Rendered isolated screenshots inspected: native toolbar icons present; normal-width pane displays clean two-row buttons; narrow pane displays clean single-column buttons. PrintWindow captures repaint the controls and are not proof of every asynchronous on-screen repaint sequence.
- Final test evidence folder: `build/commands-921eff36f17840d898ab3d8f1378d742`. Earlier inspected render evidence: `build/commands-b06deed3534847e5801f4bdcdb59b581`.
- The earlier broader command smoke run stopped because its tab-bar context-menu probe could not open the popup. That result has not been relabelled PASS; full command/context-menu regression remains outstanding. The focused run deliberately reports only toolbar and pane scope.

## Review build / deployment boundary

- New local DLL: `build/x64/Release/NppHistory.dll`, version **0.2.0.25**, SHA-256 `A58027E96C530AD54209CA609FBB8E04B44AB1D983C3EDEF963CF3243EFF4674`.
- Installed DLL observed during this task: SHA-256 `B58F1AC880C8A79AE8AA7C3A97110BC1D731162268679FFBBEEA3C456EB77182`; not modified by this task.
- No production installation or release performed. User confirmation is needed after closing Notepad++ and installing the replacement build.

After installation, check the toolbar immediately and after 30 seconds idle; toggle Capture toolbar placement with OK and Cancel; resize the History pane narrow/wide, select/unselect a revision and hover each button. All native toolbar icons should remain drawn, pane gaps should contain no stale button fragments, and configured enablement/order should remain correct. Recheck with the user's usual third-party plugins and themes.
