param([string]$NotepadExe = 'C:\iCloud\iCloudDrive\Filing\N\Notepad++\notepad++.exe')
$ErrorActionPreference = 'Stop'
# Reuse the established read-only Win32 test helpers without running that workflow.
if (-not ('NppHistoryNative' -as [type])) {
    $source = Get-Content -LiteralPath "$PSScriptRoot\runtime_smoke.ps1" -Raw
    $native = [regex]::Match($source, "(?s)Add-Type @'\r?\n(.*?)\r?\n'@")
    if (!$native.Success) { throw 'Native test helpers not found' }
    Add-Type $native.Groups[1].Value
}
if (-not ('CommandProbe' -as [type])) {
Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class CommandProbe {
    [StructLayout(LayoutKind.Sequential)] struct MENUITEMINFO {
        public uint cbSize,fMask,fType,fState,wID;
        public IntPtr hSubMenu,hbmpChecked,hbmpUnchecked,dwItemData,dwTypeData;
        public uint cch; public IntPtr hbmpItem;
    }
    [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
    public class Entry { public string Text; public uint Id; public bool Enabled, Separator, Icon; public IntPtr Sub; }
    delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p,IntPtr v);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h,StringBuilder s,int n);
    [DllImport("user32.dll")] static extern IntPtr GetMenu(IntPtr h);
    [DllImport("user32.dll")] static extern int GetMenuItemCount(IntPtr h);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern bool GetMenuItemInfo(IntPtr h,uint p,bool byPosition,ref MENUITEMINFO i);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetMenuString(IntPtr h,uint p,StringBuilder s,int n,uint flags);
    public static Entry[] Entries(IntPtr menu) {
        var result=new List<Entry>();
        for(uint i=0;i<GetMenuItemCount(menu);i++) {
            var m=new MENUITEMINFO(); m.cbSize=(uint)Marshal.SizeOf(m);m.fMask=0x187;
            if(!GetMenuItemInfo(menu,i,true,ref m))throw new Exception("Menu read failed");
            var label=new StringBuilder(256);GetMenuString(menu,i,label,256,0x400);
            result.Add(new Entry{Text=label.ToString(),Id=m.wID,Enabled=(m.fState&3)==0,
                Separator=(m.fType&0x800)!=0,Icon=m.hbmpItem!=IntPtr.Zero,Sub=m.hSubMenu});
        }
        return result.ToArray();
    }
    static IntPtr Find(IntPtr menu,string text) {
        foreach(var e in Entries(menu)) {
            if(e.Text.Replace("&","")==text)return e.Sub;
            if(e.Sub!=IntPtr.Zero){var found=Find(e.Sub,text);if(found!=IntPtr.Zero)return found;}
        }return IntPtr.Zero;
    }
    public static IntPtr PluginMenu(IntPtr window) {return Find(GetMenu(window),"NppHistory");}
    public static IntPtr Popup(uint pid) {
        IntPtr result=IntPtr.Zero;
        EnumWindows(delegate(IntPtr h,IntPtr p){uint owner;GetWindowThreadProcessId(h,out owner);
            var name=new StringBuilder(128);GetClassName(h,name,128);
            if(owner==pid&&name.ToString()=="#32768") {result=SendMessage(h,0x1E1,IntPtr.Zero,IntPtr.Zero);return false;}
            return true;},IntPtr.Zero);return result;
    }
}
'@
}
Add-Type -AssemblyName System.Drawing
$root = [IO.Path]::GetFullPath("$PSScriptRoot\..\build\commands-$([guid]::NewGuid().ToString('N'))")
[IO.Directory]::CreateDirectory($root) | Out-Null
Copy-Item -LiteralPath $NotepadExe -Destination "$root\notepad++.exe"
Copy-Item -Path "$(Split-Path $NotepadExe)\*.xml" -Destination $root
[IO.File]::WriteAllText("$root\doLocalConf.xml",'<!-- isolated test -->')
[IO.Directory]::CreateDirectory("$root\plugins\NppHistory") | Out-Null
[IO.Directory]::CreateDirectory("$root\plugins\Config\NppHistory") | Out-Null
Copy-Item -LiteralPath "$PSScriptRoot\..\build\x64\Release\NppHistory.dll" -Destination "$root\plugins\NppHistory\NppHistory.dll"
$names = @('Capture','Compare','Restore','History','Refresh','Settings','About')
$paneIds = @(1006,1004,1005,1153,1003,1007,1008)
$ini = "[NppHistory]`r`nAutoSaveEnabled=0`r`nAutoUpdateEnabled=0`r`nHistoryBeforeSave=0`r`nHistoryAfterSave=0`r`nLoggingEnabled=1`r`nLogLevel=3`r`n"
for($i=0;$i -lt $names.Count;$i++) {
    $name=$names[$i]
    $ini += "Toolbar$name=1`r`nContext$name=1`r`nHotkey$($name)Enabled=1`r`nHotkey$($name)Ctrl=1`r`nHotkey$($name)Alt=1`r`nHotkey$($name)Shift=1`r`nHotkey$($name)Key=$([int]49+$i)`r`n"
}
[IO.File]::WriteAllText("$root\plugins\Config\NppHistory\NppHistory.ini",$ini)
[IO.File]::WriteAllText("$root\commands.txt",'Original command test')
$script:checks=0
function Assert($condition,[string]$description) {
    $script:checks++
    if(!$condition){throw "FAIL: $description (evidence $root)"}
}
function Wait-Window([string]$title) {
    for($i=0;$i -lt 50;$i++) {
        $h=[NppHistoryNative]::FindTopWindowContaining([uint32]$process.Id,$title)
        if($h -ne [IntPtr]::Zero){return $h};Start-Sleep -Milliseconds 100
    }throw "Window not found: $title"
}
function Open-Settings {
    [NppHistoryNative]::BeginCommand($main,$ids[5],[IntPtr]::Zero)
    $window=Wait-Window 'NppHistory Settings'
    Start-Sleep -Milliseconds 400
    [void][NppHistoryNative]::UpdateWindow($window)
    return $window
}
function Click-Control($window,[int]$id) {
    [void][NppHistoryNative]::SendMessage([NppHistoryNative]::FindControl($window,$id),0xF5,[IntPtr]::Zero,[IntPtr]::Zero)
}
function Snapshot($window,[string]$file) {
    $r=[NppHistoryNative+RECT]::new();[void][NppHistoryNative]::GetWindowRect($window,[ref]$r)
    $b=[Drawing.Bitmap]::new($r.Right-$r.Left,$r.Bottom-$r.Top);$g=[Drawing.Graphics]::FromImage($b);$dc=$g.GetHdc()
    try {[void][NppHistoryNative]::PrintWindow($window,$dc,2)}finally{$g.ReleaseHdc($dc);$g.Dispose()}
    $b.Save("$root\$file");$b.Dispose()
}
function Read-Context([bool]$submenu) {
    [void][NppHistoryNative]::PostMessage($main,0x7B,$editor,[IntPtr](-1))
    $popup=[IntPtr]::Zero
    for($j=0;$j -lt 40;$j++){$popup=[CommandProbe]::Popup([uint32]$process.Id);if($popup -ne [IntPtr]::Zero){break};Start-Sleep -Milliseconds 50}
    Assert ($popup -ne [IntPtr]::Zero) 'document context menu opens'
    $items=@([CommandProbe]::Entries($popup));$group=@($items | Where-Object Text -eq 'NppHistory')
    if($submenu){Assert ($group.Count -eq 1) 'exactly one NppHistory submenu';$items=@([CommandProbe]::Entries($group[0].Sub))}
    else {Assert ($group.Count -eq 0) 'inline mode has no NppHistory submenu'}
    $our=@($items | Where-Object {$_.Id -in $ids})
    if(!$submenu -and $our.Count){
        $first=[array]::IndexOf($items,$our[0]);$last=[array]::IndexOf($items,$our[-1])
        Assert ($items[$first-1].Separator -and $items[$last+1].Separator) 'inline commands between two separators'
    }
    [void][NppHistoryNative]::PostMessage($main,0x1F,[IntPtr]::Zero,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    return $our
}
$process=Start-Process -FilePath "$root\notepad++.exe" -ArgumentList @('-multiInst','-nosession',('"'+"$root\commands.txt"+'"')) -WindowStyle Hidden -PassThru
try {
    $main=[IntPtr]::Zero
    for($i=0;$i -lt 80;$i++){$main=[NppHistoryNative]::FindMainWindow([uint32]$process.Id);if($main -ne [IntPtr]::Zero -and [CommandProbe]::PluginMenu($main) -ne [IntPtr]::Zero){break};Start-Sleep -Milliseconds 100}
    Start-Sleep -Milliseconds 750
    $editor=[NppHistoryNative]::FindScintillaWithContent($main)
    $menu=[CommandProbe]::PluginMenu($main);$entries=@([CommandProbe]::Entries($menu));$ids=@($entries | ForEach-Object {[int]$_.Id})
    $actualOrder=($entries.Text | ForEach-Object {($_ -split "`t")[0]}) -join '|'
    if(!$actualOrder){Snapshot $main 'startup-failure.png';Write-Output "Main=$main; Exited=$($process.HasExited); Native=$([NppHistoryNative]::PluginMenuLabels($main,'NppHistory'))"}
    Assert ($actualOrder -eq ($names -join '|')) "Plugins menu common order: $actualOrder"
    Assert ($entries.Count -eq 7 -and @($entries | Where-Object Icon).Count -eq 7) 'all seven menu icons'
    Assert (@($entries | Where-Object {$_.Text.Contains("`t")}).Count -eq 7) 'all seven native shortcuts displayed'
    Assert (([NppHistoryNative]::GetProp($main,'NppHistoryToolbarButtonsRegistered')).ToInt64() -eq 8) 'all seven toolbar commands registered'
    $toolbar=[NppHistoryNative]::FindDescendant($main,'ToolbarWindow32')
    $positions=@($ids | ForEach-Object {[NppHistoryNative]::SendMessage($toolbar,0x419,[IntPtr]$_,[IntPtr]::Zero).ToInt32()})
    Assert (@($positions | Where-Object {$_ -lt 0}).Count -eq 0 -and ($positions -join ',') -eq (($positions | Sort-Object) -join ',')) 'toolbar common order'
    $context=@(Read-Context $true)
    Assert (($context.Text -join '|') -eq ($entries.Text -join '|')) 'submenu order and shortcuts match Plugins menu'
    Assert (@($context | Where-Object Icon).Count -eq 7) 'all context icons'
    Assert (!$context[1].Enabled -and !$context[2].Enabled) 'no revisions disables Compare and Restore'
    [void][NppHistoryNative]::SendMessage($main,0x111,[IntPtr]$ids[0],[IntPtr]::Zero)
    [void][NppHistoryNative]::SendMessage($main,0x111,[IntPtr]$ids[3],[IntPtr]::Zero)
    $panel=[NppHistoryNative]::FindControl($main,1002)
    $panel=[NppHistoryNative]::GetParent($panel)
    Assert ($panel -ne [IntPtr]::Zero) 'History command opens pane'
    $buttons=@(foreach($id in $paneIds){$h=[NppHistoryNative]::FindControl($panel,$id);$r=[NppHistoryNative+RECT]::new();[void][NppHistoryNative]::GetWindowRect($h,[ref]$r);[pscustomobject]@{Id=$id;Top=$r.Top;Left=$r.Left;Visible=[NppHistoryNative]::IsWindowVisible($h)}})
    Assert ((($buttons | Sort-Object Top,Left).Id -join ',') -eq ($paneIds -join ',')) 'pane common order'
    Assert (@($buttons | Where-Object Visible).Count -eq 7) 'all pane commands initially visible'
    $context=@(Read-Context $true)
    Assert (!$context[1].Enabled -and !$context[2].Enabled) 'visible pane without selection disables Compare and Restore'
    $list=[NppHistoryNative]::FindControl($panel,1002)
    [NppHistoryNative]::BeginLeftClick($list,35,30)
    Start-Sleep -Milliseconds 200
    $context=@(Read-Context $true)
    foreach($i in @(1,2)) {
        Assert ($context[$i].Enabled -and [NppHistoryNative]::MenuCommandEnabled($main,$ids[$i]) -and [NppHistoryNative]::ToolbarCommandEnabled($main,$ids[$i])) 'selected revision enables Compare/Restore on every command surface'
    }
    $settings=Open-Settings
    Assert ([NppHistoryNative]::Text([NppHistoryNative]::FindControl($settings,1145)) -eq 'No hotkey conflicts.') "hotkey status: $([NppHistoryNative]::Text([NppHistoryNative]::FindControl($settings,1145)))"
    $restoreKey=[NppHistoryNative]::FindControl($settings,1226)
    [void][NppHistoryNative]::SendMessage($restoreKey,0x401,[IntPtr]::Zero,[IntPtr]::Zero)
    [void][NppHistoryNative]::SendMessage($settings,0x111,[IntPtr](1226 -bor (0x300 -shl 16)),$restoreKey)
    Assert ([NppHistoryNative]::Text([NppHistoryNative]::FindControl($settings,1145)) -eq 'Restore needs a key.') 'new command requires a complete enabled shortcut'
    [void][NppHistoryNative]::SendMessage($restoreKey,0x401,[IntPtr]0x0733,[IntPtr]::Zero)
    [void][NppHistoryNative]::SendMessage($settings,0x111,[IntPtr](1226 -bor (0x300 -shl 16)),$restoreKey)
    Snapshot $settings 'commands-settings.png'
    # Every Plugins checkbox is checked and locked; other placements are editable.
    for($row=0;$row -lt 7;$row++) {
        $h=[NppHistoryNative]::FindControl($settings,(1202+$row*10))
        Assert (![NppHistoryNative]::IsWindowEnabled($h) -and [NppHistoryNative]::SendMessage($h,0xF0,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() -eq 1) 'Plugins placement locked on'
    }
    Click-Control $settings 1151
    Click-Control $settings 1201 # hide Capture in pane only
    Click-Control $settings 1254 # hide About in context only
    Click-Control $settings 1
    Start-Sleep -Milliseconds 200
    Assert (![NppHistoryNative]::IsWindowVisible([NppHistoryNative]::FindControl($panel,1006))) 'pane placement applied immediately'
    $context=@(Read-Context $false)
    Assert (($context.Text -join '|') -eq (($entries | Select-Object -First 6).Text -join '|')) 'inline context filters only the disabled command'
    Assert (([CommandProbe]::Entries($menu)).Count -eq 7) 'Plugins menu remains complete'
    $again=@(Read-Context $false)
    Assert ($again.Count -eq 6) 'reopening context does not duplicate commands'
    # Dispatch the ID read from the actual popup through the host's command route.
    # Physical mouse selection remains installed-environment UAT.
    [void][NppHistoryNative]::SendMessage($main,0x111,[IntPtr]$again[4].Id,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $log=Get-Content "$root\plugins\Config\NppHistory\NppHistory.log" -Raw
    Assert ($log.Contains('[INFO] Refresh')) 'Refresh ID from context popup dispatches and logs correctly'
    [void][NppHistoryNative]::SendMessage($main,0x111,[IntPtr]41001,[IntPtr]::Zero)
    $context=@(Read-Context $false)
    foreach($i in @(0,1,2,4)) {
        Assert (!$context[$i].Enabled -and ![NppHistoryNative]::MenuCommandEnabled($main,$ids[$i]) -and ![NppHistoryNative]::ToolbarCommandEnabled($main,$ids[$i])) 'unsaved document disables file actions everywhere'
    }
    $settings=Open-Settings
    for($row=0;$row -lt 7;$row++){if($row -ne 5){Click-Control $settings (1204+$row*10)}}
    Click-Control $settings 1
    Start-Sleep -Milliseconds 200
    $context=@(Read-Context $false)
    Assert ($context.Count -eq 0) 'all context placements can be disabled'
    $settings=Open-Settings
    Click-Control $settings 1201
    Click-Control $settings 2
    Start-Sleep -Milliseconds 150
    Assert (![NppHistoryNative]::IsWindowVisible([NppHistoryNative]::FindControl($panel,1006))) 'Cancel discards placement edits'
    $settings=Open-Settings
    for($row=1;$row -lt 7;$row++){Click-Control $settings (1201+$row*10)}
    Click-Control $settings 1
    Start-Sleep -Milliseconds 150
    Assert (@($paneIds | Where-Object {[NppHistoryNative]::IsWindowVisible([NppHistoryNative]::FindControl($panel,$_))}).Count -eq 0) 'all pane buttons can be hidden without removing Plugins menu access'
    [pscustomobject]@{Passed=$true;Checks=$script:checks;EvidenceDirectory=$root}
} finally {
    if(!$process.HasExited){[void][NppHistoryNative]::PostMessage($main,0x1F,[IntPtr]::Zero,[IntPtr]::Zero);Stop-Process -Id $process.Id -Force}
}
