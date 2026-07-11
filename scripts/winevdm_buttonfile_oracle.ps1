param(
    [string]$LegacyExe = $env:CARDSTACK_LEGACY_EXE,
    [string]$LegacyDir = $env:CARDSTACK_LEGACY_DIR,
    [string]$WineVdmDir = "",
    [string]$ScenarioPath = "",
    [string]$OutputDir = "",
    [string]$RuntimeDir = "",
    [string]$StageOnlyDeck = "",
    [string]$LaunchDocument = "",
    [string[]]$Fixture = @(),
    [switch]$IncludeDisabled,
    [switch]$Interactive,
    [switch]$NoDownload,
    [switch]$KeepProcess,
    [switch]$TraceWindows
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$fixedWineVdmUrl = "https://github.com/GregoryMorse/winevdm/tree/fix-filedlg-fileok-ofn16-sync"
$defaultWineRoot = Join-Path $repoRoot ".tools\winevdm\pr"
$defaultScenarioPath = Join-Path $PSScriptRoot "winevdm_buttonfile_oracle.scenarios.json"
$defaultOutputDir = Join-Path $repoRoot "build\winevdm-oracle\fixtures"
$defaultRuntimeDir = Join-Path $defaultOutputDir "runtime"
$defaultManifestPath = Join-Path $PSScriptRoot "winevdm_legacy_manifest.json"

if ([string]::IsNullOrWhiteSpace($WineVdmDir)) {
    $WineVdmDir = $defaultWineRoot
}
if ([string]::IsNullOrWhiteSpace($ScenarioPath)) {
    $ScenarioPath = $defaultScenarioPath
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = $defaultOutputDir
}
if ([string]::IsNullOrWhiteSpace($RuntimeDir)) {
    $RuntimeDir = $defaultRuntimeDir
}

function Resolve-RepoRelativePath {
    param([string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

$WineVdmDir = Resolve-RepoRelativePath $WineVdmDir
$ScenarioPath = Resolve-RepoRelativePath $ScenarioPath
$OutputDir = Resolve-RepoRelativePath $OutputDir
$RuntimeDir = Resolve-RepoRelativePath $RuntimeDir
if (-not [string]::IsNullOrWhiteSpace($StageOnlyDeck)) {
    $StageOnlyDeck = Resolve-RepoRelativePath $StageOnlyDeck
}
if (-not [string]::IsNullOrWhiteSpace($LaunchDocument)) {
    $LaunchDocument = Resolve-RepoRelativePath $LaunchDocument
}

function Write-Step {
    param([string]$Message)
    Write-Host ("[{0:HH:mm:ss}] {1}" -f (Get-Date), $Message)
}

function Ensure-WineVdm {
    param([string]$Root)

    $otvdmw = Get-ChildItem -Path $Root -Recurse -Filter "otvdmw.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($otvdmw) {
        return $otvdmw.FullName
    }

    throw "Fixed WineVDM otvdmw.exe was not found under '$Root'. Build or copy the PR build from $fixedWineVdmUrl. Other WineVDM builds are intentionally unsupported for this oracle."
}

function Resolve-LegacyExe {
    if (-not [string]::IsNullOrWhiteSpace($LegacyExe) -and (Test-Path -LiteralPath $LegacyExe)) {
        return (Resolve-Path -LiteralPath $LegacyExe).Path
    }

    if (-not [string]::IsNullOrWhiteSpace($LegacyDir)) {
        $candidate = Join-Path $LegacyDir "BUTNFILE.EXE"
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    if (Test-Path -LiteralPath $defaultManifestPath) {
        $manifest = Get-Content -Raw -LiteralPath $defaultManifestPath | ConvertFrom-Json
        foreach ($candidate in @($manifest.legacyExecutable, $manifest.executablePath, $manifest.source)) {
            if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate)) {
                return (Resolve-Path -LiteralPath $candidate).Path
            }
        }
        foreach ($candidateDir in @($manifest.legacyDirectory, $manifest.runtimeDirectory)) {
            if (-not [string]::IsNullOrWhiteSpace($candidateDir)) {
                $candidateExe = Join-Path $candidateDir "BUTNFILE.EXE"
                if (Test-Path -LiteralPath $candidateExe) {
                    return (Resolve-Path -LiteralPath $candidateExe).Path
                }
            }
        }
    }

    throw "Could not find the legacy executable. Pass -LegacyExe, set CARDSTACK_LEGACY_EXE, set CARDSTACK_LEGACY_DIR, or populate scripts\winevdm_legacy_manifest.json."
}

function Copy-LegacyRuntime {
    param(
        [string]$ExePath,
        [string]$StageDir
    )

    New-Item -ItemType Directory -Force -Path $StageDir | Out-Null
    $sourceDir = Split-Path -Parent $ExePath
    $patterns = @("*.EXE", "*.DLL", "*.INI", "*.HLP", "*.BTN", "*.RPT", "*.BTR", "*.DAT")
    foreach ($pattern in $patterns) {
        Get-ChildItem -Path $sourceDir -Filter $pattern -File -ErrorAction SilentlyContinue | ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $StageDir $_.Name) -Force
        }
    }

    $stagedExe = Join-Path $StageDir (Split-Path -Leaf $ExePath)
    if (-not (Test-Path -LiteralPath $stagedExe)) {
        Copy-Item -LiteralPath $ExePath -Destination $stagedExe -Force
    }
    Get-ChildItem -Path $StageDir -Filter "BUTN*.INI" -File -ErrorAction SilentlyContinue | ForEach-Object {
        Remove-Item -LiteralPath $_.FullName -Force
        Write-Step ("Removed legacy session state: {0}" -f $_.FullName)
    }
    return $stagedExe
}

if (-not ("CardStack.Win32Oracle" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace CardStack {
public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

[StructLayout(LayoutKind.Sequential)]
public struct RECT {
    public int Left;
    public int Top;
    public int Right;
    public int Bottom;
}

[StructLayout(LayoutKind.Sequential)]
public struct POINT {
    public int X;
    public int Y;
}

public static class Win32Oracle {
[DllImport("user32.dll")]
public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

[DllImport("user32.dll")]
public static extern bool EnumChildWindows(IntPtr hWndParent, EnumWindowsProc lpEnumFunc, IntPtr lParam);

[DllImport("user32.dll")]
public static extern bool IsWindowVisible(IntPtr hWnd);

[DllImport("user32.dll", CharSet=CharSet.Unicode)]
public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);

[DllImport("user32.dll", CharSet=CharSet.Unicode)]
public static extern int GetClassName(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);

[DllImport("user32.dll")]
public static extern IntPtr GetDlgItem(IntPtr hDlg, int nIDDlgItem);

[DllImport("user32.dll")]
public static extern int GetDlgCtrlID(IntPtr hwndCtl);

[DllImport("user32.dll")]
public static extern IntPtr SendMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);

[DllImport("user32.dll", EntryPoint="SendMessageW")]
public static extern IntPtr SendMessageRect(IntPtr hWnd, uint Msg, IntPtr wParam, ref RECT lParam);

[DllImport("user32.dll")]
public static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam, uint fuFlags, uint uTimeout, out IntPtr lpdwResult);

[DllImport("user32.dll", CharSet=CharSet.Unicode)]
public static extern IntPtr SendMessage(IntPtr hWnd, uint Msg, IntPtr wParam, string lParam);

[DllImport("user32.dll", CharSet=CharSet.Ansi, EntryPoint="SendMessageA")]
public static extern IntPtr SendMessageAnsiString(IntPtr hWnd, uint Msg, IntPtr wParam, string lParam);

[DllImport("user32.dll", CharSet=CharSet.Ansi, EntryPoint="SendMessageA")]
public static extern IntPtr SendMessageAnsiBuffer(IntPtr hWnd, uint Msg, IntPtr wParam, StringBuilder lParam);

[DllImport("user32.dll", CharSet=CharSet.Ansi, EntryPoint="SetWindowTextA")]
public static extern bool SetWindowTextAnsi(IntPtr hWnd, string lpString);

[DllImport("user32.dll")]
public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);

[DllImport("user32.dll")]
public static extern bool SetForegroundWindow(IntPtr hWnd);

[DllImport("user32.dll")]
public static extern bool BringWindowToTop(IntPtr hWnd);

[DllImport("user32.dll")]
public static extern IntPtr SetActiveWindow(IntPtr hWnd);

[DllImport("user32.dll")]
public static extern IntPtr SetFocus(IntPtr hWnd);

[DllImport("kernel32.dll")]
public static extern uint GetCurrentThreadId();

[DllImport("user32.dll")]
public static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool fAttach);

[DllImport("user32.dll")]
public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

[DllImport("user32.dll")]
public static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);

[DllImport("user32.dll")]
public static extern bool SetCursorPos(int X, int Y);

[DllImport("user32.dll")]
public static extern void mouse_event(uint dwFlags, uint dx, uint dy, uint dwData, UIntPtr dwExtraInfo);

