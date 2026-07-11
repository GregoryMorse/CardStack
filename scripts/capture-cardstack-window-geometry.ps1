param(
    [string]$ProcessName = "CardStack",
    [string]$OutputPath = "build\cardstack-window-geometry.json"
)

$signature = @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class Win32WindowGeometry {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumChildWindows(IntPtr hWndParent, EnumWindowsProc lpEnumFunc, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);

    [DllImport("user32.dll")]
    public static extern int GetClassName(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
}
"@

Add-Type -TypeDefinition $signature -ErrorAction SilentlyContinue

function Get-WindowTextValue([IntPtr]$Handle) {
    $builder = New-Object System.Text.StringBuilder 512
    [void][Win32WindowGeometry]::GetWindowText($Handle, $builder, $builder.Capacity)
    return $builder.ToString()
}

function Get-WindowClassValue([IntPtr]$Handle) {
    $builder = New-Object System.Text.StringBuilder 256
    [void][Win32WindowGeometry]::GetClassName($Handle, $builder, $builder.Capacity)
    return $builder.ToString()
}

function Get-WindowRecord([IntPtr]$Handle, [string]$Kind, [int]$Depth) {
    $rect = New-Object Win32WindowGeometry+RECT
    [void][Win32WindowGeometry]::GetWindowRect($Handle, [ref]$rect)
    return [ordered]@{
        kind = $Kind
        depth = $Depth
        hwnd = ("0x{0:x}" -f $Handle.ToInt64())
        className = Get-WindowClassValue $Handle
        title = Get-WindowTextValue $Handle
        visible = [Win32WindowGeometry]::IsWindowVisible($Handle)
        x = $rect.Left
        y = $rect.Top
        width = $rect.Right - $rect.Left
        height = $rect.Bottom - $rect.Top
    }
}

$processes = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue)
if ($processes.Count -eq 0) {
    throw "No running process named '$ProcessName' was found."
}

$processIds = @{}
foreach ($process in $processes) {
    $processIds[[uint32]$process.Id] = $true
}

$records = New-Object System.Collections.Generic.List[object]

$topCallback = [Win32WindowGeometry+EnumWindowsProc]{
    param([IntPtr]$hWnd, [IntPtr]$lParam)

    $pid = [uint32]0
    [void][Win32WindowGeometry]::GetWindowThreadProcessId($hWnd, [ref]$pid)
    if (-not $processIds.ContainsKey($pid)) {
        return $true
    }

    $records.Add((Get-WindowRecord $hWnd "top-level" 0)) | Out-Null

    $childCallback = [Win32WindowGeometry+EnumWindowsProc]{
        param([IntPtr]$childHwnd, [IntPtr]$childLParam)
        $records.Add((Get-WindowRecord $childHwnd "child" 1)) | Out-Null
        return $true
    }
    [void][Win32WindowGeometry]::EnumChildWindows($hWnd, $childCallback, [IntPtr]::Zero)
    return $true
}

[void][Win32WindowGeometry]::EnumWindows($topCallback, [IntPtr]::Zero)

$outputItem = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath)
$null = $outputItem
$records | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
Write-Host "Wrote $($records.Count) window geometry records to $OutputPath"
