param(
    [string]$NotepadExe = '',
    [string]$PluginDll = "$PSScriptRoot\..\build\x64\Release\NppHistory.dll",
    [switch]$SkipAutomaticUpdateWait,
    [switch]$SettingsOnly,
    [switch]$CompareThenSettings
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($NotepadExe)) {
    $runningNotepad = Get-Process -Name 'notepad++' -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and (Test-Path -LiteralPath $_.Path) } |
        Select-Object -First 1
    $notepadCandidates = @(
        if ($runningNotepad) { $runningNotepad.Path }
        'C:\Program Files\Notepad++\notepad++.exe'
        'C:\iCloud\iCloudDrive\Filing\N\Notepad++\notepad++.exe'
    )
    $NotepadExe = $notepadCandidates | Where-Object {
        $_ -and (Test-Path -LiteralPath $_)
    } | Select-Object -First 1
}
if (-not $NotepadExe) { throw 'A Notepad++ executable could not be located.' }
$testRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ('..\build\runtime-autosave-test-' + [Guid]::NewGuid().ToString('N'))))
[IO.Directory]::CreateDirectory($testRoot) | Out-Null
[IO.File]::WriteAllText((Join-Path $testRoot 'doLocalConf.xml'), '<!-- isolated portable test -->')
$notePath = Join-Path $testRoot ("test-note-{0}.txt" -f [Guid]::NewGuid().ToString('N'))
$initialText = "common line`r`nrevision only`r`nanchor line`r`nold wording`r`n"
$initialText += (1..120 | ForEach-Object { "unchanged line {0:D3}`r`n" -f $_ }) -join ''
[IO.File]::WriteAllText($notePath, $initialText)
$normalTabPaths = 1..8 | ForEach-Object {
    $normalTabPath = Join-Path $testRoot ("normal-tab-{0:D2}.md" -f $_)
    [IO.File]::WriteAllText($normalTabPath, "Normal non-excluded tab $_")
    $normalTabPath
}
Copy-Item -LiteralPath $NotepadExe -Destination (Join-Path $testRoot 'notepad++.exe') -Force
Copy-Item -Path (Join-Path (Split-Path $NotepadExe) '*.xml') -Destination $testRoot -Force