[DllImport("user32.dll")]
public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);

[DllImport("user32.dll", CharSet=CharSet.Ansi, EntryPoint="VkKeyScanA")]
public static extern short VkKeyScan(byte ch);

[DllImport("user32.dll", CharSet=CharSet.Ansi, EntryPoint="RegisterWindowMessageA")]
public static extern uint RegisterWindowMessageAnsi(string lpString);

[DllImport("kernel32.dll", SetLastError=true)]
public static extern IntPtr OpenProcess(uint dwDesiredAccess, bool bInheritHandle, uint dwProcessId);

[DllImport("kernel32.dll", SetLastError=true)]
public static extern bool CloseHandle(IntPtr hObject);

[DllImport("kernel32.dll", SetLastError=true)]
public static extern IntPtr VirtualAllocEx(IntPtr hProcess, IntPtr lpAddress, UIntPtr dwSize, uint flAllocationType, uint flProtect);

[DllImport("kernel32.dll", SetLastError=true)]
public static extern bool VirtualFreeEx(IntPtr hProcess, IntPtr lpAddress, UIntPtr dwSize, uint dwFreeType);

[DllImport("kernel32.dll", SetLastError=true)]
public static extern bool WriteProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, UIntPtr nSize, out UIntPtr lpNumberOfBytesWritten);

[DllImport("kernel32.dll", SetLastError=true)]
public static extern bool ReadProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, UIntPtr nSize, out UIntPtr lpNumberOfBytesRead);

[DllImport("user32.dll")]
public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
}
}
"@
}

function Get-WindowTitle {
    param([IntPtr]$Handle)
    $buffer = New-Object System.Text.StringBuilder 512
    [void][CardStack.Win32Oracle]::GetWindowText($Handle, $buffer, $buffer.Capacity)
    return $buffer.ToString()
}

function Get-WindowClass {
    param([IntPtr]$Handle)
    $buffer = New-Object System.Text.StringBuilder 256
    [void][CardStack.Win32Oracle]::GetClassName($Handle, $buffer, $buffer.Capacity)
    return $buffer.ToString()
}

function Write-ChildControlDump {
    param([IntPtr]$ParentHandle)

    $rows = New-Object System.Collections.Generic.List[object]
    $callback = [CardStack.EnumWindowsProc]{
        param([IntPtr]$hWnd, [IntPtr]$lParam)
        $rows.Add([pscustomobject]@{
            Id = [CardStack.Win32Oracle]::GetDlgCtrlID($hWnd)
            ClassName = Get-WindowClass $hWnd
            Text = Get-WindowTitle $hWnd
            Handle = $hWnd
        }) | Out-Null
        return $true
    }
    [void][CardStack.Win32Oracle]::EnumChildWindows($ParentHandle, $callback, [IntPtr]::Zero)
    $rows | Sort-Object Id, ClassName | Format-Table -AutoSize | Out-String | Write-Host
}

function Write-ControlItemDump {
    param(
        [IntPtr]$ControlHandle,
        [int]$ControlId
    )

    $className = Get-WindowClass $ControlHandle
    if ($className -eq "ComboBox") {
        $getCount = 0x0146
        $getText = 0x0148
        $getCurSel = 0x0147
    }
    elseif ($className -eq "ListBox") {
        $getCount = 0x018B
        $getText = 0x0189
        $getCurSel = 0x0188
    }
    else {
        throw "Control ID $ControlId is '$className', not a ComboBox or ListBox."
    }

    $count = [int][CardStack.Win32Oracle]::SendMessage($ControlHandle, $getCount, [IntPtr]::Zero, [IntPtr]::Zero)
    $curSel = [int][CardStack.Win32Oracle]::SendMessage($ControlHandle, $getCurSel, [IntPtr]::Zero, [IntPtr]::Zero)
    Write-Step ("Items for control {0} ({1}), count {2}, current {3}" -f $ControlId, $className, $count, $curSel)
    for ($i = 0; $i -lt $count; $i++) {
        $buffer = New-Object System.Text.StringBuilder 260
        $result = [CardStack.Win32Oracle]::SendMessageAnsiBuffer($ControlHandle, $getText, [IntPtr]$i, $buffer)
        Write-Host ("  [{0}] len={1} '{2}'" -f $i, $result, $buffer.ToString())
    }
}

function Get-LegacyErrorDialog {
    param([int]$ProcessId)

    $errorPatterns = @(
        "Invalid file extension",
        "Cannot ",
        "Can't ",
        "Unable ",
        "Error",
        "Interrupt ",
        "not found",
        "does not exist",
        "not a valid",
        "access denied"
    )

    $dialogs = New-Object System.Collections.Generic.List[object]
    $callback = [CardStack.EnumWindowsProc]{
        param([IntPtr]$hWnd, [IntPtr]$lParam)
        if (-not [CardStack.Win32Oracle]::IsWindowVisible($hWnd)) {
            return $true
        }

        $windowProcessId = 0
        [void][CardStack.Win32Oracle]::GetWindowThreadProcessId($hWnd, [ref]$windowProcessId)
        if ($ProcessId -ne 0 -and $windowProcessId -ne $ProcessId) {
            return $true
        }

        $title = Get-WindowTitle $hWnd
        $className = Get-WindowClass $hWnd
        if ($className -ne "#32770" -or $title -like "*Evaluation Copy*" -or $title -eq "Open" -or $title -eq "Save As") {
            return $true
        }

        $texts = New-Object System.Collections.Generic.List[string]
        $buttons = New-Object System.Collections.Generic.List[string]
        $childCallback = [CardStack.EnumWindowsProc]{
            param([IntPtr]$child, [IntPtr]$childParam)
            $childClass = Get-WindowClass $child
            $childText = Get-WindowTitle $child
            if ($childClass -eq "Static" -and -not [string]::IsNullOrWhiteSpace($childText)) {
                $texts.Add($childText) | Out-Null
            }
            if ($childClass -eq "Button" -and -not [string]::IsNullOrWhiteSpace($childText)) {
                $buttons.Add($childText) | Out-Null
            }
            return $true
        }
        [void][CardStack.Win32Oracle]::EnumChildWindows($hWnd, $childCallback, [IntPtr]::Zero)

        $message = (($texts + @($title)) -join " ").Trim()
        if ([string]::IsNullOrWhiteSpace($message)) {
            return $true
        }

        $hasErrorPattern = $false
        foreach ($pattern in $errorPatterns) {
            if ($message -like "*$pattern*") {
                $hasErrorPattern = $true
                break
            }
        }
        $looksLikeButtonFileMessageBox = ($title -like "buttonFile*" -and ($buttons -contains "OK" -or $buttons -contains "&OK"))
        if ($hasErrorPattern -or $looksLikeButtonFileMessageBox) {
            $dialogs.Add([pscustomobject]@{
                Handle = $hWnd
                Title = $title
                ClassName = $className
                Message = $message
                Buttons = ($buttons -join ", ")
            }) | Out-Null
        }
        return $true
    }
    [void][CardStack.Win32Oracle]::EnumWindows($callback, [IntPtr]::Zero)
    return $dialogs | Select-Object -First 1
}

function Assert-NoLegacyErrorDialog {
    param([int]$ProcessId)

    $dialog = Get-LegacyErrorDialog -ProcessId $ProcessId
    if ($dialog) {
        throw ("Legacy error dialog: title='{0}' message='{1}' buttons='{2}'" -f $dialog.Title, $dialog.Message, $dialog.Buttons)
    }
}

function Get-LegacyOverwriteDialog {
    param([int]$ProcessId)

    $dialogs = New-Object System.Collections.Generic.List[object]
    $callback = [CardStack.EnumWindowsProc]{
        param([IntPtr]$hWnd, [IntPtr]$lParam)
        if (-not [CardStack.Win32Oracle]::IsWindowVisible($hWnd)) {
            return $true
        }

        $windowProcessId = 0
        [void][CardStack.Win32Oracle]::GetWindowThreadProcessId($hWnd, [ref]$windowProcessId)
        if ($ProcessId -ne 0 -and $windowProcessId -ne $ProcessId) {
            return $true
        }

        $title = Get-WindowTitle $hWnd
        $className = Get-WindowClass $hWnd
        if ($className -ne "#32770" -or $title -like "*Evaluation Copy*") {
            return $true
        }

        $texts = New-Object System.Collections.Generic.List[string]
        $buttons = New-Object System.Collections.Generic.List[object]
        $childCallback = [CardStack.EnumWindowsProc]{
            param([IntPtr]$child, [IntPtr]$childParam)
            $childClass = Get-WindowClass $child
            $childText = Get-WindowTitle $child
            if ($childClass -eq "Static" -and -not [string]::IsNullOrWhiteSpace($childText)) {
                $texts.Add($childText) | Out-Null
            }
            if ($childClass -eq "Button" -and -not [string]::IsNullOrWhiteSpace($childText)) {
                $buttons.Add([pscustomobject]@{
                    Handle = $child
                    Id = [CardStack.Win32Oracle]::GetDlgCtrlID($child)
                    Text = $childText
                }) | Out-Null
            }
            return $true
        }
        [void][CardStack.Win32Oracle]::EnumChildWindows($hWnd, $childCallback, [IntPtr]::Zero)

        $message = (($texts + @($title)) -join " ").Trim()
        if ([string]::IsNullOrWhiteSpace($message)) {
            return $true
        }

        if ($message -notmatch "(?i)(already exists|replace|overwrite)") {
            return $true
        }

        $button = $buttons |
            Where-Object { $_.Text -in @("&Yes", "Yes", "OK", "&OK") -or $_.Id -in @(1, 6) } |
            Select-Object -First 1
        if (-not $button) {
            return $true
        }

        $dialogs.Add([pscustomobject]@{
            Handle = $hWnd
            Title = $title
            Message = $message
            ButtonHandle = $button.Handle
            ButtonId = $button.Id
            ButtonText = $button.Text
        }) | Out-Null
        return $true
    }
    [void][CardStack.Win32Oracle]::EnumWindows($callback, [IntPtr]::Zero)
    return $dialogs | Select-Object -First 1
}

