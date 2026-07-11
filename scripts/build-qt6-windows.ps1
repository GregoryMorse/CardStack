[CmdletBinding()]
param(
    [string]$QtVersion = "6.11.1",
    [string]$QtSourceDir = "",
    [string]$QtBuildDir = "",
    [string]$QtInstallPrefix = "",
    [switch]$Clone,
    [switch]$ConfigureCardStack,
    [string]$CardStackPreset = "vs2022"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

function Get-FullPath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path)
}

function Find-Tool([string]$Name, [string[]]$Fallbacks) {
    foreach ($candidate in $Fallbacks) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "Unable to find $Name. Install it or add it to PATH."
}

function Invoke-Checked([scriptblock]$Command, [string]$Description) {
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

if ([string]::IsNullOrWhiteSpace($QtSourceDir)) {
    $QtSourceDir = Join-Path $repoRoot ".deps\qt-src"
}
if ([string]::IsNullOrWhiteSpace($QtBuildDir)) {
    $QtBuildDir = Join-Path $repoRoot ".deps\qt-build"
}
if ([string]::IsNullOrWhiteSpace($QtInstallPrefix)) {
    $QtInstallPrefix = Join-Path $repoRoot ".deps\qt-install"
}

$QtSourceDir = Get-FullPath $QtSourceDir
$QtBuildDir = Get-FullPath $QtBuildDir
$QtInstallPrefix = Get-FullPath $QtInstallPrefix
$qtRef = $QtVersion

$cmake = Find-Tool "cmake" @(
    "C:\Program Files\CMake\bin\cmake.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)

$ninja = Find-Tool "ninja" @(
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
)

$vsDevCmdCandidates = @(
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
)
$vsDevCmd = $vsDevCmdCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $vsDevCmd) {
    throw "Unable to find VsDevCmd.bat for Visual Studio 2022."
}

if (-not (Test-Path -LiteralPath $QtSourceDir)) {
    if (-not $Clone) {
        throw "Qt source not found at $QtSourceDir. Re-run with -Clone or pass -QtSourceDir."
    }

    New-Item -ItemType Directory -Path (Split-Path -Parent $QtSourceDir) -Force | Out-Null
    Invoke-Checked { git clone https://code.qt.io/qt/qt5.git $QtSourceDir } "Cloning Qt"
} elseif (Test-Path -LiteralPath (Join-Path $QtSourceDir ".git")) {
    Write-Host "Using existing Qt checkout at $QtSourceDir"
}

Invoke-Checked { git -C $QtSourceDir fetch --tags origin } "Fetching Qt tags"
Invoke-Checked { git -C $QtSourceDir checkout --force $qtRef } "Checking out Qt $qtRef"

if (-not (Test-Path -LiteralPath (Join-Path $QtSourceDir "configure.bat"))) {
    throw "configure.bat was not found under $QtSourceDir. Pass a Qt source checkout, not a build directory."
}

New-Item -ItemType Directory -Path $QtBuildDir -Force | Out-Null
New-Item -ItemType Directory -Path $QtInstallPrefix -Force | Out-Null

$toolPath = [System.IO.Path]::GetDirectoryName($cmake) + ";" + [System.IO.Path]::GetDirectoryName($ninja)
$configure = Join-Path $QtSourceDir "configure.bat"

$qtCommand = @(
    ('call "{0}" -arch=x64 -host_arch=x64' -f $vsDevCmd),
    ('set "PATH={0};!PATH!"' -f $toolPath),
    ('cd /d "{0}"' -f $QtBuildDir),
    ('"{0}" -prefix "{1}" -release -opensource -confirm-license -nomake examples -nomake tests -submodules qtbase -init-submodules' -f $configure, $QtInstallPrefix),
    ('"{0}" --build . --parallel' -f $cmake),
    ('"{0}" --install .' -f $cmake)
) -join " && "

Write-Host "Building Qt $qtRef"
Write-Host "  Source:  $QtSourceDir"
Write-Host "  Build:   $QtBuildDir"
Write-Host "  Install: $QtInstallPrefix"
& cmd.exe /d /v:on /s /c $qtCommand

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if ($ConfigureCardStack) {
    $cardStackCommand = @(
        ('call "{0}" -arch=x64 -host_arch=x64' -f $vsDevCmd),
        ('set "PATH={0};!PATH!"' -f $toolPath),
        ('cd /d "{0}"' -f $repoRoot),
        ('"{0}" --preset {1} -DCMAKE_PREFIX_PATH="{2}"' -f $cmake, $CardStackPreset, $QtInstallPrefix),
        ('"{0}" --build --preset {1}' -f $cmake, $CardStackPreset)
    ) -join " && "

    Write-Host "Configuring and building CardStack with Qt prefix $QtInstallPrefix"
    & cmd.exe /d /v:on /s /c $cardStackCommand
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
