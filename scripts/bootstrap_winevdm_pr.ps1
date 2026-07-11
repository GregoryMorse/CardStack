param(
    [string]$WineVdmRoot = "",
    [string]$RepositoryUrl = "https://github.com/GregoryMorse/winevdm.git",
    [string]$Branch = "fix-filedlg-fileok-ofn16-sync",
    [string]$Configuration = "Release",
    [string]$Platform = "Win32",
    [switch]$NoClone,
    [switch]$RunSmokeFixture
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($WineVdmRoot)) {
    $WineVdmRoot = Join-Path $repoRoot ".tools\winevdm\pr\winevdm"
}
$WineVdmRoot = [System.IO.Path]::GetFullPath($WineVdmRoot)

function Write-Step {
    param([string]$Message)
    Write-Host ("[{0:HH:mm:ss}] {1}" -f (Get-Date), $Message)
}

function Find-MSBuild {
    $candidates = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }
    $command = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    throw "MSBuild.exe was not found. Install Visual Studio Build Tools or pass through a developer shell with MSBuild on PATH."
}

function Find-WindowsSdk {
    $includeRoot = "C:\Program Files (x86)\Windows Kits\10\Include"
    if (-not (Test-Path -LiteralPath $includeRoot)) {
        throw "Windows 10/11 SDK include directory was not found: $includeRoot"
    }
    $sdk = Get-ChildItem -Directory -LiteralPath $includeRoot |
        Sort-Object Name -Descending |
        Select-Object -First 1
    if (-not $sdk) {
        throw "No Windows SDK versions were found under $includeRoot"
    }
    return $sdk.Name
}