function Accept-LegacyOverwriteDialogIfPresent {
    param(
        [int]$ProcessId,
        [int]$TimeoutMs = 0
    )

    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    do {
        $dialog = Get-LegacyOverwriteDialog -ProcessId $ProcessId
        if ($dialog) {
            Write-Step ("Accept overwrite dialog: {0}" -f $dialog.Message)
            $messageResult = [IntPtr]::Zero
            $sendResult = [CardStack.Win32Oracle]::SendMessageTimeout($dialog.ButtonHandle, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero, 0x0002, 2000, [ref]$messageResult)
            Write-Step ("Overwrite {0} BM_CLICK returned {1}, result {2}" -f $dialog.ButtonText, $sendResult, $messageResult)
            if ($sendResult -eq 0) {
                Invoke-PhysicalClick -Handle $dialog.ButtonHandle
            }
            Start-Sleep -Milliseconds 500
            return $true
        }
        if ($TimeoutMs -le 0) {
            break
        }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)

    return $false
}

function Wait-OracleIdleOrError {
    param(
        [int]$ProcessId,
        [int]$TimeoutMs = 2000
    )

    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    do {
        [void](Accept-LegacyOverwriteDialogIfPresent -ProcessId $ProcessId)
        Assert-NoLegacyErrorDialog -ProcessId $ProcessId
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
}

function Write-TopWindowDump {
    param([int]$ProcessId)

    $rows = New-Object System.Collections.Generic.List[object]
    $callback = [CardStack.EnumWindowsProc]{
        param([IntPtr]$hWnd, [IntPtr]$lParam)
        $windowProcessId = 0
        [void][CardStack.Win32Oracle]::GetWindowThreadProcessId($hWnd, [ref]$windowProcessId)
        if ($ProcessId -ne 0 -and $windowProcessId -ne $ProcessId) {
            return $true
        }
        $rows.Add([pscustomobject]@{
            Visible = [CardStack.Win32Oracle]::IsWindowVisible($hWnd)
            ClassName = Get-WindowClass $hWnd
            Text = Get-WindowTitle $hWnd
            Handle = $hWnd
        }) | Out-Null
        return $true
    }
    [void][CardStack.Win32Oracle]::EnumWindows($callback, [IntPtr]::Zero)
    $rows | Sort-Object Visible, ClassName, Text | Format-Table -AutoSize | Out-String | Write-Host
}

function Send-ControlNotification {
    param(
        [IntPtr]$ParentHandle,
        [IntPtr]$ControlHandle,
        [int]$ControlId,
        [int]$NotificationCode
    )

    $wParam = [IntPtr]((($NotificationCode -band 0xffff) -shl 16) -bor ($ControlId -band 0xffff))
    [void][CardStack.Win32Oracle]::SendMessage($ParentHandle, 0x0111, $wParam, $ControlHandle)
}

function Get-CommonDialogMessage {
    param([string]$Name)

    $message = [CardStack.Win32Oracle]::RegisterWindowMessageAnsi($Name)
    if ($message -eq 0) {
        throw "RegisterWindowMessageA failed for '$Name'."
    }
    return $message
}

function Invoke-PhysicalClick {
    param([IntPtr]$Handle)

    $rect = New-Object CardStack.RECT
    if (-not [CardStack.Win32Oracle]::GetWindowRect($Handle, [ref]$rect)) {
        throw "Could not read the target control rectangle for click."
    }
    $x = [int](($rect.Left + $rect.Right) / 2)
    $y = [int](($rect.Top + $rect.Bottom) / 2)
    Write-Step ("Physical click at {0},{1} rect=({2},{3})-({4},{5})" -f $x, $y, $rect.Left, $rect.Top, $rect.Right, $rect.Bottom)
    [void][CardStack.Win32Oracle]::SetCursorPos($x, $y)
    Start-Sleep -Milliseconds 100
    [CardStack.Win32Oracle]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 75
    [CardStack.Win32Oracle]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
}

function Invoke-KeyDown {
    param([byte]$Vk)
    [CardStack.Win32Oracle]::keybd_event($Vk, 0, 0, [UIntPtr]::Zero)
}

function Invoke-KeyUp {
    param([byte]$Vk)
    [CardStack.Win32Oracle]::keybd_event($Vk, 0, 0x0002, [UIntPtr]::Zero)
}

function Invoke-KeyTap {
    param([byte]$Vk)
    Invoke-KeyDown -Vk $Vk
    Start-Sleep -Milliseconds 80
    Invoke-KeyUp -Vk $Vk
}

function Invoke-SelectAllKeys {
    Invoke-KeyDown -Vk 0x11
    Invoke-KeyTap -Vk 0x41
    Invoke-KeyUp -Vk 0x11
    Start-Sleep -Milliseconds 75
}

function Invoke-TextKeys {
    param([string]$Text)

    foreach ($ch in $Text.ToCharArray()) {
        $scan = [CardStack.Win32Oracle]::VkKeyScan([byte][char]$ch)
        if ($scan -lt 0) {
            throw "Cannot synthesize keyboard input for character '$ch'."
        }
        $vk = [byte]($scan -band 0xff)
        $shiftState = ($scan -shr 8) -band 0xff
        if (($shiftState -band 1) -ne 0) { Invoke-KeyDown -Vk 0x10 }
        if (($shiftState -band 2) -ne 0) { Invoke-KeyDown -Vk 0x11 }
        if (($shiftState -band 4) -ne 0) { Invoke-KeyDown -Vk 0x12 }
        Invoke-KeyTap -Vk $vk
        if (($shiftState -band 4) -ne 0) { Invoke-KeyUp -Vk 0x12 }
        if (($shiftState -band 2) -ne 0) { Invoke-KeyUp -Vk 0x11 }
        if (($shiftState -band 1) -ne 0) { Invoke-KeyUp -Vk 0x10 }
        Start-Sleep -Milliseconds 90
    }
}

function Set-OracleFocus {
    param(
        [IntPtr]$WindowHandle,
        [IntPtr]$ControlHandle
    )

    $targetProcessId = 0
    $targetThreadId = [CardStack.Win32Oracle]::GetWindowThreadProcessId($ControlHandle, [ref]$targetProcessId)
    $currentThreadId = [CardStack.Win32Oracle]::GetCurrentThreadId()
    if ($targetThreadId -ne 0 -and $targetThreadId -ne $currentThreadId) {
        [void][CardStack.Win32Oracle]::AttachThreadInput($currentThreadId, $targetThreadId, $true)
    }
    [void][CardStack.Win32Oracle]::SetForegroundWindow($WindowHandle)
    [void][CardStack.Win32Oracle]::BringWindowToTop($WindowHandle)
    [void][CardStack.Win32Oracle]::SetActiveWindow($WindowHandle)
    [void][CardStack.Win32Oracle]::SetFocus($ControlHandle)
    Start-Sleep -Milliseconds 150
    if ($targetThreadId -ne 0 -and $targetThreadId -ne $currentThreadId) {
        [void][CardStack.Win32Oracle]::AttachThreadInput($currentThreadId, $targetThreadId, $false)
    }
}

function Send-RemoteAnsiTextMessage {
    param(
        [int]$ProcessId,
        [IntPtr]$WindowHandle,
        [uint32]$Message,
        [IntPtr]$WParam,
        [string]$Text,
        [uint32]$TimeoutMs = 5000
    )

    $bytes = [System.Text.Encoding]::ASCII.GetBytes($Text + [char]0)
    $processHandle = [CardStack.Win32Oracle]::OpenProcess(0x0028, $false, [uint32]$ProcessId)
    if ($processHandle -eq [IntPtr]::Zero) {
        throw "OpenProcess failed for $ProcessId."
    }
    try {
        $byteCount = [UIntPtr]::new([uint64]$bytes.Length)
        $remoteText = [CardStack.Win32Oracle]::VirtualAllocEx($processHandle, [IntPtr]::Zero, $byteCount, 0x3000, 0x04)
        if ($remoteText -eq [IntPtr]::Zero) {
            throw "VirtualAllocEx failed for remote common-dialog text."
        }
        try {
            $written = [UIntPtr]::Zero
            if (-not [CardStack.Win32Oracle]::WriteProcessMemory($processHandle, $remoteText, $bytes, $byteCount, [ref]$written)) {
                throw "WriteProcessMemory failed for remote common-dialog text."
            }
            $messageResult = [IntPtr]::Zero
            $sendResult = [CardStack.Win32Oracle]::SendMessageTimeout($WindowHandle, $Message, $WParam, $remoteText, 0x0002, $TimeoutMs, [ref]$messageResult)
            Write-Step ("remote text message 0x{0:x} returned {1}, result {2}" -f $Message, $sendResult, $messageResult)
        }
        finally {
            [void][CardStack.Win32Oracle]::VirtualFreeEx($processHandle, $remoteText, [UIntPtr]::Zero, 0x8000)
        }
    }
    finally {
        [void][CardStack.Win32Oracle]::CloseHandle($processHandle)
    }
}

function Send-RemoteAnsiBufferMessage {
    param(
        [int]$ProcessId,
        [IntPtr]$WindowHandle,
        [uint32]$Message,
        [int]$BufferChars = 260,
        [uint32]$TimeoutMs = 5000
    )

    $bufferSize = [Math]::Max($BufferChars, 2)
    $byteCount = [UIntPtr]::new([uint64]$bufferSize)
    $processHandle = [CardStack.Win32Oracle]::OpenProcess(0x0038, $false, [uint32]$ProcessId)
    if ($processHandle -eq [IntPtr]::Zero) {
        throw "OpenProcess failed for $ProcessId."
    }
    try {
        $remoteBuffer = [CardStack.Win32Oracle]::VirtualAllocEx($processHandle, [IntPtr]::Zero, $byteCount, 0x3000, 0x04)
        if ($remoteBuffer -eq [IntPtr]::Zero) {
            throw "VirtualAllocEx failed for remote common-dialog buffer."
        }
        try {
            $messageResult = [IntPtr]::Zero
            $sendResult = [CardStack.Win32Oracle]::SendMessageTimeout($WindowHandle, $Message, [IntPtr]$bufferSize, $remoteBuffer, 0x0002, $TimeoutMs, [ref]$messageResult)
            $bytes = New-Object byte[] $bufferSize
            $read = [UIntPtr]::Zero
            if (-not [CardStack.Win32Oracle]::ReadProcessMemory($processHandle, $remoteBuffer, $bytes, $byteCount, [ref]$read)) {
                throw "ReadProcessMemory failed for remote common-dialog buffer."
            }
            $nul = [Array]::IndexOf($bytes, [byte]0)
            if ($nul -lt 0) {
                $nul = $bytes.Length
            }
            $text = [System.Text.Encoding]::ASCII.GetString($bytes, 0, $nul)
            Write-Step ("remote buffer message 0x{0:x} returned {1}, result {2}, text '{3}'" -f $Message, $sendResult, $messageResult, $text)
            return $text
        }
        finally {
            [void][CardStack.Win32Oracle]::VirtualFreeEx($processHandle, $remoteBuffer, [UIntPtr]::Zero, 0x8000)
        }
    }
    finally {
        [void][CardStack.Win32Oracle]::CloseHandle($processHandle)
    }
}

function Find-OracleWindow {
    param(
        [string]$TitleLike,
        [string]$ExcludeTitleLike = "",
        [int]$ProcessId = 0
    )

    $matches = New-Object System.Collections.Generic.List[object]
    $callback = [CardStack.EnumWindowsProc]{
        param([IntPtr]$hWnd, [IntPtr]$lParam)
        if (-not [CardStack.Win32Oracle]::IsWindowVisible($hWnd)) {
            return $true
        }

        $windowProcessId = 0
        [void][CardStack.Win32Oracle]::GetWindowThreadProcessId($hWnd, [ref]$windowProcessId)
        if ($ProcessId -ne 0 -and $windowProcessId -ne $ProcessId) {
            return $true
        }

        $title = Get-WindowTitle $hWnd
        if ($title -like "*$TitleLike*") {
            if (-not [string]::IsNullOrWhiteSpace($ExcludeTitleLike) -and $title -like "*$ExcludeTitleLike*") {
                return $true
            }
            $matches.Add([pscustomobject]@{
                Handle = $hWnd
                Title = $title
                ClassName = Get-WindowClass $hWnd
                ProcessId = $windowProcessId
            }) | Out-Null
        }
        return $true
    }
    [void][CardStack.Win32Oracle]::EnumWindows($callback, [IntPtr]::Zero)
    return $matches | Select-Object -First 1
}

function Wait-OracleWindow {
    param(
        [string]$TitleLike,
        [string]$ExcludeTitleLike = "",
        [int]$TimeoutMs,
        [int]$ProcessId = 0
    )

    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    do {
        $window = Find-OracleWindow -TitleLike $TitleLike -ExcludeTitleLike $ExcludeTitleLike -ProcessId $ProcessId
        if ($window) {
            return $window
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)

    $exclude = if ([string]::IsNullOrWhiteSpace($ExcludeTitleLike)) { "" } else { " excluding '$ExcludeTitleLike'" }
    throw "Timed out waiting for a visible window with title containing '$TitleLike'$exclude."
}

function Expand-Token {
    param(
        [string]$Value,
        [object]$FixtureSpec,
        [string]$FixturePath
    )

    if ($null -eq $Value) {
        return ""
    }
    $result = $Value.Replace("{fixturePath}", $FixturePath)
    $result = $result.Replace("{fixtureFile}", (Split-Path -Leaf $FixturePath))
    $result = $result.Replace("{fixtureBase}", [System.IO.Path]::GetFileNameWithoutExtension($FixturePath))
    if ($FixtureSpec.sourceDeckCopy) {
        $result = $result.Replace("{sourceDeckFile}", [string]$FixtureSpec.sourceDeckCopy)
    }
    if ($FixtureSpec.password) {
        $result = $result.Replace("{password}", [string]$FixtureSpec.password)
    }
    return $result
}

function Remove-StaleFixtureOutputs {
    param(
        [object]$FixtureSpec,
        [string]$FixturePath,
        [string]$OutputDir
    )

    $runtimeDir = Split-Path -Parent $FixturePath
    $names = New-Object System.Collections.Generic.List[string]
    $patterns = New-Object System.Collections.Generic.List[string]

    $fixtureFile = Split-Path -Leaf $FixturePath
    $fixtureBase = [System.IO.Path]::GetFileNameWithoutExtension($FixturePath)
    $names.Add($fixtureFile) | Out-Null
    $names.Add($fixtureBase + ".RPT") | Out-Null

    foreach ($step in $FixtureSpec.steps) {
        if ([string]$step.action -eq "copyRuntimeFile" -and $step.destination) {
            $names.Add((Expand-Token -Value ([string]$step.destination) -FixtureSpec $FixtureSpec -FixturePath $FixturePath)) | Out-Null
        }
        if ([string]$step.action -eq "assertAnyFile" -and $step.patterns) {
            foreach ($pattern in $step.patterns) {
                $value = Expand-Token -Value ([string]$pattern) -FixtureSpec $FixtureSpec -FixturePath $FixturePath
                if ($value.Contains("*") -or $value.Contains("?")) {
                    $patterns.Add($value) | Out-Null
                }
                else {
                    $names.Add($value) | Out-Null
                }
            }
        }
    }

    $directories = @($runtimeDir, $OutputDir) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique
    foreach ($directory in $directories) {
        foreach ($name in ($names | Select-Object -Unique)) {
            $path = if ([System.IO.Path]::IsPathRooted($name)) { $name } else { Join-Path $directory $name }
            if (Test-Path -LiteralPath $path) {
                try {
                    Remove-Item -LiteralPath $path -Force
                    Write-Step ("Removed stale fixture target: {0}" -f $path)
                }
                catch {
                    Write-Step ("Could not remove stale fixture target yet: {0} ({1})" -f $path, $_.Exception.Message)
                }
            }
        }

        foreach ($pattern in ($patterns | Select-Object -Unique)) {
            Get-ChildItem -Path $directory -Filter $pattern -File -ErrorAction SilentlyContinue |
                ForEach-Object {
                    try {
                        Remove-Item -LiteralPath $_.FullName -Force
                        Write-Step ("Removed stale fixture target: {0}" -f $_.FullName)
                    }
                    catch {
                        Write-Step ("Could not remove stale fixture target yet: {0} ({1})" -f $_.FullName, $_.Exception.Message)
                    }
                }
        }
    }
}

function Invoke-FixtureStep {
    param(
        [object]$Step,
        [object]$FixtureSpec,
        [string]$FixturePath,
        [string]$OutputDir,
        [System.Diagnostics.Process]$Process,
        [ref]$CurrentWindow
    )

    if ($Step.comment) {
        Write-Step ([string]$Step.comment)
    }

    switch ([string]$Step.action) {
        "sleep" {
            Start-Sleep -Milliseconds ([int]$Step.ms)
        }
        "waitWindow" {
            $timeout = if ($Step.timeoutMs) { [int]$Step.timeoutMs } else { 10000 }
            $excludeTitle = if ($Step.excludeTitleLike) { [string]$Step.excludeTitleLike } else { "" }
            try {
                $CurrentWindow.Value = Wait-OracleWindow -TitleLike ([string]$Step.titleLike) -ExcludeTitleLike $excludeTitle -TimeoutMs $timeout -ProcessId $Process.Id
            } catch {
                Write-TopWindowDump -ProcessId $Process.Id
                if ($Step.optional) {
                    Write-Step ("Optional waitWindow skipped: {0}" -f $_.Exception.Message)
                    $CurrentWindow.Value = $null
                    break
                }
                throw
            }
            Write-Step ("Window: {0}" -f $CurrentWindow.Value.Title)
        }
        "waitWindowGone" {
            $timeout = if ($Step.timeoutMs) { [int]$Step.timeoutMs } else { 10000 }
            $excludeTitle = if ($Step.excludeTitleLike) { [string]$Step.excludeTitleLike } else { "" }
            $deadline = (Get-Date).AddMilliseconds($timeout)
            do {
                [void](Accept-LegacyOverwriteDialogIfPresent -ProcessId $Process.Id)
                Assert-NoLegacyErrorDialog -ProcessId $Process.Id
                $window = Find-OracleWindow -TitleLike ([string]$Step.titleLike) -ExcludeTitleLike $excludeTitle -ProcessId $Process.Id
                if (-not $window) {
                    Write-Step ("Window gone: {0}" -f ([string]$Step.titleLike))
                    $CurrentWindow.Value = $null
                    break
                }
                Start-Sleep -Milliseconds 250
            } while ((Get-Date) -lt $deadline)
            if ($window) {
                throw "Timed out waiting for visible window with title containing '$($Step.titleLike)' to close."
            }
        }
        "command" {
            if (-not $CurrentWindow.Value) {
                $CurrentWindow.Value = Wait-OracleWindow -TitleLike "buttonFile" -TimeoutMs 10000 -ProcessId $Process.Id
            }
            [void][CardStack.Win32Oracle]::PostMessage($CurrentWindow.Value.Handle, 0x0111, [IntPtr]([int]$Step.id), [IntPtr]::Zero)
            $delay = if ($Step.delayMs) { [int]$Step.delayMs } else { 250 }
            Start-Sleep -Milliseconds $delay
        }
        "closeDecks" {
            $repeat = if ($Step.repeat) { [int]$Step.repeat } else { 8 }
            for ($i = 0; $i -lt $repeat; $i++) {
                if (-not $CurrentWindow.Value) {
                    $CurrentWindow.Value = Wait-OracleWindow -TitleLike "buttonFile" -TimeoutMs 10000 -ProcessId $Process.Id
                }
                [void][CardStack.Win32Oracle]::PostMessage($CurrentWindow.Value.Handle, 0x0111, [IntPtr]2704, [IntPtr]::Zero)
                Start-Sleep -Milliseconds 350
                [void](Accept-LegacyOverwriteDialogIfPresent -ProcessId $Process.Id)
            }
            Start-Sleep -Milliseconds 500
        }
        "setText" {
            if (-not $CurrentWindow.Value) {
                throw "setText requires a current window. Add waitWindow before this step."
            }
            $control = [CardStack.Win32Oracle]::GetDlgItem($CurrentWindow.Value.Handle, [int]$Step.controlId)
            if ($control -eq [IntPtr]::Zero) {
                throw "Control ID $($Step.controlId) was not found in '$($CurrentWindow.Value.Title)'."
            }
            $text = Expand-Token -Value ([string]$Step.text) -FixtureSpec $FixtureSpec -FixturePath $FixturePath
            [void][CardStack.Win32Oracle]::SetWindowTextAnsi($control, $text)
            Send-ControlNotification -ParentHandle $CurrentWindow.Value.Handle -ControlHandle $control -ControlId ([int]$Step.controlId) -NotificationCode 0x0300
        }
        "commonDialogSetControlText" {
            if (-not $CurrentWindow.Value) {
                throw "commonDialogSetControlText requires a current window. Add waitWindow before this step."
            }
            $text = Expand-Token -Value ([string]$Step.text) -FixtureSpec $FixtureSpec -FixturePath $FixturePath
            Send-RemoteAnsiTextMessage -ProcessId $Process.Id -WindowHandle $CurrentWindow.Value.Handle -Message 0x0468 -WParam ([IntPtr]([int]$Step.controlId)) -Text $text
            Start-Sleep -Milliseconds 250
        }
        "remoteSetText" {
            if (-not $CurrentWindow.Value) {
                throw "remoteSetText requires a current window. Add waitWindow before this step."
            }
            $control = [CardStack.Win32Oracle]::GetDlgItem($CurrentWindow.Value.Handle, [int]$Step.controlId)
            if ($control -eq [IntPtr]::Zero) {
                throw "Control ID $($Step.controlId) was not found in '$($CurrentWindow.Value.Title)'."
            }
            $text = Expand-Token -Value ([string]$Step.text) -FixtureSpec $FixtureSpec -FixturePath $FixturePath
            Send-RemoteAnsiTextMessage -ProcessId $Process.Id -WindowHandle $control -Message 0x000C -WParam ([IntPtr]::Zero) -Text $text
            Send-ControlNotification -ParentHandle $CurrentWindow.Value.Handle -ControlHandle $control -ControlId ([int]$Step.controlId) -NotificationCode 0x0300
            Start-Sleep -Milliseconds 250
        }
        "remoteGetText" {
            if (-not $CurrentWindow.Value) {
                throw "remoteGetText requires a current window. Add waitWindow before this step."
            }
            $control = [CardStack.Win32Oracle]::GetDlgItem($CurrentWindow.Value.Handle, [int]$Step.controlId)
            if ($control -eq [IntPtr]::Zero) {
                throw "Control ID $($Step.controlId) was not found in '$($CurrentWindow.Value.Title)'."
            }
            [void](Send-RemoteAnsiBufferMessage -ProcessId $Process.Id -WindowHandle $control -Message 0x000D)
        }
        "ansiSetText" {
            if (-not $CurrentWindow.Value) {
                throw "ansiSetText requires a current window. Add waitWindow before this step."
            }
            $control = [CardStack.Win32Oracle]::GetDlgItem($CurrentWindow.Value.Handle, [int]$Step.controlId)
            if ($control -eq [IntPtr]::Zero) {
                throw "Control ID $($Step.controlId) was not found in '$($CurrentWindow.Value.Title)'."
            }
            $text = Expand-Token -Value ([string]$Step.text) -FixtureSpec $FixtureSpec -FixturePath $FixturePath
            $result = [CardStack.Win32Oracle]::SendMessageAnsiString($control, 0x000C, [IntPtr]::Zero, $text)
            Write-Step ("WM_SETTEXTA control {0} returned {1}" -f ([int]$Step.controlId), $result)
            Send-ControlNotification -ParentHandle $CurrentWindow.Value.Handle -ControlHandle $control -ControlId ([int]$Step.controlId) -NotificationCode 0x0300
            Start-Sleep -Milliseconds 250
        }
        "ansiGetText" {
            if (-not $CurrentWindow.Value) {
                throw "ansiGetText requires a current window. Add waitWindow before this step."
            }
            $control = [CardStack.Win32Oracle]::GetDlgItem($CurrentWindow.Value.Handle, [int]$Step.controlId)
            if ($control -eq [IntPtr]::Zero) {
                throw "Control ID $($Step.controlId) was not found in '$($CurrentWindow.Value.Title)'."
            }
            $buffer = New-Object System.Text.StringBuilder 260
            $result = [CardStack.Win32Oracle]::SendMessageAnsiBuffer($control, 0x000D, [IntPtr]$buffer.Capacity, $buffer)
            Write-Step ("WM_GETTEXTA control {0} returned {1}, text '{2}'" -f ([int]$Step.controlId), $result, $buffer.ToString())
        }
        "commonDialogGetFilePath" {
            if (-not $CurrentWindow.Value) {
                throw "commonDialogGetFilePath requires a current window. Add waitWindow before this step."
            }
            [void](Send-RemoteAnsiBufferMessage -ProcessId $Process.Id -WindowHandle $CurrentWindow.Value.Handle -Message 0x0465)
        }
        "commonDialogGetSpec" {
            if (-not $CurrentWindow.Value) {
                throw "commonDialogGetSpec requires a current window. Add waitWindow before this step."
            }
            [void](Send-RemoteAnsiBufferMessage -ProcessId $Process.Id -WindowHandle $CurrentWindow.Value.Handle -Message 0x0464)
        }
        "typeText" {
            if (-not $CurrentWindow.Value) {
                throw "typeText requires a current window. Add waitWindow before this step."
            }
            $control = [CardStack.Win32Oracle]::GetDlgItem($CurrentWindow.Value.Handle, [int]$Step.controlId)
            if ($control -eq [IntPtr]::Zero) {
                throw "Control ID $($Step.controlId) was not found in '$($CurrentWindow.Value.Title)'."
            }
            $text = Expand-Token -Value ([string]$Step.text) -FixtureSpec $FixtureSpec -FixturePath $FixturePath
            Set-OracleFocus -WindowHandle $CurrentWindow.Value.Handle -ControlHandle $control
            Invoke-PhysicalClick -Handle $control
            Start-Sleep -Milliseconds 150
            Set-OracleFocus -WindowHandle $CurrentWindow.Value.Handle -ControlHandle $control
            Invoke-SelectAllKeys
            Invoke-TextKeys -Text $text
            Start-Sleep -Milliseconds 250
        }
        "selectIndex" {
            if (-not $CurrentWindow.Value) {
                throw "selectIndex requires a current window. Add waitWindow before this step."
            }
            $control = [CardStack.Win32Oracle]::GetDlgItem($CurrentWindow.Value.Handle, [int]$Step.controlId)
            if ($control -eq [IntPtr]::Zero) {
                throw "Control ID $($Step.controlId) was not found in '$($CurrentWindow.Value.Title)'."
            }
            $index = [IntPtr]([int]$Step.index)
            [void][CardStack.Win32Oracle]::SendMessage($control, 0x0186, $index, [IntPtr]::Zero)
            [void][CardStack.Win32Oracle]::SendMessage($control, 0x014E, $index, [IntPtr]::Zero)
            Send-ControlNotification -ParentHandle $CurrentWindow.Value.Handle -ControlHandle $control -ControlId ([int]$Step.controlId) -NotificationCode 1
            Start-Sleep -Milliseconds 250
        }
        "selectText" {
            if (-not $CurrentWindow.Value) {
                throw "selectText requires a current window. Add waitWindow before this step."
            }
            $control = [CardStack.Win32Oracle]::GetDlgItem($CurrentWindow.Value.Handle, [int]$Step.controlId)
            if ($control -eq [IntPtr]::Zero) {
                throw "Control ID $($Step.controlId) was not found in '$($CurrentWindow.Value.Title)'."
            }
            $className = Get-WindowClass $control
            if ($className -eq "ComboBox") {
                $getCount = 0x0146
                $getText = 0x0148
                $setCurSel = 0x014E
            }
            elseif ($className -eq "ListBox") {
                $getCount = 0x018B
                $getText = 0x0189
                $setCurSel = 0x0186
            }
            else {
                throw "Control ID $($Step.controlId) is '$className', not a ComboBox or ListBox."
            }
            $wanted = Expand-Token -Value ([string]$Step.text) -FixtureSpec $FixtureSpec -FixturePath $FixturePath
            $count = [int][CardStack.Win32Oracle]::SendMessage($control, $getCount, [IntPtr]::Zero, [IntPtr]::Zero)
            $match = -1
            for ($i = 0; $i -lt $count; $i++) {
                $buffer = New-Object System.Text.StringBuilder 260
                [void][CardStack.Win32Oracle]::SendMessageAnsiBuffer($control, $getText, [IntPtr]$i, $buffer)
                $itemText = $buffer.ToString()
                if ($itemText -ieq $wanted -or $itemText -ilike $wanted -or $wanted -ilike $itemText) {
                    $match = $i
                    break
                }
            }
            if ($match -lt 0) {
                Write-ControlItemDump -ControlHandle $control -ControlId ([int]$Step.controlId)
                throw "Could not find '$wanted' in $className control $($Step.controlId)."
            }
            [void][CardStack.Win32Oracle]::SendMessage($control, $setCurSel, [IntPtr]$match, [IntPtr]::Zero)
            Send-ControlNotification -ParentHandle $CurrentWindow.Value.Handle -ControlHandle $control -ControlId ([int]$Step.controlId) -NotificationCode 1
            Write-Step ("Selected '{0}' at index {1} in control {2}" -f $wanted, $match, ([int]$Step.controlId))
            Start-Sleep -Milliseconds 250
        }
        "controlNotify" {
            if (-not $CurrentWindow.Value) {
                throw "controlNotify requires a current window. Add waitWindow before this step."
            }
            $control = [CardStack.Win32Oracle]::GetDlgItem($CurrentWindow.Value.Handle, [int]$Step.controlId)
            if ($control -eq [IntPtr]::Zero) {
                throw "Control ID $($Step.controlId) was not found in '$($CurrentWindow.Value.Title)'."
            }
            Send-ControlNotification -ParentHandle $CurrentWindow.Value.Handle -ControlHandle $control -ControlId ([int]$Step.controlId) -NotificationCode ([int]$Step.notificationCode)
            Write-Step ("WM_COMMAND notification {0} sent for control {1}" -f ([int]$Step.notificationCode), ([int]$Step.controlId))
            $timeout = if ($Step.timeoutMs) { [int]$Step.timeoutMs } else { 500 }
            Wait-OracleIdleOrError -ProcessId $Process.Id -TimeoutMs $timeout
        }
        "click" {
            if (-not $CurrentWindow.Value) {
                if ($Step.optional) {
                    Write-Step "Optional click skipped because there is no current window."
                    break
                }
                throw "click requires a current window. Add waitWindow before this step."
            }
            $control = [CardStack.Win32Oracle]::GetDlgItem($CurrentWindow.Value.Handle, [int]$Step.controlId)
            if ($control -eq [IntPtr]::Zero) {
                if ($Step.optional) {
                    Write-Step ("Optional click skipped; control {0} is no longer present in '{1}'." -f ([int]$Step.controlId), $CurrentWindow.Value.Title)
                    break
                }
                throw "Control ID $($Step.controlId) was not found in '$($CurrentWindow.Value.Title)'."
            }
            Set-OracleFocus -WindowHandle $CurrentWindow.Value.Handle -ControlHandle $control
            [void][CardStack.Win32Oracle]::SendMessage($control, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
            Send-ControlNotification -ParentHandle $CurrentWindow.Value.Handle -ControlHandle $control -ControlId ([int]$Step.controlId) -NotificationCode 0
            Start-Sleep -Milliseconds 500
        }
        "physicalClick" {
            if (-not $CurrentWindow.Value) {
                throw "physicalClick requires a current window. Add waitWindow before this step."
            }
            $control = [CardStack.Win32Oracle]::GetDlgItem($CurrentWindow.Value.Handle, [int]$Step.controlId)
            if ($control -eq [IntPtr]::Zero) {
                if ($Step.optional) {
                    Write-Step ("Optional physicalClick skipped; control {0} is no longer present in '{1}'." -f ([int]$Step.controlId), $CurrentWindow.Value.Title)
                    break
                }
                throw "Control ID $($Step.controlId) was not found in '$($CurrentWindow.Value.Title)'."
            }
            Set-OracleFocus -WindowHandle $CurrentWindow.Value.Handle -ControlHandle $control
            Invoke-PhysicalClick -Handle $control
            Start-Sleep -Milliseconds 500
        }
        "listDoubleClick" {
            if (-not $CurrentWindow.Value) {
                throw "listDoubleClick requires a current window. Add waitWindow before this step."
            }
            $control = [CardStack.Win32Oracle]::GetDlgItem($CurrentWindow.Value.Handle, [int]$Step.controlId)
            if ($control -eq [IntPtr]::Zero) {
                throw "Control ID $($Step.controlId) was not found in '$($CurrentWindow.Value.Title)'."
            }
            $className = Get-WindowClass $control
            if ($className -ne "ListBox") {
                throw "Control ID $($Step.controlId) is '$className', not a ListBox."
            }
            $index = [int][CardStack.Win32Oracle]::SendMessage($control, 0x0188, [IntPtr]::Zero, [IntPtr]::Zero)
            if ($index -lt 0) {
                throw "ListBox control $($Step.controlId) does not have a current selection."
            }
            $rect = New-Object CardStack.RECT
            [void][CardStack.Win32Oracle]::SendMessageRect($control, 0x0198, [IntPtr]$index, [ref]$rect)
            $point = New-Object CardStack.POINT
            $point.X = [int](($rect.Left + $rect.Right) / 2)
            $point.Y = [int](($rect.Top + $rect.Bottom) / 2)
            [void][CardStack.Win32Oracle]::ClientToScreen($control, [ref]$point)
            Write-Step ("Physical double-click list item {0} at {1},{2} rect=({3},{4})-({5},{6})" -f $index, $point.X, $point.Y, $rect.Left, $rect.Top, $rect.Right, $rect.Bottom)
            [void][CardStack.Win32Oracle]::SetForegroundWindow($CurrentWindow.Value.Handle)
            [void][CardStack.Win32Oracle]::SetCursorPos($point.X, $point.Y)
            Start-Sleep -Milliseconds 100
            for ($i = 0; $i -lt 2; $i++) {
                [CardStack.Win32Oracle]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
                Start-Sleep -Milliseconds 50
                [CardStack.Win32Oracle]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
                Start-Sleep -Milliseconds 150
            }
            Send-ControlNotification -ParentHandle $CurrentWindow.Value.Handle -ControlHandle $control -ControlId ([int]$Step.controlId) -NotificationCode 2
            Start-Sleep -Milliseconds 500
        }
        "acceptLegacyDialog" {
            $messageLike = if ($Step.messageLike) { [string]$Step.messageLike } else { "" }
            $buttonId = if ($Step.buttonId) { [int]$Step.buttonId } else { 1 }
            $timeout = if ($Step.timeoutMs) { [int]$Step.timeoutMs } else { 10000 }
            $deadline = (Get-Date).AddMilliseconds($timeout)
            $dialog = $null
            do {
                $candidate = Get-LegacyErrorDialog -ProcessId $Process.Id
                if ($candidate -and ([string]::IsNullOrWhiteSpace($messageLike) -or $candidate.Message -like "*$messageLike*")) {
                    $dialog = $candidate
                    break
                }
                Start-Sleep -Milliseconds 250
            } while ((Get-Date) -lt $deadline)
            if (-not $dialog) {
                throw "Timed out waiting for expected legacy dialog matching '$messageLike'."
            }
            Write-Step ("Accept legacy dialog: {0}" -f $dialog.Message)
            $CurrentWindow.Value = $dialog
            $control = [CardStack.Win32Oracle]::GetDlgItem($dialog.Handle, $buttonId)
            if ($control -eq [IntPtr]::Zero) {
                throw "Button ID $buttonId was not found in expected legacy dialog '$($dialog.Title)'."
            }
            [void][CardStack.Win32Oracle]::SendMessage($control, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
            Start-Sleep -Milliseconds 250
            $CurrentWindow.Value = $null
        }
        "acceptLegacyDialogs" {
            $messageLike = if ($Step.messageLike) { [string]$Step.messageLike } else { "" }
            $buttonId = if ($Step.buttonId) { [int]$Step.buttonId } else { 1 }
            $repeat = if ($Step.repeat) { [int]$Step.repeat } else { 8 }
            for ($i = 0; $i -lt $repeat; $i++) {
                $dialog = Get-LegacyErrorDialog -ProcessId $Process.Id
                if (-not $dialog -or (-not [string]::IsNullOrWhiteSpace($messageLike) -and $dialog.Message -notlike "*$messageLike*")) {
                    break
                }
                Write-Step ("Accept legacy dialog: {0}" -f $dialog.Message)
                $control = [CardStack.Win32Oracle]::GetDlgItem($dialog.Handle, $buttonId)
                if ($control -eq [IntPtr]::Zero) {
                    throw "Button ID $buttonId was not found in expected legacy dialog '$($dialog.Title)'."
                }
                [void][CardStack.Win32Oracle]::SendMessage($control, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
                Start-Sleep -Milliseconds 300
            }
            $CurrentWindow.Value = $null
        }
        "buttonClick" {
            if (-not $CurrentWindow.Value) {
                if ($Step.optional) {
                    Write-Step "Optional buttonClick skipped because there is no current window."
                    break
                }
                throw "buttonClick requires a current window. Add waitWindow before this step."
            }
            $control = [CardStack.Win32Oracle]::GetDlgItem($CurrentWindow.Value.Handle, [int]$Step.controlId)
            if ($control -eq [IntPtr]::Zero) {
                if ($Step.optional) {
                    Write-Step ("Optional buttonClick skipped; control {0} is no longer present in '{1}'." -f ([int]$Step.controlId), $CurrentWindow.Value.Title)
                    break
                }
                throw "Control ID $($Step.controlId) was not found in '$($CurrentWindow.Value.Title)'."
            }
            Set-OracleFocus -WindowHandle $CurrentWindow.Value.Handle -ControlHandle $control
            $messageResult = [IntPtr]::Zero
            $sendResult = [CardStack.Win32Oracle]::SendMessageTimeout($control, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero, 0x0002, 5000, [ref]$messageResult)
            Write-Step ("BM_CLICK SendMessageTimeout returned {0}, result {1}" -f $sendResult, $messageResult)
            $timeout = if ($Step.timeoutMs) { [int]$Step.timeoutMs } else { 300 }
            if ($Step.allowLegacyError) {
                Start-Sleep -Milliseconds $timeout
            }
            else {
                Wait-OracleIdleOrError -ProcessId $Process.Id -TimeoutMs $timeout
            }
        }
        "dialogOk" {
            if (-not $CurrentWindow.Value) {
                throw "dialogOk requires a current window. Add waitWindow before this step."
            }
            $control = [CardStack.Win32Oracle]::GetDlgItem($CurrentWindow.Value.Handle, 1)
            if ($control -eq [IntPtr]::Zero) {
                throw "Control ID 1 was not found in '$($CurrentWindow.Value.Title)'."
            }
            [void][CardStack.Win32Oracle]::SetForegroundWindow($CurrentWindow.Value.Handle)
            $wParam = [IntPtr]((0 -shl 16) -bor 1)
            $messageResult = [IntPtr]::Zero
            $sendResult = [CardStack.Win32Oracle]::SendMessageTimeout($CurrentWindow.Value.Handle, 0x0111, $wParam, $control, 0x0002, 5000, [ref]$messageResult)
            Write-Step ("WM_COMMAND/IDOK SendMessageTimeout returned {0}, result {1}" -f $sendResult, $messageResult)
            $timeout = if ($Step.timeoutMs) { [int]$Step.timeoutMs } else { 5000 }
            Wait-OracleIdleOrError -ProcessId $Process.Id -TimeoutMs $timeout
        }
        "commonDialogFileOk" {
            if (-not $CurrentWindow.Value) {
                throw "commonDialogFileOk requires a current window. Add waitWindow before this step."
            }
            $fileOkMessage = Get-CommonDialogMessage -Name "commdlg_FileNameOK"
            [void][CardStack.Win32Oracle]::PostMessage($CurrentWindow.Value.Handle, $fileOkMessage, [IntPtr]::Zero, [IntPtr]::Zero)
            Write-Step "Posted commdlg_FileNameOK"
            $timeout = if ($Step.timeoutMs) { [int]$Step.timeoutMs } else { 5000 }
            Wait-OracleIdleOrError -ProcessId $Process.Id -TimeoutMs $timeout
        }
        "commonDialogListSelectionChanged" {
            if (-not $CurrentWindow.Value) {
                throw "commonDialogListSelectionChanged requires a current window. Add waitWindow before this step."
            }
            $selectionMessage = Get-CommonDialogMessage -Name "commdlg_LBSelChangedNotify"
            $controlId = if ($Step.controlId) { [int]$Step.controlId } else { 0 }
            $result = [CardStack.Win32Oracle]::SendMessage($CurrentWindow.Value.Handle, $selectionMessage, [IntPtr]$controlId, [IntPtr]::Zero)
            Write-Step ("commdlg_LBSelChangedNotify returned {0}" -f $result)
            Start-Sleep -Milliseconds 500
        }
        "sendKeys" {
            if (-not $CurrentWindow.Value) {
                $CurrentWindow.Value = Wait-OracleWindow -TitleLike "buttonFile" -TimeoutMs 10000 -ProcessId $Process.Id
            }
            [void][CardStack.Win32Oracle]::SetForegroundWindow($CurrentWindow.Value.Handle)
            Start-Sleep -Milliseconds 150
            $keys = Expand-Token -Value ([string]$Step.keys) -FixtureSpec $FixtureSpec -FixturePath $FixturePath
            $shell = New-Object -ComObject WScript.Shell
            $shell.SendKeys($keys)
            if ($Step.delayMs) {
                Start-Sleep -Milliseconds ([int]$Step.delayMs)
            }
        }
        "dumpControls" {
            if (-not $CurrentWindow.Value) {
                throw "dumpControls requires a current window. Add waitWindow before this step."
            }
            Write-Step ("Controls for: {0}" -f $CurrentWindow.Value.Title)
            Write-ChildControlDump -ParentHandle $CurrentWindow.Value.Handle
        }
        "dumpItems" {
            if (-not $CurrentWindow.Value) {
                throw "dumpItems requires a current window. Add waitWindow before this step."
            }
            $control = [CardStack.Win32Oracle]::GetDlgItem($CurrentWindow.Value.Handle, [int]$Step.controlId)
            if ($control -eq [IntPtr]::Zero) {
                throw "Control ID $($Step.controlId) was not found in '$($CurrentWindow.Value.Title)'."
            }
            Write-ControlItemDump -ControlHandle $control -ControlId ([int]$Step.controlId)
        }
        "dumpWindows" {
            Write-Step "Top-level windows for oracle process:"
            Write-TopWindowDump -ProcessId $Process.Id
        }
        "snapshot" {
            $label = if ($Step.label) { [string]$Step.label } else { [string]$FixtureSpec.name }
            $snapshotDir = Join-Path $OutputDir ("snapshot-" + $label)
            New-Item -ItemType Directory -Force -Path $snapshotDir | Out-Null
            Get-ChildItem -Path $snapshotDir -File -ErrorAction SilentlyContinue | Remove-Item -Force
            $runtimeDir = Split-Path -Parent $FixturePath
            $snapshotFiles = @()
            if ($Step.patterns) {
                foreach ($pattern in $Step.patterns) {
                    $expandedPattern = Expand-Token -Value ([string]$pattern) -FixtureSpec $FixtureSpec -FixturePath $FixturePath
                    $snapshotFiles += Get-ChildItem -Path $runtimeDir -Filter $expandedPattern -File -ErrorAction SilentlyContinue
                }
            }
            else {
                $snapshotFiles = Get-ChildItem -Path $runtimeDir -File -ErrorAction SilentlyContinue |
                    Where-Object { $_.Extension -match '^\.(BTN|RPT|BTR|DAT|DBF|DBT|CRD|WP|TN|CSV)$' }
            }
            $snapshotFiles | Select-Object -Unique | ForEach-Object {
                try {
                    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $snapshotDir $_.Name) -Force
                }
                catch {
                    Write-Step ("Snapshot skipped locked file: {0}" -f $_.FullName)
                }
            }
            Write-Step "Snapshot copied to $snapshotDir"
        }
        "copyRuntimeFile" {
            $runtimeDir = Split-Path -Parent $FixturePath
            $sourceName = Expand-Token -Value ([string]$Step.source) -FixtureSpec $FixtureSpec -FixturePath $FixturePath
            $destinationName = Expand-Token -Value ([string]$Step.destination) -FixtureSpec $FixtureSpec -FixturePath $FixturePath
            $sourcePath = Join-Path $runtimeDir $sourceName
            $destinationPath = Join-Path $runtimeDir $destinationName
            if (-not (Test-Path -LiteralPath $sourcePath)) {
                throw "Runtime source file was not found: $sourcePath"
            }
            Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Force
            Write-Step "Copied runtime fixture $sourceName -> $destinationName"
        }
        "assertAnyFile" {
            $found = $false
            $matchedFile = $null
            foreach ($pattern in $Step.patterns) {
                $candidate = Get-ChildItem -Path (Split-Path -Parent $FixturePath) -Filter ([string]$pattern) -File -ErrorAction SilentlyContinue | Select-Object -First 1
                if ($candidate) {
                    $found = $true
                    $matchedFile = $candidate
                    break
                }
            }
            if (-not $found) {
                throw "Expected one of these files after fixture '$($FixtureSpec.name)': $($Step.patterns -join ', ')"
            }
            Copy-Item -LiteralPath $matchedFile.FullName -Destination (Join-Path $OutputDir $matchedFile.Name) -Force
            Write-Step ("Golden fixture exported: {0}" -f (Join-Path $OutputDir $matchedFile.Name))
        }
        "closeApp" {
            if ($Process -and -not $Process.HasExited) {
                $Process.CloseMainWindow() | Out-Null
                if (-not $Process.WaitForExit(3000)) {
                    Write-Step "Legacy oracle process did not close after request; terminating it to release fixture files."
                    $Process.Kill()
                    $Process.WaitForExit(3000) | Out-Null
                }
            }
        }
        default {
            throw "Unknown oracle action '$($Step.action)'."
        }
    }

    if (-not $Step.allowLegacyError) {
        Assert-NoLegacyErrorDialog -ProcessId $Process.Id
    }
}

$otvdmwPath = Ensure-WineVdm -Root $WineVdmDir
$legacyExePath = Resolve-LegacyExe
$scenario = Get-Content -Raw -LiteralPath $ScenarioPath | ConvertFrom-Json

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$stageDir = $RuntimeDir
$stagedExe = Copy-LegacyRuntime -ExePath $legacyExePath -StageDir $stageDir
if (-not [string]::IsNullOrWhiteSpace($StageOnlyDeck)) {
    $wantedDeck = [System.IO.Path]::GetFileName($StageOnlyDeck)
    $wantedPath = Join-Path $stageDir $wantedDeck
    if (-not (Test-Path -LiteralPath $wantedPath)) {
        throw "Requested staged deck '$wantedDeck' was not found in runtime directory '$stageDir'."
    }
    Get-ChildItem -Path $stageDir -Filter "*.BTN" -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -ine $wantedDeck } |
        ForEach-Object {
            Remove-Item -LiteralPath $_.FullName -Force
            Write-Step ("Removed extra staged deck to avoid startup auto-open: {0}" -f $_.Name)
        }
}

$selectedFixtures = @($scenario.fixtures | Where-Object {
    ($IncludeDisabled -or $_.enabled) -and
    ($Fixture.Count -eq 0 -or $Fixture -contains $_.name)
})

if ($selectedFixtures.Count -eq 0) {
    throw "No fixtures selected. Check -Fixture names or use -IncludeDisabled."
}

foreach ($fixtureSpec in $selectedFixtures) {
    if ($fixtureSpec.sourceDeckCopy) {
        $sourceDeck = Join-Path $stageDir "SOFTWARE.BTN"
        $copiedDeck = Join-Path $stageDir ([string]$fixtureSpec.sourceDeckCopy)
        Copy-Item -LiteralPath $sourceDeck -Destination $copiedDeck -Force
        Write-Step ("Prepared private source deck copy: {0}" -f ([string]$fixtureSpec.sourceDeckCopy))
    }
}

Write-Step "WineVDM: $otvdmwPath"
Write-Step "Legacy EXE: $stagedExe"
Write-Step "Output: $OutputDir"
Write-Step "Runtime: $stageDir"
if (-not [string]::IsNullOrWhiteSpace($LaunchDocument)) {
    Write-Step "Launch document: $LaunchDocument"
}

$launchArgs = "`"$stagedExe`""
if (-not [string]::IsNullOrWhiteSpace($LaunchDocument)) {
    $launchArgs = $launchArgs + " `"$LaunchDocument`""
}
$process = Start-Process -FilePath $otvdmwPath -ArgumentList $launchArgs -WorkingDirectory $stageDir -PassThru
try {
    $currentWindow = $null
    foreach ($step in $scenario.startup) {
        Invoke-FixtureStep -Step $step -FixtureSpec ([pscustomobject]@{ name = "startup" }) -FixturePath (Join-Path $stageDir "startup.BTN") -OutputDir $OutputDir -Process $process -CurrentWindow ([ref]$currentWindow)
        if ($TraceWindows) {
            Write-TopWindowDump -ProcessId $process.Id
            if ($currentWindow) {
                Write-ChildControlDump -ParentHandle $currentWindow.Handle
            }
        }
    }

    foreach ($fixtureSpec in $selectedFixtures) {
        Write-Step "Fixture: $($fixtureSpec.name)"
        $stem = if ($fixtureSpec.outputStem) { [string]$fixtureSpec.outputStem } else { [string]$fixtureSpec.name }
        $extension = if ($fixtureSpec.outputExtension) { [string]$fixtureSpec.outputExtension } else { ".BTN" }
        if (-not $extension.StartsWith(".")) {
            $extension = "." + $extension
        }
        $fixturePath = Join-Path $stageDir ($stem + $extension)
        Remove-StaleFixtureOutputs -FixtureSpec $fixtureSpec -FixturePath $fixturePath -OutputDir $OutputDir
        foreach ($step in $fixtureSpec.steps) {
            Invoke-FixtureStep -Step $step -FixtureSpec $fixtureSpec -FixturePath $fixturePath -OutputDir $OutputDir -Process $process -CurrentWindow ([ref]$currentWindow)
            if ($TraceWindows) {
                Write-TopWindowDump -ProcessId $process.Id
                if ($currentWindow) {
                    Write-ChildControlDump -ParentHandle $currentWindow.Handle
                }
            }
            if ($Interactive) {
                Read-Host "Press Enter for next oracle step"
            }
        }
    }
}
finally {
    if (-not $KeepProcess -and $process -and -not $process.HasExited) {
        Write-Step "Closing legacy oracle process"
        $process.CloseMainWindow() | Out-Null
        Start-Sleep -Milliseconds 1500
        if (-not $process.HasExited) {
            $process.Kill()
        }
    }
}

Write-Step "WineVDM oracle run complete."
