# Function verification matrix

This inventory covers the project-defined functions in `src`. “Indirect” means a private helper is reached through a public operation and its observable result is asserted. “Live” means the rebuilt DLL is exercised in an isolated real Notepad++ process. PASS means the intended behaviour represented by that function has automated evidence; PARTIAL means its core behaviour is tested but a modal, destructive, lifecycle or external side effect remains manual UAT.

## Utilities.cpp

| Function | Evidence | Status |
|---|---|---|
| `readAllBytes` | Direct core: normal, empty and missing files | PASS |
| `writeAllBytesAtomic` | Direct core: create, replace and empty content | PASS |
| `sha256Hex(vector)` | Direct core: official empty and `abc` vectors | PASS |
| `sha256Hex(wstring)` | Direct core: wide input | PASS |
| `normalizePath` | Direct core | PASS |
| `wideToUtf8` | Direct core: Unicode and empty | PASS |
| `utf8ToWide` | Direct core: Unicode, empty and invalid input | PASS |
| `decodeText` | Direct core: BOM UTF-8, UTF-16LE and ANSI fallback | PASS |
| `utcTimestampCompact` | Direct format check | PASS |
| `localTimestampDisplay` | Direct locale-format check | PASS |
| `localDateDisplay` | Direct valid, empty and invalid-date checks | PASS |
| `centerWindowOnOwner` | Isolated Win32 positioning | PASS |
| `fitWindowWithinOwner` | Isolated Win32 sizing/positioning | PASS |

## Settings.cpp / Settings

| Function | Evidence | Status |
|---|---|---|
| `readNumber` | Indirect through settings dialog/load and minimum normalization | PASS |
| `readBoolean` | Indirect through every Boolean setting round-trip | PASS |
| `ensureUnicodeIni` | Direct observable ANSI migration and Unicode path preservation | PASS |
| `updateLocationControls` | Live custom/adjacent enablement | PASS |
| `updateHistoryControls` | Live History master enablement | PASS |
| `updateAutoSaveControls` | Live Auto Save master/interval enablement | PASS |
| `updateUpdateControls` | Live automatic-update master/dependent-control enablement | PASS |
| `showSettingsPage` | Live three-tab selection/rendering | PASS |
| `browseForHistoryRoot` | Control presence/enablement live; native picker selection not automated | PARTIAL |
| `settingsProc` | Live initialization, commands, page changes and dependent states | PASS |
| `Settings::shouldAutoSave` | Direct all five triggers, selected/cleared/master-off | PASS |
| `Settings::shouldCreateRevision` | Direct all revision paths, selected/cleared/master-off | PASS |
| `Settings::afterEditDue` | Direct pre/exact threshold, disabled and tick reversal | PASS |
| `Settings::intervalDue` | Direct pre/exact threshold, disabled and tick reversal | PASS |
| `Settings::updateCheckDue` | Direct disabled, daily, weekly and monthly boundaries | PASS |
| `Settings::load` | Direct defaults, all fields, clamping and legacy migration | PASS |
| `Settings::save` | Direct all fields/frequencies and Unicode persistence | PASS |
| `Settings::edit` | Live modal creation, centring, icon and content | PASS |

## UpdateChecker.cpp

| Function | Evidence | Status |
|---|---|---|
| `parseSemanticVersion` | Direct valid, prerelease, metadata, overflow/format and leading-zero cases | PASS |
| `compareSemanticVersions` | Direct major/minor/patch, stable/prerelease, numeric/string and sequence precedence | PASS |
| `parseGitHubReleases` | Direct nested fields, draft, trusted origin, incomplete and malformed responses | PASS |
| `selectNewestUpdate` | Direct stable/prerelease channels, newest/current and invalid-installed-version cases | PASS |
| `elapsedFrequencyDue` | Direct first run, pre/exact threshold and clock rollback | PASS |
| `trustedReleaseUrl` | Direct HTTPS host/owner/repository allow-list cases | PASS |
| `shouldNotifyUpdate` | Direct new, duplicate, manual and empty-tag cases | PASS |
| `updateAccessErrorMessage` | Direct rate-limit, repository, proxy, HTTP, timeout, DNS, connection, TLS and unknown errors | PASS |
| `currentUnixSeconds` | Indirect persisted successful live-check timestamp | PASS |
| `checkGitHubForUpdates` | Live background request to the real public Releases endpoint | PASS |
| `downloadReleaseJson` | Indirect live HTTPS request, status/size handling and core failure classification | PASS |

## TextDiff.cpp

