[CmdletBinding()]
param(
    [string]$BuildDir = "build\vs2026",
    [string]$Config = "Debug",
    [string]$Target = "CardStack",
    [string]$CMake = "",
    [string]$VsDevCmd = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

function Find-VsDevCmd {
    if ($VsDevCmd) {
        if (-not (Test-Path -LiteralPath $VsDevCmd)) {
            throw "VsDevCmd.bat was not found at $VsDevCmd."
        }
        return $VsDevCmd
    }

    $candidates = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat",
        "${env:ProgramFiles}\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat",
        "${env:ProgramFiles}\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }
    throw "Neither Visual Studio 2026 nor the Visual Studio 2022 fallback was found."
}

function Find-CMake([string]$DevCmd) {
    if ($CMake) {
        return $CMake
    }

    $vsRoot = [System.IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $DevCmd) "..\.."))
    $candidates = @(
        (Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"),
        "${env:ProgramFiles}\CMake\bin\cmake.exe",
        "${env:ProgramFiles(x86)}\CMake\bin\cmake.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    throw "cmake was not found. Pass -CMake with the full path to cmake.exe."
}

$vsDevCmdBat = Find-VsDevCmd
$cmakeExe = Find-CMake $vsDevCmdBat
$buildPath = Join-Path $RepoRoot $BuildDir
$commands = @(
    "call `"$vsDevCmdBat`" -arch=x64 -host_arch=x64",
    "cd /d `"$RepoRoot`"",
    "`"$cmakeExe`" --build `"$buildPath`" --config `"$Config`" --target `"$Target`""
)

& cmd.exe /d /s /c ($commands -join " && ")
exit $LASTEXITCODE