$pluginFolder = Join-Path $testRoot 'plugins\NppHistory'
$configFolder = Join-Path $testRoot 'plugins\Config\NppHistory'
[IO.Directory]::CreateDirectory($pluginFolder) | Out-Null
[IO.Directory]::CreateDirectory($configFolder) | Out-Null
Copy-Item -LiteralPath $PluginDll -Destination (Join-Path $pluginFolder 'NppHistory.dll') -Force
[IO.File]::WriteAllText((Join-Path $configFolder 'NppHistory.ini'), @"
[NppHistory]
AutoSaveEnabled=1
Mode=0
AutoSaveAfterEdit=1
AfterEditSeconds=10
PeriodicSeconds=300
AutoSaveOnFocusLoss=0
AutoSaveAtIntervals=0
IntervalMinutes=10
AutoSaveOnTabChange=0
AutoSaveOnExit=0
AutoSaveScope=1
ToolbarCapture=1
ToolbarCompare=1
ToolbarHistory=1
HotkeyCaptureEnabled=1
HotkeyCaptureCtrl=1
HotkeyCaptureAlt=1
HotkeyCaptureShift=1
HotkeyCaptureKey=67
HotkeyCompareEnabled=1
HotkeyCompareCtrl=1
HotkeyCompareAlt=1
HotkeyCompareShift=1
HotkeyCompareKey=77
HotkeyHistoryEnabled=1
HotkeyHistoryCtrl=1
HotkeyHistoryAlt=1
HotkeyHistoryShift=1
HotkeyHistoryKey=72
AutoUpdateEnabled=1
UpdateFrequency=1
IncludePrereleaseUpdates=1
HistoryEnabled=1
HistoryBeforeSave=1
HistoryAfterSave=1
HistoryBeforeRestore=1
LoggingEnabled=1
LogLevel=3
LogLocationMode=0
LogMaximumSizeMb=5
LogRolloverMode=1
LogArchivesToRetain=5
"@)

$updateFeedAccessible = $false
$expectedPublishedVersion = ''
$expectedUpdateStatus = ''
$versionHeader = [IO.File]::ReadAllText((Join-Path $PSScriptRoot '..\src\Version.h'))
$displayVersionMatch = [regex]::Match($versionHeader, 'NPPHISTORY_VERSION_TEXT\s+"([^"]+)"')
$releaseDateMatch = [regex]::Match($versionHeader, 'NPPHISTORY_RELEASE_DATE\s+"([^"]*)"')
if (-not $displayVersionMatch.Success -or -not $releaseDateMatch.Success) {
    throw 'Version.h display metadata could not be parsed.'
}
$expectedInstalledDisplayVersion = $displayVersionMatch.Groups[1].Value
$embeddedReleaseDate = $releaseDateMatch.Groups[1].Value
function Convert-NppComparableVersion([string]$value) {
    $match = [regex]::Match($value.TrimStart('v','V'),
        '^(\d+)\.(\d+)\.(\d+)(?:-beta\.(\d+))?$')
    if (-not $match.Success) { return $null }
    $prerelease = if ($match.Groups[4].Success) { [int]$match.Groups[4].Value } else { 65535 }
    return [version]::new([int]$match.Groups[1].Value, [int]$match.Groups[2].Value,
        [int]$match.Groups[3].Value, $prerelease)
}
try {
    $releaseFeed = Invoke-RestMethod -Uri 'https://api.github.com/repos/terryrogers/NppHistory-Plugin/releases?per_page=20' `
        -Headers @{ Accept = 'application/vnd.github+json'; 'User-Agent' = 'NppHistory-Live-Test' } `
        -TimeoutSec 15
    $latestEligible = @($releaseFeed | Where-Object { -not $_.draft } | Select-Object -First 1)
    if ($latestEligible.Count -gt 0) {
        $expectedPublishedVersion = ([string]$latestEligible[0].tag_name).TrimStart('v','V')
        $expectedDisplayedVersion = $expectedPublishedVersion -replace '-beta\.', '.'
        $installedMatch = [regex]::Match($versionHeader, 'NPPHISTORY_VERSION_SEMVER_W\s+L"([^"]+)"')
        if ($installedMatch.Success) {
            $installedVersion = $installedMatch.Groups[1].Value
            $publishedComparable = Convert-NppComparableVersion $expectedPublishedVersion
            $installedComparable = Convert-NppComparableVersion $installedVersion
            $expectedUpdateStatus = if ($publishedComparable -and $installedComparable -and
                $publishedComparable -le $installedComparable) {
                'Up to date ' + [char]0x2014 + ' latest published version: ' + $expectedDisplayedVersion
            } else {
                'Update available: ' + $expectedDisplayedVersion
            }
            $updateFeedAccessible = $true
        }
    }
} catch {
    $updateFeedAccessible = $false
}

Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
public static class NppHistoryNative {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct MENUITEMINFO {
        public uint cbSize, fMask, fType, fState, wID;
        public IntPtr hSubMenu, hbmpChecked, hbmpUnchecked, dwItemData, dwTypeData;
        public uint cch;
        public IntPtr hbmpItem;
    }
    private delegate bool EnumProc(IntPtr window, IntPtr state);
    [DllImport("user32.dll")] private static extern bool EnumChildWindows(IntPtr parent, EnumProc callback, IntPtr state);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc callback, IntPtr state);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetClassName(IntPtr window, StringBuilder name, int length);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetWindowText(IntPtr window, StringBuilder text, int length);
    [DllImport("user32.dll")]
    public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")]
    private static extern IntPtr SendMessageText(IntPtr window, uint message, IntPtr wParam, StringBuilder text);
    [DllImport("user32.dll", EntryPoint="SendMessageW")]
    private static extern IntPtr SendMessagePoint(IntPtr window, uint message, IntPtr wParam, ref POINT point);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool UpdateWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr window);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr window, string name);
    [DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")] public static extern IntPtr GetWindowLongPtr(IntPtr window, int index);
    [DllImport("user32.dll")] public static extern bool RedrawWindow(IntPtr window, IntPtr updateRect, IntPtr updateRegion, uint flags);
    [DllImport("user32.dll")] private static extern IntPtr GetMenu(IntPtr window);
    [DllImport("user32.dll")] private static extern IntPtr GetSubMenu(IntPtr menu, int position);
    [DllImport("user32.dll")] private static extern int GetMenuItemCount(IntPtr menu);
    [DllImport("user32.dll")] private static extern uint GetMenuItemID(IntPtr menu, int position);
    [DllImport("user32.dll")] private static extern uint GetMenuState(IntPtr menu, uint item, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern bool GetMenuItemInfo(
        IntPtr menu, uint item, bool byPosition, ref MENUITEMINFO information);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetMenuString(IntPtr menu, uint item, StringBuilder text, int length, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string className, string title);
    [DllImport("user32.dll")] private static extern int GetDlgCtrlID(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr window);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out RECT rectangle);
    [DllImport("user32.dll")] private static extern bool ClientToScreen(IntPtr window, ref POINT point);
    [DllImport("user32.dll")] private static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window, IntPtr deviceContext, uint flags);
    public static bool CenteredOn(IntPtr window, IntPtr owner, int tolerance) {
        RECT windowBounds, ownerBounds;
        if (!GetWindowRect(window, out windowBounds) || !GetWindowRect(owner, out ownerBounds))
            return false;
        int windowCenterX = windowBounds.Left + (windowBounds.Right - windowBounds.Left) / 2;
        int windowCenterY = windowBounds.Top + (windowBounds.Bottom - windowBounds.Top) / 2;
        int ownerCenterX = ownerBounds.Left + (ownerBounds.Right - ownerBounds.Left) / 2;
        int ownerCenterY = ownerBounds.Top + (ownerBounds.Bottom - ownerBounds.Top) / 2;
        return Math.Abs(windowCenterX - ownerCenterX) <= tolerance
            && Math.Abs(windowCenterY - ownerCenterY) <= tolerance;
    }
    public static IntPtr FindDescendant(IntPtr parent, string className) {
        IntPtr result = IntPtr.Zero;
        EnumChildWindows(parent, delegate(IntPtr window, IntPtr state) {
            var name = new StringBuilder(128);
            GetClassName(window, name, name.Capacity);
            if (name.ToString() == className) { result = window; return false; }
            return true;
        }, IntPtr.Zero);
        return result;
    }
    public static IntPtr FindScintillaWithContent(IntPtr parent) {
        IntPtr result = IntPtr.Zero;
        EnumChildWindows(parent, delegate(IntPtr window, IntPtr state) {
            var name = new StringBuilder(128);
            GetClassName(window, name, name.Capacity);
            if (name.ToString() == "Scintilla" && SendMessage(window, 2006, IntPtr.Zero, IntPtr.Zero).ToInt64() > 0) {
                result = window;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }
    public static IntPtr FindControl(IntPtr parent, int id) {
        IntPtr result = IntPtr.Zero;
        EnumChildWindows(parent, delegate(IntPtr window, IntPtr state) {
            if (GetDlgCtrlID(window) == id) { result = window; return false; }
            return true;
        }, IntPtr.Zero);
        return result;
    }
    public static IntPtr FindControlByText(IntPtr parent, string className, string text) {
        IntPtr result = IntPtr.Zero;
        EnumChildWindows(parent, delegate(IntPtr window, IntPtr state) {
            var actualClass = new StringBuilder(128);
            var actualText = new StringBuilder(256);
            GetClassName(window, actualClass, actualClass.Capacity);
            GetWindowText(window, actualText, actualText.Capacity);
            if (actualClass.ToString() == className && actualText.ToString() == text) { result = window; return false; }
            return true;
        }, IntPtr.Zero);
        return result;
    }
    public static string Describe(IntPtr window) {
        if (window == IntPtr.Zero) return "zero";
        var actualClass = new StringBuilder(128);
        var actualText = new StringBuilder(256);
        GetClassName(window, actualClass, actualClass.Capacity);
        GetWindowText(window, actualText, actualText.Capacity);
        return actualClass + "|" + actualText + "|" + GetDlgCtrlID(window);
    }
    public static string Text(IntPtr window) {
        int length = SendMessage(window, 0x000E, IntPtr.Zero, IntPtr.Zero).ToInt32();
        var value = new StringBuilder(length + 1);
        SendMessageText(window, 0x000D, (IntPtr)value.Capacity, value);
        return value.ToString();
    }
    public static void SetText(IntPtr window, string text) {
        var value = new StringBuilder(text);
        SendMessageText(window, 0x000C, IntPtr.Zero, value);
    }
    public static string AllChildText(IntPtr parent) {
        var result = new StringBuilder();
        EnumChildWindows(parent, delegate(IntPtr window, IntPtr state) {
            var value = new StringBuilder(1024);
            GetWindowText(window, value, value.Capacity);
            if (value.Length > 0) result.Append(value).Append(" ");
            return true;
        }, IntPtr.Zero);
        return result.ToString();
    }
    public static POINT EditScroll(IntPtr window) {
        POINT point = new POINT();
        SendMessagePoint(window, 0x04DD, IntPtr.Zero, ref point); // EM_GETSCROLLPOS
        return point;
    }
    public static void SetEditScroll(IntPtr window, int x, int y) {
        POINT point = new POINT();
        point.X = x; point.Y = y;
        SendMessagePoint(window, 0x04DE, IntPtr.Zero, ref point); // EM_SETSCROLLPOS
    }
    public static void BeginCommand(IntPtr parent, int command, IntPtr control) {
        var thread = new Thread(delegate() { SendMessage(parent, 0x0111, (IntPtr)command, control); });
        thread.IsBackground = true;
        thread.Start();
    }
    public static void BeginLeftClick(IntPtr window, int x, int y) {
        POINT screen = new POINT(); screen.X = x; screen.Y = y;
        ClientToScreen(window, ref screen);
        SetCursorPos(screen.X, screen.Y);
        IntPtr point = (IntPtr)((y << 16) | (x & 0xffff));
        PostMessage(window, 0x0201, (IntPtr)1, point);
        PostMessage(window, 0x0202, IntPtr.Zero, point);
    }
    public static void MoveCursorToCenter(IntPtr window) {
        RECT bounds;
        if (!GetWindowRect(window, out bounds)) return;
        SetCursorPos((bounds.Left + bounds.Right) / 2, (bounds.Top + bounds.Bottom) / 2);
    }
    public static void MoveCursorAway() { SetCursorPos(0, 0); }
    public static void BeginRightClick(IntPtr window, int x, int y) {
        POINT screen = new POINT(); screen.X = x; screen.Y = y;
        ClientToScreen(window, ref screen);
        SetCursorPos(screen.X, screen.Y);
        IntPtr point = (IntPtr)((y << 16) | (x & 0xffff));
        PostMessage(window, 0x0204, (IntPtr)2, point);
        PostMessage(window, 0x0205, IntPtr.Zero, point);
    }
    public static string TopWindows(uint processId) {
        var result = new StringBuilder();
        EnumWindows(delegate(IntPtr window, IntPtr state) {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            if (owner == processId) result.Append(Describe(window)).Append(";");
            return true;
        }, IntPtr.Zero);
        return result.ToString();
    }
    public static IntPtr FindTopWindowContaining(uint processId, string titlePart) {
        IntPtr result = IntPtr.Zero;
        EnumWindows(delegate(IntPtr window, IntPtr state) {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            var title = new StringBuilder(256);
            GetWindowText(window, title, title.Capacity);
            if (owner == processId && title.ToString().Contains(titlePart)) { result = window; return false; }
            return true;
        }, IntPtr.Zero);
        return result;
    }
    public static IntPtr FindTopWindowByClassAndText(uint processId, string className, string textPart) {
        IntPtr result = IntPtr.Zero;
        EnumWindows(delegate(IntPtr window, IntPtr state) {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            var actualClass = new StringBuilder(128);
            var title = new StringBuilder(512);
            GetClassName(window, actualClass, actualClass.Capacity);
            GetWindowText(window, title, title.Capacity);
            if (owner == processId && actualClass.ToString() == className &&
                title.ToString().Contains(textPart)) { result = window; return false; }
            return true;
        }, IntPtr.Zero);
        return result;
    }
    private static int FindMenuCommandIn(IntPtr menu, string text) {
        int count = GetMenuItemCount(menu);
        for (int index = 0; index < count; index++) {
            IntPtr child = GetSubMenu(menu, index);
            if (child != IntPtr.Zero) {
                int nested = FindMenuCommandIn(child, text);
                if (nested != 0) return nested;
            }
            var label = new StringBuilder(256);
            GetMenuString(menu, (uint)index, label, label.Capacity, 0x400);
            string actual = label.ToString().Replace("&", "");
            int shortcut = actual.IndexOf('\t');
            if ((shortcut >= 0 ? actual.Substring(0, shortcut) : actual) == text)
                return unchecked((int)GetMenuItemID(menu, index));
        }
        return 0;
    }
    private static IntPtr FindNamedSubMenu(IntPtr menu, string text) {
        int count = GetMenuItemCount(menu);
        for (int index = 0; index < count; index++) {
            IntPtr child = GetSubMenu(menu, index);
            var label = new StringBuilder(256);
            GetMenuString(menu, (uint)index, label, label.Capacity, 0x400);
            if (child != IntPtr.Zero && label.ToString().Replace("&", "") == text) return child;
            if (child != IntPtr.Zero) {
                IntPtr nested = FindNamedSubMenu(child, text);
                if (nested != IntPtr.Zero) return nested;
            }
        }
        return IntPtr.Zero;
    }
    public static int FindMenuCommand(IntPtr window, string text) { return FindMenuCommandIn(GetMenu(window), text); }
    public static int FindPluginMenuCommand(IntPtr window, string plugin, string text) {
        IntPtr pluginMenu = FindNamedSubMenu(GetMenu(window), plugin);
        return pluginMenu == IntPtr.Zero ? 0 : FindMenuCommandIn(pluginMenu, text);
    }
    public static string PluginMenuLabels(IntPtr window, string plugin) {
        IntPtr menu = FindNamedSubMenu(GetMenu(window), plugin);
        if (menu == IntPtr.Zero) return "";
        var result = new StringBuilder();
        int count = GetMenuItemCount(menu);
        for (int index = 0; index < count; ++index) {
            var label = new StringBuilder(256);
            GetMenuString(menu, (uint)index, label, label.Capacity, 0x400);
            if (result.Length > 0) result.Append('|');
            string text = label.ToString().Replace("&", "");
            int shortcut = text.IndexOf('\t');
            result.Append(shortcut >= 0 ? text.Substring(0, shortcut) : text);
        }
        return result.ToString();
    }
    public static int PluginMenuBitmapCount(IntPtr window, string plugin) {
        IntPtr menu = FindNamedSubMenu(GetMenu(window), plugin);
        if (menu == IntPtr.Zero) return 0;
        int result = 0;
        int count = GetMenuItemCount(menu);
        for (int index = 0; index < count; ++index) {
            MENUITEMINFO information = new MENUITEMINFO();
            information.cbSize = (uint)Marshal.SizeOf(typeof(MENUITEMINFO));
            information.fMask = 0x00000080; // MIIM_BITMAP
            if (GetMenuItemInfo(menu, (uint)index, true, ref information)
                && information.hbmpItem != IntPtr.Zero) ++result;
        }
        return result;
    }
    private static IntPtr FindMenuContainingCommand(IntPtr menu, int command) {
        int count = GetMenuItemCount(menu);
        for (int index = 0; index < count; ++index) {
            if (unchecked((int)GetMenuItemID(menu, index)) == command) return menu;
            IntPtr child = GetSubMenu(menu, index);
            if (child != IntPtr.Zero) {
                IntPtr found = FindMenuContainingCommand(child, command);
                if (found != IntPtr.Zero) return found;
            }
        }
        return IntPtr.Zero;
    }
    public static bool MenuCommandEnabled(IntPtr window, int command) {
        if (command == 0) return false;
        IntPtr menu = FindMenuContainingCommand(GetMenu(window), command);
        return menu != IntPtr.Zero && (GetMenuState(menu, (uint)command, 0) & 3) == 0;
    }
    public static bool ToolbarCommandEnabled(IntPtr parent, int command) {
        bool found = false;
        bool enabled = false;
        EnumChildWindows(parent, delegate(IntPtr window, IntPtr state) {
            var name = new StringBuilder(128);
            GetClassName(window, name, name.Capacity);
            if (name.ToString() != "ToolbarWindow32") return true;
            if (SendMessage(window, 0x0419, (IntPtr)command, IntPtr.Zero).ToInt64() >= 0) {
                found = true;
                enabled = SendMessage(window, 0x0409, (IntPtr)command, IntPtr.Zero) != IntPtr.Zero;
            }
            return true;
        }, IntPtr.Zero);
        return found && enabled;
    }
}
'@

$startupDocuments = @($normalTabPaths) + $notePath
$notepadArguments = '-multiInst -nosession ' + (($startupDocuments | ForEach-Object {
    '"' + $_.Replace('"', '\"') + '"'
}) -join ' ')
$process = Start-Process -FilePath (Join-Path $testRoot 'notepad++.exe') -ArgumentList $notepadArguments -PassThru -WindowStyle Hidden
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    $editor = [IntPtr]::Zero
    while ([DateTime]::UtcNow -lt $deadline -and $editor -eq [IntPtr]::Zero) {
        Start-Sleep -Milliseconds 250
        $process.Refresh()
        $editor = [NppHistoryNative]::FindScintillaWithContent($process.MainWindowHandle)
    }
    if ($editor -eq [IntPtr]::Zero) { throw 'Could not find the active Scintilla editor.' }

    if ($SettingsOnly.IsPresent) {
        $settingsOnlyCommand = [NppHistoryNative]::FindPluginMenuCommand(
            $process.MainWindowHandle, 'NppHistory', 'Settings')
        [NppHistoryNative]::BeginCommand($process.MainWindowHandle,
            $settingsOnlyCommand, [IntPtr]::Zero)
        $settingsOnlyDeadline = [DateTime]::UtcNow.AddSeconds(5)
        $settingsOnlyWindow = [IntPtr]::Zero
        while ([DateTime]::UtcNow -lt $settingsOnlyDeadline -and $settingsOnlyWindow -eq [IntPtr]::Zero) {
            Start-Sleep -Milliseconds 100
            $settingsOnlyWindow = [NppHistoryNative]::FindTopWindowContaining(
                [uint32]$process.Id, 'NppHistory Settings')
        }
        Start-Sleep -Seconds 3
        [pscustomobject]@{
            ProcessAlive = -not $process.HasExited
            SettingsAlive = [NppHistoryNative]::IsWindow($settingsOnlyWindow)
            TopWindows = [NppHistoryNative]::TopWindows([uint32]$process.Id)
        }
        return
    }
    if ($CompareThenSettings.IsPresent) {
        $captureOnlyCommand = [NppHistoryNative]::FindPluginMenuCommand(
            $process.MainWindowHandle, 'NppHistory', 'Capture')
        $compareOnlyCommand = [NppHistoryNative]::FindPluginMenuCommand(
            $process.MainWindowHandle, 'NppHistory', 'Compare')
        $settingsOnlyCommand = [NppHistoryNative]::FindPluginMenuCommand(
            $process.MainWindowHandle, 'NppHistory', 'Settings')
        [void][NppHistoryNative]::SendMessage($process.MainWindowHandle, 0x0111,
            [IntPtr]$captureOnlyCommand, [IntPtr]::Zero)
        [NppHistoryNative]::BeginCommand($process.MainWindowHandle,
            $compareOnlyCommand, [IntPtr]::Zero)
        $compareOnlyDeadline = [DateTime]::UtcNow.AddSeconds(5)
        $compareOnlyWindow = [IntPtr]::Zero
        while ([DateTime]::UtcNow -lt $compareOnlyDeadline -and $compareOnlyWindow -eq [IntPtr]::Zero) {
            Start-Sleep -Milliseconds 100
            $compareOnlyWindow = [NppHistoryNative]::FindTopWindowContaining(
                [uint32]$process.Id, ([string][char]0x2014 + ' NppHistory'))
        }
        [void][NppHistoryNative]::PostMessage($compareOnlyWindow, 0x0111, [IntPtr]1, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 500
        [NppHistoryNative]::BeginCommand($process.MainWindowHandle,
            $settingsOnlyCommand, [IntPtr]::Zero)
        $settingsOnlyDeadline = [DateTime]::UtcNow.AddSeconds(5)
        $settingsOnlyWindow = [IntPtr]::Zero
        while ([DateTime]::UtcNow -lt $settingsOnlyDeadline -and $settingsOnlyWindow -eq [IntPtr]::Zero) {
            Start-Sleep -Milliseconds 100
            $settingsOnlyWindow = [NppHistoryNative]::FindTopWindowContaining(
                [uint32]$process.Id, 'NppHistory Settings')
        }
        Start-Sleep -Seconds 3
        [pscustomobject]@{
            ProcessAlive = -not $process.HasExited
            CompareOpened = $compareOnlyWindow -ne [IntPtr]::Zero
            SettingsAlive = [NppHistoryNative]::IsWindow($settingsOnlyWindow)
            TopWindows = [NppHistoryNative]::TopWindows([uint32]$process.Id)
        }
        return
    }

    $automaticUpdateCheckPassed = $SkipAutomaticUpdateWait.IsPresent
    $automaticUpdateLogPath = Join-Path $configFolder 'NppHistory.log'
    # Production deliberately delays a due startup check by 90 seconds so plugin loading is
    # never held up by network activity. Allow enough time to exercise that real schedule.
    $automaticUpdateDeadline = [DateTime]::UtcNow.AddSeconds(110)
    while (-not $SkipAutomaticUpdateWait.IsPresent -and
        [DateTime]::UtcNow -lt $automaticUpdateDeadline -and -not $automaticUpdateCheckPassed) {
        Start-Sleep -Milliseconds 100
        $automaticUpdateLog = if (Test-Path $automaticUpdateLogPath) {
            [IO.File]::ReadAllText($automaticUpdateLogPath)
        } else { '' }
        $automaticUpdateCheckPassed = $automaticUpdateLog.Contains('Automatic update check started') -and
            ($automaticUpdateLog.Contains('Automatic update check completed') -or
                $automaticUpdateLog.Contains('[WARNING] Update check failure'))
    }

    $lengthBefore = [NppHistoryNative]::SendMessage($editor, 2006, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    [void][NppHistoryNative]::SendMessage($editor, 2160, [IntPtr]13, [IntPtr]28) # SCI_SETSEL: revision-only line
    [void][NppHistoryNative]::SendMessage($editor, 2180, [IntPtr]::Zero, [IntPtr]::Zero) # SCI_CLEAR
    [void][NppHistoryNative]::SendMessage($editor, 2160, [IntPtr]26, [IntPtr]37) # SCI_SETSEL: old wording
    foreach ($character in "new wording".ToCharArray()) {
        [void][NppHistoryNative]::SendMessage($editor, 0x0102, [IntPtr][int]$character, [IntPtr]::Zero) # WM_CHAR
    }
    [void][NppHistoryNative]::SendMessage($editor, 2025, [IntPtr]39, [IntPtr]::Zero) # SCI_GOTOPOS
    foreach ($character in "current only".ToCharArray()) {
        [void][NppHistoryNative]::SendMessage($editor, 0x0102, [IntPtr][int]$character, [IntPtr]::Zero) # WM_CHAR
    }
    [void][NppHistoryNative]::SendMessage($editor, 2329, [IntPtr]::Zero, [IntPtr]::Zero) # SCI_NEWLINE
    [void][NppHistoryNative]::SendMessage($editor, 2160, [IntPtr]1233, [IntPtr]1251) # line 060
    foreach ($character in "changed middle 060".ToCharArray()) {
        [void][NppHistoryNative]::SendMessage($editor, 0x0102, [IntPtr][int]$character, [IntPtr]::Zero)
    }
    [void][NppHistoryNative]::SendMessage($editor, 2160, [IntPtr]2033, [IntPtr]2053) # remove line 100
    [void][NppHistoryNative]::SendMessage($editor, 2180, [IntPtr]::Zero, [IntPtr]::Zero) # SCI_CLEAR

    $lengthAfter = [NppHistoryNative]::SendMessage($editor, 2006, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    $modifiedAfter = [NppHistoryNative]::SendMessage($editor, 2159, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    Start-Sleep -Seconds 12
    $savedText = [IO.File]::ReadAllText($notePath)
    $normalizedNotePath = [IO.Path]::GetFullPath($notePath).ToLowerInvariant()
    $adjacentHistoryRoot = Join-Path $testRoot '.npphistory'
    $pathMarker = Get-ChildItem $adjacentHistoryRoot -Filter path.txt -Recurse -ErrorAction Stop |
        Where-Object { [IO.File]::ReadAllText($_.FullName) -eq $normalizedNotePath } |
        Select-Object -First 1
    $revisions = @()
    $metadataText = ''
    if ($pathMarker) {
        $revisions = @(Get-ChildItem $pathMarker.DirectoryName -Filter *.rev)
        $metadataText = (Get-ChildItem $pathMarker.DirectoryName -Filter *.meta | ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) -join "`n"
    }
    $reasonsCaptured = $metadataText.Contains('reason=Before save') -and $metadataText.Contains('reason=Saved')
    $historyAttributes = [IO.File]::GetAttributes($adjacentHistoryRoot)
    $hiddenHistoryRoot = ($historyAttributes -band [IO.FileAttributes]::Hidden) -ne 0

    $captureMenuCommand = [NppHistoryNative]::FindPluginMenuCommand($process.MainWindowHandle, 'NppHistory', 'Capture')
    $compareMenuCommand = [NppHistoryNative]::FindPluginMenuCommand($process.MainWindowHandle, 'NppHistory', 'Compare')
    $historyMenuCommand = [NppHistoryNative]::FindPluginMenuCommand($process.MainWindowHandle, 'NppHistory', 'History')
    $settingsMenuCommand = [NppHistoryNative]::FindPluginMenuCommand($process.MainWindowHandle, 'NppHistory', 'Settings')
    $aboutMenuCommand = [NppHistoryNative]::FindPluginMenuCommand($process.MainWindowHandle, 'NppHistory', 'About')
    $pluginMenuLabels = [NppHistoryNative]::PluginMenuLabels($process.MainWindowHandle, 'NppHistory')
    $pluginMenuPassed = $pluginMenuLabels -eq 'Capture|Compare|History|Settings|About' -and
        $captureMenuCommand -ne 0 -and $compareMenuCommand -ne 0 -and
        $historyMenuCommand -ne 0 -and $settingsMenuCommand -ne 0 -and
        $aboutMenuCommand -ne 0
    $pluginMenuBitmapCount = [NppHistoryNative]::PluginMenuBitmapCount(
        $process.MainWindowHandle, 'NppHistory')
    $pluginMenuIconsPassed = $pluginMenuBitmapCount -eq 5 -and
        [NppHistoryNative]::GetProp(
            $process.MainWindowHandle, 'NppHistoryPluginMenuIconsReady').ToInt64() -eq 6
    $hiddenCaptureStatePassed = [NppHistoryNative]::MenuCommandEnabled(
        $process.MainWindowHandle, $captureMenuCommand) -and
        [NppHistoryNative]::ToolbarCommandEnabled($process.MainWindowHandle, $captureMenuCommand)
    $hiddenCompareStatePassed = [NppHistoryNative]::MenuCommandEnabled(
        $process.MainWindowHandle, $compareMenuCommand) -and
        [NppHistoryNative]::ToolbarCommandEnabled($process.MainWindowHandle, $compareMenuCommand)
    $hiddenPaneComparePassed = $false
    if ($hiddenCompareStatePassed) {
        [NppHistoryNative]::BeginCommand($process.MainWindowHandle, $compareMenuCommand, [IntPtr]::Zero)
        $hiddenCompareDeadline = [DateTime]::UtcNow.AddSeconds(5)
        $hiddenCompareWindow = [IntPtr]::Zero
        while ([DateTime]::UtcNow -lt $hiddenCompareDeadline -and $hiddenCompareWindow -eq [IntPtr]::Zero) {
            Start-Sleep -Milliseconds 100
            $hiddenCompareWindow = [NppHistoryNative]::FindTopWindowContaining(
                [uint32]$process.Id, ([string][char]0x2014 + ' NppHistory'))
        }
        if ($hiddenCompareWindow -ne [IntPtr]::Zero) {
            $hiddenPaneComparePassed = [NppHistoryNative]::FindControl($hiddenCompareWindow, 1023) -ne [IntPtr]::Zero
            [void][NppHistoryNative]::PostMessage(
                $hiddenCompareWindow, 0x0111, [IntPtr]2, [IntPtr]::Zero)
            Start-Sleep -Milliseconds 250
        }
    }
    if ($historyMenuCommand -ne 0) {
        [void][NppHistoryNative]::SendMessage($process.MainWindowHandle, 0x0111, [IntPtr]$historyMenuCommand, [IntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 500
    $compareButton = [NppHistoryNative]::FindControlByText($process.MainWindowHandle, 'Button', 'Compare')
    $historyPanel = [NppHistoryNative]::GetParent($compareButton)
    $historyList = [NppHistoryNative]::FindControl($historyPanel, 1002)
    $panelButtonLabels = @('Capture','Refresh','Compare','Restore','Settings','About')
    $panelButtonIds = @(1006,1003,1004,1005,1007,1008)
    $panelButtonsPassed = $historyPanel -ne [IntPtr]::Zero
    $panelButtonWidths = @()
    for ($buttonIndex = 0; $buttonIndex -lt $panelButtonIds.Count; $buttonIndex++) {
        $button = [NppHistoryNative]::FindControl($historyPanel, $panelButtonIds[$buttonIndex])
        $panelButtonsPassed = $panelButtonsPassed -and $button -ne [IntPtr]::Zero -and
            [NppHistoryNative]::Text($button) -eq $panelButtonLabels[$buttonIndex]
        $bounds = [NppHistoryNative+RECT]::new()
        if ($button -ne [IntPtr]::Zero -and [NppHistoryNative]::GetWindowRect($button, [ref]$bounds)) {
            $panelButtonWidths += $bounds.Right - $bounds.Left
        }
    }
    $panelButtonWidthsPassed = $panelButtonWidths.Count -eq $panelButtonIds.Count -and
        (@($panelButtonWidths | Select-Object -Unique).Count -eq 1)
    $savedPaneStatePassed = -not [NppHistoryNative]::IsWindowVisible(
        [NppHistoryNative]::FindControl($historyPanel, 1104)) -and
        [NppHistoryNative]::IsWindowEnabled([NppHistoryNative]::FindControl($historyPanel, 1006)) -and
        [NppHistoryNative]::IsWindowEnabled([NppHistoryNative]::FindControl($historyPanel, 1003)) -and
        -not [NppHistoryNative]::IsWindowEnabled([NppHistoryNative]::FindControl($historyPanel, 1004)) -and
        -not [NppHistoryNative]::IsWindowEnabled([NppHistoryNative]::FindControl($historyPanel, 1005))
    Start-Sleep -Milliseconds 1100
    $visibleUnselectedCommandStatePassed = -not [NppHistoryNative]::MenuCommandEnabled(
        $process.MainWindowHandle, $compareMenuCommand) -and
        -not [NppHistoryNative]::ToolbarCommandEnabled($process.MainWindowHandle, $compareMenuCommand)
    $dockIconPassed = [NppHistoryNative]::GetProp($historyPanel, 'NppHistoryDockIconReady') -ne [IntPtr]::Zero
    $responsiveButtonsPassed = [NppHistoryNative]::GetProp($historyPanel, 'NppHistoryResponsiveButtonRows').ToInt64() -ge 1
    $panelButtonIconsPassed = [NppHistoryNative]::GetProp($historyPanel, 'NppHistoryPanelButtonIconsReady') -ne [IntPtr]::Zero
    $panelButtonHoverRegistrationCount = [NppHistoryNative]::GetProp(
        $historyPanel, 'NppHistoryPanelButtonHoverReady').ToInt64()
    $panelButtonHoverReady = $panelButtonHoverRegistrationCount -eq 6
    $panelButtonTooltipCount = [NppHistoryNative]::GetProp(
        $historyPanel, 'NppHistoryPanelButtonTooltipsReady').ToInt64()
    $panelButtonTooltipsPassed = $panelButtonTooltipCount -eq 6
    $panelHoverButton = [NppHistoryNative]::FindControl($historyPanel, 1006)
    $panelButtonHotOnEnter = 0
    for ($hoverAttempt = 0; $hoverAttempt -lt 5 -and $panelButtonHotOnEnter -ne 1006; $hoverAttempt++) {
        [NppHistoryNative]::MoveCursorToCenter($panelHoverButton)
        [void][NppHistoryNative]::SendMessage($panelHoverButton, 0x0200, [IntPtr]::Zero, [IntPtr]0x00050005)
        $panelButtonHotOnEnter = [NppHistoryNative]::GetProp(
            $historyPanel, 'NppHistoryPanelHotButton').ToInt64()
        if ($panelButtonHotOnEnter -ne 1006) { Start-Sleep -Milliseconds 20 }
    }
    $panelButtonHoverPassed = $panelButtonHoverReady -and $panelButtonHotOnEnter -eq 1006
    [void][NppHistoryNative]::SendMessage($panelHoverButton, 0x02A3, [IntPtr]::Zero, [IntPtr]::Zero)
    $panelButtonHotAfterLeave = [NppHistoryNative]::GetProp(
        $historyPanel, 'NppHistoryPanelHotButton').ToInt64()
    $panelButtonHoverPassed = $panelButtonHoverPassed -and $panelButtonHotAfterLeave -eq 0
    [NppHistoryNative]::BeginRightClick($historyList, 10, 35)
    Start-Sleep -Milliseconds 350
    $revisionActionsPassed = [NppHistoryNative]::GetProp($historyPanel, 'NppHistoryRevisionActionsReady').ToInt64() -eq 5
    [void][NppHistoryNative]::PostMessage($historyPanel, 0x001F, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    $selectedPaneActionsPassed =
        [NppHistoryNative]::IsWindowEnabled([NppHistoryNative]::FindControl($historyPanel, 1004)) -and
        [NppHistoryNative]::IsWindowEnabled([NppHistoryNative]::FindControl($historyPanel, 1005))
    Start-Sleep -Milliseconds 150
    $visibleSelectedCommandStatePassed = [NppHistoryNative]::MenuCommandEnabled(
        $process.MainWindowHandle, $compareMenuCommand) -and
        [NppHistoryNative]::ToolbarCommandEnabled($process.MainWindowHandle, $compareMenuCommand)
    $mainToolbarButtonsRegistered = [NppHistoryNative]::GetProp($process.MainWindowHandle, 'NppHistoryToolbarButtonsRegistered').ToInt64() - 1
    $captureButtonPassed = $false
    $captureButton = [NppHistoryNative]::FindControl($historyPanel, 1006)
    $historyListCount = if ($historyList -ne [IntPtr]::Zero) {
        [NppHistoryNative]::SendMessage($historyList, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    } else { -1 }
    $historyPanelScreenshot = Join-Path $testRoot 'history-panel.png'
    Add-Type -AssemblyName System.Drawing
    $mainCaptureRectangle = [NppHistoryNative+RECT]::new()
    if ([NppHistoryNative]::GetWindowRect($process.MainWindowHandle, [ref]$mainCaptureRectangle)) {
        $bitmap = [Drawing.Bitmap]::new($mainCaptureRectangle.Right - $mainCaptureRectangle.Left, $mainCaptureRectangle.Bottom - $mainCaptureRectangle.Top)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        $deviceContext = $graphics.GetHdc()
        try { [void][NppHistoryNative]::PrintWindow($process.MainWindowHandle, $deviceContext, 2) }
        finally { $graphics.ReleaseHdc($deviceContext); $graphics.Dispose() }
        $bitmap.Save($historyPanelScreenshot, [Drawing.Imaging.ImageFormat]::Png)
        $bitmap.Dispose()
    }
    if ($compareButton -ne [IntPtr]::Zero) {
        [NppHistoryNative]::BeginCommand([NppHistoryNative]::GetParent($compareButton), 1004, $compareButton)
    }
    $compareDeadline = [DateTime]::UtcNow.AddSeconds(5)
    $compareWindow = [IntPtr]::Zero
    while ([DateTime]::UtcNow -lt $compareDeadline -and $compareWindow -eq [IntPtr]::Zero) {
        Start-Sleep -Milliseconds 100
        $compareWindow = [NppHistoryNative]::FindTopWindowContaining([uint32]$process.Id, ([string][char]0x2014 + ' NppHistory'))
    }
    $compareErrorWindow = if ($compareWindow -eq [IntPtr]::Zero) { [NppHistoryNative]::FindWindow($null, 'NppHistory') } else { [IntPtr]::Zero }
    $leftComparison = if ($compareWindow -ne [IntPtr]::Zero) { [NppHistoryNative]::FindControl($compareWindow, 1021) } else { [IntPtr]::Zero }
    $rightComparison = if ($compareWindow -ne [IntPtr]::Zero) { [NppHistoryNative]::FindControl($compareWindow, 1022) } else { [IntPtr]::Zero }
    $revisionSelector = if ($compareWindow -ne [IntPtr]::Zero) { [NppHistoryNative]::FindControl($compareWindow, 1023) } else { [IntPtr]::Zero }
    $leftMarkers = if ($compareWindow -ne [IntPtr]::Zero) { [NppHistoryNative]::FindControl($compareWindow, 1026) } else { [IntPtr]::Zero }
    $centreScrollbar = if ($compareWindow -ne [IntPtr]::Zero) { [NppHistoryNative]::FindControl($compareWindow, 1027) } else { [IntPtr]::Zero }
    $rightMarkers = if ($compareWindow -ne [IntPtr]::Zero) { [NppHistoryNative]::FindControl($compareWindow, 1028) } else { [IntPtr]::Zero }
    $previousDifference = if ($compareWindow -ne [IntPtr]::Zero) { [NppHistoryNative]::FindControl($compareWindow, 1029) } else { [IntPtr]::Zero }
    $nextDifference = if ($compareWindow -ne [IntPtr]::Zero) { [NppHistoryNative]::FindControl($compareWindow, 1030) } else { [IntPtr]::Zero }
    $differenceStatus = if ($compareWindow -ne [IntPtr]::Zero) { [NppHistoryNative]::FindControl($compareWindow, 1031) } else { [IntPtr]::Zero }
    $ignoreWhitespace = if ($compareWindow -ne [IntPtr]::Zero) { [NppHistoryNative]::FindControl($compareWindow, 1032) } else { [IntPtr]::Zero }
    $ignoreBlank = if ($compareWindow -ne [IntPtr]::Zero) { [NppHistoryNative]::FindControl($compareWindow, 1033) } else { [IntPtr]::Zero }
    $ignoreCase = if ($compareWindow -ne [IntPtr]::Zero) { [NppHistoryNative]::FindControl($compareWindow, 1034) } else { [IntPtr]::Zero }
    $ignoreEol = if ($compareWindow -ne [IntPtr]::Zero) { [NppHistoryNative]::FindControl($compareWindow, 1035) } else { [IntPtr]::Zero }
    $locationPane = if ($compareWindow -ne [IntPtr]::Zero) { [NppHistoryNative]::FindControl($compareWindow, 1037) } else { [IntPtr]::Zero }
    $leftHeader = if ($compareWindow -ne [IntPtr]::Zero) { [NppHistoryNative]::FindControl($compareWindow, 1038) } else { [IntPtr]::Zero }
    $rightHeader = if ($compareWindow -ne [IntPtr]::Zero) { [NppHistoryNative]::FindControl($compareWindow, 1039) } else { [IntPtr]::Zero }
    $leftStatus = if ($compareWindow -ne [IntPtr]::Zero) { [NppHistoryNative]::FindControl($compareWindow, 1040) } else { [IntPtr]::Zero }
    $rightStatus = if ($compareWindow -ne [IntPtr]::Zero) { [NppHistoryNative]::FindControl($compareWindow, 1041) } else { [IntPtr]::Zero }
    $comparisonToolbar = if ($compareWindow -ne [IntPtr]::Zero) { [NppHistoryNative]::FindControl($compareWindow, 1043) } else { [IntPtr]::Zero }
    $locationClose = if ($compareWindow -ne [IntPtr]::Zero) { [NppHistoryNative]::FindControl($compareWindow, 1052) } else { [IntPtr]::Zero }
    $overallStatus = if ($compareWindow -ne [IntPtr]::Zero) { [NppHistoryNative]::FindControl($compareWindow, 1053) } else { [IntPtr]::Zero }
    $revisionChoiceCount = if ($revisionSelector -ne [IntPtr]::Zero) {
        [NppHistoryNative]::SendMessage($revisionSelector, 0x0146, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
    } else { 0 }
    $toolbarButtonCount = if ($comparisonToolbar -ne [IntPtr]::Zero) { [NppHistoryNative]::SendMessage($comparisonToolbar, 0x0418, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64() } else { 0 }
    $comparisonIconCount = if ($comparisonToolbar -ne [IntPtr]::Zero) { [NppHistoryNative]::GetProp($comparisonToolbar, 'NppHistoryComparisonImageCount').ToInt64() } else { 0 }
    $comparisonIconSize = if ($comparisonToolbar -ne [IntPtr]::Zero) { [NppHistoryNative]::GetProp($comparisonToolbar, 'NppHistoryComparisonImageSize').ToInt64() } else { 0 }
    $comparisonIconsPassed = $comparisonIconCount -eq 12 -and $comparisonIconSize -eq 24
    $toolbarTooltip = if ($comparisonToolbar -ne [IntPtr]::Zero) { [NppHistoryNative]::SendMessage($comparisonToolbar, 0x0423, [IntPtr]::Zero, [IntPtr]::Zero) } else { [IntPtr]::Zero } # TB_GETTOOLTIPS
    $hoverTooltip = if ($comparisonToolbar -ne [IntPtr]::Zero) { [NppHistoryNative]::GetProp($comparisonToolbar, 'NppHistoryHoverTooltip') } else { [IntPtr]::Zero }
    $tooltipToolCount = if ($toolbarTooltip -ne [IntPtr]::Zero) { [NppHistoryNative]::SendMessage($toolbarTooltip, 0x040D, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64() } else { 0 } # TTM_GETTOOLCOUNT
    $allToolbarHintsRegistered = $true
    $tooltipTextPassed = $compareWindow -ne [IntPtr]::Zero -and [NppHistoryNative]::GetProp($compareWindow, 'NppHistoryToolbarTooltipsReady') -ne [IntPtr]::Zero
    $allToolbarHintsRegistered = $tooltipTextPassed
    $windowStyle = if ($compareWindow -ne [IntPtr]::Zero) { [NppHistoryNative]::GetWindowLongPtr($compareWindow, -16).ToInt64() } else { 0 }
    $standardWindowFrame = ($windowStyle -band 0x00C00000) -eq 0x00C00000 -and ($windowStyle -band 0x00080000) -ne 0 -and ($windowStyle -band 0x00020000) -ne 0 -and ($windowStyle -band 0x00010000) -ne 0
    $captionText = [NppHistoryNative]::Text($compareWindow)
    $captionIcon = if ($compareWindow -ne [IntPtr]::Zero) { [NppHistoryNative]::SendMessage($compareWindow, 0x007F, [IntPtr]0, [IntPtr]::Zero) } else { [IntPtr]::Zero } # WM_GETICON/ICON_SMALL
    $headerLabelsPassed = ([NppHistoryNative]::Text($leftHeader) -match '^test-note-.*\.txt$') -and ([NppHistoryNative]::Text($rightHeader) -match '^test-note-.*\.txt') -and -not ([NppHistoryNative]::Text($leftHeader).Contains([char]0x2261)) -and -not ([NppHistoryNative]::Text($rightHeader).Contains([char]0x2261)) -and -not ([NppHistoryNative]::Text($rightHeader).Contains('.npphistory')) -and $captionText.Contains([IO.Path]::GetFileName($notePath)) -and ($captionText -match ' - (Saved|Before save) ') -and $captionIcon -ne [IntPtr]::Zero
    $winMergeFramePresent = $locationPane -ne [IntPtr]::Zero -and $leftHeader -ne [IntPtr]::Zero -and $rightHeader -ne [IntPtr]::Zero -and $leftStatus -ne [IntPtr]::Zero -and $rightStatus -ne [IntPtr]::Zero -and $comparisonToolbar -ne [IntPtr]::Zero -and $overallStatus -ne [IntPtr]::Zero -and $toolbarButtonCount -ge 12 -and $toolbarButtonCount -le 16 -and $standardWindowFrame -and $headerLabelsPassed
    $comparisonOpened = $leftComparison -ne [IntPtr]::Zero -and $rightComparison -ne [IntPtr]::Zero -and $revisionChoiceCount -eq 2 -and $winMergeFramePresent
    $comparisonCentered = $false
    $comparisonCenterDelta = ''
    if ($comparisonOpened) {
        $mainRectangle = [NppHistoryNative+RECT]::new()
        $compareRectangle = [NppHistoryNative+RECT]::new()
        [void][NppHistoryNative]::GetWindowRect($process.MainWindowHandle, [ref]$mainRectangle)
        [void][NppHistoryNative]::GetWindowRect($compareWindow, [ref]$compareRectangle)
        $comparisonCentered = [Math]::Abs(($mainRectangle.Left + $mainRectangle.Right) - ($compareRectangle.Left + $compareRectangle.Right)) -le 12 -and
            [Math]::Abs(($mainRectangle.Top + $mainRectangle.Bottom) - ($compareRectangle.Top + $compareRectangle.Bottom)) -le 12
        $comparisonCenterDelta = "x=$([Math]::Abs(($mainRectangle.Left + $mainRectangle.Right) - ($compareRectangle.Left + $compareRectangle.Right))) y=$([Math]::Abs(($mainRectangle.Top + $mainRectangle.Bottom) - ($compareRectangle.Top + $compareRectangle.Bottom))) main=$($mainRectangle.Left),$($mainRectangle.Top),$($mainRectangle.Right),$($mainRectangle.Bottom) compare=$($compareRectangle.Left),$($compareRectangle.Top),$($compareRectangle.Right),$($compareRectangle.Bottom)"
    }
    $headerDoubleClickPassed = $false
    if ($comparisonOpened) {
        [void][NppHistoryNative]::SendMessage($leftHeader, 0x0203, [IntPtr]::Zero, [IntPtr]::Zero) # WM_LBUTTONDBLCLK
        Start-Sleep -Milliseconds 100
        $maximized = [NppHistoryNative]::IsZoomed($compareWindow)
        [void][NppHistoryNative]::SendMessage($leftHeader, 0x0203, [IntPtr]::Zero, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 100
        $restored = -not [NppHistoryNative]::IsZoomed($compareWindow)
        $headerDoubleClickPassed = $maximized -and $restored
    }
    $tooltipHoverPassed = $false
    $tooltipHoverText = ''
    if ($comparisonOpened -and $hoverTooltip -ne [IntPtr]::Zero) {
        # The first visible toolbar button occupies the first 37 x 34 client pixels.
        $hoverPosition = [IntPtr](18 -bor (17 -shl 16))
        [void][NppHistoryNative]::SendMessage($comparisonToolbar, 0x0200, [IntPtr]::Zero, $hoverPosition) # WM_MOUSEMOVE
        $tooltipHoverText = [NppHistoryNative]::Text($hoverTooltip)
        $tooltipHoverPassed = $tooltipHoverText.Contains('Choose revision') -and [NppHistoryNative]::GetProp($comparisonToolbar, 'NppHistoryHoverShown') -ne [IntPtr]::Zero
        [void][NppHistoryNative]::SendMessage($comparisonToolbar, 0x02A3, [IntPtr]::Zero, [IntPtr]::Zero) # WM_MOUSELEAVE
    }
    if ($comparisonOpened) {
        [void][NppHistoryNative]::SendMessage($revisionSelector, 0x014E, [IntPtr]1, [IntPtr]::Zero) # CB_SETCURSEL
        [void][NppHistoryNative]::SendMessage($compareWindow, 0x0111, [IntPtr]66559, $revisionSelector) # CBN_SELCHANGE + control ID
        Start-Sleep -Milliseconds 200
    }
    $lineNumbersRendered = $comparisonOpened -and [NppHistoryNative]::SendMessage($leftComparison, 2243, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64() -gt 0 # SCI_GETMARGINWIDTHN
    $differenceNavigationPassed = $false
    $currentDifferencePassed = $false
    $currentDifferenceTop = -1
    $tooltipToolCountAfterCurrent = -1
    $locationVisibleAfterCurrent = $false
    $revisionToolbarNavigationPassed = $false
    $comparisonOptionsPassed = $false
    if ($comparisonOpened) {
        $beforeNavigation = [NppHistoryNative]::Describe($differenceStatus)
        [void][NppHistoryNative]::SendMessage($compareWindow, 0x0111, [IntPtr]3001, $comparisonToolbar)
        Start-Sleep -Milliseconds 100
        $afterNavigation = [NppHistoryNative]::Describe($differenceStatus)
        $differenceNavigationPassed = $beforeNavigation.Contains('Difference 1 of') -and $afterNavigation.Contains('Difference 2 of')
        [void][NppHistoryNative]::SendMessage($compareWindow, 0x0111, [IntPtr]3005, $comparisonToolbar) # last difference
        $selectedBeforeCurrent = [NppHistoryNative]::Describe($differenceStatus)
        [void][NppHistoryNative]::SendMessage($compareWindow, 0x0115, [IntPtr]6, $centreScrollbar) # SB_TOP
        [void][NppHistoryNative]::SendMessage($compareWindow, 0x0111, [IntPtr]3004, $comparisonToolbar) # current difference
        Start-Sleep -Milliseconds 100
        $currentDifferenceTop = [NppHistoryNative]::SendMessage($leftComparison, 2152, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
        $selectedAfterCurrent = [NppHistoryNative]::Describe($differenceStatus)
        $tooltipToolCountAfterCurrent = if ([NppHistoryNative]::GetProp($comparisonToolbar, 'NppHistoryHoverTooltip') -ne [IntPtr]::Zero) { 1 } else { 0 }
        $locationVisibleAfterCurrent = [NppHistoryNative]::IsWindowVisible($locationPane)
        $currentDifferencePassed = $currentDifferenceTop -gt 0 -and $selectedAfterCurrent -eq $selectedBeforeCurrent -and $locationVisibleAfterCurrent -and $tooltipToolCountAfterCurrent -ge 1
        [void][NppHistoryNative]::SendMessage($compareWindow, 0x0111, [IntPtr]3006, $comparisonToolbar) # first revision
        $firstRevision = [NppHistoryNative]::SendMessage($revisionSelector, 0x0147, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64() # CB_GETCURSEL
        [void][NppHistoryNative]::SendMessage($compareWindow, 0x0111, [IntPtr]3009, $comparisonToolbar) # last revision
        $lastRevision = [NppHistoryNative]::SendMessage($revisionSelector, 0x0147, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
        $revisionToolbarNavigationPassed = $firstRevision -eq 0 -and $lastRevision -eq 1
        [void][NppHistoryNative]::SendMessage($ignoreWhitespace, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) # BM_CLICK
        $checked = [NppHistoryNative]::SendMessage($ignoreWhitespace, 0x00F0, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64() # BM_GETCHECK
        [void][NppHistoryNative]::SendMessage($ignoreWhitespace, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
        $comparisonOptionsPassed = $checked -eq 1
    }
    $sharedScrollPassed = $false
    if ($comparisonOpened) {
        [void][NppHistoryNative]::SendMessage($compareWindow, 0x0115, [IntPtr]3, $centreScrollbar) # WM_VSCROLL/SB_PAGEDOWN
        $leftTopLine = [NppHistoryNative]::SendMessage($leftComparison, 2152, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64() # SCI_GETFIRSTVISIBLELINE
        $rightTopLine = [NppHistoryNative]::SendMessage($rightComparison, 2152, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
        $sharedScrollPassed = $leftTopLine -gt 0 -and $leftTopLine -eq $rightTopLine
        [void][NppHistoryNative]::SendMessage($compareWindow, 0x0115, [IntPtr]6, $centreScrollbar) # SB_TOP
    }
    $locationPaneCollapsePassed = $false
    if ($comparisonOpened -and $locationClose -ne [IntPtr]::Zero) {
        $leftBefore = [NppHistoryNative+RECT]::new()
        $leftAfter = [NppHistoryNative+RECT]::new()
        $leftStatusAfter = [NppHistoryNative+RECT]::new()
        $rightStatusAfter = [NppHistoryNative+RECT]::new()
        [void][NppHistoryNative]::GetWindowRect($leftHeader, [ref]$leftBefore)
        [void][NppHistoryNative]::SendMessage($compareWindow, 0x0111, [IntPtr]1052, $locationClose)
        Start-Sleep -Milliseconds 150
        [void][NppHistoryNative]::GetWindowRect($leftHeader, [ref]$leftAfter)
        [void][NppHistoryNative]::GetWindowRect($leftStatus, [ref]$leftStatusAfter)
        [void][NppHistoryNative]::GetWindowRect($rightStatus, [ref]$rightStatusAfter)
        $locationPaneCollapsePassed = -not [NppHistoryNative]::IsWindowVisible($locationPane) -and $leftAfter.Left -lt $leftBefore.Left -and ($leftAfter.Right - $leftAfter.Left) -gt ($leftBefore.Right - $leftBefore.Left) -and $leftStatusAfter.Right -le $rightStatusAfter.Left
    }
    Start-Sleep -Milliseconds 200
    $comparisonScreenshot = Join-Path $testRoot 'side-by-side-comparison.png'
    $winMergePaletteRendered = $false
    if ($comparisonOpened) {
        [void][NppHistoryNative]::RedrawWindow($compareWindow, [IntPtr]::Zero, [IntPtr]::Zero, 0x0181) # invalidate/update/all children
        Add-Type -AssemblyName System.Drawing
        $rectangle = [NppHistoryNative+RECT]::new()
        if ([NppHistoryNative]::GetWindowRect($compareWindow, [ref]$rectangle)) {
            $bitmap = [Drawing.Bitmap]::new($rectangle.Right - $rectangle.Left, $rectangle.Bottom - $rectangle.Top)
            $graphics = [Drawing.Graphics]::FromImage($bitmap)
            $deviceContext = $graphics.GetHdc()
            try { [void][NppHistoryNative]::PrintWindow($compareWindow, $deviceContext, 2) }
            finally { $graphics.ReleaseHdc($deviceContext); $graphics.Dispose() }
            $bitmap.Save($comparisonScreenshot, [Drawing.Imaging.ImageFormat]::Png)
            $bitmap.Dispose()
            $bitmap = [Drawing.Bitmap]::FromFile($comparisonScreenshot)
            $foundGold = $false; $foundSelectedRed = $false; $foundHeaderBlue = $false
            for ($y = 0; $y -lt $bitmap.Height -and -not ($foundGold -and $foundSelectedRed -and $foundHeaderBlue); $y++) {
                for ($x = 0; $x -lt $bitmap.Width; $x++) {
                    $pixel = $bitmap.GetPixel($x, $y)
                    if ($pixel.R -gt 220 -and $pixel.G -gt 160 -and $pixel.B -lt 60) { $foundGold = $true }
                    if ($pixel.R -gt 210 -and $pixel.G -gt 70 -and $pixel.G -lt 160 -and $pixel.B -gt 60 -and $pixel.B -lt 160) { $foundSelectedRed = $true }
                    if ($pixel.B -gt 220 -and $pixel.G -gt 200 -and $pixel.R -gt 180 -and $pixel.B -gt $pixel.R) { $foundHeaderBlue = $true }
                }
            }
            $winMergePaletteRendered = $foundGold -and $foundSelectedRed -and $foundHeaderBlue
            $bitmap.Dispose()
        }
    }
    if ($compareWindow -ne [IntPtr]::Zero) {
        [void][NppHistoryNative]::PostMessage($compareWindow, 0x0111, [IntPtr]1, [IntPtr]::Zero)
    }
    if ($compareErrorWindow -ne [IntPtr]::Zero) {
        [void][NppHistoryNative]::PostMessage($compareErrorWindow, 0x0111, [IntPtr]1, [IntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 300
    if ($captureButton -ne [IntPtr]::Zero) {
        [void][NppHistoryNative]::SendMessage($historyPanel, 0x0111, [IntPtr]1006, $captureButton)
        Start-Sleep -Milliseconds 350
        $historyListCountAfterCapture = [NppHistoryNative]::SendMessage($historyList, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
        $manualMetadata = (Get-ChildItem $pathMarker.DirectoryName -Filter *.meta | ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) -join "`n"
        $captureButtonPassed = $historyListCountAfterCapture -eq 3 -and $manualMetadata.Contains('reason=Manual capture')
    }

    $commentUpdatePassed = $false
    $commentUpdateLogged = $false
    $revisionDeletionPassed = $false
    $revisionDeletionLogged = $false
    $restoreActionPassed = $false
    $restoreSafetyPassed = $false
    $restoreLogged = $false
    $editDialogCentered = $false
    $deleteDialogCentered = $false
    $restoreDialogCentered = $false
    if ($captureButtonPassed) {
        [NppHistoryNative]::BeginLeftClick($historyList, 10, 35)
        Start-Sleep -Milliseconds 100
        [NppHistoryNative]::BeginCommand($historyPanel, 4102, [IntPtr]::Zero)
        $editDialog = [IntPtr]::Zero
        $editDeadline = [DateTime]::UtcNow.AddSeconds(5)
        while ([DateTime]::UtcNow -lt $editDeadline -and $editDialog -eq [IntPtr]::Zero) {
            Start-Sleep -Milliseconds 100
            $editDialog = [NppHistoryNative]::FindTopWindowContaining([uint32]$process.Id, 'Edit Revision Comment')
        }
        if ($editDialog -ne [IntPtr]::Zero) {
            $editDialogCentered = [NppHistoryNative]::CenteredOn(
                $editDialog, $process.MainWindowHandle, 4)
            [NppHistoryNative]::SetText([NppHistoryNative]::FindControl($editDialog, 1100), 'Automated deletion audit')
            [void][NppHistoryNative]::SendMessage($editDialog, 0x0111, [IntPtr]1, [IntPtr]::Zero)
            Start-Sleep -Milliseconds 250
            $editedMetadata = (Get-ChildItem $pathMarker.DirectoryName -Filter *.meta | ForEach-Object {
                [IO.File]::ReadAllText($_.FullName)
            }) -join "`n"
            $commentUpdatePassed = $editedMetadata.Contains('reason=Automated deletion audit')
        }

        [NppHistoryNative]::BeginLeftClick($historyList, 10, 35)
        Start-Sleep -Milliseconds 100
        [NppHistoryNative]::BeginCommand($historyPanel, 4101, [IntPtr]::Zero)
        $deleteDialog = [IntPtr]::Zero
        $deleteDeadline = [DateTime]::UtcNow.AddSeconds(5)
        while ([DateTime]::UtcNow -lt $deleteDeadline -and $deleteDialog -eq [IntPtr]::Zero) {
            Start-Sleep -Milliseconds 100
            $deleteDialog = [NppHistoryNative]::FindTopWindowContaining([uint32]$process.Id, 'Delete Revision')
        }
        if ($deleteDialog -ne [IntPtr]::Zero) {
            $deleteDialogCentered = [NppHistoryNative]::CenteredOn(
                $deleteDialog, $process.MainWindowHandle, 16)
            [void][NppHistoryNative]::SendMessage($deleteDialog, 0x0111, [IntPtr]6, [IntPtr]::Zero) # IDYES
            Start-Sleep -Milliseconds 300
            $afterDeleteCount = [NppHistoryNative]::SendMessage($historyList, 0x1004,
                [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
            $revisionDeletionPassed = $afterDeleteCount -eq 2 -and
                -not ((Get-ChildItem $pathMarker.DirectoryName -Filter *.meta | ForEach-Object {
                    [IO.File]::ReadAllText($_.FullName)
                }) -join "`n").Contains('reason=Automated deletion audit')
        }

        if ($revisionDeletionPassed) {
            $restoreSafetyMarker = 'unsaved restore safety marker'
            [void][NppHistoryNative]::SendMessage($editor, 2318,
                [IntPtr]::Zero, [IntPtr]::Zero) # SCI_DOCUMENTEND
            [void][NppHistoryNative]::SendMessage($editor, 2329,
                [IntPtr]::Zero, [IntPtr]::Zero) # SCI_NEWLINE
            foreach ($character in $restoreSafetyMarker.ToCharArray()) {
                [void][NppHistoryNative]::SendMessage($editor, 0x0102,
                    [IntPtr][int]$character, [IntPtr]::Zero)
            }
            $restoreEditorWasModified = [NppHistoryNative]::SendMessage(
                $editor, 2159, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64() -ne 0
            [NppHistoryNative]::BeginLeftClick($historyList, 10, 55)
            Start-Sleep -Milliseconds 100
            [NppHistoryNative]::BeginCommand($historyPanel, 4104, [IntPtr]::Zero)
            $restoreDialog = [IntPtr]::Zero
            $restoreDeadline = [DateTime]::UtcNow.AddSeconds(5)
            while ([DateTime]::UtcNow -lt $restoreDeadline -and $restoreDialog -eq [IntPtr]::Zero) {
                Start-Sleep -Milliseconds 100
                $restoreDialog = [NppHistoryNative]::FindTopWindowContaining([uint32]$process.Id, 'NppHistory')
            }
            if ($restoreDialog -ne [IntPtr]::Zero) {
                $restoreDialogCentered = [NppHistoryNative]::CenteredOn(
                    $restoreDialog, $process.MainWindowHandle, 16)
                [void][NppHistoryNative]::SendMessage($restoreDialog, 0x0111, [IntPtr]6, [IntPtr]::Zero) # IDYES
                Start-Sleep -Milliseconds 500
                $restoredText = [IO.File]::ReadAllText($notePath)
                $afterRestoreCount = [NppHistoryNative]::SendMessage($historyList, 0x1004,
                    [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()
                $restoreActionPassed = $restoredText.Contains('revision only') -and
                    $restoredText.Contains('old wording') -and $afterRestoreCount -ge 2
                $remainingMetadata = (Get-ChildItem $pathMarker.DirectoryName -Filter *.meta | ForEach-Object {
                    [IO.File]::ReadAllText($_.FullName)
                }) -join "`n"
                $beforeRestoreMetadata = Get-ChildItem $pathMarker.DirectoryName -Filter *.meta |
                    Where-Object { [IO.File]::ReadAllText($_.FullName).Contains('reason=Before restore') } |
                    Select-Object -First 1
                $beforeRestoreRevision = if ($beforeRestoreMetadata) {
                    [IO.Path]::ChangeExtension($beforeRestoreMetadata.FullName, '.rev')
                } else { '' }
                $restoreSafetyPassed = $restoreEditorWasModified -and
                    $remainingMetadata.Contains('reason=Before restore') -and
                    (Test-Path -LiteralPath $beforeRestoreRevision) -and
                    [IO.File]::ReadAllText($beforeRestoreRevision).Contains($restoreSafetyMarker)

                # Notepad++ notices the intentionally external restore write and can
                # show its own reload prompt. Resolve it before opening another modal
                # plugin window so the runtime test does not create nested dialogs.
                $reloadDialog = [IntPtr]::Zero
                $reloadDeadline = [DateTime]::UtcNow.AddSeconds(3)
                while ([DateTime]::UtcNow -lt $reloadDeadline -and $reloadDialog -eq [IntPtr]::Zero) {
                    Start-Sleep -Milliseconds 100
                    $reloadDialog = [NppHistoryNative]::FindTopWindowContaining(
                        [uint32]$process.Id, 'Reload')
                }
                if ($reloadDialog -ne [IntPtr]::Zero) {
                    [void][NppHistoryNative]::SendMessage(
                        $reloadDialog, 0x0111, [IntPtr]6, [IntPtr]::Zero) # IDYES
                    Start-Sleep -Milliseconds 250
                }
            }
        }

        $actionLogPath = Join-Path $configFolder 'NppHistory.log'
        $actionLogText = if (Test-Path $actionLogPath) { [IO.File]::ReadAllText($actionLogPath) } else { '' }
        $commentUpdateLogged = $actionLogText.Contains('[INFO] Revision comment updated')
        $revisionDeletionLogged = $actionLogText.Contains('[INFO] Revision deleted') -and
            $actionLogText.Contains('Automated deletion audit')
        $restoreLogged = $actionLogText.Contains('[INFO] Restore')
    }

    $settingsWindow = [IntPtr]::Zero
    $settingsCentered = $false
    $settingsIconPassed = $false
    $settingsTabsPassed = $false
    $settingsGeneralPassed = $false
    $settingsAutoSavePassed = $false
    $autoSaveConflictNoticeHidden = $false
    $autoSaveConflictNoticeText = ''
    $autoSaveConflictNoticeVisible = $false
    $settingsAutoSaveEnablementPassed = $false
    $settingsTooltipsPassed = $false
    $settingsTooltipCount = 0
    $settingsTooltipActive = 0
    $settingsHotkeysPassed = $false
    $settingsHotkeyConflictValidationPassed = $false
    $settingsHistoryPassed = $false
    $settingsHistoryEnablementPassed = $false
    $settingsUpdateEnablementPassed = $false
    $settingsLoggingPassed = $false
    $settingsLoggingEnablementPassed = $false
    $manualUpdateCheckPassed = $false
    $manualUpdateAccessError = $false
    $updatePopupSuppressed = $false
    $updateDialogText = ''
    $updateTimestampPersisted = $false
    $settingsGeneralScreenshot = Join-Path $testRoot 'settings-general.png'
    $settingsScreenshot = Join-Path $testRoot 'settings-auto-save.png'
    $settingsHistoryScreenshot = Join-Path $testRoot 'settings-history.png'
    $settingsLoggingScreenshot = Join-Path $testRoot 'settings-logging.png'
    $settingsUpdatesScreenshot = Join-Path $testRoot 'settings-updates.png'
    if ($settingsMenuCommand -ne 0) {
        [NppHistoryNative]::MoveCursorAway()
        [NppHistoryNative]::BeginCommand($process.MainWindowHandle, $settingsMenuCommand, [IntPtr]::Zero)
        $settingsDeadline = [DateTime]::UtcNow.AddSeconds(5)
        while ([DateTime]::UtcNow -lt $settingsDeadline -and $settingsWindow -eq [IntPtr]::Zero) {
            Start-Sleep -Milliseconds 100
            $settingsWindow = [NppHistoryNative]::FindTopWindowContaining([uint32]$process.Id, 'NppHistory Settings')
        }
        if ($settingsWindow -ne [IntPtr]::Zero) {
            Start-Sleep -Milliseconds 250
            [void][NppHistoryNative]::UpdateWindow($settingsWindow)
            $mainRectangle = [NppHistoryNative+RECT]::new()
            $settingsRectangle = [NppHistoryNative+RECT]::new()
            [void][NppHistoryNative]::GetWindowRect($process.MainWindowHandle, [ref]$mainRectangle)
            [void][NppHistoryNative]::GetWindowRect($settingsWindow, [ref]$settingsRectangle)
            $settingsCentered = [Math]::Abs(($mainRectangle.Left + $mainRectangle.Right) - ($settingsRectangle.Left + $settingsRectangle.Right)) -le 12 -and
                [Math]::Abs(($mainRectangle.Top + $mainRectangle.Bottom) - ($settingsRectangle.Top + $settingsRectangle.Bottom)) -le 12
            $settingsIconPassed = [NppHistoryNative]::SendMessage($settingsWindow, 0x007F, [IntPtr]0, [IntPtr]::Zero) -ne [IntPtr]::Zero
            $settingsTabs = [NppHistoryNative]::FindControl($settingsWindow, 1070)
            $settingsTabsPassed = $settingsTabs -ne [IntPtr]::Zero -and
                [NppHistoryNative]::SendMessage($settingsTabs, 0x1304, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 5
            $settingsGeneralPassed = [NppHistoryNative]::Text([NppHistoryNative]::FindControl($settingsWindow, 1071)) -eq 'Capture' -and
                [NppHistoryNative]::Text([NppHistoryNative]::FindControl($settingsWindow, 1072)) -eq 'Compare' -and
                [NppHistoryNative]::Text([NppHistoryNative]::FindControl($settingsWindow, 1073)) -eq 'History' -and
                -not [NppHistoryNative]::IsWindowVisible([NppHistoryNative]::FindControl($settingsWindow, 1074))
            $settingsHotkeysPassed = [NppHistoryNative]::IsWindowVisible(
                [NppHistoryNative]::FindControl($settingsWindow, 1134)) -and
                [NppHistoryNative]::IsWindowEnabled([NppHistoryNative]::FindControl($settingsWindow, 1136)) -and
                [NppHistoryNative]::IsWindowEnabled([NppHistoryNative]::FindControl($settingsWindow, 1139)) -and
                [NppHistoryNative]::IsWindowEnabled([NppHistoryNative]::FindControl($settingsWindow, 1142)) -and
                [NppHistoryNative]::Text([NppHistoryNative]::FindControl($settingsWindow, 1145)) -eq 'No hotkey conflicts.'
            $settingsHotkeyConflictValidationPassed = [NppHistoryNative]::GetProp(
                $settingsWindow, 'NppHistoryHotkeysValid').ToInt64() -eq 2
            $captureHotkey = [NppHistoryNative]::FindControl($settingsWindow, 1136)
            $compareHotkey = [NppHistoryNative]::FindControl($settingsWindow, 1139)
            [void][NppHistoryNative]::SendMessage($compareHotkey, 0x0401, [IntPtr]0x0743, [IntPtr]::Zero)
            [void][NppHistoryNative]::SendMessage(
                $settingsWindow, 0x0111, [IntPtr](1139 -bor (0x0300 -shl 16)), $compareHotkey)
            Start-Sleep -Milliseconds 100
            $duplicateRejected = [NppHistoryNative]::GetProp(
                $settingsWindow, 'NppHistoryHotkeysValid').ToInt64() -eq 1 -and
                [NppHistoryNative]::Text([NppHistoryNative]::FindControl(
                    $settingsWindow, 1145)).Contains('selected more than once')
            [void][NppHistoryNative]::SendMessage($compareHotkey, 0x0401, [IntPtr]0x074D, [IntPtr]::Zero)
            [void][NppHistoryNative]::SendMessage(
                $settingsWindow, 0x0111, [IntPtr](1139 -bor (0x0300 -shl 16)), $compareHotkey)
            Start-Sleep -Milliseconds 100
            $settingsHotkeyConflictValidationPassed = $settingsHotkeyConflictValidationPassed -and
                $duplicateRejected -and [NppHistoryNative]::GetProp(
                    $settingsWindow, 'NppHistoryHotkeysValid').ToInt64() -eq 2
            if (-not [NppHistoryNative]::IsWindow($settingsWindow)) {
                throw ('Settings closed during hotkey validation. Top windows: ' +
                    [NppHistoryNative]::TopWindows([uint32]$process.Id))
            }
            Add-Type -AssemblyName System.Drawing
            $settingsCaptureRectangle = [NppHistoryNative+RECT]::new()
            [void][NppHistoryNative]::GetWindowRect($settingsWindow, [ref]$settingsCaptureRectangle)
            $bitmap = [Drawing.Bitmap]::new($settingsCaptureRectangle.Right - $settingsCaptureRectangle.Left, $settingsCaptureRectangle.Bottom - $settingsCaptureRectangle.Top)
            $graphics = [Drawing.Graphics]::FromImage($bitmap)
            $deviceContext = $graphics.GetHdc()
            try { [void][NppHistoryNative]::PrintWindow($settingsWindow, $deviceContext, 2) }
            finally { $graphics.ReleaseHdc($deviceContext); $graphics.Dispose() }
            $bitmap.Save($settingsGeneralScreenshot, [Drawing.Imaging.ImageFormat]::Png)
            $bitmap.Dispose()
            $autoTabPoint = [IntPtr](155 -bor (10 -shl 16))
            [void][NppHistoryNative]::SendMessage($settingsTabs, 0x0201, [IntPtr]1, $autoTabPoint)
            [void][NppHistoryNative]::SendMessage($settingsTabs, 0x0202, [IntPtr]::Zero, $autoTabPoint)
            Start-Sleep -Milliseconds 150
            $autoSaveConflictNotice = [NppHistoryNative]::FindControl($settingsWindow, 1133)
            $autoSaveConflictNoticeText = [NppHistoryNative]::Text($autoSaveConflictNotice)
            $autoSaveConflictNoticeVisible = [NppHistoryNative]::IsWindowVisible($autoSaveConflictNotice)
            $autoSaveConflictNoticeHidden = $autoSaveConflictNotice -ne [IntPtr]::Zero -and
                $autoSaveConflictNoticeText -eq 'Disabled because AutoSave.dll is installed.' -and
                -not $autoSaveConflictNoticeVisible
            $settingsAutoSavePassed = [NppHistoryNative]::IsWindowVisible([NppHistoryNative]::FindControl($settingsWindow, 1085)) -and
                [NppHistoryNative]::Text([NppHistoryNative]::FindControl($settingsWindow, 1010)) -eq 'Enable automatic saving' -and
                [NppHistoryNative]::Text([NppHistoryNative]::FindControl($settingsWindow, 1076)) -eq 'Notepad++ loses focus' -and
                [NppHistoryNative]::Text([NppHistoryNative]::FindControl($settingsWindow, 1077)) -eq 'At timed intervals every' -and
                [NppHistoryNative]::Text([NppHistoryNative]::FindControl($settingsWindow, 1079)) -eq 'File tab changes' -and
                [NppHistoryNative]::Text([NppHistoryNative]::FindControl($settingsWindow, 1080)) -eq 'Notepad++ exits' -and
                [NppHistoryNative]::Text([NppHistoryNative]::FindControl($settingsWindow, 1082)) -eq 'All open files' -and
                [NppHistoryNative]::Text([NppHistoryNative]::FindControl($settingsWindow, 1127)) -eq 'Disable Auto Save for:' -and
                [NppHistoryNative]::Text([NppHistoryNative]::FindControl($settingsWindow, 1131)) -eq 'One wildcard pattern per line' -and
                [NppHistoryNative]::IsWindowVisible([NppHistoryNative]::FindControl($settingsWindow, 1128))
            $autoSaveMaster = [NppHistoryNative]::FindControl($settingsWindow, 1010)
            $afterEditControl = [NppHistoryNative]::FindControl($settingsWindow, 1011)
            $intervalControl = [NppHistoryNative]::FindControl($settingsWindow, 1077)
            $intervalMinutesControl = [NppHistoryNative]::FindControl($settingsWindow, 1078)
            $autoSaveExclusions = [NppHistoryNative]::FindControl($settingsWindow, 1128)
            $intervalInitiallyDisabled = -not [NppHistoryNative]::IsWindowEnabled($intervalMinutesControl)
            $settingsTooltipCount = [NppHistoryNative]::GetProp(
                $settingsWindow, 'NppHistorySettingsTooltipCount').ToInt64()
            for ($settingsTooltipTick = 0; $settingsTooltipTick -lt 15; ++$settingsTooltipTick) {
                [NppHistoryNative]::MoveCursorToCenter($intervalMinutesControl)
                Start-Sleep -Milliseconds 100
                [void][NppHistoryNative]::SendMessage(
                    $settingsWindow, 0x0113, [IntPtr]0x4E51, [IntPtr]::Zero)
                if ([NppHistoryNative]::GetProp(
                    $settingsWindow, 'NppHistorySettingsTooltipActive').ToInt64() -eq 1078) { break }
            }
            $settingsTooltipWindow = [NppHistoryNative]::GetProp(
                $settingsWindow, 'NppHistorySettingsTooltipWindow')
            $settingsTooltipActive = [NppHistoryNative]::GetProp(
                $settingsWindow, 'NppHistorySettingsTooltipActive').ToInt64()
            $settingsTooltipsPassed = $settingsTooltipCount -eq 51 -and
                $settingsTooltipWindow -ne [IntPtr]::Zero
            [void][NppHistoryNative]::SendMessage($intervalControl, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
            $intervalEnabledOnSelection = [NppHistoryNative]::IsWindowEnabled($intervalMinutesControl)
            [void][NppHistoryNative]::SendMessage($intervalControl, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
            [void][NppHistoryNative]::SendMessage($autoSaveMaster, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
            $autoSaveChildrenDisabled = -not [NppHistoryNative]::IsWindowEnabled($afterEditControl) -and
                -not [NppHistoryNative]::IsWindowEnabled($intervalControl) -and
                -not [NppHistoryNative]::IsWindowEnabled($autoSaveExclusions)
            [void][NppHistoryNative]::SendMessage($autoSaveMaster, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
            $settingsAutoSaveEnablementPassed = $intervalInitiallyDisabled -and
                $intervalEnabledOnSelection -and $autoSaveChildrenDisabled -and
                [NppHistoryNative]::IsWindowEnabled($afterEditControl) -and
                [NppHistoryNative]::IsWindowEnabled($autoSaveExclusions)
            [NppHistoryNative]::SetText($autoSaveExclusions, "*.txt`r`n*.log")
            $settingsCaptureRectangle = [NppHistoryNative+RECT]::new()
            [void][NppHistoryNative]::GetWindowRect($settingsWindow, [ref]$settingsCaptureRectangle)
            $bitmap = [Drawing.Bitmap]::new($settingsCaptureRectangle.Right - $settingsCaptureRectangle.Left, $settingsCaptureRectangle.Bottom - $settingsCaptureRectangle.Top)
            $graphics = [Drawing.Graphics]::FromImage($bitmap)
            $deviceContext = $graphics.GetHdc()
            try { [void][NppHistoryNative]::PrintWindow($settingsWindow, $deviceContext, 2) }
            finally { $graphics.ReleaseHdc($deviceContext); $graphics.Dispose() }
            $bitmap.Save($settingsScreenshot, [Drawing.Imaging.ImageFormat]::Png)
            $bitmap.Dispose()
            $historyTabPoint = [IntPtr](215 -bor (10 -shl 16))
            [void][NppHistoryNative]::SendMessage($settingsTabs, 0x0201, [IntPtr]1, $historyTabPoint)
            [void][NppHistoryNative]::SendMessage($settingsTabs, 0x0202, [IntPtr]::Zero, $historyTabPoint)
            Start-Sleep -Milliseconds 100
            $settingsHistoryPassed = [NppHistoryNative]::IsWindowVisible([NppHistoryNative]::FindControl($settingsWindow, 1087)) -and
                [NppHistoryNative]::Text([NppHistoryNative]::FindControl($settingsWindow, 1096)) -eq 'Enable revision history' -and
                [NppHistoryNative]::Text([NppHistoryNative]::FindControl($settingsWindow, 1098)) -eq 'Before a file is saved' -and
                [NppHistoryNative]::Text([NppHistoryNative]::FindControl($settingsWindow, 1099)) -eq 'After a file is saved' -and
                [NppHistoryNative]::Text([NppHistoryNative]::FindControl($settingsWindow, 1101)) -eq 'Before restoring a revision' -and
                [NppHistoryNative]::Text([NppHistoryNative]::FindControl($settingsWindow, 1015)).Contains('.npphistory') -and
                [NppHistoryNative]::Text([NppHistoryNative]::FindControl($settingsWindow, 1129)) -eq 'Disable revision history for:' -and
                [NppHistoryNative]::Text([NppHistoryNative]::FindControl($settingsWindow, 1132)) -eq 'One wildcard pattern per line' -and
                [NppHistoryNative]::IsWindowVisible([NppHistoryNative]::FindControl($settingsWindow, 1130))
            $historyMaster = [NppHistoryNative]::FindControl($settingsWindow, 1096)
            $historyBeforeSave = [NppHistoryNative]::FindControl($settingsWindow, 1098)
            $historyCustom = [NppHistoryNative]::FindControl($settingsWindow, 1016)
            $historyPath = [NppHistoryNative]::FindControl($settingsWindow, 1017)
            $historyBrowse = [NppHistoryNative]::FindControl($settingsWindow, 1018)
            $historyExclusions = [NppHistoryNative]::FindControl($settingsWindow, 1130)
            $customInitiallyDisabled = -not [NppHistoryNative]::IsWindowEnabled($historyPath) -and
                -not [NppHistoryNative]::IsWindowEnabled($historyBrowse)
            [void][NppHistoryNative]::SendMessage($historyCustom, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
            $customEnabledOnSelection = [NppHistoryNative]::IsWindowEnabled($historyPath) -and
                [NppHistoryNative]::IsWindowEnabled($historyBrowse)
            $historyAdjacent = [NppHistoryNative]::FindControl($settingsWindow, 1015)
            [void][NppHistoryNative]::SendMessage($historyCustom, 0x00F1, [IntPtr]::Zero, [IntPtr]::Zero)
            [void][NppHistoryNative]::SendMessage($historyAdjacent, 0x00F1, [IntPtr]1, [IntPtr]::Zero)
            [void][NppHistoryNative]::SendMessage(
                $settingsWindow, 0x0111, [IntPtr]1015, $historyAdjacent)
            [void][NppHistoryNative]::SendMessage($historyMaster, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
            $historyChildrenDisabled = -not [NppHistoryNative]::IsWindowEnabled($historyBeforeSave) -and
                -not [NppHistoryNative]::IsWindowEnabled($historyCustom) -and
                -not [NppHistoryNative]::IsWindowEnabled($historyExclusions)
            [void][NppHistoryNative]::SendMessage($historyMaster, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
            $settingsHistoryEnablementPassed = $customInitiallyDisabled -and
                $customEnabledOnSelection -and $historyChildrenDisabled -and
                [NppHistoryNative]::IsWindowEnabled($historyBeforeSave) -and
                [NppHistoryNative]::IsWindowEnabled($historyExclusions)
            [NppHistoryNative]::SetText($historyExclusions, "*.txt`r`n*.tmp")
            $settingsCaptureRectangle = [NppHistoryNative+RECT]::new()
            [void][NppHistoryNative]::GetWindowRect($settingsWindow, [ref]$settingsCaptureRectangle)
            $bitmap = [Drawing.Bitmap]::new($settingsCaptureRectangle.Right - $settingsCaptureRectangle.Left, $settingsCaptureRectangle.Bottom - $settingsCaptureRectangle.Top)
            $graphics = [Drawing.Graphics]::FromImage($bitmap)
            $deviceContext = $graphics.GetHdc()
            try { [void][NppHistoryNative]::PrintWindow($settingsWindow, $deviceContext, 2) }
            finally { $graphics.ReleaseHdc($deviceContext); $graphics.Dispose() }
            $bitmap.Save($settingsHistoryScreenshot, [Drawing.Imaging.ImageFormat]::Png)
            $bitmap.Dispose()
            $loggingTabPoint = [IntPtr](267 -bor (10 -shl 16))
            [void][NppHistoryNative]::SendMessage($settingsTabs, 0x0201, [IntPtr]1, $loggingTabPoint)
            [void][NppHistoryNative]::SendMessage($settingsTabs, 0x0202, [IntPtr]::Zero, $loggingTabPoint)
            Start-Sleep -Milliseconds 100
            $loggingMaster = [NppHistoryNative]::FindControl($settingsWindow, 1107)
            $loggingLevel = [NppHistoryNative]::FindControl($settingsWindow, 1110)
            $loggingDefault = [NppHistoryNative]::FindControl($settingsWindow, 1112)
            $loggingCustom = [NppHistoryNative]::FindControl($settingsWindow, 1113)
            $loggingPath = [NppHistoryNative]::FindControl($settingsWindow, 1114)
            $loggingBrowse = [NppHistoryNative]::FindControl($settingsWindow, 1115)
            $loggingOpen = [NppHistoryNative]::FindControl($settingsWindow, 1116)
            $loggingMax = [NppHistoryNative]::FindControl($settingsWindow, 1120)
            $loggingRollover = [NppHistoryNative]::FindControl($settingsWindow, 1123)
            $loggingArchives = [NppHistoryNative]::FindControl($settingsWindow, 1125)
            $settingsLoggingPassed = [NppHistoryNative]::IsWindowVisible($loggingMaster) -and
                [NppHistoryNative]::Text($loggingMaster) -eq 'Enable plugin logging' -and
                [NppHistoryNative]::Text($loggingLevel) -eq 'Debug' -and
                [NppHistoryNative]::Text($loggingDefault).Contains('plugin configuration folder') -and
                [NppHistoryNative]::Text($loggingOpen) -eq 'Open Log' -and
                [NppHistoryNative]::Text($loggingRollover) -eq 'Start new archive'
            $customInitiallyDisabled = -not [NppHistoryNative]::IsWindowEnabled($loggingPath) -and
                -not [NppHistoryNative]::IsWindowEnabled($loggingBrowse)
            [void][NppHistoryNative]::SendMessage($loggingMaster, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
            $loggingChildrenDisabled = -not [NppHistoryNative]::IsWindowEnabled($loggingLevel) -and
                -not [NppHistoryNative]::IsWindowEnabled($loggingMax)
            [void][NppHistoryNative]::SendMessage($loggingMaster, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
            $settingsLoggingEnablementPassed = $customInitiallyDisabled -and $loggingChildrenDisabled -and
                [NppHistoryNative]::IsWindowEnabled($loggingLevel) -and
                [NppHistoryNative]::IsWindowEnabled($loggingArchives)
            [NppHistoryNative]::SetText($loggingMax, '6')
            $settingsCaptureRectangle = [NppHistoryNative+RECT]::new()
            [void][NppHistoryNative]::GetWindowRect($settingsWindow, [ref]$settingsCaptureRectangle)
            $bitmap = [Drawing.Bitmap]::new($settingsCaptureRectangle.Right - $settingsCaptureRectangle.Left, $settingsCaptureRectangle.Bottom - $settingsCaptureRectangle.Top)
            $graphics = [Drawing.Graphics]::FromImage($bitmap)
            $deviceContext = $graphics.GetHdc()
            try { [void][NppHistoryNative]::PrintWindow($settingsWindow, $deviceContext, 2) }
            finally { $graphics.ReleaseHdc($deviceContext); $graphics.Dispose() }
            $bitmap.Save($settingsLoggingScreenshot, [Drawing.Imaging.ImageFormat]::Png)
            $bitmap.Dispose()
            $updatesTabPoint = [IntPtr](322 -bor (10 -shl 16))
            [void][NppHistoryNative]::SendMessage($settingsTabs, 0x0201, [IntPtr]1, $updatesTabPoint)
            [void][NppHistoryNative]::SendMessage($settingsTabs, 0x0202, [IntPtr]::Zero, $updatesTabPoint)
            Start-Sleep -Milliseconds 100
            $updateMaster = [NppHistoryNative]::FindControl($settingsWindow, 1074)
            $updateFrequency = [NppHistoryNative]::FindControl($settingsWindow, 1075)
            $updatePrereleases = [NppHistoryNative]::FindControl($settingsWindow, 1102)
            $updateCheckNow = [NppHistoryNative]::FindControl($settingsWindow, 1103)
            $updateStatus = [NppHistoryNative]::FindControl($settingsWindow, 1106)
            $updateInstall = [NppHistoryNative]::FindControl($settingsWindow, 1126)
            $updateStatusText = [NppHistoryNative]::Text($updateStatus)
            $updateChildrenInitiallyEnabled = [NppHistoryNative]::IsWindowEnabled($updateFrequency) -and
                [NppHistoryNative]::IsWindowEnabled($updatePrereleases) -and
                [NppHistoryNative]::IsWindowEnabled($updateCheckNow)
            [void][NppHistoryNative]::SendMessage($updateMaster, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
            $updateChildrenDisabled = -not [NppHistoryNative]::IsWindowEnabled($updateFrequency) -and
                [NppHistoryNative]::IsWindowEnabled($updatePrereleases)
            [void][NppHistoryNative]::SendMessage($updateMaster, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
            $updateChildrenReenabled = [NppHistoryNative]::IsWindowEnabled($updateFrequency) -and
                [NppHistoryNative]::IsWindowEnabled($updatePrereleases)
            $settingsUpdateEnablementPassed = $updateChildrenInitiallyEnabled -and
                $updateChildrenDisabled -and $updateChildrenReenabled -and
                [NppHistoryNative]::IsWindowVisible($updateStatus) -and
                $updateStatusText.Contains('Status:') -and
                $updateStatusText.Contains('Next automatic check:') -and
                $updateStatusText.Contains("`r`n`r`nNext automatic check:") -and
                (-not $updateStatusText.Contains('Last successful check:') -or
                    $updateStatusText.Contains("`r`n`r`nLast successful check:")) -and
                $updateInstall -eq [IntPtr]::Zero
            $settingsCaptureRectangle = [NppHistoryNative+RECT]::new()
            [void][NppHistoryNative]::GetWindowRect($settingsWindow, [ref]$settingsCaptureRectangle)
            $bitmap = [Drawing.Bitmap]::new($settingsCaptureRectangle.Right - $settingsCaptureRectangle.Left, $settingsCaptureRectangle.Bottom - $settingsCaptureRectangle.Top)
            $graphics = [Drawing.Graphics]::FromImage($bitmap)
            $deviceContext = $graphics.GetHdc()
            try { [void][NppHistoryNative]::PrintWindow($settingsWindow, $deviceContext, 2) }
            finally { $graphics.ReleaseHdc($deviceContext); $graphics.Dispose() }
            $bitmap.Save($settingsUpdatesScreenshot, [Drawing.Imaging.ImageFormat]::Png)
            $bitmap.Dispose()
            [void][NppHistoryNative]::SendMessage($settingsWindow, 0x0111, [IntPtr]1103, [NppHistoryNative]::FindControl($settingsWindow, 1103))
            $checkingTextShown = [NppHistoryNative]::Text($updateStatus).Contains('Checking...') -and
                -not [NppHistoryNative]::IsWindowEnabled($updateCheckNow)
            $updateDeadline = [DateTime]::UtcNow.AddSeconds(20)
            while ([DateTime]::UtcNow -lt $updateDeadline -and
                [NppHistoryNative]::Text($updateStatus).Contains('Checking...')) {
                Start-Sleep -Milliseconds 100
            }
            $updateDialogText = [NppHistoryNative]::Text($updateStatus)
            $manualUpdateAccessError = $updateDialogText.Contains('Last check failed:')
            $successfulUpdateStatus = $updateDialogText.Contains('Status: Up to date') -or
                $updateDialogText.Contains('Status: Update available:')
            $expectedStatusMatched = -not $updateFeedAccessible -or
                $updateDialogText.Contains($expectedUpdateStatus)
            $manualUpdateCheckPassed = $checkingTextShown -and
                (($successfulUpdateStatus -and $expectedStatusMatched) -or $manualUpdateAccessError) -and
                [NppHistoryNative]::IsWindowEnabled($updateCheckNow)
            $updatePopupSuppressed = [NppHistoryNative]::FindTopWindowContaining(
                [uint32]$process.Id, 'NppHistory Update Check') -eq [IntPtr]::Zero
            $preSaveHotkeyStatus = [NppHistoryNative]::Text(
                [NppHistoryNative]::FindControl($settingsWindow, 1145))
            if ([NppHistoryNative]::GetProp(
                    $settingsWindow, 'NppHistoryHotkeysValid').ToInt64() -ne 2) {
                throw "Hotkey validation became invalid before save: $preSaveHotkeyStatus"
            }
            $historyCustomSelected = [NppHistoryNative]::SendMessage(
                [NppHistoryNative]::FindControl($settingsWindow, 1016),
                0x00F0, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 1
            $loggingCustomSelected = [NppHistoryNative]::SendMessage(
                [NppHistoryNative]::FindControl($settingsWindow, 1113),
                0x00F0, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 1
            if ($historyCustomSelected -and -not [NppHistoryNative]::Text(
                    [NppHistoryNative]::FindControl($settingsWindow, 1017))) {
                throw 'History custom location remained selected with no path before save.'
            }
            if ($loggingCustomSelected -and -not [NppHistoryNative]::Text(
                    [NppHistoryNative]::FindControl($settingsWindow, 1114))) {
                throw 'Logging custom location remained selected with no path before save.'
            }
            [void][NppHistoryNative]::SendMessage($settingsWindow, 0x0111, [IntPtr]1, [IntPtr]::Zero)
        }
    }
    Start-Sleep -Milliseconds 250
    $savedSettingsText = [IO.File]::ReadAllText((Join-Path $configFolder 'NppHistory.ini'))
    $timestampMatch = [regex]::Match($savedSettingsText, '(?m)^LastUpdateCheck=(\d+)\s*$')
    $updateTimestampPersisted = if ($manualUpdateAccessError) {
        $savedSettingsText.Contains('LastUpdateStatus=Last check failed:')
    } else {
        $timestampMatch.Success -and [uint64]$timestampMatch.Groups[1].Value -gt 0 -and
            (-not $updateFeedAccessible -or $savedSettingsText.Contains('LastUpdateStatus=' + $expectedUpdateStatus))
    }
    $exclusionSettingsPersisted = $savedSettingsText.Contains('AutoSaveExclusions=*.txt|*.log') -and
        $savedSettingsText.Contains('HistoryExclusions=*.txt|*.tmp')
    $toolbarHotkeySettingsPersisted = $savedSettingsText.Contains('ToolbarHistory=1') -and
        $savedSettingsText.Contains('HotkeyCaptureEnabled=1') -and
        $savedSettingsText.Contains('HotkeyCompareEnabled=1') -and
        $savedSettingsText.Contains('HotkeyHistoryEnabled=1')
    $exclusionStatus = [NppHistoryNative]::FindControl($historyPanel, 1104)
    $exclusionPanelIndicatorPassed = [NppHistoryNative]::IsWindowVisible($exclusionStatus) -and
        [NppHistoryNative]::Text($exclusionStatus) -eq 'File Excluded in Settings' -and
        -not [NppHistoryNative]::IsWindowEnabled([NppHistoryNative]::FindControl($historyPanel, 1006)) -and
        -not [NppHistoryNative]::IsWindowEnabled([NppHistoryNative]::FindControl($historyPanel, 1003))
    $excludedCommandStatePassed = -not [NppHistoryNative]::MenuCommandEnabled(
        $process.MainWindowHandle, $captureMenuCommand) -and
        -not [NppHistoryNative]::ToolbarCommandEnabled($process.MainWindowHandle, $captureMenuCommand) -and
        -not [NppHistoryNative]::MenuCommandEnabled($process.MainWindowHandle, $compareMenuCommand) -and
        -not [NppHistoryNative]::ToolbarCommandEnabled($process.MainWindowHandle, $compareMenuCommand)
    $excludedRefreshButton = [NppHistoryNative]::FindControl($historyPanel, 1003)
    for ($tooltipTick = 0; $tooltipTick -lt 15; ++$tooltipTick) {
        [NppHistoryNative]::MoveCursorToCenter($excludedRefreshButton)
        Start-Sleep -Milliseconds 100
        [void][NppHistoryNative]::SendMessage($historyPanel, 0x0113, [IntPtr]0x4E50, [IntPtr]::Zero)
        if ([NppHistoryNative]::GetProp(
            $historyPanel, 'NppHistoryPanelTooltipActive').ToInt64() -eq 1003) { break }
    }
    $disabledRefreshTooltip = [NppHistoryNative]::GetProp(
        $historyPanel, 'NppHistoryPanelButtonTooltipWindow')
    $disabledRefreshTooltipActive = [NppHistoryNative]::GetProp(
        $historyPanel, 'NppHistoryPanelTooltipActive').ToInt64()
    $disabledRefreshTooltipPassed = $disabledRefreshTooltipActive -eq 1003 -and
        $disabledRefreshTooltip -ne [IntPtr]::Zero -and
        [NppHistoryNative]::IsWindowVisible($disabledRefreshTooltip)
    Start-Sleep -Milliseconds 750
    $autoSaveTabIndicatorCount = [NppHistoryNative]::GetProp(
        $process.MainWindowHandle, 'NppHistoryAutoSaveTabIndicatorCount').ToInt64()
    $historyTabIndicatorCount = [NppHistoryNative]::GetProp(
        $process.MainWindowHandle, 'NppHistoryHistoryTabIndicatorCount').ToInt64()
    $excludedDocumentTabCount = [NppHistoryNative]::GetProp(
        $process.MainWindowHandle, 'NppHistoryExcludedDocumentTabCount').ToInt64()
    $normalDocumentTabCount = [NppHistoryNative]::GetProp(
        $process.MainWindowHandle, 'NppHistoryNormalDocumentTabCount').ToInt64()
    $documentTabCount = [NppHistoryNative]::GetProp(
        $process.MainWindowHandle, 'NppHistoryDocumentTabCount').ToInt64()
    $tabIndicatorIconCount = [NppHistoryNative]::GetProp(
        $process.MainWindowHandle, 'NppHistoryTabIndicatorIconCount').ToInt64()
    $reservedDocumentTabCount = [NppHistoryNative]::GetProp(
        $process.MainWindowHandle, 'NppHistoryReservedDocumentTabCount').ToInt64()
    $tabReservationPassed = $reservedDocumentTabCount -eq $excludedDocumentTabCount
    $exclusionTabIndicatorsPassed = $autoSaveTabIndicatorCount -ge 1 -and
        $historyTabIndicatorCount -ge 1 -and $excludedDocumentTabCount -eq 1 -and
        $normalDocumentTabCount -ge 8 -and $documentTabCount -ge 9 -and
        $tabIndicatorIconCount -eq 2 -and $tabReservationPassed
    $exclusionIndicatorsScreenshot = Join-Path $testRoot 'exclusion-indicators.png'
    $exclusionRectangle = [NppHistoryNative+RECT]::new()
    if ([NppHistoryNative]::GetWindowRect($process.MainWindowHandle, [ref]$exclusionRectangle)) {
        $bitmap = [Drawing.Bitmap]::new($exclusionRectangle.Right - $exclusionRectangle.Left,
            $exclusionRectangle.Bottom - $exclusionRectangle.Top)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        $deviceContext = $graphics.GetHdc()
        try { [void][NppHistoryNative]::PrintWindow($process.MainWindowHandle, $deviceContext, 2) }
        finally { $graphics.ReleaseHdc($deviceContext); $graphics.Dispose() }
        $bitmap.Save($exclusionIndicatorsScreenshot, [Drawing.Imaging.ImageFormat]::Png)
        $bitmap.Dispose()
    }
    $excludedDiskBefore = [IO.File]::ReadAllText($notePath)
    $excludedRevisionCountBefore = @(Get-ChildItem $pathMarker.DirectoryName -Filter *.rev).Count
    [void][NppHistoryNative]::SendMessage($editor, 2318, [IntPtr]::Zero, [IntPtr]::Zero) # SCI_DOCUMENTEND
    foreach ($character in "`r`nexcluded automatic save edit".ToCharArray()) {
        [void][NppHistoryNative]::SendMessage($editor, 0x0102, [IntPtr][int]$character, [IntPtr]::Zero)
    }
    Start-Sleep -Seconds 12
    $autoSaveExclusionEnforced = [IO.File]::ReadAllText($notePath) -eq $excludedDiskBefore -and
        [NppHistoryNative]::SendMessage($editor, 2159, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 1
    [void][NppHistoryNative]::SendMessage($process.MainWindowHandle, 0x0111, [IntPtr]41006, [IntPtr]::Zero) # IDM_FILE_SAVE
    Start-Sleep -Milliseconds 750
    $historyExclusionEnforced = @(Get-ChildItem $pathMarker.DirectoryName -Filter *.rev).Count -eq
        $excludedRevisionCountBefore
    $manualSaveAllowedForExcludedFile = [IO.File]::ReadAllText($notePath).Contains(
        'excluded automatic save edit')

    $aboutWindow = [IntPtr]::Zero
    $aboutCentered = $false
    $aboutWindowPassed = $false
    $aboutScreenshot = Join-Path $testRoot 'about-npphistory.png'
    if ($aboutMenuCommand -ne 0) {
        [NppHistoryNative]::BeginCommand($process.MainWindowHandle, $aboutMenuCommand, [IntPtr]::Zero)
        $aboutDeadline = [DateTime]::UtcNow.AddSeconds(5)
        while ([DateTime]::UtcNow -lt $aboutDeadline -and $aboutWindow -eq [IntPtr]::Zero) {
            Start-Sleep -Milliseconds 100
            $aboutWindow = [NppHistoryNative]::FindTopWindowContaining([uint32]$process.Id, 'About NppHistory')
        }
        if ($aboutWindow -ne [IntPtr]::Zero) {
            $mainRectangle = [NppHistoryNative+RECT]::new()
            $aboutRectangle = [NppHistoryNative+RECT]::new()
            [void][NppHistoryNative]::GetWindowRect($process.MainWindowHandle, [ref]$mainRectangle)
            [void][NppHistoryNative]::GetWindowRect($aboutWindow, [ref]$aboutRectangle)
            $aboutCentered = [Math]::Abs(($mainRectangle.Left + $mainRectangle.Right) - ($aboutRectangle.Left + $aboutRectangle.Right)) -le 12 -and
                [Math]::Abs(($mainRectangle.Top + $mainRectangle.Bottom) - ($aboutRectangle.Top + $aboutRectangle.Bottom)) -le 12
            $aboutVersion = [NppHistoryNative]::Text([NppHistoryNative]::FindControl($aboutWindow, 1061))
            $aboutAuthor = [NppHistoryNative]::Text([NppHistoryNative]::FindControl($aboutWindow, 1062))
            $aboutReleaseDate = [NppHistoryNative]::Text([NppHistoryNative]::FindControl($aboutWindow, 1063))
            $aboutCaptionIcon = [NppHistoryNative]::SendMessage($aboutWindow, 0x007F, [IntPtr]0, [IntPtr]::Zero)
            $aboutContentIcon = [NppHistoryNative]::FindControl($aboutWindow, 1060)
            $expectedReleaseDate = if ($embeddedReleaseDate) {
                ([DateTime]$embeddedReleaseDate).ToString('d', [Globalization.CultureInfo]::CurrentCulture)
            } else { '' }
            $releaseDatePassed = if ($expectedReleaseDate) {
                $aboutReleaseDate.Contains($expectedReleaseDate)
            } else {
                $aboutReleaseDate.Trim() -eq 'Release Date:'
            }
            $aboutWindowPassed = $aboutVersion.Contains($expectedInstalledDisplayVersion) -and
                $aboutAuthor.Contains('Terry Rogers') -and $aboutAuthor.Contains('terryrogers.me') -and
                $releaseDatePassed -and
                $aboutCaptionIcon -ne [IntPtr]::Zero -and $aboutContentIcon -ne [IntPtr]::Zero
            Add-Type -AssemblyName System.Drawing
            $bitmap = [Drawing.Bitmap]::new($aboutRectangle.Right - $aboutRectangle.Left, $aboutRectangle.Bottom - $aboutRectangle.Top)
            $graphics = [Drawing.Graphics]::FromImage($bitmap)
            $deviceContext = $graphics.GetHdc()
            try { [void][NppHistoryNative]::PrintWindow($aboutWindow, $deviceContext, 2) }
            finally { $graphics.ReleaseHdc($deviceContext); $graphics.Dispose() }
            $bitmap.Save($aboutScreenshot, [Drawing.Imaging.ImageFormat]::Png)
            $bitmap.Dispose()
            [void][NppHistoryNative]::PostMessage($aboutWindow, 0x0111, [IntPtr]1, [IntPtr]::Zero)
        }
    }

    $newCommand = 41001 # Notepad++ IDM_FILE_NEW
    if ($newCommand -ne 0) {
        [void][NppHistoryNative]::SendMessage($process.MainWindowHandle, 0x0111,
            [IntPtr]$newCommand, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 500
    }
    $unsavedWarning = [NppHistoryNative]::FindControl($historyPanel, 1104)
    $unsavedPaneStatePassed = $newCommand -ne 0 -and
        [NppHistoryNative]::IsWindowVisible($unsavedWarning) -and
        [NppHistoryNative]::Text($unsavedWarning) -eq 'Save File First' -and
        (1006,1003,1004,1005 | ForEach-Object {
            [NppHistoryNative]::IsWindowEnabled([NppHistoryNative]::FindControl($historyPanel, $_))
        } | Where-Object { $_ }).Count -eq 0
    $unsavedCommandStatePassed = -not [NppHistoryNative]::MenuCommandEnabled(
        $process.MainWindowHandle, $captureMenuCommand) -and
        -not [NppHistoryNative]::ToolbarCommandEnabled($process.MainWindowHandle, $captureMenuCommand) -and
        -not [NppHistoryNative]::MenuCommandEnabled($process.MainWindowHandle, $compareMenuCommand) -and
        -not [NppHistoryNative]::ToolbarCommandEnabled($process.MainWindowHandle, $compareMenuCommand)
    $unsavedPanelScreenshot = Join-Path $testRoot 'history-panel-unsaved.png'
    $unsavedRectangle = [NppHistoryNative+RECT]::new()
    if ([NppHistoryNative]::GetWindowRect($historyPanel, [ref]$unsavedRectangle)) {
        $bitmap = [Drawing.Bitmap]::new($unsavedRectangle.Right - $unsavedRectangle.Left,
            $unsavedRectangle.Bottom - $unsavedRectangle.Top)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        $deviceContext = $graphics.GetHdc()
        try { [void][NppHistoryNative]::PrintWindow($historyPanel, $deviceContext, 2) }
        finally { $graphics.ReleaseHdc($deviceContext); $graphics.Dispose() }
        $bitmap.Save($unsavedPanelScreenshot, [Drawing.Imaging.ImageFormat]::Png)
        $bitmap.Dispose()
    }

    $logPath = Join-Path $configFolder 'NppHistory.log'
    $logText = if (Test-Path $logPath) { [IO.File]::ReadAllText($logPath) } else { '' }
    $loggingEventsPassed = $logText.Contains('[INFO] File saved') -and
        $logText.Contains('[INFO] Revision created') -and
        $logText.Contains('[INFO] Capture') -and
        $logText.Contains('[INFO] Compare') -and
        $logText.Contains('[INFO] Settings changed') -and
        $commentUpdateLogged -and $revisionDeletionLogged -and $restoreLogged -and
        ($SkipAutomaticUpdateWait.IsPresent -or
            ($logText.Contains('Automatic update check started') -and
                ($logText.Contains('Automatic update check completed') -or
                    $logText.Contains('[WARNING] Update check failure')))) -and
        $logText.Contains('Manual update check started') -and
        ($logText.Contains('Manual update check completed') -or
            $logText.Contains('[WARNING] Update check failure')) -and
        $logText.Contains('[DEBUG] Button click')
    $settingsControlLoggingPassed = $logText.Contains('[DEBUG] Settings tab | Logging') -and
        $logText.Contains('[DEBUG] Settings control | Plugin logging enabled') -and
        $logText.Contains('[DEBUG] Settings control | OK') -and
        -not ($logText -match '\[DEBUG\] Settings control \| \d+')
    $settingsChangeLoggingPassed =
        $logText.Contains('[DEBUG] Setting change | Auto Save exclusions: (not set) -> *.txt  *.log') -and
        $logText.Contains('[DEBUG] Setting change | History exclusions: (not set) -> *.txt  *.tmp') -and
        $logText.Contains('[INFO] Settings changed | 3 option(s) updated')
    $singleRouteButtonLoggingPassed =
        [regex]::Matches($logText, '(?m)\[DEBUG\] Button click \| Capture\r?$').Count -eq 1 -and
        [regex]::Matches($logText, '(?m)\[DEBUG\] Button click \| Settings\r?$').Count -eq 1 -and
        [regex]::Matches($logText, '(?m)\[DEBUG\] Button click \| About\r?$').Count -eq 1
    $popupCenteringPassed = $editDialogCentered -and $deleteDialogCentered -and
        $restoreDialogCentered -and $comparisonCentered -and $settingsCentered -and
        $aboutCentered
    $displayVersionLoggingPassed = -not $updateFeedAccessible -or
        $logText.Contains($expectedUpdateStatus)

    $autoSaveCorrect = $savedText.Contains('new wording') -and $savedText.Contains('current only') -and $savedText.Contains('changed middle 060') -and -not $savedText.Contains('revision only') -and -not $savedText.Contains('unchanged line 100')
    $passed = $autoSaveCorrect -and $revisions.Count -eq 2 -and $reasonsCaptured -and $hiddenHistoryRoot -and $automaticUpdateCheckPassed -and $pluginMenuPassed -and $pluginMenuIconsPassed -and $hiddenCaptureStatePassed -and $hiddenCompareStatePassed -and $hiddenPaneComparePassed -and $panelButtonsPassed -and $panelButtonWidthsPassed -and $savedPaneStatePassed -and $visibleUnselectedCommandStatePassed -and $selectedPaneActionsPassed -and $visibleSelectedCommandStatePassed -and $unsavedPaneStatePassed -and $unsavedCommandStatePassed -and $panelButtonIconsPassed -and $panelButtonHoverPassed -and $panelButtonTooltipsPassed -and $disabledRefreshTooltipPassed -and $revisionActionsPassed -and $captureButtonPassed -and $commentUpdatePassed -and $commentUpdateLogged -and $revisionDeletionPassed -and $revisionDeletionLogged -and $restoreActionPassed -and $restoreSafetyPassed -and $restoreLogged -and $mainToolbarButtonsRegistered -eq 3 -and $dockIconPassed -and $responsiveButtonsPassed -and $comparisonOpened -and $comparisonCentered -and $comparisonIconsPassed -and $sharedScrollPassed -and $lineNumbersRendered -and $differenceNavigationPassed -and $currentDifferencePassed -and $revisionToolbarNavigationPassed -and $allToolbarHintsRegistered -and $tooltipHoverPassed -and $headerDoubleClickPassed -and $winMergePaletteRendered -and $locationPaneCollapsePassed -and $settingsCentered -and $settingsIconPassed -and $settingsTabsPassed -and $settingsGeneralPassed -and $settingsHotkeysPassed -and $settingsHotkeyConflictValidationPassed -and $toolbarHotkeySettingsPersisted -and $settingsUpdateEnablementPassed -and $settingsLoggingPassed -and $settingsLoggingEnablementPassed -and $loggingEventsPassed -and $settingsControlLoggingPassed -and $settingsChangeLoggingPassed -and $singleRouteButtonLoggingPassed -and $displayVersionLoggingPassed -and $settingsAutoSavePassed -and $autoSaveConflictNoticeHidden -and $settingsAutoSaveEnablementPassed -and $settingsTooltipsPassed -and $settingsHistoryPassed -and $settingsHistoryEnablementPassed -and $exclusionSettingsPersisted -and $exclusionPanelIndicatorPassed -and $excludedCommandStatePassed -and $exclusionTabIndicatorsPassed -and $autoSaveExclusionEnforced -and $historyExclusionEnforced -and $manualSaveAllowedForExcludedFile -and $manualUpdateCheckPassed -and $updatePopupSuppressed -and $updateTimestampPersisted -and $aboutCentered -and $aboutWindowPassed -and $popupCenteringPassed
    [pscustomobject]@{
        AutoSaveUpdatedFile = $autoSaveCorrect
        EditorLengthBefore = $lengthBefore
        EditorLengthAfter = $lengthAfter
        EditorMarkedModified = $modifiedAfter
        RevisionCount = $revisions.Count
        PreAndPostSaveCaptured = $reasonsCaptured
        HiddenAdjacentHistory = $hiddenHistoryRoot
        InternalCatalogCreated = (Test-Path (Join-Path $configFolder 'catalog.db'))
        AutomaticUpdateCheckPassed = $automaticUpdateCheckPassed
        HistoryPanelRevisionCount = $historyListCount
        HistoryPanelScreenshot = if (Test-Path $historyPanelScreenshot) { $historyPanelScreenshot } else { '' }
        PluginMenuPassed = $pluginMenuPassed
        PluginMenuIconsPassed = $pluginMenuIconsPassed
        PluginMenuBitmapCount = $pluginMenuBitmapCount
        PluginMenuLabels = $pluginMenuLabels
        HiddenCaptureCommandStatePassed = $hiddenCaptureStatePassed
        HiddenCompareCommandStatePassed = $hiddenCompareStatePassed
        HiddenPaneDefaultComparePassed = $hiddenPaneComparePassed
        PanelButtonsPassed = $panelButtonsPassed
        PanelButtonWidthsPassed = $panelButtonWidthsPassed
        PanelButtonWidths = $panelButtonWidths -join ','
        SavedFilePaneStatePassed = $savedPaneStatePassed
        VisibleUnselectedCommandStatePassed = $visibleUnselectedCommandStatePassed
        SelectedRevisionPaneActionsPassed = $selectedPaneActionsPassed
        VisibleSelectedCommandStatePassed = $visibleSelectedCommandStatePassed
        UnsavedFilePaneStatePassed = $unsavedPaneStatePassed
        UnsavedCommandStatePassed = $unsavedCommandStatePassed
        UnsavedWarningText = [NppHistoryNative]::Text($unsavedWarning)
        UnsavedWarningVisible = [NppHistoryNative]::IsWindowVisible($unsavedWarning)
        UnsavedPanelScreenshot = if (Test-Path $unsavedPanelScreenshot) { $unsavedPanelScreenshot } else { '' }
        PanelButtonIconsPassed = $panelButtonIconsPassed
        PanelButtonHoverPassed = $panelButtonHoverPassed
        PanelButtonTooltipsPassed = $panelButtonTooltipsPassed
        PanelButtonTooltipCount = $panelButtonTooltipCount
        DisabledRefreshTooltipPassed = $disabledRefreshTooltipPassed
        DisabledRefreshTooltipActive = $disabledRefreshTooltipActive
        PanelButtonHoverRegistrationCount = $panelButtonHoverRegistrationCount
        PanelButtonHotOnEnter = $panelButtonHotOnEnter
        PanelButtonHotAfterLeave = $panelButtonHotAfterLeave
        RevisionActionsPassed = $revisionActionsPassed
        CaptureButtonPassed = $captureButtonPassed
        CommentUpdatePassed = $commentUpdatePassed
        CommentUpdateLogged = $commentUpdateLogged
        RevisionDeletionPassed = $revisionDeletionPassed
        RevisionDeletionLogged = $revisionDeletionLogged
        RestoreActionPassed = $restoreActionPassed
        RestoreSafetyPassed = $restoreSafetyPassed
        RestoreLogged = $restoreLogged
        PopupCenteringPassed = $popupCenteringPassed
        EditCommentCentered = $editDialogCentered
        DeleteConfirmationCentered = $deleteDialogCentered
        RestoreConfirmationCentered = $restoreDialogCentered
        MainToolbarButtonsRegistered = $mainToolbarButtonsRegistered
        DockIconPassed = $dockIconPassed
        ResponsiveButtonsPassed = $responsiveButtonsPassed
        HistoryPanelDescription = [NppHistoryNative]::Describe($historyPanel)
        ResponsiveButtonRowsValue = [NppHistoryNative]::GetProp($historyPanel, 'NppHistoryResponsiveButtonRows').ToInt64()
        SideBySideComparisonOpened = $comparisonOpened
        ComparisonCentered = $comparisonCentered
        ComparisonCenterDelta = $comparisonCenterDelta
        WinMergeFramePresent = $winMergeFramePresent
        ToolbarButtonCount = $toolbarButtonCount
        ComparisonIconsPassed = $comparisonIconsPassed
        ComparisonIconSize = "$comparisonIconSize x $comparisonIconSize"
        ToolbarTooltipCount = $tooltipToolCount
        ToolbarTooltipTextPassed = $tooltipTextPassed
        ToolbarTooltipHoverPassed = $tooltipHoverPassed
        ToolbarTooltipHoverText = $tooltipHoverText
        StandardWindowFrame = $standardWindowFrame
        HeaderLabelsPassed = $headerLabelsPassed
        ComparisonCaption = $captionText
        CaptionIconPresent = $captionIcon -ne [IntPtr]::Zero
        HeaderDoubleClickPassed = $headerDoubleClickPassed
        LocationPaneCollapsePassed = $locationPaneCollapsePassed
        SynchronizedScrollPassed = $sharedScrollPassed
        LineNumbersRendered = $lineNumbersRendered
        DifferenceNavigationPassed = $differenceNavigationPassed
        CurrentDifferencePassed = $currentDifferencePassed
        CurrentDifferenceTopLine = $currentDifferenceTop
        LocationVisibleAfterCurrent = $locationVisibleAfterCurrent
        TooltipCountAfterCurrent = $tooltipToolCountAfterCurrent
        RevisionToolbarNavigationPassed = $revisionToolbarNavigationPassed
        WinMergePaletteRendered = $winMergePaletteRendered
        RevisionSelectorCount = $revisionChoiceCount
        ComparisonScreenshot = if (Test-Path $comparisonScreenshot) { $comparisonScreenshot } else { '' }
        SettingsCentered = $settingsCentered
        SettingsIconPassed = $settingsIconPassed
        SettingsTabsPassed = $settingsTabsPassed
        SettingsGeneralPassed = $settingsGeneralPassed
        SettingsHotkeysPassed = $settingsHotkeysPassed
        SettingsHotkeyConflictValidationPassed = $settingsHotkeyConflictValidationPassed
        ToolbarHotkeySettingsPersisted = $toolbarHotkeySettingsPersisted
        SettingsUpdateEnablementPassed = $settingsUpdateEnablementPassed
        SettingsLoggingPassed = $settingsLoggingPassed
        SettingsLoggingEnablementPassed = $settingsLoggingEnablementPassed
        LoggingEventsPassed = $loggingEventsPassed
        SettingsControlLoggingPassed = $settingsControlLoggingPassed
        SettingsChangeLoggingPassed = $settingsChangeLoggingPassed
        SingleRouteButtonLoggingPassed = $singleRouteButtonLoggingPassed
        DisplayVersionLoggingPassed = $displayVersionLoggingPassed
        LogPath = $logPath
        ManualUpdateCheckPassed = $manualUpdateCheckPassed
        UpdateFeedAccessible = $updateFeedAccessible
        ExpectedPublishedVersion = $expectedPublishedVersion
        UpdatePopupSuppressed = $updatePopupSuppressed
        ManualUpdateAccessError = $manualUpdateAccessError
        UpdateDialogText = $updateDialogText
        UpdateTimestampPersisted = $updateTimestampPersisted
        SettingsAutoSavePassed = $settingsAutoSavePassed
        AutoSaveConflictNoticeHidden = $autoSaveConflictNoticeHidden
        AutoSaveConflictNoticeText = $autoSaveConflictNoticeText
        AutoSaveConflictNoticeVisible = $autoSaveConflictNoticeVisible
        SettingsAutoSaveEnablementPassed = $settingsAutoSaveEnablementPassed
        SettingsTooltipsPassed = $settingsTooltipsPassed
        SettingsTooltipCount = $settingsTooltipCount
        SettingsTooltipActive = $settingsTooltipActive
        SettingsHistoryPassed = $settingsHistoryPassed
        SettingsHistoryEnablementPassed = $settingsHistoryEnablementPassed
        ExclusionSettingsPersisted = $exclusionSettingsPersisted
        ExclusionPanelIndicatorPassed = $exclusionPanelIndicatorPassed
        ExcludedCommandStatePassed = $excludedCommandStatePassed
        ExclusionPanelIndicatorText = [NppHistoryNative]::Text($exclusionStatus)
        ExclusionTabIndicatorsPassed = $exclusionTabIndicatorsPassed
        AutoSaveTabIndicatorCount = $autoSaveTabIndicatorCount
        HistoryTabIndicatorCount = $historyTabIndicatorCount
        ExcludedDocumentTabCount = $excludedDocumentTabCount
        DocumentTabReservedSpacePassed = $tabReservationPassed
        ReservedDocumentTabCount = $reservedDocumentTabCount
        NormalDocumentTabCount = $normalDocumentTabCount
        DocumentTabCount = $documentTabCount
        TabIndicatorIconCount = $tabIndicatorIconCount
        ExclusionIndicatorsScreenshot = if (Test-Path $exclusionIndicatorsScreenshot) {
            $exclusionIndicatorsScreenshot
        } else { '' }
        AutoSaveExclusionEnforced = $autoSaveExclusionEnforced
        HistoryExclusionEnforced = $historyExclusionEnforced
        ManualSaveAllowedForExcludedFile = $manualSaveAllowedForExcludedFile
        SettingsGeneralScreenshot = if (Test-Path $settingsGeneralScreenshot) { $settingsGeneralScreenshot } else { '' }
        SettingsScreenshot = if (Test-Path $settingsScreenshot) { $settingsScreenshot } else { '' }
        SettingsHistoryScreenshot = if (Test-Path $settingsHistoryScreenshot) { $settingsHistoryScreenshot } else { '' }
        SettingsLoggingScreenshot = if (Test-Path $settingsLoggingScreenshot) { $settingsLoggingScreenshot } else { '' }
        SettingsUpdatesScreenshot = if (Test-Path $settingsUpdatesScreenshot) { $settingsUpdatesScreenshot } else { '' }
        AboutCentered = $aboutCentered
        AboutWindowPassed = $aboutWindowPassed
        AboutScreenshot = if (Test-Path $aboutScreenshot) { $aboutScreenshot } else { '' }
        Passed = $passed
    }
    if (-not $passed) { exit 1 }
}
finally {
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id
        $process.WaitForExit(5000) | Out-Null
    }
}
