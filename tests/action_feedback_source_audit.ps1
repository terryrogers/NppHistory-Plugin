# Source-only coverage guard. Does not compile code or start Notepad++.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$header = Get-Content -Raw -LiteralPath (Join-Path $root 'src/ActionFeedback.h')
$implementation = Get-Content -Raw -LiteralPath (Join-Path $root 'src/ActionFeedback.cpp')
$tests = Get-Content -Raw -LiteralPath (Join-Path $root 'tests/test_action_feedback.cpp')
$enum = [regex]::Match($header, '(?s)enum class ActionEvent\s*\{(.*?)\}').Groups[1].Value
$events = @($enum -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
foreach ($event in $events) {
    if ($implementation -notmatch ('case ActionEvent::' + $event + '\s*:')) { throw "Missing formatter: $event" }
    if ($tests -notmatch ('\{ActionEvent::' + $event + '\s*,')) { throw "Missing exact-wording/log-pair fixture: $event" }
}
$producers = (Get-Content -Raw -LiteralPath (Join-Path $root 'src/NppHistory.cpp')) +
    (Get-Content -Raw -LiteralPath (Join-Path $root 'src/HistoryPanel.cpp'))
foreach ($event in $events) {
    if ($producers -notmatch ('ActionEvent::' + $event + '\b')) { throw "Unwired event: $event" }
}
$direct = @(Get-ChildItem -LiteralPath (Join-Path $root 'src') -Filter '*.cpp' |
    Select-String -Pattern 'actionStatus\(\)\.show\(')
if ($direct.Count -ne 1 -or $direct[0].Filename -ne 'ActionFeedback.cpp') {
    throw 'Temporary feedback bypasses the paired log/status reporter'
}
$panel = Get-Content -Raw -LiteralPath (Join-Path $root 'src/HistoryPanel.cpp')
$compareInit = [regex]::Match($panel, '(?s)INT_PTR CALLBACK HistoryPanel::compareProc.*?if \(message == WM_INITDIALOG\)(.*?)return TRUE;').Groups[1].Value
if ($compareInit -notmatch 'reportAction\(ActionEvent::comparisonOpened') { throw 'Comparison-open feedback missing from initialization' }
if ($implementation -notmatch 'pluginLogger\(\)\.write\(feedback.level, feedback.message, detail\)') { throw 'Log does not use the paired message and severity' }
[pscustomobject]@{ SourceAudit='PASS'; EventKinds=$events.Count; ExactWordingFixtures=$events.Count; DirectStatusProducers=$direct.Count; Compiled=$false; RuntimeTested=$false }
