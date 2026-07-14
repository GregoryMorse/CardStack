[CmdletBinding()]
param(
    [string]$BuildDir = "build\vs2022",
    [string]$Config = "Debug",
    [string]$Target = "CardStack",
    [string]$CMake = "",
    [string]$VsDevCmd = ""
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($VsDevCmd)) {
    $VsDevCmd = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
    ) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if ([string]::IsNullOrWhiteSpace($VsDevCmd)) {
    throw "Visual Studio 2022 was not found. Use build-cardstack-windows.ps1 for VS2026-first selection."
}

$arguments = @{
    BuildDir = $BuildDir
    Config = $Config
    Target = $Target
    VsDevCmd = $VsDevCmd
}
if ($CMake) {
    $arguments.CMake = $CMake
}
& (Join-Path $PSScriptRoot "build-cardstack-windows.ps1") @arguments
exit $LASTEXITCODE
