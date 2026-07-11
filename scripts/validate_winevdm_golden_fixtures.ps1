param(
    [string]$FixtureDir = "",
    [string]$BuildDir = "",
    [string]$Configuration = "Debug",
    [switch]$SkipTests,
    [switch]$RequirePending
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($FixtureDir)) {
    $FixtureDir = Join-Path $repoRoot "tests\fixtures\legacy\winevdm"
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $vsBuild = Join-Path $repoRoot "build\vs2022"
    $defaultBuild = Join-Path $repoRoot "build\default"
    if (Test-Path -LiteralPath (Join-Path $vsBuild "CMakeCache.txt")) {
        $BuildDir = $vsBuild
    } elseif (Test-Path -LiteralPath (Join-Path $defaultBuild "CMakeCache.txt")) {
        $BuildDir = $defaultBuild
    } else {
        $BuildDir = Join-Path $repoRoot "build"
    }
}

$FixtureDir = [System.IO.Path]::GetFullPath($FixtureDir)
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

function Resolve-CtestPath {
    $command = Get-Command ctest -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidates = @()
    if ($env:ProgramFiles) {
        $candidates += (Join-Path $env:ProgramFiles "CMake\bin\ctest.exe")
    }
    $programFilesX86 = [Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
    if ($programFilesX86) {
        $candidates += (Join-Path $programFilesX86 "CMake\bin\ctest.exe")
    }

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw "ctest was not found on PATH or in the standard CMake install directories."
}

$matrix = @(
    [pscustomobject]@{
        Id = "plain"
        Files = @("plain.BTN")
        Required = $true
        Purpose = "Unprotected baseline deck"
        Validation = "LegacyDeckReaderTests golden fixture import"
    },
    [pscustomobject]@{
        Id = "password-only"
        Files = @("pwonly.BTN")
        Required = $true
        Purpose = "Owner password, no data encryption"
        Validation = "LegacyDeckReaderTests password-required/import checks"
    },
    [pscustomobject]@{
        Id = "password-encrypted"
        Files = @("crypt.BTN")
        Required = $true
        Purpose = "Owner password plus legacy data encryption"
        Validation = "LegacyDeckReaderTests password-required/encrypted import checks"
    },
    [pscustomobject]@{
        Id = "reports-sidecar"
        Files = @("reports.BTN", "reports.RPT")
        Required = $true
        Purpose = "Deck plus same-basename report sidecar"
        Validation = "LegacyReportReaderTests report metadata/frame checks"
    },
    [pscustomobject]@{
        Id = "notes-heavy"
        Files = @("notes_heavy.BTN")
        Required = $true
        Purpose = "Long and multiline notes records"
        Validation = "LegacyDeckReaderTests notes text comparison"
    },
    [pscustomobject]@{
        Id = "many-fields"
        Files = @("many_fields.BTN")
        Required = $true
        Purpose = "Near/max field count and varied field widths"
        Validation = "LegacyDeckReaderTests 14-field schema/value comparison"
    },
    [pscustomobject]@{
        Id = "max-field-lengths"
        Files = @("max_lengths.BTN")
        Required = $true
        Purpose = "Boundary text lengths, truncation, empty fields"
        Validation = "LegacyDeckReaderTests max-length field comparison"
    },
    [pscustomobject]@{
        Id = "security-cycle"
        Files = @("security_cycle.BTN")
        Required = $true
        Purpose = "Add password, remove password, reopen without password"
        Validation = "LegacyDeckReaderTests no-password import success check"
    },
    [pscustomobject]@{
        Id = "dbase-export"
        Files = @("EXDBF.DBF", "EXDBF.DBT")
        Required = $true
        Purpose = "ButtonFile Save As dBaseIII+/PC-File export, including memo sidecar"
        Validation = "LegacyInterchangeReaderTests DBF field/card/memo-sidecar checks"
    },
    [pscustomobject]@{
        Id = "cardfile-export"
        Files = @("EXCRD.CRD")
        Required = $true
        Purpose = "ButtonFile Save As Microsoft Cardfile export"
        Validation = "LegacyInterchangeReaderTests CRD title/text checks"
    },
    [pscustomobject]@{
        Id = "wordperfect-export"
        Files = @("EXWP.WP")
        Required = $true
        Purpose = "ButtonFile Save As WordPerfect 5.1 merge export"
        Validation = "LegacyInterchangeReaderTests WP merge field/card checks"
    },
    [pscustomobject]@{
        Id = "takenote-saveas"
        Files = @("EXTN.TN")
        Required = $true
        Purpose = "ButtonFile Save As TakeNote-compatible file; produced without Export dialog"
        Validation = "LegacyInterchangeReaderTests Btrieve-backed TN import checks"
    },
    [pscustomobject]@{
        Id = "password-takenote"
        Files = @("PWTN.TN")
        Required = $true
        Purpose = "Owner-password protected TakeNote-compatible Save As file"
        Validation = "LegacyInterchangeReaderTests password-required/rejected/verified checks"
    }
)

Write-Host "WineVDM golden fixture directory: $FixtureDir"
Write-Host "CMake build directory: $BuildDir"
Write-Host ""

$rows = foreach ($entry in $matrix) {
    $missing = @()
    $sizes = @()
    foreach ($file in $entry.Files) {
        $path = Join-Path $FixtureDir $file
        if (Test-Path -LiteralPath $path) {
            $item = Get-Item -LiteralPath $path
            $sizes += ("{0}:{1}" -f $file, $item.Length)
        } else {
            $missing += $file
        }
    }

    $required = [bool]$entry.Required
    $done = $missing.Count -eq 0
    [pscustomobject]@{
        Id = $entry.Id
        Status = if ($done) { "present" } elseif ($required -or $RequirePending) { "missing" } else { "pending" }
        Files = ($entry.Files -join ", ")
        Sizes = ($sizes -join ", ")
        Missing = ($missing -join ", ")
        Required = $required
        Validation = $entry.Validation
    }
}

$rows | Format-Table -AutoSize

$failures = @($rows | Where-Object { $_.Status -eq "missing" })
if ($failures.Count -gt 0) {
    throw "Missing required WineVDM golden fixture(s): $($failures.Id -join ', ')"
}

if (-not $SkipTests) {
    if (-not (Test-Path -LiteralPath (Join-Path $BuildDir "CMakeCache.txt"))) {
        throw "Build directory does not contain CMakeCache.txt: $BuildDir"
    }

    $ctest = Resolve-CtestPath

    $env:CARDSTACK_WINEVDM_GOLDEN_DIR = $FixtureDir
    $env:CARDSTACK_LEGACY_DECK_SAMPLE = Join-Path $FixtureDir "plain.BTN"
    $env:CARDSTACK_LEGACY_REPORT_SAMPLE = Join-Path $FixtureDir "reports.RPT"

    Write-Host ""
    Write-Host "CTest executable: $ctest"
    Write-Host "Running legacy validation tests..."
    & $ctest --test-dir $BuildDir -C $Configuration --output-on-failure -R "LegacyDeckReaderTests|LegacyInterchangeReaderTests|LegacyReportReaderTests|BtrieveAuditReaderTests"
    if ($LASTEXITCODE -ne 0) {
        throw "Legacy validation tests failed with exit code $LASTEXITCODE."
    }
}

Write-Host ""
Write-Host "WineVDM golden fixture validation complete."
