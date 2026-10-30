param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [string[]]$ArgumentList = @(),
    [Parameter(Mandatory=$true)][string]$WorkingDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDir,
    [ValidateRange(1,3600)][int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSEdition -ne 'Desktop') { throw 'Use Windows PowerShell 5.1 (powershell.exe) for this driver.' }
if (-not ('CardStack.Oracle.Native' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'WineVdmOracleNative.cs') -ReferencedAssemblies System.dll,System.Core.dll,System.Drawing.dll,System.Windows.Forms.dll
}
$root = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$allowed = @((Join-Path $root 'build'), (Join-Path $root 'docs\audit')) | ForEach-Object { $_ + [IO.Path]::DirectorySeparatorChar }
if (-not @($allowed | Where-Object { $OutputDir.StartsWith($_, [StringComparison]::OrdinalIgnoreCase) }).Count) {
    throw "Private-desktop output must be below build or docs/audit: $OutputDir"
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$result = [CardStack.Oracle.Native]::Run([IO.Path]::GetFullPath($Executable), $ArgumentList, [IO.Path]::GetFullPath($WorkingDirectory), $OutputDir, $TimeoutSeconds)
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDir 'isolation.json') -Encoding UTF8
foreach ($log in @('worker.stdout.log','worker.stderr.log')) {
    $path = Join-Path $OutputDir $log
    if (Test-Path -LiteralPath $path) { Get-Content -LiteralPath $path | Write-Host }
}
if ($result.ExitCode -ne 0) { throw "Isolated worker failed (exit $($result.ExitCode), timeout $($result.TimedOut)); see $OutputDir." }