| Function | Evidence | Status |
|---|---|---|
| `splitSourceLines` | Indirect direct-core diff cases including line endings | PASS |
| `splitLines` | Indirect unified-diff cases | PASS |
| `isBlank` | Indirect ignore-blank-lines cases | PASS |
| `comparisonKey` | Indirect every comparison option | PASS |
| `inlineDifferences` | Direct changed-span and large-line fallback checks | PASS |
| `makeSideBySideDiff` | Direct empty/equal/add/remove/change/large cases | PASS |
| `makeUnifiedDiff` | Direct changed and large-fallback cases | PASS |

## HistoryStore.cpp / HistoryStore

| Function | Evidence | Status |
|---|---|---|
| `setRoot` | Direct custom-root capture/path tests | PASS |
| `setCatalog` | Direct catalogue-backed capture and null-catalogue tests | PASS |
| `root` | Direct getter/path assertion | PASS |
| `bucketFor` | Direct adjacent, custom and catalogue paths | PASS |
| `captureFile` | Direct missing, normal, duplicate, forced and changed content | PASS |
| `captureBytes` | Direct empty/binary byte captures and duplicate logic | PASS |
| `revisionsFor` | Direct parse, sort, malformed/orphan/empty metadata cases | PASS |
| `readRevision` | Direct content read | PASS |
| `restoreRevision` | Direct success and missing-file rejection; modal live action not confirmed | PARTIAL |
| `updateComment` | Direct success, sanitization and missing-metadata rejection; modal submit not automated | PARTIAL |
| `deleteRevision` | Direct success, state recalculation and missing-file rejection; confirm dialog not automated | PARTIAL |

## HistoryCatalog.cpp / HistoryCatalog

| Function | Evidence | Status |
|---|---|---|
| `hexEncode` | Indirect Unicode catalogue save/reload | PASS |
| `hexValue` | Indirect valid and malformed database load | PASS |
| `hexDecode` | Indirect Unicode catalogue save/reload and malformed tolerance | PASS |
| `newId` | Direct observable stable/unique record IDs | PASS |
| `samePath` | Indirect reconcile/current-record matching | PASS |
| `hashFile` | Indirect content-match, missing and ambiguous reconciliation | PASS |
| `configure` | Direct setup and resulting database path | PASS |
| `load` | Direct reload and malformed database tolerance | PASS |
| `save` | Direct persistence/reload | PASS |
| `databaseFile` | Direct path assertion | PASS |
| `desiredHistoryPath` | Direct adjacent/custom path assertions | PASS |
| `ensureHiddenAdjacentRoot` | Direct adjacent-root creation plus live hidden folder | PASS |
| `moveHistory` | Direct successful and blocked migration | PASS |
| `reconcile` | Direct new, unchanged, explicit move, hash match, missing, ambiguous, blocked and legacy cases | PASS |
| `historyPathFor` | Direct known and unknown record paths | PASS |
| `recordCapture` | Direct new/existing record capture and persistence | PASS |
| `records` | Direct count/content assertions | PASS |

## HistoryPanel.cpp / HistoryPanel

| Function | Evidence | Status |
|---|---|---|
| `createMenuBitmap` | Live right-click command icons | PASS |
| `applyMenuBitmap` | Live right-click command icons | PASS |
| `toolbarCommandAtPoint` | Live hover over displayed toolbar rectangles | PASS |
| `hideToolbarTooltip` | Live hover transition/current-difference regression | PASS |
| `showToolbarTooltip` | Live hint text (`Choose revision to compare`) | PASS |
| `comparisonToolbarSubclass` | Live toolbar hover/click workflow | PASS |
| `comparisonEditSubclass` | Live synchronized editor scroll | PASS |
| `comparisonMarkerSubclass` | Live Location Pane navigation/render | PASS |
| `comparisonHeaderSubclass` | Live revision-header picker | PASS |
| `create` | Live dock creation, icon and controls | PASS |
| `show` | Live Plugins menu display | PASS |
| `refresh` | Live revisions and forced capture refresh | PASS |
| `selectedIndex` | Live selection/action-menu targeting | PASS |
| `showRevisionActions` | Live right-click popup and four icon commands | PASS |
| `editSelectedComment` | Store logic direct; modal edit submission not automated | PARTIAL |
| `deleteSelected` | Store logic direct; confirmation click not automated | PARTIAL |
| `compareSelected` | Live comparison launch | PASS |
| `currentSourceText` | Live source content displayed in comparison | PASS |
| `renderSide` | Direct diff rows plus live two-pane rendering | PASS |
| `setStyle` | Live Scintilla colours/styles | PASS |
| `styleText` | Live syntax/difference text styling | PASS |
| `configureScintilla` | Live line numbers, editors and scrolling | PASS |
| `configureWinMergeToolbar` | Live 15-button toolbar | PASS |
| `toolbarHint` | Live tooltip text/hover | PASS |
| `isDifference` | Direct diff kinds plus live navigation count | PASS |
| `markerColour` | Live location/difference palette | PASS |
| `renderComparison` | Live headers, rows, palette and status | PASS |
| `configureComparisonScroll` | Live central synchronized scroll | PASS |
| `scrollComparisonTo` | Live synchronized scroll/navigation | PASS |
| `updateComparisonNavigation` | Live Previous/Next/Current states | PASS |
| `updateComparisonStatus` | Live difference counter/status | PASS |
| `navigateDifference` | Live previous/next navigation | PASS |
| `showRevisionPicker` | Live picker position and revision count | PASS |
| `restoreSelected` | Core restore path direct; modal overwrite/reload not automated | PARTIAL |
| `layout` | Live normal, resized, wrapped and collapsed layouts | PASS |
| `configureButtonIcons` | Live all six pane icons | PASS |
| `dialogProc` | Live pane init, resize, commands and context menu | PASS |
| `editCommentProc` | Dialog construction/underlying update covered; submit not automated | PARTIAL |
| `compareProc` | Live init, resize, toolbar, headers, scroll and close paths | PASS |
| `compare` | Live selected-revision comparison | PASS |
| `restore` | Core restore and toolbar wiring; modal completion not automated | PARTIAL |
| `handle` | Live dock notification/command handling | PASS |

