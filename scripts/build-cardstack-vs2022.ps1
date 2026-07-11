[CmdletBinding()]
param(
    [string]$BuildDir = "build\vs2022",
    [string]$Config = "Debug",
    [string]$Target = "CardStack",
    [string]$CMake = "",
    [string]$VsDevCmd = ""
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

function Find-CMake {
    if ($CMake) {
        return $CMake
    }

    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidates = @(
        "${env:ProgramFiles}\CMake\bin\cmake.exe",
        "${env:ProgramFiles(x86)}\CMake\bin\cmake.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw "cmake was not found on PATH or in the usual Windows install locations. Pass -CMake with the full path to cmake.exe."
}

function Find-VsDevCmd {
    if ($VsDevCmd) {
        return $VsDevCmd
    }

    $candidates = @(
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

    throw "VsDevCmd.bat was not found. Pass -VsDevCmd with the full path to your Visual Studio developer command script."
}

$cmakeExe = Find-CMake
$vsDevCmdBat = Find-VsDevCmd
$buildPath = Join-Path $RepoRoot $BuildDir

$commands = @(
    "call `"$vsDevCmdBat`" -arch=x64 -host_arch=x64",
    "cd /d `"$RepoRoot`"",
    "`"$cmakeExe`" --build `"$buildPath`" --config `"$Config`" --target `"$Target`""
)

& cmd.exe /d /s /c ($commands -join " && ")
exit $LASTEXITCODE