function Find-BinutilsDir {
    $candidates = @(
        "C:\msys64\mingw64\bin",
        "C:\msys64\ucrt64\bin",
        "C:\msys64\mingw32\bin",
        "C:\msys64\usr\bin"
    )
    foreach ($candidate in $candidates) {
        if ((Test-Path -LiteralPath (Join-Path $candidate "as.exe")) -and
            (Test-Path -LiteralPath (Join-Path $candidate "objcopy.exe"))) {
            return ([System.IO.Path]::GetFullPath($candidate).TrimEnd("\") + "\")
        }
    }
    throw "GNU binutils were not found. Install MSYS2 with binutils, or make sure as.exe and objcopy.exe exist under C:\msys64\mingw64\bin or another supported MSYS2 bin directory."
}

function Ensure-WineVdmSource {
    if (Test-Path -LiteralPath (Join-Path $WineVdmRoot "otvdm.sln")) {
        return
    }
    if ($NoClone) {
        throw "WineVDM PR source was not found at '$WineVdmRoot' and -NoClone was supplied."
    }

    $parent = Split-Path -Parent $WineVdmRoot
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    Write-Step "Cloning fixed WineVDM PR branch into $WineVdmRoot"
    & git clone --branch $Branch --single-branch $RepositoryUrl $WineVdmRoot
    if ($LASTEXITCODE -ne 0) {
        throw "git clone failed with exit code $LASTEXITCODE."
    }
}

function Write-PropertySheet {
    param(
        [string]$SdkVersion,
        [string]$BinutilsDir
    )

    $propertyPath = Join-Path $WineVdmRoot "PropertySheet.props"
    $content = @'
<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ImportGroup Label="PropertySheets" />
  <PropertyGroup Label="UserMacros">
    <AsmPath>{ASM_PATH}</AsmPath>
    <NtDllLibPath>$(UniversalCRTSdkDir)Lib\{SDK_VERSION}\um\x86\</NtDllLibPath>
  </PropertyGroup>
  <PropertyGroup>
    <_PropertySheetDisplayName>MacroPropertySheet</_PropertySheetDisplayName>
    <IncludePath>$(SolutionDir)wow32;$(IncludePath)</IncludePath>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary>
      <PreprocessorDefinitions>__CI_VERSION=local;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <WarningLevel>TurnOffAllWarnings</WarningLevel>
    </ClCompile>
  </ItemDefinitionGroup>
  <ItemGroup>
    <BuildMacro Include="AsmPath"><Value>$(AsmPath)</Value></BuildMacro>
    <BuildMacro Include="NtDllLibPath"><Value>$(NtDllLibPath)</Value></BuildMacro>
  </ItemGroup>
</Project>
'@
    $content = $content.Replace("{ASM_PATH}", $BinutilsDir).Replace("{SDK_VERSION}", $SdkVersion)
    Set-Content -LiteralPath $propertyPath -Value $content -Encoding UTF8
}

function Invoke-MSBuildProject {
    param(
        [string]$MSBuild,
        [string]$Project,
        [string]$SdkVersion,
        [string]$OutDir
    )

    Write-Step "Building $Project"
    & $MSBuild (Join-Path $WineVdmRoot $Project) `
        /m `
        /p:Configuration=$Configuration `
        /p:Platform=$Platform `
        /p:WindowsTargetPlatformVersion=$SdkVersion `
        /p:PlatformToolset=v143 `
        /p:BuildProjectReferences=false `
        /p:SolutionDir="$WineVdmRoot\" `
        /p:OutDir=$OutDir
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed for $Project with exit code $LASTEXITCODE."
    }
}

Ensure-WineVdmSource
$msbuild = Find-MSBuild
$sdkVersion = Find-WindowsSdk
$binutilsDir = Find-BinutilsDir
$outDir = Join-Path $WineVdmRoot "$Configuration"
$outDir = [System.IO.Path]::GetFullPath($outDir).TrimEnd("\") + "\"

Write-Step "WineVDM source: $WineVdmRoot"
Write-Step "MSBuild: $msbuild"
Write-Step "Windows SDK: $sdkVersion"
Write-Step "GNU binutils: $binutilsDir"
Write-PropertySheet -SdkVersion $sdkVersion -BinutilsDir $binutilsDir

$runtimeProjects = @(
    "wine\wine.vcxproj",
    "convspec\convspec.vcxproj",
    "winecrt0\winecrt0.vcxproj",
    "krnl386\krnl386.vcxproj",
    "user\user.vcxproj",
    "gdi\gdi.vcxproj",
    "win87em\win87em.vcxproj",
    "shell\shell.vcxproj",
    "vm86\vm86.vcxproj",
    "avifile\avifile.vcxproj",
    "comm\comm.vcxproj",
    "commctrl\commctrl.vcxproj",
    "commdlg\commdlg.vcxproj",
    "ctl3d\ctl3d.vcxproj",
    "ctl3dv2\ctl3dv2.vcxproj",
    "ddeml\ddeml.vcxproj",
    "dispdib\dispdib.vcxproj",
    "display\display.vcxproj",
    "haxmvm\haxmvm.vcxproj",
    "keyboard\keyboard.vcxproj",
    "lzexpand\lzexpand.vcxproj",
    "mmsystem\mmsystem.vcxproj",
    "mouse\mouse.vcxproj",
    "msacm\msacm.vcxproj",
    "msvideo\msvideo.vcxproj",
    "nddeapi\nddeapi.vcxproj",
    "netapi\netapi.vcxproj",
    "olecli\olecli.vcxproj",
    "olesvr\olesvr.vcxproj",
    "regedit\regedit.vcxproj",
    "rmpatch\rmpatch.vcxproj",
    "sound\sound.vcxproj",
    "system\system.vcxproj",
    "timer\timer.vcxproj",
    "toolhelp\toolhelp.vcxproj",
    "ver\ver.vcxproj",
    "wifeman\wifeman.vcxproj",
    "wing\wing.vcxproj",
    "winnls\winnls.vcxproj",
    "winoldap\winoldap.vcxproj",
    "winsock\winsock.vcxproj",
    "winspool\winspool.vcxproj",
    "gvm\gvm.vcxproj",
    "ntvdm\ntvdm.vcxproj",
    "otvdm\otvdm.vcxproj",
    "otvdm\otvdmw.vcxproj"
)

foreach ($project in $runtimeProjects) {
    Invoke-MSBuildProject -MSBuild $msbuild -Project $project -SdkVersion $sdkVersion -OutDir $outDir
}

$otvdmw = Join-Path $outDir "otvdmw.exe"
if (-not (Test-Path -LiteralPath $otvdmw)) {
    throw "Build completed without producing $otvdmw"
}
Write-Step "Built fixed WineVDM: $otvdmw"

if ($RunSmokeFixture) {
    $oracle = Join-Path $PSScriptRoot "winevdm_buttonfile_oracle.ps1"
    $outputDir = Join-Path $repoRoot "build\winevdm-oracle\fixtures"
    $runtimeDir = Join-Path $outputDir "runtime"
    Write-Step "Running WineVDM oracle smoke fixture"
    & powershell -ExecutionPolicy Bypass -File $oracle `
        -WineVdmDir (Split-Path -Parent $WineVdmRoot) `
        -RuntimeDir $runtimeDir `
        -OutputDir $outputDir `
        -Fixture smoke-launch
    if ($LASTEXITCODE -ne 0) {
        throw "WineVDM smoke fixture failed with exit code $LASTEXITCODE."
    }
}
