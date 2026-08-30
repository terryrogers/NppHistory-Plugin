param(
    [string]$UpdaterExe = "$PSScriptRoot\..\build\x64\Release\NppHistoryUpdater.exe",
    [string]$InstalledDll = "$PSScriptRoot\..\build\x64\Release\NppHistory.dll",
    [string]$ReplacementFile = "$PSScriptRoot\..\build\tests\NppHistory.Tests.exe"
)

$ErrorActionPreference = 'Stop'
$testRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ('..\build\updater-test-' + [Guid]::NewGuid().ToString('N'))))
$successRoot = Join-Path $testRoot 'success'
$failureRoot = Join-Path $testRoot 'failure'

function Invoke-TestUpdater([string]$folder, [string]$expectedHash) {
    $arguments = @('--wait-pid','4294967294',
        '--source',(Join-Path $folder 'NppHistory.pending.dll'),
        '--target',(Join-Path $folder 'NppHistory.dll'),
        '--restart',([IO.Path]::GetFullPath($UpdaterExe)),
        '--result',(Join-Path $folder 'result.ini'),
        '--version','v-updater-test','--sha256',$expectedHash)
    Start-Process -FilePath $UpdaterExe -ArgumentList $arguments -Wait -PassThru
}

try {
    [IO.Directory]::CreateDirectory($successRoot) | Out-Null
    [IO.Directory]::CreateDirectory($failureRoot) | Out-Null
    foreach ($folder in @($successRoot, $failureRoot)) {
        Copy-Item -LiteralPath $InstalledDll -Destination (Join-Path $folder 'NppHistory.dll')
        Copy-Item -LiteralPath $ReplacementFile -Destination (Join-Path $folder 'NppHistory.pending.dll')
    }
    $replacementHash = (Get-FileHash (Join-Path $successRoot 'NppHistory.pending.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
    $oldFailureHash = (Get-FileHash (Join-Path $failureRoot 'NppHistory.dll') -Algorithm SHA256).Hash
    $successProcess = Invoke-TestUpdater $successRoot $replacementHash
    $failureProcess = Invoke-TestUpdater $failureRoot ('0' * 64)
    Start-Sleep -Milliseconds 300
    $successResult = Get-Content (Join-Path $successRoot 'result.ini') -Raw
    $failureResult = Get-Content (Join-Path $failureRoot 'result.ini') -Raw
    $passed = $successProcess.ExitCode -eq 0 -and
        (Get-FileHash (Join-Path $successRoot 'NppHistory.dll') -Algorithm SHA256).Hash.ToLowerInvariant() -eq $replacementHash -and
        (Test-Path (Join-Path $successRoot 'NppHistory.previous.dll')) -and
        $successResult.Contains('Status=success') -and
        $failureProcess.ExitCode -ne 0 -and
        (Get-FileHash (Join-Path $failureRoot 'NppHistory.dll') -Algorithm SHA256).Hash -eq $oldFailureHash -and
        $failureResult.Contains('Status=failure') -and
        $failureResult.Contains('SHA-256')
    [pscustomobject]@{
        Passed = $passed
        SuccessfulReplacement = $successProcess.ExitCode -eq 0
        BackupCreated = Test-Path (Join-Path $successRoot 'NppHistory.previous.dll')
        InvalidDigestRejected = $failureProcess.ExitCode -ne 0
        ExistingDllPreserved = (Get-FileHash (Join-Path $failureRoot 'NppHistory.dll') -Algorithm SHA256).Hash -eq $oldFailureHash
    }
}
finally {
    if ([IO.Directory]::Exists($testRoot)) { [IO.Directory]::Delete($testRoot, $true) }
}
