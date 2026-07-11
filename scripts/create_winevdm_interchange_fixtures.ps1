param(
    [string[]]$Fixture = @(
        "export-dbase-deck",
        "export-cardfile-deck",
        "export-wordperfect-deck",
        "export-takenote-deck"
    ),
    [string]$OutputDir = "",
    [string]$WineVdmDir = "",
    [switch]$NoDownload,
    [switch]$SkipValidation,
    [switch]$NoPromote
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$oracleScript = Join-Path $PSScriptRoot "winevdm_buttonfile_oracle.ps1"
$validateScript = Join-Path $PSScriptRoot "validate_winevdm_golden_fixtures.ps1"
$generatedFixtureDir = Join-Path $repoRoot "build\winevdm-oracle\fixtures"
$testFixtureDir = Join-Path $repoRoot "tests\fixtures\legacy\winevdm"

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = $generatedFixtureDir
}

$runtimeByFixture = @{
    "export-dbase-deck" = "runtime-dbf"
    "export-cardfile-deck" = "runtime-crd"
    "export-wordperfect-deck" = "runtime-wp"
    "export-takenote-deck" = "runtime-tn"
}

$expectedFiles = @{
    "export-dbase-deck" = @("EXDBF.DBF", "EXDBF.DBT")
    "export-cardfile-deck" = @("EXCRD.CRD")
    "export-wordperfect-deck" = @("EXWP.WP")
    "export-takenote-deck" = @("EXTN.TN")
}

function Write-Step {
    param([string]$Message)
    Write-Host ("[{0:HH:mm:ss}] {1}" -f (Get-Date), $Message)
}

function Stop-WineVdmProcesses {
    Get-Process -ErrorAction SilentlyContinue |
        Where-Object { $_.ProcessName -match 'otvdm|winevdm|butnfile|wowexec|ntvdm' } |
        Stop-Process -Force
}

function Reset-OracleRuntimeDir {
    param([string]$Name)

    if ($Name -notmatch '^runtime-[a-z0-9-]+$') {
        throw "Refusing unsafe runtime directory name '$Name'."
    }

    $root = Join-Path $repoRoot "build\winevdm-oracle\scratch"
    $path = Join-Path $root $Name
    $resolvedRoot = [System.IO.Path]::GetFullPath($root)
    $resolvedPath = [System.IO.Path]::GetFullPath($path)
    if (-not $resolvedPath.StartsWith($resolvedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing runtime directory outside oracle scratch root: $resolvedPath"
    }
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $path | Out-Null
    return $path
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
if (-not $NoPromote) {
    New-Item -ItemType Directory -Force -Path $testFixtureDir | Out-Null
}

foreach ($fixtureName in $Fixture) {
    if (-not $runtimeByFixture.ContainsKey($fixtureName)) {
        throw "Unknown interchange fixture '$fixtureName'."
    }

    Write-Step "Preparing clean WineVDM run for $fixtureName"
    Stop-WineVdmProcesses
    $runtimeDir = Reset-OracleRuntimeDir -Name $runtimeByFixture[$fixtureName]

    $args = @(
        "-ExecutionPolicy", "Bypass",
        "-File", $oracleScript,
        "-Fixture", $fixtureName,
        "-IncludeDisabled",
        "-RuntimeDir", $runtimeDir,
        "-OutputDir", $OutputDir,
        "-StageOnlyDeck", "SOFTWARE.BTN"
    )
    if (-not [string]::IsNullOrWhiteSpace($WineVdmDir)) {
        $args += @("-WineVdmDir", $WineVdmDir)
    }
    if ($NoDownload) {
        $args += "-NoDownload"
    }

    Write-Step "Running oracle fixture $fixtureName in $runtimeDir"
    & powershell @args
    if ($LASTEXITCODE -ne 0) {
        throw "Oracle fixture '$fixtureName' failed with exit code $LASTEXITCODE."
    }

    foreach ($file in $expectedFiles[$fixtureName]) {
        $path = Join-Path $OutputDir $file
        if (-not (Test-Path -LiteralPath $path)) {
            throw "Expected fixture was not generated: $path"
        }
        $item = Get-Item -LiteralPath $path
        if ($item.Length -le 0) {
            throw "Generated fixture is empty: $path"
        }
        Write-Step ("Generated {0} ({1} bytes)" -f $item.Name, $item.Length)

        if (-not $NoPromote) {
            Copy-Item -LiteralPath $item.FullName -Destination (Join-Path $testFixtureDir $item.Name) -Force
        }
    }

    Stop-WineVdmProcesses
}

if (-not $SkipValidation) {
    Write-Step "Validating promoted fixture presence"
    & powershell -ExecutionPolicy Bypass -File $validateScript -SkipTests
    if ($LASTEXITCODE -ne 0) {
        throw "Fixture validation failed with exit code $LASTEXITCODE."
    }
}

Write-Step "Interchange fixture generation complete."
