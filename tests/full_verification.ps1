param(
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputRoot = Join-Path $projectRoot 'build\verification-beta25-rc'
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null

$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path $msbuild)) {
    $msbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
}
if (-not (Test-Path $msbuild)) { throw 'MSBuild was not found.' }

function Invoke-Build([string]$project, [string]$logName) {
    $arguments = @($project, '/t:Rebuild', "/p:Configuration=$Configuration",
        "/p:Platform=$Platform", '/m')
    $output = & $msbuild @arguments 2>&1
    $code = $LASTEXITCODE
    $output | Set-Content -LiteralPath (Join-Path $outputRoot $logName) -Encoding utf8
    if ($code -ne 0) { throw "Build failed for $project (exit $code)." }
}

Push-Location $projectRoot
try {
    Invoke-Build 'vs.proj\NppHistory.vcxproj' 'plugin-build.log'
    Invoke-Build 'vs.proj\NppHistoryUpdater.vcxproj' 'updater-build.log'
    Invoke-Build 'vs.proj\NppHistory.Tests.vcxproj' 'tests-build.log'

    $coreOutput = & '.\build\tests\NppHistory.Tests.exe' 2>&1
    $coreExit = $LASTEXITCODE
    $coreOutput | Set-Content -LiteralPath (Join-Path $outputRoot 'core-tests.log') -Encoding utf8
    if ($coreExit -ne 0) { throw "Core tests failed (exit $coreExit)." }
    $coreText = $coreOutput -join "`n"
    if ($coreText -notmatch 'core verification:\s+(\d+) checks,\s+(\d+) failures') {
        throw 'Core test summary could not be parsed.'
    }
    $coreChecks = [int]$Matches[1]
    $coreFailures = [int]$Matches[2]
    if ($coreFailures -ne 0) { throw "Core tests reported $coreFailures failures." }

    $updaterResult = & '.\tests\updater_smoke.ps1'
    if (-not $updaterResult.Passed) { throw 'The isolated restart-installer verification failed.' }

    $dll = Join-Path $projectRoot 'build\x64\Release\NppHistory.dll'
    $updater = Join-Path $projectRoot 'build\x64\Release\NppHistoryUpdater.exe'
    if (-not (Test-Path $updater)) { throw 'The restart installer was not built.' }
    $version = (Get-Item $dll).VersionInfo
    $versionHeader = [IO.File]::ReadAllText((Join-Path $projectRoot 'src\Version.h'))
    $displayVersionMatch = [regex]::Match($versionHeader,
        '#define NPPHISTORY_VERSION_TEXT "([^"]+)"')
    if (-not $displayVersionMatch.Success) { throw 'Display version was not found in Version.h.' }
    $expectedDisplayVersion = $displayVersionMatch.Groups[1].Value
    $expectedMetadata = [ordered]@{
        FileDescription = 'NppHistory'
        ProductName = 'NppHistory'
        FileVersion = $expectedDisplayVersion
        ProductVersion = $expectedDisplayVersion
        Comments = 'Automatic saving and continuous local revision history for Notepad++.'
    }
    foreach ($entry in $expectedMetadata.GetEnumerator()) {
        if ($version.($entry.Key) -ne $entry.Value) {
            throw "DLL metadata $($entry.Key) was '$($version.($entry.Key))'; expected '$($entry.Value)'."
        }
    }
    $vcRoot = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC'
    if (-not (Test-Path $vcRoot)) {
        $vcRoot = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC'
    }
    $dumpbin = Get-ChildItem $vcRoot -Filter dumpbin.exe -Recurse |
        Where-Object FullName -Match 'Hostx64\\x64\\dumpbin\.exe$' |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
    if (-not $dumpbin) { throw 'dumpbin.exe was not found.' }
    $exports = & $dumpbin /exports $dll 2>&1
    $exports | Set-Content -LiteralPath (Join-Path $outputRoot 'dll-exports.log') -Encoding utf8
    $requiredExports = @('setInfo','getName','getFuncsArray','beNotified','messageProc','isUnicode')
    $exportsText = $exports -join "`n"
    $missingExports = @($requiredExports | Where-Object { $exportsText -notmatch "\b$_\b" })
    if ($missingExports.Count -ne 0) {
        throw "Missing plugin exports: $($missingExports -join ', ')"
    }

    $runtimeResult = & '.\tests\runtime_smoke.ps1'
    $runtimeObject = @($runtimeResult | Where-Object {
        $_.PSObject.Properties.Name -contains 'Passed'
    }) | Select-Object -Last 1
    $runtimeResult | Format-List * | Out-String |
        Set-Content -LiteralPath (Join-Path $outputRoot 'runtime-tests.log') -Encoding utf8
    if (-not $runtimeObject -or -not $runtimeObject.Passed) {
        throw 'Live Notepad++ verification failed.'
    }

    $tabResults = @(
        & '.\tests\tab_indicators_smoke.ps1'
        & '.\tests\tab_indicators_smoke.ps1' -LargeTabs
        & '.\tests\tab_indicators_smoke.ps1' -DarkMode
        & '.\tests\tab_indicators_smoke.ps1' -LargeTabs -DarkMode
        & '.\tests\tab_indicators_smoke.ps1' -Vertical
        & '.\tests\tab_indicators_smoke.ps1' -Vertical -LargeTabs
        & '.\tests\tab_indicators_smoke.ps1' -Vertical -DarkMode
        & '.\tests\tab_indicators_smoke.ps1' -Vertical -LargeTabs -DarkMode
    )
    $tabResults | ConvertTo-Json -Depth 5 |
        Set-Content -LiteralPath (Join-Path $outputRoot 'tab-layout-tests.json') -Encoding utf8
    if ($tabResults.Count -ne 8 -or @($tabResults | Where-Object { -not $_.Passed }).Count) {
        throw 'Per-document tab spacing verification failed.'
    }

    $hash = Get-FileHash $dll -Algorithm SHA256
    $summary = [pscustomobject]@{
        Passed = $true
        CoreChecks = $coreChecks
        CoreFailures = $coreFailures
        DocumentTabLayoutConfigurations = $tabResults.Count
        DocumentTabReservedSpace = $runtimeObject.DocumentTabReservedSpacePassed
        RequiredExports = $requiredExports.Count
        LiveNotepadVerification = $runtimeObject.Passed
        AutomaticUpdateCheck = $runtimeObject.AutomaticUpdateCheckPassed
        ManualUpdateCheck = $runtimeObject.ManualUpdateCheckPassed
        RestartInstaller = $updaterResult.Passed
        InstallerRollbackProtection = $updaterResult.InvalidDigestRejected -and $updaterResult.ExistingDllPreserved
        UpdateFeedAccessible = $runtimeObject.UpdateFeedAccessible
        RevisionCommentUpdate = $runtimeObject.CommentUpdatePassed
        RevisionCommentUpdateLogged = $runtimeObject.CommentUpdateLogged
        RevisionDeletion = $runtimeObject.RevisionDeletionPassed
        RevisionDeletionLogged = $runtimeObject.RevisionDeletionLogged
        RevisionRestore = $runtimeObject.RestoreActionPassed
        RevisionRestoreLogged = $runtimeObject.RestoreLogged
        FileVersion = $version.FileVersion
        ProductVersion = $version.ProductVersion
        FileDescription = $version.FileDescription
        ProductName = $version.ProductName
        Comments = $version.Comments
        DllSha256 = $hash.Hash
        UpdaterSha256 = (Get-FileHash $updater -Algorithm SHA256).Hash
        EvidenceDirectory = $outputRoot
        RuntimeEvidenceDirectory = Split-Path $runtimeObject.HistoryPanelScreenshot -Parent
    }
    $summary | Format-List * | Out-String |
        Set-Content -LiteralPath (Join-Path $outputRoot 'summary.log') -Encoding utf8
    $summary
}
finally {
    Pop-Location
}