## NppHistory.cpp and exported plugin surface

| Function | Evidence | Status |
|---|---|---|
| `pathForBuffer` | Live active test-file resolution | PASS |
| `currentBuffer` | Live active-buffer commands | PASS |
| `currentPath` | Live pane/capture/compare source path | PASS |
| `currentEditor` | Live edit injection and comparison source retrieval | PASS |
| `isSavableFile` | Live saved test file; untitled/device exclusions are code-path only | PARTIAL |
| `existingFile` | Live save/reconcile and direct filesystem cases | PASS |
| `showReconcileAlert` | Reconcile outcomes direct; MessageBox display not automated | PARTIAL |
| `reconcileFile` | Live active file plus direct catalogue outcomes | PASS |
| `detectMissingBuffers` | Notification/timer wiring built; disappearing-open-file alert remains manual UAT | PARTIAL |
| `refreshPanel` | Live list refresh | PASS |
| `saveBuffer` | Live actual after-edit save | PASS |
| `saveConfiguredScope` | Live current test file and direct scope persistence; multi-file live path not automated | PARTIAL |
| `mainWindowSubclass` | Live main-window timer/toolbar lifecycle; shutdown/focus details remain manual UAT | PARTIAL |
| `updateThreadProc` | Live non-blocking GitHub request and completion message | PASS |
| `startUpdateCheck` | Live manual background check; duplicate-start policy direct through helper | PASS |
| `handleUpdateCompletion` | Live up-to-date result and timestamp persistence; available-link prompt covered at core policy level | PARTIAL |
| `timerProc` | Live after-edit timer; interval branch directly policy-tested | PASS |
| `showHistory` | Live menu command and dock display | PASS |
| `captureNow` | Live forced manual revision | PASS |
| `compareFromToolbar` | Live main-toolbar comparison | PASS |
| `restoreFromToolbar` | Command registered; modal completion not automated | PARTIAL |
| `editSettings` | Live settings open/apply UI | PASS |
| `aboutProc` | Live About content/icon/close; external link launch not automated | PARTIAL |
| `showAbout` | Live centred About window | PASS |
| `setMenuItem` | Live exact four menu entries | PASS |
| `ensureConfigurationLoaded` | Live isolated INI/catalogue initialization | PASS |
| `createToolbarBitmap` | Live three main-toolbar icons | PASS |
| `registerConfiguredToolbarButtons` | Live three configured toolbar buttons | PASS |
| `removeMenuCommand` | Live resulting trimmed Plugins menu | PASS |
| `initialise` | Live plugin initialization/dock/menu/timer | PASS |
| `DllMain` | DLL loads in real Notepad++ | PASS |
| `setInfo` | ABI present and live Notepad++ initialization | PASS |
| `getName` | ABI present; plugin appears as NppHistory | PASS |
| `getFuncsArray` | ABI present; menu commands populated | PASS |
| `handleNotification` | Live ready/toolbar/file/save notifications; every lifecycle branch not individually fired | PARTIAL |
| `beNotified` | ABI present and live notifications received | PASS |
| `messageProc` | ABI present/live load | PASS |
| `isUnicode` | ABI present/live load | PASS |

## Interpretation

Every pure core function has direct or indirect automated evidence. Every plugin entry point and the principal pane/settings/comparison flows have live Notepad++ evidence. The PARTIAL rows identify interaction branches that should not be misrepresented as unit tested: confirmation dialogs, external URL launch, app shutdown/focus/tab lifecycle, missing-open-file alerts, and multi-file/custom-root installed-plugin workflows.
