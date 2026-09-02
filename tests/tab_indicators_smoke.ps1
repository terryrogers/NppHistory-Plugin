param(
    [string]$NotepadExe = 'C:\iCloud\iCloudDrive\Filing\N\Notepad++\notepad++.exe',
    [string]$PluginDll = "$PSScriptRoot\..\build\x64\Release\NppHistory.dll",
    [switch]$LargeTabs,
    [switch]$DarkMode,
    [switch]$Vertical
)
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $NotepadExe)) { $NotepadExe = 'C:\Program Files\Notepad++\notepad++.exe' }
Add-Type -AssemblyName System.Drawing
if (-not ('TabProbe' -as [type])) { Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class TabProbe {
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h, EnumProc p, IntPtr v);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p, IntPtr v);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
    [DllImport("user32.dll")] static extern IntPtr GetParent(IntPtr h);
    [DllImport("user32.dll",EntryPoint="GetWindowLongPtrW")] static extern IntPtr GetStyle(IntPtr h,int index);
    public static bool IsVertical(IntPtr tabs) { return (GetStyle(tabs,-16).ToInt64() & 0x80)!=0; }
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h,StringBuilder s,int n);
    [DllImport("user32.dll",EntryPoint="SendMessageW")] public static extern IntPtr Send(IntPtr h,uint m,IntPtr w,IntPtr l);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr h,string name);
    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint a,bool inherit,int pid);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr p);
    [DllImport("kernel32.dll")] static extern IntPtr VirtualAllocEx(IntPtr p,IntPtr a,UIntPtr n,uint type,uint protection);
    [DllImport("kernel32.dll")] static extern bool VirtualFreeEx(IntPtr p,IntPtr a,UIntPtr n,uint type);
    [DllImport("kernel32.dll")] static extern bool WriteProcessMemory(IntPtr p,IntPtr a,byte[] data,UIntPtr n,out UIntPtr written);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p,IntPtr a,byte[] data,UIntPtr n,out UIntPtr read);
    public static IntPtr Tabs(IntPtr main) {
        IntPtr found=IntPtr.Zero; long count=-1;
        EnumChildWindows(main,(h,p)=>{var s=new StringBuilder(128);GetClassName(h,s,128);
            if(GetParent(h)==main && s.ToString()=="SysTabControl32") {long c=Send(h,0x1304,IntPtr.Zero,IntPtr.Zero).ToInt64();if(c>count){found=h;count=c;}} return true;},IntPtr.Zero);
        return found;
    }
    public static IntPtr Main(int pid) {
        IntPtr result=IntPtr.Zero;
        EnumWindows((h,p)=>{uint owner;GetWindowThreadProcessId(h,out owner);var s=new StringBuilder(128);GetClassName(h,s,128);
            if(owner==pid && s.ToString()=="Notepad++"){result=h;return false;}return true;},IntPtr.Zero);
        return result;
    }
    // Messages above WM_USER do not marshal pointer arguments across processes.
    // TCITEM and RECT live in the disposable target process, never at a caller-local address.
    public static string[] Items(int pid,IntPtr tabs) {
        IntPtr p=OpenProcess(0x38,false,pid); if(p==IntPtr.Zero)throw new Exception("OpenProcess failed");
        IntPtr remote=VirtualAllocEx(p,IntPtr.Zero,(UIntPtr)4096,0x3000,4);
        if(remote==IntPtr.Zero){CloseHandle(p);throw new Exception("VirtualAllocEx failed");}
        try {var rows=new List<string>();int count=Send(tabs,0x1304,IntPtr.Zero,IntPtr.Zero).ToInt32();
            for(int i=0;i<count;i++) {
                byte[] item=new byte[40];Array.Copy(BitConverter.GetBytes((uint)9),0,item,0,4);
                Array.Copy(BitConverter.GetBytes(remote.ToInt64()+64),0,item,16,8);
                Array.Copy(BitConverter.GetBytes(1024),0,item,24,4); UIntPtr n;
                if(!WriteProcessMemory(p,remote,item,(UIntPtr)40,out n))throw new Exception("TCITEM write failed");
                if(Send(tabs,0x133c,(IntPtr)i,remote)==IntPtr.Zero)throw new Exception("TCM_GETITEM failed");
                byte[] text=new byte[2048];ReadProcessMemory(p,IntPtr.Add(remote,64),text,(UIntPtr)2048,out n);
                ReadProcessMemory(p,remote,item,(UIntPtr)40,out n);long key=BitConverter.ToInt64(item,32);
                if(Send(tabs,0x130a,(IntPtr)i,remote)==IntPtr.Zero)throw new Exception("TCM_GETITEMRECT failed");
                byte[] rect=new byte[16];ReadProcessMemory(p,remote,rect,(UIntPtr)16,out n);
                int extent=IsVertical(tabs)
                    ? BitConverter.ToInt32(rect,12)-BitConverter.ToInt32(rect,4)
                    : BitConverter.ToInt32(rect,8)-BitConverter.ToInt32(rect,0);
                rows.Add(Encoding.Unicode.GetString(text).Split('\0')[0]+"|"+extent+"|"+key);
            }return rows.ToArray();
        } finally {VirtualFreeEx(p,remote,UIntPtr.Zero,0x8000);CloseHandle(p);}
    }
}
'@
}
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ('..\build\tab-layout-' + [guid]::NewGuid().ToString('N'))))
$names = @('ordinary.md', 'sample.log', 'sample.tmp', 'sample.txt')
$results = @{}
foreach ($mode in @('baseline','excluded')) {
    $folder = Join-Path $root $mode
    $pluginFolder = Join-Path $folder 'plugins\NppHistory'
    $configFolder = Join-Path $folder 'plugins\Config\NppHistory'
    [IO.Directory]::CreateDirectory($pluginFolder) | Out-Null
    [IO.Directory]::CreateDirectory($configFolder) | Out-Null
    Copy-Item -LiteralPath $NotepadExe -Destination (Join-Path $folder 'notepad++.exe')
    Copy-Item -LiteralPath $PluginDll -Destination (Join-Path $pluginFolder 'NppHistory.dll')
    Copy-Item -Path (Join-Path (Split-Path $NotepadExe) '*.xml') -Destination $folder
    [IO.File]::WriteAllText((Join-Path $folder 'doLocalConf.xml'),'')
    # Configure native pin/close buttons explicitly; no unrelated personal settings are changed.
    $configPath = Join-Path $folder 'config.xml'
    [xml]$config = [IO.File]::ReadAllText($configPath)
    $tabConfig = $config.SelectSingleNode('//GUIConfig[@name="TabBar"]')
    if ($tabConfig) {
        $tabConfig.SetAttribute('closeButton','yes'); $tabConfig.SetAttribute('pinButton','yes')
        $tabConfig.SetAttribute('vertical',$(if($Vertical){'yes'}else{'no'})); $tabConfig.SetAttribute('multiLine','no')
        $tabConfig.SetAttribute('reduce',$(if($LargeTabs){'no'}else{'yes'}))
        $darkConfig = $config.SelectSingleNode('//GUIConfig[@name="DarkMode"]')
        if($darkConfig){$darkConfig.SetAttribute('enable',$(if($DarkMode){'yes'}else{'no'}))}
        $config.Save($configPath)
    }
    $enabled = if($mode -eq 'excluded'){1}else{0}
    [IO.File]::WriteAllText((Join-Path $configFolder 'NppHistory.ini'), @"
[NppHistory]
AutoSaveEnabled=$enabled
AutoSaveAfterEdit=0
AutoSaveAtIntervals=0
AutoSaveOnFocusLoss=0
AutoSaveOnTabChange=0
AutoSaveOnExit=0
HistoryEnabled=$enabled
HistoryBeforeSave=0
HistoryAfterSave=0
AutoSaveExclusions=*.log|*.txt
HistoryExclusions=*.tmp|*.txt
AutoUpdateEnabled=0
LoggingEnabled=0
"@)
    $files = foreach($name in $names) { $path=Join-Path $folder $name; [IO.File]::WriteAllText($path,'Tab layout UAT'); $path }
    $arguments = '-multiInst -nosession ' + (($files | ForEach-Object {'"'+$_+'"'}) -join ' ')
    $process = Start-Process -FilePath (Join-Path $folder 'notepad++.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds(15)
        do {
            Start-Sleep -Milliseconds 250
            $main = [TabProbe]::Main($process.Id)
            $tabs = if($main -ne [IntPtr]::Zero){[TabProbe]::Tabs($main)}else{[IntPtr]::Zero}
        } while($tabs -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
        if($tabs -eq [IntPtr]::Zero){throw 'Document tabs not found'}
        $deadline = [DateTime]::UtcNow.AddSeconds(15)
        while([TabProbe]::GetProp($main,'NppHistoryTabIndicatorIconCount').ToInt64() -ne 2 -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 100}
        if([TabProbe]::GetProp($main,'NppHistoryTabIndicatorIconCount').ToInt64() -ne 2){throw 'Plugin readiness timed out'}
        Start-Sleep -Milliseconds 200
        if([TabProbe]::IsVertical($tabs) -ne [bool]$Vertical){throw 'Native tab orientation does not match requested test configuration'}
        $expectedReserved = if($mode -eq 'excluded'){3}else{0}
        if([TabProbe]::GetProp($main,'NppHistoryReservedDocumentTabCount').ToInt64() -ne $expectedReserved){throw 'Reserved indicator geometry does not match excluded tabs'}
        $before = [TabProbe]::Items($process.Id,$tabs)
        for($cycle=0;$cycle -lt 4;$cycle++) { for($i=0;$i -lt 4;$i++) {
            [void][TabProbe]::Send($main,0x804,[IntPtr]::Zero,[IntPtr]$i)
        }}
        Start-Sleep -Milliseconds 300
        $after = [TabProbe]::Items($process.Id,$tabs)
        if(($before -join ';') -ne ($after -join ';')){throw "Tab state changed in ${mode}: before=$($before -join ';'); after=$($after -join ';')"}
        $rect = [TabProbe+Rect]::new()
        [void][TabProbe]::GetWindowRect($main,[ref]$rect)
        $bitmap = [Drawing.Bitmap]::new($rect.Right-$rect.Left,$rect.Bottom-$rect.Top)
        $graphics = [Drawing.Graphics]::FromImage($bitmap); $dc=$graphics.GetHdc()
        try {[void][TabProbe]::PrintWindow($main,$dc,2)} finally {$graphics.ReleaseHdc($dc);$graphics.Dispose()}
        $bitmap.Save((Join-Path $folder 'tabs.png')); $bitmap.Dispose()
        [void][TabProbe]::Send($main,0x111,[IntPtr]41001,[IntPtr]::Zero)
        Start-Sleep -Milliseconds 200
        $withNew = [TabProbe]::Items($process.Id,$tabs)
        if($withNew.Count -ne 5 -or (($withNew | Where-Object {$_ -notmatch '^new \d+\|'}) -join ';') -ne ($before -join ';')) {
            throw 'New document altered the existing tab names, widths, identities or ordering'
        }
        [void][TabProbe]::Send($main,0x111,[IntPtr]41003,[IntPtr]::Zero)
        Start-Sleep -Milliseconds 200
        $results[$mode] = $before | ForEach-Object { $v=$_ -split '\|'; [pscustomobject]@{Name=$v[0];Width=[int]$v[1]} }
    } finally {
        if(!$process.HasExited){$null=$process.CloseMainWindow();if(!$process.WaitForExit(3000)){$process.Kill();$process.WaitForExit()}}
    }
}
$deltas = for($i=0;$i -lt 4;$i++) {
    if($results.baseline[$i].Name -ne $names[$i] -or $results.excluded[$i].Name -ne $names[$i]){throw 'Canonical filename changed'}
    [pscustomobject]@{Name=$names[$i];Baseline=$results.baseline[$i].Width;Excluded=$results.excluded[$i].Width;Extra=$results.excluded[$i].Width-$results.baseline[$i].Width}
}
$passed = $deltas[0].Extra -eq 0 -and $deltas[1].Extra -gt 0 -and $deltas[2].Extra -gt 0 -and $deltas[3].Extra -gt [Math]::Max($deltas[1].Extra,$deltas[2].Extra)
[pscustomobject]@{Passed=$passed;Tabs=$deltas;EvidenceDirectory=$root;StableRefreshAndNewDocument=$true;LargeTabs=[bool]$LargeTabs;DarkMode=[bool]$DarkMode;Vertical=[bool]$Vertical}
if(!$passed){throw 'Per-tab indicator width test failed'}
