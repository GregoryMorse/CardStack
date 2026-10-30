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
    [switch]$TraceWindows,
    [ValidateRange(1,3600)][int]$TimeoutSeconds = 300,
    [switch]$IsolatedWorker,
    [string]$RunRequest = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not ("CardStack.Oracle.Native" -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot "WineVdmOracleNative.cs") -ReferencedAssemblies System.dll,System.Core.dll,System.Drawing.dll,System.Windows.Forms.dll
}
if ($IsolatedWorker) {
    [CardStack.Oracle.Native]::RequirePrivate()
    $request = Get-Content -LiteralPath $RunRequest -Raw | ConvertFrom-Json
    foreach ($name in @('LegacyExe','LegacyDir','WineVdmDir','ScenarioPath','OutputDir','RuntimeDir','StageOnlyDeck','LaunchDocument','Fixture','IncludeDisabled','NoDownload','TraceWindows')) {
        if ($null -ne $request.$name) { Set-Variable -Name $name -Value $request.$name -Scope Script }
    }
}
$fixedWineVdmUrl = "https://github.com/GregoryMorse/winevdm/tree/fix-filedlg-fileok-ofn16-sync"
$defaultWineRoot = Join-Path $repoRoot ".tools\winevdm\pr"
$defaultScenarioPath = Join-Path $PSScriptRoot "winevdm_buttonfile_oracle.scenarios.json"
$defaultOutputDir = Join-Path $repoRoot "build\winevdm-oracle\fixtures"
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
    $RuntimeDir = Join-Path (Join-Path $repoRoot 'docs\audit\wvdm') ([Guid]::NewGuid().ToString('N').Substring(0,8))
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

    $sourceDir = Split-Path -Parent $ExePath
    $sourceFull=[IO.Path]::GetFullPath($sourceDir).TrimEnd('\')+'\'
    $stageFull=[IO.Path]::GetFullPath($StageDir).TrimEnd('\')+'\'
    if ($sourceFull.StartsWith($stageFull,[StringComparison]::OrdinalIgnoreCase)) { throw 'The runtime scratch directory must not be the source directory or its ancestor.' }
    New-Item -ItemType Directory -Force -Path $StageDir | Out-Null
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
    param([int]$ProcessId, [string]$ExpectedMessage = "")

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
        $matchesExpected = $ExpectedMessage -and $message -like "*$ExpectedMessage*"
        if ($hasErrorPattern -or $looksLikeButtonFileMessageBox -or $matchesExpected) {
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

function Get-OracleDialogButton {
    param([IntPtr]$Dialog, [int]$Id)
    [CardStack.Oracle.Native]::RequireTarget($Dialog)
    $control=[CardStack.Win32Oracle]::GetDlgItem($Dialog,$Id)
    if ($control -ne [IntPtr]::Zero) { return $control }
    try { Save-OracleCapture -Handle $Dialog -Key 'diagnostic.modal' -OutputDir $OutputDir } catch { Write-Warning $_.Exception.Message }
    $caption=switch ($Id) { 1 {'OK'} 2 {'Cancel'} 6 {'Yes'} 7 {'No'} default {''} }
    if (!$caption) { return [IntPtr]::Zero }
    $matches=@([CardStack.Oracle.Native]::Controls($Dialog) | Where-Object {
        $_.class -eq 'Button' -and $_.visible -and $_.enabled -and
        ([string]$_.text).Replace('&','').Trim() -eq $caption -and
        [CardStack.Oracle.Native]::GetParent([IntPtr][long]$_.handle) -eq $Dialog
    })
    if ($matches.Count -eq 1) { return [IntPtr][long]$matches[0].handle }
    return [IntPtr]::Zero
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

function Invoke-MessageClick {
    param([IntPtr]$Handle)
    [CardStack.Oracle.Native]::Click($Handle, -1, -1, $false)
    Start-Sleep -Milliseconds 150
}

function Set-OracleFocus {
    param([IntPtr]$WindowHandle, [IntPtr]$ControlHandle)
    [CardStack.Oracle.Native]::RequireTarget($ControlHandle)
    [void][CardStack.Oracle.Native]::Send($WindowHandle, 0x0028, $ControlHandle, [IntPtr]1)
    if ([CardStack.Oracle.Native]::FocusWindow($ControlHandle) -ne $ControlHandle) {
        Invoke-MessageClick -Handle $ControlHandle
    }
}

function Save-OracleCapture {
    param([IntPtr]$Handle, [string]$Key, [string]$OutputDir)
    if ($Key -notmatch '^[a-z0-9_]+[.][a-z0-9_]+$') { throw "Invalid semantic capture key: $Key" }
    $directory=Join-Path $OutputDir 'captures'
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    $path=Join-Path $directory ($Key+'.png')
    $menus=@([CardStack.Oracle.Native]::Menus($Handle))
    $controls=@([CardStack.Oracle.Native]::Controls($Handle))
    $image=[CardStack.Oracle.Native]::Capture($Handle,$path)
    $evidence=[ordered]@{key=$Key;image=($Key+'.png');desktop=[CardStack.Oracle.Native]::DesktopName();window=[CardStack.Oracle.Native]::Describe($Handle);controls=$controls;menus=$menus;capture=$image}
    $mdi=@($evidence.controls | Where-Object { $_.class -eq 'MDIClient' })
    if ($mdi.Count -and @($controls | Where-Object { $_.class -like '*tnMDIChild*' }).Count -and !@($mdi | Where-Object { $_.visible }).Count) {
        $evidence.capture['partial']=$true
        $evidence.capture['limitation']='The original MDI workspace is hidden; this image is toolbar evidence only.'
        $diagnostics=New-Object 'System.Collections.Generic.List[object]'
        $children=@($controls | Where-Object { $_.class -like '*tnMDIChild*' } | Select-Object -First 4)
        for ($index=0;$index -lt $children.Count;$index++) {
            $name=$Key+'_hidden_workspace_'+$index+'.png'
            try {
                $render=[CardStack.Oracle.Native]::Capture([IntPtr][long]$children[$index].handle,(Join-Path $directory $name))
                $diagnostics.Add([ordered]@{image=$name;diagnosticOnly=$true;visible=$children[$index].visible;capture=$render})
            } catch { Write-Warning ('Hidden workspace diagnostic: '+$_.Exception.Message) }
        }
        $evidence.capture['diagnostics']=$diagnostics.ToArray()
    }
    $evidence | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $directory ($Key+'.json')) -Encoding UTF8
    $script:OracleCaptures.Add($evidence)
    Write-Step ("Captured {0}" -f $Key)
}

function Test-OracleExpectation {
    param([object]$Expected,[IntPtr]$Window)
    if ($null -ne $Expected.focusId) {
        $focus=[CardStack.Oracle.Native]::FocusWindow($Window)
        return $focus -ne [IntPtr]::Zero -and [CardStack.Oracle.Native]::GetDlgCtrlID($focus) -eq [int]$Expected.focusId
    }
    if ($null -ne $Expected.menuChecked) {
        return @([CardStack.Oracle.Native]::Menus($Window) | Where-Object { $_.id -eq [uint32]$Expected.menuChecked -and $_.checked }).Count -gt 0
    }
    if ($null -ne $Expected.menuPresent) {
        return @([CardStack.Oracle.Native]::Menus($Window) | Where-Object { $_.id -eq [uint32]$Expected.menuPresent }).Count -gt 0
    }
    if ($null -ne $Expected.menuAbsent) {
        $items=@([CardStack.Oracle.Native]::Menus($Window))
        return $items.Count -gt 0 -and @($items | Where-Object { $_.id -eq [uint32]$Expected.menuAbsent }).Count -eq 0
    }
    if ($null -ne $Expected.controlId) {
        $control=[CardStack.Oracle.Native]::GetDlgItem($Window,[int]$Expected.controlId)
        if ($control -eq [IntPtr]::Zero) { return $false }
        if ($null -ne $Expected.text) { return [CardStack.Oracle.Native]::ReadText($control) -eq [string]$Expected.text }
        if ($null -ne $Expected.checked) { return [CardStack.Oracle.Native]::Send($control,0xF0,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -eq [int]$Expected.checked }
    }
    throw 'Unknown expectation; an input action must have a specific observable postcondition.'
}

function Wait-OracleExpectation {
    param([object]$Expected,[IntPtr]$Window,[int]$TimeoutMs=3000)
    $clock=[Diagnostics.Stopwatch]::StartNew()
    do {
        if (Test-OracleExpectation $Expected $Window) { return }
        Start-Sleep -Milliseconds 20
    } while ($clock.ElapsedMilliseconds -lt $TimeoutMs)
    throw ("Observable postcondition did not occur: {0}" -f ($Expected | ConvertTo-Json -Compress))
}

function Set-OracleInterestingState {
    param([IntPtr]$Window)
    foreach ($entry in @([CardStack.Oracle.Native]::Controls($Window))) {
        if (!$entry.visible -or !$entry.enabled -or $entry.class -notin @('ComboBox','ListBox')) { continue }
        $handle=[IntPtr][long]$entry.handle
        $combo=$entry.class -eq 'ComboBox'
        $count=[CardStack.Oracle.Native]::Send($handle,$(if($combo){0x146}else{0x18B}),[IntPtr]::Zero,[IntPtr]::Zero).ToInt32()
        if ($count -lt 1 -or $count -gt 1024) { continue }
        $measured=[CardStack.Oracle.Native]::WidestItem($handle)
        $journal=$script:OracleSteps[$script:OracleSteps.Count-1]
        if (!$journal.Contains('measuredItems')) { $journal['measuredItems']=@() }
        $journal['measuredItems']+=@([ordered]@{controlId=[int]$entry.id;selection=$measured})
        [void][CardStack.Oracle.Native]::Send($handle,$(if($combo){0x14E}else{0x186}),[IntPtr][int]$measured.index,[IntPtr]::Zero)
        Send-ControlNotification -ParentHandle $Window -ControlHandle $handle -ControlId ([int]$entry.id) -NotificationCode 1
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
    $result = $result.Replace("{iteration}", ([string]$script:OracleIteration))
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

    $protectedInputPaths=@()
    if (![string]::IsNullOrWhiteSpace($LaunchDocument)) {
        $inputPath=if([IO.Path]::IsPathRooted($LaunchDocument)){$LaunchDocument}else{Join-Path $runtimeDir $LaunchDocument}
        $inputPath=[IO.Path]::GetFullPath($inputPath)
        $protectedInputPaths=@($inputPath,[IO.Path]::ChangeExtension($inputPath,'.RPT'))
    }
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
            if ($protectedInputPaths -icontains [IO.Path]::GetFullPath($path)) {
                Write-Step ("Retained staged input document or sidecar: {0}" -f $path)
                continue
            }
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
                Where-Object { $protectedInputPaths -inotcontains [IO.Path]::GetFullPath($_.FullName) } |
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

    $script:OracleSteps.Add([ordered]@{action=[string]$Step.action;controlId=$Step.controlId;commandId=$Step.id;route='private-desktop-window-messages'})
    if ($CurrentWindow.Value -and [CardStack.Oracle.Native]::IsWindow($CurrentWindow.Value.Handle)) { [CardStack.Oracle.Native]::RequireTarget($CurrentWindow.Value.Handle) }
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
            [void][CardStack.Oracle.Native]::Post($CurrentWindow.Value.Handle, 0x0111, [IntPtr]([int]$Step.id), [IntPtr]::Zero)
            $delay = if ($Step.delayMs) { [int]$Step.delayMs } else { 250 }
            Start-Sleep -Milliseconds $delay
        }
        "closeDecks" {
            $repeat = if ($Step.repeat) { [int]$Step.repeat } else { 8 }
            for ($i = 0; $i -lt $repeat; $i++) {
                if (-not $CurrentWindow.Value) {
                    $CurrentWindow.Value = Wait-OracleWindow -TitleLike "buttonFile" -TimeoutMs 10000 -ProcessId $Process.Id
                }
                [void][CardStack.Oracle.Native]::Post($CurrentWindow.Value.Handle, 0x0111, [IntPtr]2704, [IntPtr]::Zero)
                Start-Sleep -Milliseconds 350
                [void](Accept-LegacyOverwriteDialogIfPresent -ProcessId $Process.Id)
            }
            Start-Sleep -Milliseconds 500
        }
        "waitDeck" {
            $deadline=[DateTime]::UtcNow.AddSeconds(10)
            do {
                $ready=$false
                $mdis=@([CardStack.Oracle.Native]::Controls($CurrentWindow.Value.Handle) | Where-Object { $_.class -eq 'MDIClient' -and $_.visible })
                if ($mdis.Count -eq 1 -and [CardStack.Oracle.Native]::IsWindowEnabled($CurrentWindow.Value.Handle)) {
                    $active=[CardStack.Oracle.Native]::Send([IntPtr][long]$mdis[0].handle,0x229,[IntPtr]::Zero,[IntPtr]::Zero)
                    $ready=[CardStack.Oracle.Native]::IsWindow($active) -and [CardStack.Oracle.Native]::IsWindowVisible($active)
                    if ($ready -and $Step.titleLike) { $ready=[CardStack.Oracle.Native]::ReadText($active) -like ('*'+[string]$Step.titleLike+'*') }
                }
                if ($ready) { break }
                Start-Sleep -Milliseconds 50
            } while ([DateTime]::UtcNow -lt $deadline)
            if (!$ready) { throw 'No visible active deck became ready; no forced-visibility fallback is allowed.' }
        }
        "nudgeFrame" {
            $index=if($Step.grid){[int]$script:OracleIteration-1}else{[int]$Step.index}
            $dx=[int]$Step.dx;$dy=[int]$Step.dy
            if($Step.grid){
                $columns=[int]$Step.grid.columns
                if($columns -lt 1 -or $columns -gt 40){throw 'Invalid authoring grid.'}
                $dx=[int]$Step.grid.offsetX+($index % $columns)*[int]$Step.grid.columnPitch
                $dy=[int]$Step.grid.offsetY+[Math]::Floor($index/$columns)*[int]$Step.grid.rowPitch
            }
            if([Math]::Abs($dx)+[Math]::Abs($dy) -gt 2400 -or $dx % 3 -ne 0 -or $dy % 3 -ne 0){throw 'A bounded three-pixel keyboard nudge is required.'}
            $controls=@([CardStack.Oracle.Native]::Controls($CurrentWindow.Value.Handle))
            $frames=@($controls | Where-Object {$_.class -like '*TNcontrols' -and $_.visible -and $_.id -eq $index})
            $mdi=@($controls | Where-Object {$_.class -eq 'MDIClient' -and $_.visible})
            if($frames.Count -ne 1 -or $mdi.Count -ne 1){throw 'The newly added frame must be uniquely visible.'}
            $frame=[IntPtr][long]$frames[0].handle
            $target=[CardStack.Oracle.Native]::Send([IntPtr][long]$mdi[0].handle,0x229,[IntPtr]::Zero,[IntPtr]::Zero)
            [CardStack.Oracle.Native]::FocusPrivateWindow($target)
            $script:OracleSteps[$script:OracleSteps.Count-1]['focusSetup']='private-logical-focus-only'
            $before=@($frames[0].rect)
            $lease=[CardStack.Oracle.Native]::BeginKeys($target,@())
            try{
                foreach($axis in @(@{delta=$dx;positive=39;negative=37},@{delta=$dy;positive=40;negative=38})){
                    $key=if($axis.delta -ge 0){$axis.positive}else{$axis.negative}
                    for($pulse=0;$pulse -lt [Math]::Abs($axis.delta)/3;$pulse++){$lease.Down($key);$lease.Up($key)}
                }
                $deadline=[DateTime]::UtcNow.AddSeconds(15)
                do{
                    $after=@(([CardStack.Oracle.Native]::Describe($frame)).rect)
                    $verified=$after[0]-$before[0] -eq $dx -and $after[1]-$before[1] -eq $dy -and $after[2] -eq $before[2] -and $after[3] -eq $before[3]
                    if($verified){break};Start-Sleep -Milliseconds 20
                }while([DateTime]::UtcNow -lt $deadline)
                if(!$verified){throw 'The original arrow-key route did not produce the required frame displacement.'}
                $journal=$script:OracleSteps[$script:OracleSteps.Count-1]
                $journal['frameIndex']=$index;$journal['dx']=$dx;$journal['dy']=$dy;$journal['before']=$before;$journal['after']=$after;$journal['postconditionVerified']=$true
            }finally{$lease.Dispose()}
        }
        "frameDoubleClick" {
            $frames=@([CardStack.Oracle.Native]::Controls($CurrentWindow.Value.Handle) | Where-Object { $_.class -like '*TNcontrols' -and $_.visible -and $_.id -eq [int]$Step.controlId })
            if($frames.Count -ne 1){throw 'Designer frame target is missing or ambiguous.'}
            [CardStack.Oracle.Native]::Click([IntPtr][long]$frames[0].handle,-1,-1,$true)
        }
        "assertField" {
            $mdi=@([CardStack.Oracle.Native]::Controls($CurrentWindow.Value.Handle) | Where-Object { $_.class -eq 'MDIClient' -and $_.visible })
            if($mdi.Count -ne 1){throw 'A visible MDI client is required for field readback.'}
            $active=[CardStack.Oracle.Native]::Send([IntPtr][long]$mdi[0].handle,0x229,[IntPtr]::Zero,[IntPtr]::Zero)
            $field=@([CardStack.Oracle.Native]::Controls($active) | Where-Object { $_.id -eq (5200+[int]$Step.index) -and $_.class -like '*EntryField*' -and $_.visible })
            if($field.Count -ne 1){throw 'Reopened field is missing or ambiguous.'}
            $expected=Expand-Token -Value ([string]$Step.text) -FixtureSpec $FixtureSpec -FixturePath $FixturePath
            if([CardStack.Oracle.Native]::ReadText([IntPtr][long]$field[0].handle) -cne $expected){throw "Reopened field $($Step.index) differs from its authored value."}
            $script:OracleSteps[$script:OracleSteps.Count-1]['readbackVerified']=$true
        }
        "assertDeckCount" {
            $deadline=[DateTime]::UtcNow.AddSeconds(5)
            do {
                $counters=@([CardStack.Oracle.Native]::Controls($CurrentWindow.Value.Handle) | Where-Object { $_.id -eq 4010 -and $_.class -eq 'Static' -and $_.visible })
                $matches=@($counters | Where-Object { $_.text -match ('\bof\s+'+[regex]::Escape([string]$Step.count)+'\s*$') })
                if ($matches.Count) { break };Start-Sleep -Milliseconds 50
            } while([DateTime]::UtcNow -lt $deadline)
            if (!$matches.Count) { throw "Original card counter did not report $($Step.count) records." }
        }
        "repeat" {
            $count=[int]$Step.count
            if ($count -lt 1 -or $count -gt 100) { throw 'Repeat count must be between 1 and 100.' }
            $previous=$script:OracleIteration
            try { for ($iteration=1;$iteration -le $count;$iteration++) {
                $script:OracleIteration=$iteration
                foreach($childStep in $Step.steps) { Invoke-FixtureStep -Step $childStep -FixtureSpec $FixtureSpec -FixturePath $FixturePath -OutputDir $OutputDir -Process $Process -CurrentWindow $CurrentWindow }
            }} finally { $script:OracleIteration=$previous }
        }
        "saveDeckAs" {
            $main=Wait-OracleWindow -TitleLike 'buttonFile' -ExcludeTitleLike 'Evaluation Copy' -TimeoutMs 10000 -ProcessId $Process.Id
            $deadline=[DateTime]::UtcNow.AddSeconds(10);$requested=$false;$fileDialog=$null
            do {
                $found=New-Object 'System.Collections.Generic.List[object]'
                $callback=[CardStack.EnumWindowsProc]{ param([IntPtr]$window,[IntPtr]$parameter)
                    $pidValue=0;[void][CardStack.Win32Oracle]::GetWindowThreadProcessId($window,[ref]$pidValue)
                    if ($pidValue -eq $Process.Id -and [CardStack.Win32Oracle]::IsWindowVisible($window) -and [CardStack.Win32Oracle]::GetDlgItem($window,1152) -ne [IntPtr]::Zero) {
                        $found.Add([pscustomobject]@{Handle=$window;Title=(Get-WindowTitle $window)})
                    };return $true
                }
                [void][CardStack.Win32Oracle]::EnumWindows($callback,[IntPtr]::Zero)
                if ($found.Count -eq 1) { $fileDialog=$found[0];break }
                Assert-NoLegacyErrorDialog -ProcessId $Process.Id
                if (!$requested -and [CardStack.Oracle.Native]::IsWindowEnabled($main.Handle)) {
                    [CardStack.Oracle.Native]::Post($main.Handle,0x111,[IntPtr]2005,[IntPtr]::Zero);$requested=$true
                }
                Start-Sleep -Milliseconds 100
            } while([DateTime]::UtcNow -lt $deadline)
            if (!$fileDialog) { throw 'A file destination dialog did not become available.' }
            $edit=[CardStack.Win32Oracle]::GetDlgItem($fileDialog.Handle,1152)
            $name=if($Step.fileName){[string]$Step.fileName}else{Split-Path -Leaf $FixturePath}
            if([IO.Path]::GetFileName($name) -ne $name -or $name -in @('','.','..')){throw 'Save destination must be a leaf filename.'}
            [CardStack.Oracle.Native]::WriteText($edit,$name)
            [CardStack.Oracle.Native]::Post($fileDialog.Handle,0x111,[IntPtr]1,[CardStack.Win32Oracle]::GetDlgItem($fileDialog.Handle,1))
            do { Assert-NoLegacyErrorDialog -ProcessId $Process.Id;if(![CardStack.Oracle.Native]::IsWindow($fileDialog.Handle)){break};Start-Sleep -Milliseconds 50 } while([DateTime]::UtcNow -lt $deadline)
            if ([CardStack.Oracle.Native]::IsWindow($fileDialog.Handle)) { throw 'The file destination dialog did not close.' }
            $CurrentWindow.Value=$main
        }
        "editField" {
            $index=[int]$Step.index
            if ($index -lt 0 -or $index -ge 40) { throw 'Field index must be between 0 and 39.' }
            $mdi=@([CardStack.Oracle.Native]::Controls($CurrentWindow.Value.Handle) | Where-Object { $_.class -eq 'MDIClient' })
            if ($mdi.Count -ne 1) { throw 'A unique MDI client is required for field editing.' }
            $active=[CardStack.Oracle.Native]::Send([IntPtr][long]$mdi[0].handle,0x229,[IntPtr]::Zero,[IntPtr]::Zero)
            $fields=@([CardStack.Oracle.Native]::Controls($active) | Where-Object { $_.id -eq (5200+$index) -and $_.class -like '*EntryField*' -and $_.visible })
            if ($fields.Count -ne 1) { throw "A unique visible field $index was not found in the active deck." }
            $control=[IntPtr][long]$fields[0].handle
            Invoke-MessageClick -Handle $control
            [CardStack.Oracle.Native]::FocusPrivateWindow($control)
            $text=Expand-Token -Value ([string]$Step.text) -FixtureSpec $FixtureSpec -FixturePath $FixturePath
            $expected=if ($null -ne $Step.expectedText) { [string]$Step.expectedText } else { $text }
            [CardStack.Oracle.Native]::TypeText($control,$text)
            $deadline=[DateTime]::UtcNow.AddSeconds([Math]::Min(45,5+[Math]::Ceiling($text.Length/500.0)))
            do { $actual=[CardStack.Oracle.Native]::ReadText($control);if($actual -eq $expected){break};Start-Sleep -Milliseconds 20 } while([DateTime]::UtcNow -lt $deadline)
            if ($actual -ne $expected) { throw "Field $index input readback differs: expected length $($expected.Length), got $($actual.Length)." }
            $lease=[CardStack.Oracle.Native]::BeginKeys($control,@())
            try {
                $tabMoved=$null
                if ($Step.testTab) {
                    $lease.Down(9)
                    $deadline=[DateTime]::UtcNow.AddSeconds(3)
                    while ([CardStack.Oracle.Native]::FocusWindow($control) -eq $control -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 10 }
                    $lease.Up(9)
                    $tabMoved=[CardStack.Oracle.Native]::FocusWindow($control) -ne $control
                }
                if (!$tabMoved) {
                    # Fixture authoring may change logical focus directly; this is NOT counted as a Tab pass.
                    $nextFields=@([CardStack.Oracle.Native]::Controls($active) | Where-Object { $_.class -like '*EntryField*' -and $_.visible -and $_.enabled -and $_.id -gt (5200+$index) } | Sort-Object id)
                    if (!$nextFields.Count) { $nextFields=@([CardStack.Oracle.Native]::Controls($active) | Where-Object { $_.class -like '*EntryField*' -and $_.visible -and $_.enabled -and $_.id -ne (5200+$index) } | Sort-Object id) }
                    if (!$nextFields.Count) { throw 'No alternate entry field is available for a verified commit.' }
                    $nextFocus=[IntPtr][long]$nextFields[0].handle
                    [CardStack.Oracle.Native]::FocusPrivateWindow($nextFocus)
                }
                if ([CardStack.Oracle.Native]::FocusWindow($control) -eq $control) { throw 'Field edit did not leave its editor.' }
            } finally { $lease.Dispose() }
            $journal=$script:OracleSteps[$script:OracleSteps.Count-1]
            $journal['fieldIndex']=$index;$journal['expectedText']=$expected;$journal['readbackVerified']=$true
            $journal['tabMovedFocus']=$tabMoved;$journal['commitRoute']=if($tabMoved){'key-tab'}else{'private-logical-focus'}
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
            [CardStack.Oracle.Native]::TypeText($control,$text)
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
            Invoke-MessageClick -Handle $control
            Start-Sleep -Milliseconds 500
        }
        { $_ -in @("mouseClick","physicalClick") } {
            if (-not $CurrentWindow.Value) { throw 'mouseClick requires a current window.' }
            $control=[CardStack.Win32Oracle]::GetDlgItem($CurrentWindow.Value.Handle,[int]$Step.controlId)
            if ($control -eq [IntPtr]::Zero) {
                if ($Step.optional) { break }
                throw "Missing mouse target control $($Step.controlId)."
            }
            Invoke-MessageClick -Handle $control
        }
        "listDoubleClick" {
            if (-not $CurrentWindow.Value) { throw 'listDoubleClick requires a current window.' }
            $control=[CardStack.Win32Oracle]::GetDlgItem($CurrentWindow.Value.Handle,[int]$Step.controlId)
            $index=[int]$Step.index
            $rect=New-Object CardStack.RECT
            [void][CardStack.Win32Oracle]::SendMessageRect($control,0x198,[IntPtr]$index,[ref]$rect)
            [CardStack.Oracle.Native]::Click($control,[int](($rect.Left+$rect.Right)/2),[int](($rect.Top+$rect.Bottom)/2),$true)
            Start-Sleep -Milliseconds 300
        }
        "acceptLegacyDialog" {
            $messageLike = if ($Step.messageLike) { [string]$Step.messageLike } else { "" }
            $buttonId = if ($Step.buttonId) { [int]$Step.buttonId } else { 1 }
            $timeout = if ($Step.timeoutMs) { [int]$Step.timeoutMs } else { 10000 }
            $deadline = (Get-Date).AddMilliseconds($timeout)
            $dialog = $null
            do {
                $candidate = Get-LegacyErrorDialog -ProcessId $Process.Id -ExpectedMessage $messageLike
                if ($candidate -and ([string]::IsNullOrWhiteSpace($messageLike) -or $candidate.Message -like "*$messageLike*")) {
                    $dialog = $candidate
                    break
                }
                Start-Sleep -Milliseconds 250
            } while ((Get-Date) -lt $deadline)
            if (-not $dialog) {
                throw "Timed out waiting for expected legacy dialog matching '$messageLike'. Last observed message: $($candidate.Message)"
            }
            Write-Step ("Accept legacy dialog: {0}" -f $dialog.Message)
            $CurrentWindow.Value = $dialog
            $control = Get-OracleDialogButton -Dialog $dialog.Handle -Id $buttonId
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
                $control = Get-OracleDialogButton -Dialog $dialog.Handle -Id $buttonId
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
            Invoke-MessageClick -Handle $control
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
            $button=[CardStack.Win32Oracle]::GetDlgItem($CurrentWindow.Value.Handle,1)
            [CardStack.Oracle.Native]::Post($CurrentWindow.Value.Handle,0x111,[IntPtr]1,$button)
            Write-Step 'Submitted the actual common-dialog IDOK route (not a synthetic FILEOK hook).'
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
            $keys=Expand-Token -Value ([string]$Step.keys) -FixtureSpec $FixtureSpec -FixturePath $FixturePath
            if ($keys -match '[{+^%]') { throw 'SendKeys syntax is not accepted. Use a key action with virtualKey, modifiers and an observable expect condition.' }
            $target=[CardStack.Oracle.Native]::FocusWindow($CurrentWindow.Value.Handle)
            if ($target -eq [IntPtr]::Zero) { throw 'No logical focus target for literal text.' }
            [CardStack.Oracle.Native]::TypeText($target,$keys)
        }
        "focus" {
            $control=[CardStack.Win32Oracle]::GetDlgItem($CurrentWindow.Value.Handle,[int]$Step.controlId)
            Set-OracleFocus $CurrentWindow.Value.Handle $control
        }
        "key" {
            if (!$Step.expect) { throw 'Key delivery requires an observable expect condition.' }
            $target=if ($Step.controlId) { [CardStack.Win32Oracle]::GetDlgItem($CurrentWindow.Value.Handle,[int]$Step.controlId) } else { [CardStack.Oracle.Native]::FocusWindow($CurrentWindow.Value.Handle) }
            $focusDeadline=[DateTime]::UtcNow.AddSeconds(3)
            while ($target -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $focusDeadline) {
                Start-Sleep -Milliseconds 10
                $target=[CardStack.Oracle.Native]::FocusWindow($CurrentWindow.Value.Handle)
            }
            if ($target -eq [IntPtr]::Zero) { throw 'No logical focus target for this keyboard probe.' }
            $journal=$script:OracleSteps[$script:OracleSteps.Count-1]
            $journal['targetHandle']=$target.ToInt64()
            $journal['virtualKey']=[int]$Step.virtualKey
            $journal['modifiers']=@($Step.modifiers)
            $journal['expected']=$Step.expect
            $lease=[CardStack.Oracle.Native]::BeginKeys($target,[string[]]@($Step.modifiers))
            try {
                $lease.Down([int]$Step.virtualKey)
                Wait-OracleExpectation -Expected $Step.expect -Window $CurrentWindow.Value.Handle
                $journal['postconditionVerified']=$true
                $lease.Up([int]$Step.virtualKey)
            } finally { $lease.Dispose() }
        }
        "assert" {
            Wait-OracleExpectation -Expected $Step.expect -Window $CurrentWindow.Value.Handle
        }
        "capture" {
            Save-OracleCapture -Handle $CurrentWindow.Value.Handle -Key ([string]$Step.key) -OutputDir $OutputDir
        }
        "interestingState" {
            Set-OracleInterestingState -Window $CurrentWindow.Value.Handle
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
            $label=if($Step.label){[string]$Step.label}else{[string]$FixtureSpec.name}
            if ($label -notmatch '^[A-Za-z0-9_-]+$') { throw 'Unsafe snapshot label.' }
            $snapshotDir=[IO.Path]::GetFullPath((Join-Path $OutputDir ('snapshot-'+$label)))
            if (!$snapshotDir.StartsWith($OutputDir+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Snapshot escaped its output directory.' }
            New-Item -ItemType Directory -Force -Path $snapshotDir | Out-Null
            Get-ChildItem -LiteralPath $snapshotDir -File -ErrorAction SilentlyContinue | Remove-Item -Force
            $runtimeDir=Split-Path -Parent $FixturePath
            $snapshotFiles=@()
            if ($Step.patterns) {
                foreach($pattern in $Step.patterns) {
                    $expanded=Expand-Token -Value ([string]$pattern) -FixtureSpec $FixtureSpec -FixturePath $FixturePath
                    $snapshotFiles+=Get-ChildItem -LiteralPath $runtimeDir -Filter $expanded -File -ErrorAction SilentlyContinue
                }
            } else {
                $snapshotFiles=@(Get-ChildItem -LiteralPath $runtimeDir -File | Where-Object { $_.Extension -match '^\.(BTN|RPT|BTR|DAT|DBF|DBT|CRD|WP|TN|CSV)$' })
            }
            foreach($file in @($snapshotFiles | Sort-Object FullName -Unique)) { Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $snapshotDir $file.Name) -Force }
            $retained=@(Get-ChildItem -LiteralPath $snapshotDir -File | Where-Object { $_.Length -gt 0 })
            if (!$retained.Count -and !$Step.allowEmpty) { throw "Snapshot retained no nonempty artifacts: $label" }
            $proof=@($retained | ForEach-Object { [ordered]@{name=$_.Name;size=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash} })
            [ordered]@{label=$label;files=$proof} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $snapshotDir 'snapshot.json') -Encoding UTF8
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
                $expandedPattern=Expand-Token -Value ([string]$pattern) -FixtureSpec $FixtureSpec -FixturePath $FixturePath
                $candidate = Get-ChildItem -Path (Split-Path -Parent $FixturePath) -Filter $expandedPattern -File -ErrorAction SilentlyContinue | Where-Object { $_.Length -gt 0 } | Select-Object -First 1
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

# Isolation is the default and only automation mode. The desktop is never made visible.
foreach ($path in @($OutputDir,$RuntimeDir)) {
    $allowed = @((Join-Path $repoRoot 'build'), (Join-Path $repoRoot 'docs\audit')) | ForEach-Object { $_ + [IO.Path]::DirectorySeparatorChar }
    if (-not @($allowed | Where-Object { $path.StartsWith($_,[StringComparison]::OrdinalIgnoreCase) }).Count) { throw "Oracle scratch paths must be below build or docs/audit: $path" }
}
if (-not $IsolatedWorker) {
    if ($Interactive -or $KeepProcess) { throw 'Interactive foreground and persistent-process modes are not allowed. Use captures, assertions and trace output on the private desktop.' }
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    $lock=[IO.File]::Open((Join-Path $OutputDir '.oracle.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    try {
        $definition=Get-Content -LiteralPath $ScenarioPath -Raw -Encoding UTF8 | ConvertFrom-Json
        $chosen=@($definition.fixtures | Where-Object { ($IncludeDisabled -or $_.enabled) -and ($Fixture.Count -eq 0 -or $Fixture -contains $_.name) })
        if ($chosen.Count -eq 0 -or $chosen.Count -gt 128) { throw 'Select between 1 and 128 explicit fixture cases.' }
        $runs=New-Object 'System.Collections.Generic.List[object]'
        foreach ($spec in $chosen) {
            if ([string]$spec.name -notmatch '^[A-Za-z0-9_-]+$') { throw 'Unsafe fixture case name.' }
            $runDir=Join-Path (Join-Path $OutputDir 'runs') ([string]$spec.name)
            New-Item -ItemType Directory -Force -Path $runDir | Out-Null
            $packet=[ordered]@{LegacyExe=$LegacyExe;LegacyDir=$LegacyDir;WineVdmDir=$WineVdmDir;ScenarioPath=$ScenarioPath;OutputDir=$OutputDir;RuntimeDir=$RuntimeDir;StageOnlyDeck=$StageOnlyDeck;LaunchDocument=$LaunchDocument;Fixture=@([string]$spec.name);IncludeDisabled=[bool]$IncludeDisabled;NoDownload=[bool]$NoDownload;TraceWindows=[bool]$TraceWindows}
            $packetPath=Join-Path $runDir 'run-request.json'
            $packet | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $packetPath -Encoding UTF8
            & (Join-Path $PSScriptRoot 'invoke_private_desktop.ps1') -Executable (Join-Path $PSHOME 'powershell.exe') -ArgumentList @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$PSCommandPath,'-IsolatedWorker','-RunRequest',$packetPath) -WorkingDirectory $repoRoot -OutputDir $runDir -TimeoutSeconds $TimeoutSeconds
            $evidence=Join-Path $OutputDir 'run-evidence.json'
            if (Test-Path -LiteralPath $evidence) { Copy-Item -LiteralPath $evidence -Destination (Join-Path $runDir 'run-evidence.json') -Force }
            $runs.Add([ordered]@{fixture=[string]$spec.name;status='passed';evidence=(Join-Path $runDir 'run-evidence.json')})
        }
        $runs | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $OutputDir 'runs.json') -Encoding UTF8
    } finally { $lock.Dispose() }
    exit 0
}
[CardStack.Oracle.Native]::RequirePrivate()
$script:OracleCaptures=New-Object 'System.Collections.Generic.List[object]'
$script:OracleSteps=New-Object 'System.Collections.Generic.List[object]'
$script:OracleOutcome='failed'
$otvdmwPath = Ensure-WineVdm -Root $WineVdmDir
$legacyExePath = Resolve-LegacyExe
$scenario = Get-Content -Raw -LiteralPath $ScenarioPath -Encoding UTF8 | ConvertFrom-Json

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

if ([string]::IsNullOrWhiteSpace($LaunchDocument) -and $selectedFixtures.Count -eq 1 -and $selectedFixtures[0].launchDocument) {
    $leaf=[string]$selectedFixtures[0].launchDocument
    if ([IO.Path]::GetFileName($leaf) -ne $leaf) { throw 'Scenario launchDocument must be a staged filename, not a path.' }
    $LaunchDocument=Join-Path $stageDir $leaf
    if (!(Test-Path -LiteralPath $LaunchDocument -PathType Leaf)) { throw "Missing staged launch document: $leaf" }
}
$launchArgs = @($stagedExe)
if (-not [string]::IsNullOrWhiteSpace($LaunchDocument)) {
    $launchArgs += $LaunchDocument
}
$process = [CardStack.Oracle.Native]::StartChild($otvdmwPath,[string[]]$launchArgs,$stageDir)
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
    $script:OracleOutcome='passed'
}
catch {
    Write-TopWindowDump -ProcessId $process.Id
    if ($currentWindow -and [CardStack.Oracle.Native]::IsWindow($currentWindow.Handle)) {
        try { Save-OracleCapture -Handle $currentWindow.Handle -Key 'failure.initial' -OutputDir $OutputDir } catch { Write-Warning $_.Exception.Message }
    }
    throw
}
finally {
    [ordered]@{version=1;status=$script:OracleOutcome;desktop=[CardStack.Oracle.Native]::DesktopName();inputDesktop=[CardStack.Oracle.Native]::InputDesktopName();inputMode='window-messages-only';steps=($script:OracleSteps.ToArray());captures=($script:OracleCaptures.ToArray())} |
        ConvertTo-Json -Depth 24 | Set-Content -LiteralPath (Join-Path $OutputDir 'run-evidence.json') -Encoding UTF8
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
