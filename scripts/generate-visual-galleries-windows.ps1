[CmdletBinding()]
param(
    [string]$BuildDir = "build\vs2022-local-qt",
    [string]$Config = "Debug",
    [string]$QtRoot = "",
    [string]$OutputRoot = "build",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildPath = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $BuildDir))
$OutputPath = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $OutputRoot))
$BuildScript = Join-Path $PSScriptRoot "build-cardstack-vs2022.ps1"
$TestExe = Join-Path $BuildPath "$Config\CardStackTests.exe"
$DeployDir = Join-Path $BuildPath "$Config\CardStackDeploy"
$RuntimePath = [System.IO.Path]::GetFullPath((Join-Path $BuildPath "visual-gallery-runtime"))

function Get-RootLegacyWorkArtifacts {
    return @(Get-ChildItem -LiteralPath $RepoRoot -File | Where-Object {
        $_.Name -match '^(?i:noname)'
    })
}

function Reset-RuntimeDirectory {
    $expectedPrefix = $BuildPath.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    if (-not $RuntimePath.StartsWith($expectedPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Gallery runtime path escaped the configured build directory: $RuntimePath"
    }

    if (Test-Path -LiteralPath $RuntimePath) {
        Remove-Item -LiteralPath $RuntimePath -Recurse -Force
    }
    New-Item -ItemType Directory -Path $RuntimePath | Out-Null
}

function Resolve-QtRoot {
    if ($QtRoot) {
        return (Resolve-Path -LiteralPath $QtRoot).Path
    }

    $cachePath = Join-Path $BuildPath "CMakeCache.txt"
    if (-not (Test-Path -LiteralPath $cachePath)) {
        throw "CMake cache not found at $cachePath. Build first or pass -QtRoot."
    }

    $qt6DirLine = Get-Content -LiteralPath $cachePath | Where-Object { $_ -like "Qt6_DIR:PATH=*" } | Select-Object -First 1
    if (-not $qt6DirLine) {
        throw "Qt6_DIR was not found in $cachePath. Pass -QtRoot with the Qt installation directory."
    }

    $qt6Dir = $qt6DirLine.Substring("Qt6_DIR:PATH=".Length)
    $candidate = [System.IO.Path]::GetFullPath((Join-Path $qt6Dir "..\..\.."))
    if (-not (Test-Path -LiteralPath (Join-Path $candidate "bin\Qt6Test.dll"))) {
        throw "Could not find Qt6Test.dll below the Qt installation inferred from Qt6_DIR: $candidate"
    }
    return $candidate
}

function Reset-GalleryDirectory {
    param([Parameter(Mandatory)][string]$Name)

    $path = [System.IO.Path]::GetFullPath((Join-Path $OutputPath $Name))
    $expectedPrefix = $OutputPath.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    if (-not $path.StartsWith($expectedPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Gallery path escaped the configured output root: $path"
    }

    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Recurse -Force
    }
    New-Item -ItemType Directory -Path $path | Out-Null
    return $path
}

function Invoke-GalleryTest {
    param(
        [Parameter(Mandatory)][string]$EnvironmentVariable,
        [Parameter(Mandatory)][string]$OutputDirectory,
        [Parameter(Mandatory)][string]$Suite,
        [Parameter(Mandatory)][string]$Test
    )

    [System.Environment]::SetEnvironmentVariable($EnvironmentVariable, $OutputDirectory, "Process")
    & $TestExe --test $Suite $Test
    if ($LASTEXITCODE -ne 0) {
        throw "$Suite gallery generation failed with exit code $LASTEXITCODE."
    }
}

if (-not $SkipBuild) {
    & powershell.exe -ExecutionPolicy Bypass -File $BuildScript -BuildDir $BuildDir -Config $Config -Target CardStack
    if ($LASTEXITCODE -ne 0) {
        throw "CardStack build/deployment failed with exit code $LASTEXITCODE."
    }

    & powershell.exe -ExecutionPolicy Bypass -File $BuildScript -BuildDir $BuildDir -Config $Config -Target CardStackTests
    if ($LASTEXITCODE -ne 0) {
        throw "CardStackTests build failed with exit code $LASTEXITCODE."
    }
}

if (-not (Test-Path -LiteralPath $TestExe)) {
    throw "Gallery test executable not found at $TestExe. Run without -SkipBuild first."
}
if (-not (Test-Path -LiteralPath $DeployDir)) {
    throw "Deployed application directory not found at $DeployDir. Run without -SkipBuild first."
}

$existingRootArtifacts = Get-RootLegacyWorkArtifacts
if ($existingRootArtifacts.Count -ne 0) {
    $names = ($existingRootArtifacts.Name | Sort-Object) -join ", "
    throw "Refusing to generate galleries while legacy work artifacts exist in the repository root: $names"
}

$ResolvedQtRoot = Resolve-QtRoot
Reset-RuntimeDirectory
$galleryDirectories = @{
    App = Reset-GalleryDirectory "manual-app-gallery"
    Deck = Reset-GalleryDirectory "manual-deck-gallery"
    Designer = Reset-GalleryDirectory "manual-designer-gallery"
    Dialog = Reset-GalleryDirectory "manual-dialog-gallery"
    ReportPreview = Reset-GalleryDirectory "manual-report-preview-gallery"
}

$savedPath = $env:PATH
$savedPluginPath = $env:QT_PLUGIN_PATH
$savedPlatform = $env:QT_QPA_PLATFORM
$savedTemp = $env:TEMP
$savedTmp = $env:TMP
try {
    $env:PATH = "$DeployDir;$(Join-Path $ResolvedQtRoot 'bin');$env:PATH"
    $env:QT_PLUGIN_PATH = Join-Path $ResolvedQtRoot "plugins"
    $env:QT_QPA_PLATFORM = "windows"
    $env:TEMP = $RuntimePath
    $env:TMP = $RuntimePath

    Push-Location -LiteralPath $RuntimePath

    try {
        Invoke-GalleryTest "CARDSTACK_APP_GALLERY_DIR" $galleryDirectories.App "MainWindowActionTests" "writesManualAppInspectionImagesWhenConfigured"
        Invoke-GalleryTest "CARDSTACK_DECK_GALLERY_DIR" $galleryDirectories.Deck "DeckWorkspaceTests" "writesManualDeckInspectionImagesWhenConfigured"
        Invoke-GalleryTest "CARDSTACK_DESIGNER_GALLERY_DIR" $galleryDirectories.Designer "TemplateDesignerWidgetTests" "writesManualDesignerInspectionImagesWhenConfigured"
        Invoke-GalleryTest "CARDSTACK_DESIGNER_GALLERY_DIR" $galleryDirectories.Designer "ReportDesignerWidgetTests" "writesManualDesignerInspectionImagesWhenConfigured"
        Invoke-GalleryTest "CARDSTACK_DIALOG_GALLERY_DIR" $galleryDirectories.Dialog "UiBuilderTests" "writesManualDialogInspectionImagesWhenConfigured"
        Invoke-GalleryTest "CARDSTACK_REPORT_PREVIEW_IMAGE_DIR" $galleryDirectories.ReportPreview "ReportPreviewRendererTests" "writesManualInspectionPreviewImagesWhenConfigured"
    } finally {
        Pop-Location
    }
} finally {
    $env:PATH = $savedPath
    $env:QT_PLUGIN_PATH = $savedPluginPath
    $env:QT_QPA_PLATFORM = $savedPlatform
    $env:TEMP = $savedTemp
    $env:TMP = $savedTmp
    foreach ($name in @(
        "CARDSTACK_APP_GALLERY_DIR",
        "CARDSTACK_DECK_GALLERY_DIR",
        "CARDSTACK_DESIGNER_GALLERY_DIR",
        "CARDSTACK_DIALOG_GALLERY_DIR",
        "CARDSTACK_REPORT_PREVIEW_IMAGE_DIR")) {
        [System.Environment]::SetEnvironmentVariable($name, $null, "Process")
    }
}

$createdRootArtifacts = Get-RootLegacyWorkArtifacts
if ($createdRootArtifacts.Count -ne 0) {
    $names = ($createdRootArtifacts.Name | Sort-Object) -join ", "
    throw "Gallery generation created forbidden legacy work artifacts in the repository root: $names"
}

Write-Host "Visual galleries regenerated:"
foreach ($entry in $galleryDirectories.GetEnumerator() | Sort-Object Name) {
    $count = (Get-ChildItem -LiteralPath $entry.Value -File | Measure-Object).Count
    Write-Host ("  {0,-14} {1,3} images  {2}" -f $entry.Name, $count, $entry.Value)
}
