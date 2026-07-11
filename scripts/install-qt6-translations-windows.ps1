[CmdletBinding()]
param(
    [string]$QtSourceDir = "C:\dev\qt6",
    [string]$QtBuildDir = "C:\dev\qt6-build",
    [string]$QtInstallDir = "C:\dev\qt6-install",
    [string]$CMake = "",
    [string]$VsDevCmd = "",
    [switch]$BuildQtTools
)

$ErrorActionPreference = "Stop"

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

    throw "cmake was not found. Pass -CMake with the full path to cmake.exe."
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

function Invoke-QtBuildTarget {
    param(
        [string]$Target
    )

    $command = @(
        "call `"$vsDevCmdBat`" -arch=x64 -host_arch=x64",
        "`"$cmakeExe`" --build `"$QtBuildDir`" --target `"$Target`""
    ) -join " && "

    & cmd.exe /d /s /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "Building Qt target '$Target' failed. If the error mentions stale precompiled headers, use a clean Qt build tree or remove the stale Qt build artifacts before retrying."
    }
}

if (-not (Test-Path -LiteralPath $QtSourceDir)) {
    throw "Qt source directory not found: $QtSourceDir"
}

if (-not (Test-Path -LiteralPath $QtBuildDir)) {
    throw "Qt build directory not found: $QtBuildDir"
}

$cmakeExe = Find-CMake
$vsDevCmdBat = Find-VsDevCmd

& git -C $QtSourceDir submodule update --init qttranslations
if ($BuildQtTools) {
    & git -C $QtSourceDir submodule update --init --recursive qttools
}

if ($LASTEXITCODE -ne 0) {
    throw "Qt submodule update failed."
}

if ($BuildQtTools) {
    Write-Host "qttools submodules are initialized. The qttranslations/install target will build required Linguist tools such as lrelease as dependencies when the Qt tree is configured with qttools."
}

Invoke-QtBuildTarget -Target "qttranslations/install"

if (-not (Test-Path -LiteralPath (Join-Path $QtInstallDir "translations\catalogs.json"))) {
    Write-Warning "qttranslations installed, but catalogs.json was not found under $QtInstallDir\translations. Check the Qt install prefix used by the build tree."
}
