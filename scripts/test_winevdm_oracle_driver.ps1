param([string]$OutputDir='', [switch]$Worker, [switch]$Hang)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if (!$OutputDir) { $OutputDir=Join-Path $root 'build\oracle-driver-self-test' }
$OutputDir=[IO.Path]::GetFullPath($OutputDir)
if (-not ('CardStack.Oracle.Native' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'WineVdmOracleNative.cs') -ReferencedAssemblies System.dll,System.Core.dll,System.Drawing.dll,System.Windows.Forms.dll
}
if ($Worker) {
    [CardStack.Oracle.Native]::RequirePrivate()
    if ($Hang) { Start-Sleep -Seconds 300; exit 9 }
    $evidence=[CardStack.Oracle.Native]::SelfTest($OutputDir)
    $evidence | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $OutputDir 'driver-evidence.json') -Encoding UTF8
    Write-Host 'Private desktop, character replacement, mouse messages, Ctrl+A and GDI capture passed.'
    exit 0
}
$source=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'WineVdmOracleNative.cs') -Raw
$forbidden='extern\s+\w+\s+(SendInput|SetCursorPos|mouse_event|keybd_event|SetForegroundWindow|SwitchDesktop)\s*\('
if ($source -match $forbidden) { throw 'A forbidden global-input or desktop-switch API was imported.' }
$blocked=$false
try { [void][CardStack.Oracle.Native]::BeginKeys([IntPtr]::Zero, @('Control')) } catch { $blocked=$true }
if (!$blocked) { throw 'Keyboard queue guard did not fail closed outside a private desktop.' }
$ps=Join-Path $PSHOME 'powershell.exe'
& (Join-Path $PSScriptRoot 'invoke_private_desktop.ps1') -Executable $ps -ArgumentList @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$PSCommandPath,'-Worker','-OutputDir',$OutputDir) -WorkingDirectory $root -OutputDir $OutputDir -TimeoutSeconds 30
$watch=Join-Path $OutputDir 'watchdog'
New-Item -ItemType Directory -Force -Path $watch | Out-Null
$timer=[Diagnostics.Stopwatch]::StartNew()
$result=[CardStack.Oracle.Native]::Run($ps,@('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$PSCommandPath,'-Worker','-Hang','-OutputDir',$watch),$root,$watch,1)
if (!$result.TimedOut -or $result.ExitCode -ne 124 -or $timer.ElapsedMilliseconds -gt 10000) { throw 'The owned-process watchdog did not terminate its worker within bounds.' }
$result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $watch 'isolation.json') -Encoding UTF8
Write-Host 'Oracle driver self-test and owned-process watchdog passed. No physical input was injected.'
