[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$DeployDir,
    [string]$LogDir = "build/deployment-smoke",
    [int]$TimeoutSeconds = 20
)

$ErrorActionPreference = "Stop"
$deploy = (Resolve-Path -LiteralPath $DeployDir).Path
$executable = Join-Path $deploy "CardStack.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "CardStack.exe was not found in $deploy."
}
if ($TimeoutSeconds -lt 1) { throw "TimeoutSeconds must be positive." }
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$logs = (Resolve-Path -LiteralPath $LogDir).Path

$process = New-Object System.Diagnostics.Process
$process.StartInfo = New-Object System.Diagnostics.ProcessStartInfo
$process.StartInfo.FileName = $executable
$process.StartInfo.Arguments = "--smoke-test"
$process.StartInfo.WorkingDirectory = $deploy
$process.StartInfo.UseShellExecute = $false
$process.StartInfo.CreateNoWindow = $true
$process.StartInfo.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden
$process.StartInfo.RedirectStandardOutput = $true
$process.StartInfo.RedirectStandardError = $true
# Isolate only the child process; keep the caller's Qt environment unchanged.
$process.StartInfo.EnvironmentVariables["PATH"] = "$env:SystemRoot\System32;$env:SystemRoot"
foreach ($name in @("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QT_QPA_PLATFORM", "QT_SCREEN_SCALE_FACTORS", "QT_SCALE_FACTOR")) {
    $process.StartInfo.EnvironmentVariables.Remove($name)
}
$process.StartInfo.EnvironmentVariables["QT_DEBUG_PLUGINS"] = "1"
$process.StartInfo.EnvironmentVariables["QT_LOGGING_TO_CONSOLE"] = "1"

try {
    if (-not $process.Start()) { throw "Could not start the deployed application." }
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill()
        $process.WaitForExit()
        throw "Deployed startup smoke test timed out."
    }
    $output = $stdout.GetAwaiter().GetResult()
    $diagnostics = $stderr.GetAwaiter().GetResult()
    [IO.File]::WriteAllText((Join-Path $logs "stdout.txt"), $output)
    [IO.File]::WriteAllText((Join-Path $logs "plugins.txt"), $diagnostics)
    Write-Host $output.Trim()
    if ($process.ExitCode -ne 0) {
        throw "Deployed startup smoke test failed ($($process.ExitCode)). See $logs."
    }
    $loaded = @([regex]::Matches($diagnostics, '"([^"\r\n]+\.dll)" loaded library') |
        ForEach-Object { $_.Groups[1].Value })
    $prefix = $deploy.Replace('\', '/').TrimEnd('/') + '/'
    foreach ($path in $loaded) {
        if (-not $path.Replace('\', '/').StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "A plugin loaded from outside the deployment: $path"
        }
    }
    foreach ($plugin in @("qwindows.dll", "qsqlite.dll", "qsvgicon.dll")) {
        $matches = @($loaded | Where-Object { [IO.Path]::GetFileName($_) -ieq $plugin })
        if ($matches.Count -ne 1) { throw "Expected one deployed $plugin, found $($matches.Count)." }
    }
    Write-Host "Isolated deployment passed: platform, SQLite and SVG plugins are packaged."
} finally {
    $process.Dispose()
}
