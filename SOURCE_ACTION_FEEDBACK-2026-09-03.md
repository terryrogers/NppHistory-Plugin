# Contextual action messages and paired logging

Status: **source-only; not compiled, installed or runtime-tested**. This supersedes the initial short messages in `SOURCE_STATUS_MESSAGES-2026-09-03.md`. No release or deployment is authorised.

## Complete wording and severity contract

Every status message retains the `NppHistory: ` prefix. `Test.log` below is replaced with the actual filename, retaining its spelling/case. `{revision}` is the selected revision's existing Windows-localized timestamp, not a newly invented revision number. `{version}` is the numeric display version, such as `0.2.0.25`.

| Event | Message after prefix | Log severity |
|---|---|---|
| Capture succeeds | Test.log Revision Captured. | INFO |
| Capture fails, including prerequisite save | Revision Capture for Test.log Failed! | CRITICAL |
| Refresh | History for Test.log Refreshed. | INFO |
| Comparison view opens | Test.log Comparison View Opened. | INFO |
| Comparison view cannot open | Comparison View for Test.log Failed to Open! | WARNING |
| Restore and editor reload succeed | Restored {revision} Revision for Test.log. | INFO |
| Restore fails, including prerequisite save | Restore of {revision} Revision for Test.log Failed! | CRITICAL |
| Restored on disk but editor reload fails | Restored {revision} Revision for Test.log; File Reload Failed! | CRITICAL |
| Revision deleted | Test.log Revision {revision} Deleted. | INFO |
| Deletion fails | Deletion of Test.log Revision {revision} Failed! | WARNING |
| Comment updated | Test.log Revision {revision} Comment Updated. | INFO |
| Comment update fails | Test.log Revision {revision} Comment Update Failed! | WARNING |
| Settings OK persisted | Settings Saved. | INFO |
| Settings persistence fails | Settings Save Failed! | CRITICAL |
| File saved without an after-save revision | Test.log Saved. | INFO |
| File saved with a new after-save revision | Test.log Saved; Revision Created. | INFO |
| Automatic save succeeds | Test.log Automatically Saved. | INFO |
| Automatic save fails | Automatic Save for Test.log Failed! | CRITICAL |
| Update available | Manual Update Check: Update Available (Version {version}). | INFO |
| No newer update found | Manual Update Check: Up to Date (Version {version}). | INFO |
| Update check fails | Manual Update Check Failed! | WARNING |

The three update messages say **Automatic Update Check** for scheduled checks. Omit version parentheses if no published version is returned. Comparison now reports during view initialization, after its controls/content are initialized; closing it does not produce the former `Comparison closed` message. Existing cancellation and disabled-action guards remain unchanged.

## Corresponding logs

`reportAction` formats each message once and submits that same wording and its mapped severity to the logger before queuing the transient status display. Logs add labelled full file path, revision timestamp, version and diagnostic detail where supplied. Revision-operation details retain the revision-file path; deletion also retains its comment. This distinguishes same-named files and revisions with identical displayed minute timestamps.

Rapid and compound actions can replace each other's displayed messages, but each report still produces its own eligible log entry. Existing distinct records such as before-save revision creation and DEBUG option changes remain; duplicate former Capture/Refresh/Compare/Restore outcome records have been replaced by the paired event. Read/control errors and other unrelated ERROR records are not relabelled wholesale.

Successful actions are INFO. Comparison-open, deletion, comment-edit and update-check failures are WARNING. Failed capture/save/restore/settings persistence or a restored file that cannot reload is CRITICAL. Critical is more severe than the existing Error level and is included at every enabled logging threshold. Existing Error/Warning/Informational/Debug settings retain their numeric values and selection behaviour; no new dropdown selection is required.

Logging **still respects the enable switch and selected threshold**. INFO events are intentionally filtered at Warning/Error thresholds. Disabled logging writes nothing, even for Critical; this feature does not secretly enable logging. If the log location is inaccessible, an entry cannot be guaranteed, but status feedback remains independent. To record all these action outcomes, enable logging at **Informational** or **Debug**.

Control characters are flattened to prevent multiline status text or injected log lines. Status text uses the basename; full paths and diagnostic detail stay in the log. No document contents are added. The five-second timeout, native ownership, restored baseline and no-layout-change behaviour are unchanged. Long filenames/timestamps may still be clipped by the native field; full event wording remains in eligible logs. Native legibility needs UAT.

## Validation boundary

- Source audit PASS: 21 event kinds, exact-wording fixtures for all 21, all events wired, one direct status producer (the paired reporter), comparison-open reporting in initialization.
- Settings tooltip source audit PASS: 79 eligible controls, 79 hints, none missing or attached to non-input controls.
- Both project XML/source-reference checks PASS; updated runtime smoke PowerShell syntax PASS. Smoke script was parsed only, not run.
- Prepared unit cases verify exact wording/severity, every event's single matching log entry, full context, Unicode, localized timestamp preservation, automatic/manual variants, fallback text, control-character sanitation, old log-level values, thresholds and disabled/inaccessible logging independent of status.
- Native smoke assertions now expect filename-aware capture/save/compare/restore/comment/delete messages and distinguish manual/automatic update outcomes. **Not executed.** Existing PASS reports do not validate this batch.

After an authorised build, run the core suite and native smoke suite, then test every row above in real Notepad++. Check comparison feedback while the view is open (not on closure), long/Unicode filenames, localized timestamps, narrow status fields, two same-named files in different folders and successive actions. At Informational, confirm one matching outcome log per report; repeat with Warning, Error, Debug, logging disabled and an inaccessible log location. Cancelled dialogs must not emit success. Keep existing restoration/native-status ownership tests.
