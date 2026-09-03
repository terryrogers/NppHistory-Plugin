# Read-only source audit: does not compile, start Notepad++, or use a built DLL.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$resource = Get-Content (Join-Path $root 'src/NppHistory.rc') -Raw
$header = Get-Content (Join-Path $root 'src/resource.h') -Raw
$settings = Get-Content (Join-Path $root 'src/Settings.cpp') -Raw
$ids = @{ IDOK=1; IDCANCEL=2 }
foreach($match in [regex]::Matches($header,'(?m)^#define\s+(\w+)\s+(\d+)')) {
    $ids[$match.Groups[1].Value] = [int]$match.Groups[2].Value
}
function Resolve-Id([string]$value) {
    if($value -match '^\d+$'){return [int]$value}
    if(!$ids.ContainsKey($value)){throw "Unknown resource ID: $value"}
    return $ids[$value]
}
$page = ($resource -split 'IDD_SETTINGS DIALOGEX',2)[1] -split 'IDD_EDIT_COMMENT DIALOGEX',2 | Select-Object -First 1
$inputs = [Collections.Generic.HashSet[int]]::new()
foreach($line in ($page -split '\r?\n')) {
    if($line -match '^\s*(?:EDITTEXT|COMBOBOX)\s+(\w+),') {
        [void]$inputs.Add((Resolve-Id $Matches[1]))
    } elseif($line -match '^\s*(?:DEFPUSHBUTTON|PUSHBUTTON)\s+"[^"]*",\s*(\w+),') {
        [void]$inputs.Add((Resolve-Id $Matches[1]))
    } elseif($line -match '^\s*CONTROL\s+"[^"]*",\s*(\w+),\s*"(Button|msctls_hotkey32)"') {
        [void]$inputs.Add((Resolve-Id $Matches[1]))
    }
}
$hints = [Collections.Generic.HashSet[int]]::new()
$staticHints = ($settings -split 'settingsTooltips = \{',2)[1] -split '\r?\n\};',2 | Select-Object -First 1
foreach($match in [regex]::Matches($staticHints,'\{(\w+), L"([^"]+)"\}')) {
    [void]$hints.Add((Resolve-Id $match.Groups[1].Value))
}
$toolbar = @(1071,1072,1223,1233,1243,1253,1073)
$enabled = @(1135,1138,1225,1235,1245,1255,1141)
$hotkeys = @(1136,1139,1226,1236,1246,1256,1142)
for($row=0;$row -lt 7;$row++) {
    foreach($id in @((1201+$row*10),(1202+$row*10),(1204+$row*10),$toolbar[$row],$enabled[$row],$hotkeys[$row])) {
        [void]$hints.Add($id)
    }
}
[void]$hints.Add($ids.IDC_CONTEXT_SUBMENU)
$missing = @($inputs | Where-Object {!$hints.Contains($_)})
$invalid = @($hints | Where-Object {!$inputs.Contains($_)})
if($missing.Count -or $invalid.Count) {throw "Tooltip coverage mismatch. Missing inputs: $missing. Non-input targets: $invalid."}
if($inputs.Count -ne 79){throw "Unexpected input count: $($inputs.Count); review changed resource controls."}
if($page -match 'LTEXT\s+"Plugins"|CTEXT\s+"Plugins"'){throw 'Plugins settings column is still present.'}
if(!$settings.Contains('isTooltipInputControl(GetDlgItem(dialog, entry.id))')){throw 'Tooltip registration type guard missing.'}
[pscustomobject]@{SourceAudit='PASS'; SettingsInputs=$inputs.Count; TooltipTargets=$hints.Count; MissingInputs=$missing.Count; NonInputTargets=$invalid.Count; Compiled=$false; RuntimeTested=$false}
