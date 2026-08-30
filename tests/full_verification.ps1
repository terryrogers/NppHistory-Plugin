param(
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputRoot = Join-Path $projectRoot 'build\verification-beta24'
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

    $dll = Join-Path $projectRoot 'build\x64\Release\NppHistory.dll'
    $version = (Get-Item $dll).VersionInfo
    $expectedMetadata = [ordered]@{
        FileDescription = 'NppHistory'
        ProductName = 'NppHistory'
        FileVersion = '0.2.0.24'
        ProductVersion = '0.2.0.24'
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

    $hash = Get-FileHash $dll -Algorithm SHA256
    $summary = [pscustomobject]@{
        Passed = $true
        CoreChecks = $coreChecks
        CoreFailures = $coreFailures
        RequiredExports = $requiredExports.Count
        LiveNotepadVerification = $runtimeObject.Passed
        FileVersion = $version.FileVersion
        ProductVersion = $version.ProductVersion
        FileDescription = $version.FileDescription
        ProductName = $version.ProductName
        Comments = $version.Comments
        DllSha256 = $hash.Hash
        EvidenceDirectory = $outputRoot
    }
    $summary | Format-List * | Out-String |
        Set-Content -LiteralPath (Join-Path $outputRoot 'summary.log') -Encoding utf8
    $summary
}
finally {
    Pop-Location
}
