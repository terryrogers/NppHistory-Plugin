param([string]$NotepadExe = 'C:\iCloud\iCloudDrive\Filing\N\Notepad++\notepad++.exe', [switch]$WithInstalledToolbarPlugins, [string]$CaseFilter = '', [string]$PluginDll = "$PSScriptRoot\..\build\x64\Release\NppHistory.dll")
$ErrorActionPreference='Stop'
$native=[regex]::Match((Get-Content "$PSScriptRoot\runtime_smoke.ps1" -Raw),"(?s)Add-Type @'\r?\n(.*?)\r?\n'@")
if (-not ('NppHistoryNative' -as [type])) { Add-Type $native.Groups[1].Value }
$hostRoot=Split-Path $NotepadExe
$root=[IO.Path]::GetFullPath("$PSScriptRoot\..\build\toolbar-geometry-$([guid]::NewGuid().ToString('N'))")
[IO.Directory]::CreateDirectory($root) | Out-Null
$results=@()
$cases=@('History-standard','History-small','History-large','History-small2','History-large2')
if ($WithInstalledToolbarPlugins) { $cases += @('Customize','Search','Customize-Search','History-Customize-Search') }
if ($CaseFilter) { $cases = @($cases | Where-Object { $_ -eq $CaseFilter }) }
foreach ($case in $cases) {
    $folder=Join-Path $root $case
    [IO.Directory]::CreateDirectory("$folder\plugins\Config\NppHistory") | Out-Null
    Copy-Item -LiteralPath $NotepadExe -Destination "$folder\notepad++.exe"
    Copy-Item -Path "$hostRoot\*.xml" -Destination $folder
    $style=($case -split '-')[-1]
    $styles=@('small','large','small2','large2','standard')
    if ($style -in $styles) {
        [xml]$config=Get-Content -LiteralPath "$folder\config.xml" -Raw
        $toolbarConfig=$config.SelectSingleNode('//GUIConfig[@name="ToolBar"]')
        $toolbarConfig.InnerText=$style
        $toolbarConfig.SetAttribute('visible','yes')
        $themeConfig=$config.SelectSingleNode('//GUIConfig[@name="DarkMode"]')
        if ($themeConfig) {
            $themeConfig.SetAttribute('enable','no')
            $themeConfig.SetAttribute('enableWindowsMode','no')
            $themeConfig.SetAttribute('lightToolBarIconSet',[string][array]::IndexOf($styles,$style))
        }
        $config.Save("$folder\config.xml")
    }
    [IO.File]::WriteAllText("$folder\doLocalConf.xml",'<!-- isolated toolbar regression -->')
    [IO.File]::WriteAllText("$folder\toolbar-test.txt",'Disposable toolbar regression file.')
    if ($case.Contains('History')) {
        [IO.Directory]::CreateDirectory("$folder\plugins\NppHistory") | Out-Null
        Copy-Item -LiteralPath $PluginDll -Destination "$folder\plugins\NppHistory\NppHistory.dll"
        [IO.File]::WriteAllText("$folder\plugins\Config\NppHistory\NppHistory.ini","[NppHistory]`r`nAutoSaveEnabled=0`r`nAutoUpdateEnabled=0`r`nLoggingEnabled=0`r`nHistoryBeforeSave=0`r`nHistoryAfterSave=0`r`nToolbarCapture=1`r`nToolbarCompare=1`r`nToolbarRestore=1`r`nToolbarHistory=1`r`nToolbarRefresh=0`r`nToolbarSettings=0`r`nToolbarAbout=0`r`n")
    }
    if ($case.Contains('Customize')) {
        [IO.Directory]::CreateDirectory("$folder\plugins\_CustomizeToolbar") | Out-Null
        Copy-Item -LiteralPath "$hostRoot\plugins\_CustomizeToolbar\_CustomizeToolbar.dll" -Destination "$folder\plugins\_CustomizeToolbar\_CustomizeToolbar.dll"
        Copy-Item -LiteralPath "$hostRoot\plugins\Config\CustomizeToolbar.dat" -Destination "$folder\plugins\Config\CustomizeToolbar.dat"
    }
    if ($case.Contains('Search')) {
        [IO.Directory]::CreateDirectory("$folder\plugins\NppMenuSearch") | Out-Null
        Copy-Item -LiteralPath "$hostRoot\plugins\NppMenuSearch\NppMenuSearch.dll" -Destination "$folder\plugins\NppMenuSearch\NppMenuSearch.dll"
        Copy-Item -LiteralPath "$hostRoot\plugins\Config\NppMenuSearch.xml" -Destination "$folder\plugins\Config\NppMenuSearch.xml"
    }
    $process=Start-Process -FilePath "$folder\notepad++.exe" -ArgumentList @('-multiInst','-nosession',('"'+"$folder\toolbar-test.txt"+'"')) -WindowStyle Hidden -PassThru
    try {
        $main=[IntPtr]::Zero
        for($i=0;$i -lt 50;$i++) {
            $main=[NppHistoryNative]::FindMainWindow([uint32]$process.Id)
            if ($main -ne [IntPtr]::Zero) { break }
            Start-Sleep -Milliseconds 100
        }
        Start-Sleep -Milliseconds 2200
        $toolbar=[NppHistoryNative]::FindDescendant($main,'ToolbarWindow32')
        if ($toolbar -eq [IntPtr]::Zero) { throw "Toolbar not found for $case" }
        $r=[NppHistoryNative+RECT]::new()
        [void][NppHistoryNative]::GetWindowRect($toolbar,[ref]$r)
        $size=[NppHistoryNative]::SendMessage($toolbar,0x43A,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64()
        $height=$r.Bottom-$r.Top
        $minimumHeight=$height
        # Exercise the host's layout path after startup, not only settled creation.
        for ($iteration=0; $iteration -lt 8; $iteration++) {
            [void][NppHistoryNative]::SendMessage($main,5,[IntPtr]::Zero,[IntPtr]::Zero)
            Start-Sleep -Milliseconds 180
            [void][NppHistoryNative]::GetWindowRect($toolbar,[ref]$r)
            $minimumHeight=[Math]::Min($minimumHeight,$r.Bottom-$r.Top)
        }
        $buttonHeight=($size -shr 16) -band 65535
        $styleMatches=$true
        if ($style -in $styles) {
            $styleMatches=[NppHistoryNative]::SendMessage($main,2142,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -eq [array]::IndexOf($styles,$style)
        }
        $loaded=@($process.Modules | Where-Object {$_.FileName.StartsWith($folder+'\plugins\')} | ForEach-Object ModuleName)
        $results += [pscustomobject]@{Case=$case;ToolbarHeight=$height;MinimumHeight=$minimumHeight;ButtonHeight=$buttonHeight;Fits=($buttonHeight -ge 16 -and $minimumHeight -ge $buttonHeight -and $styleMatches);Loaded=$loaded;Evidence=$folder}
    } finally {
        if (!$process.HasExited) { Stop-Process -Id $process.Id -Force }
    }
}
$results | ConvertTo-Json -Depth 3
if (@($results | Where-Object {!$_.Fits}).Count) { throw 'Toolbar geometry regression detected; see case results above.' }
