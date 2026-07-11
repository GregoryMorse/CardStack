param(
    [string]$FixtureDir = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($FixtureDir)) {
    $FixtureDir = Join-Path $repoRoot "tests\fixtures\legacy\winevdm"
}
$FixtureDir = [System.IO.Path]::GetFullPath($FixtureDir)

function Set-U16 {
    param([byte[]]$Bytes, [int]$Offset, [int]$Value)
    $Bytes[$Offset] = [byte]($Value -band 0xff)
    $Bytes[$Offset + 1] = [byte](($Value -shr 8) -band 0xff)
}

function Set-WordSwappedU32 {
    param([byte[]]$Bytes, [int]$Offset, [uint32]$Value)
    Set-U16 $Bytes $Offset (($Value -shr 16) -band 0xffff)
    Set-U16 $Bytes ($Offset + 2) ($Value -band 0xffff)
}

function Set-AsciiField {
    param([byte[]]$Bytes, [int]$Offset, [int]$Length, [string]$Text)
    $data = [Text.Encoding]::ASCII.GetBytes($Text)
    for ($index = 0; $index -lt $Length; $index++) {
        $Bytes[$Offset + $index] = if ($index -lt $data.Length) { $data[$index] } else { 0 }
    }
}

function Set-FieldDefinition {
    param(
        [byte[]]$Bytes,
        [int]$Offset,
        [string]$Name,
        [int]$Type,
        [int]$RecordOffset,
        [int]$Length
    )

    Set-AsciiField $Bytes $Offset 16 $Name
    $Bytes[$Offset + 16] = [byte]$Type
    $Bytes[$Offset + 17] = 0
    $Bytes[$Offset + 18] = 0
    $Bytes[$Offset + 19] = 0
    Set-U16 $Bytes ($Offset + 20) $RecordOffset
    Set-U16 $Bytes ($Offset + 22) $Length
}

function Write-PatchedSoftwareFixture {
    param(
        [string]$SourcePath,
        [string]$DestinationPath,
        [scriptblock]$Patch
    )

    $bytes = [IO.File]::ReadAllBytes($SourcePath)
    & $Patch $bytes
    [IO.File]::WriteAllBytes($DestinationPath, $bytes)
}

function Write-ManyFieldsFixture {
    param([string]$DestinationPath)

    $pageSize = 4096
    $fixedRecordLength = 635
    $physicalRecordLength = 640
    $bytes = New-Object byte[] ($pageSize * 2)

    Set-U16 $bytes 4 1
    Set-U16 $bytes 6 0x0500
    Set-U16 $bytes 8 $pageSize
    Set-U16 $bytes 0x14 1
    Set-U16 $bytes 0x16 $fixedRecordLength
    Set-U16 $bytes 0x18 $physicalRecordLength
    Set-WordSwappedU32 $bytes 0x1a 3
    Set-U16 $bytes 0x22 0xffff
    $bytes[0x38] = 0xfd
    Set-AsciiField $bytes 0x3c 8 "CASEINSX"
    Set-U16 $bytes 0x106 0x1234

    $keyOffset = 0x110
    Set-U16 $bytes $keyOffset 0
    Set-U16 $bytes ($keyOffset + 2) 1
    Set-WordSwappedU32 $bytes ($keyOffset + 4) 3
    Set-U16 $bytes ($keyOffset + 8) 0
    Set-U16 $bytes ($keyOffset + 10) 5
    Set-U16 $bytes ($keyOffset + 12) 13
    Set-U16 $bytes ($keyOffset + 14) 100
    Set-U16 $bytes ($keyOffset + 16) 50
    Set-U16 $bytes ($keyOffset + 18) 0
    Set-U16 $bytes ($keyOffset + 20) 0
    Set-U16 $bytes ($keyOffset + 22) 5

    $dataPage = $pageSize
    Set-U16 $bytes $dataPage 0
    Set-U16 $bytes ($dataPage + 2) 1
    Set-U16 $bytes ($dataPage + 4) 0x8003

    $schema = $dataPage + 6
    Set-AsciiField $bytes ($schema + 5) 5 "TN20"
    Set-AsciiField $bytes ($schema + 11) 64 "Many Fields Library"
    $bytes[$schema + 87] = 14

    $names = @("Product", "Version", "Company", "Serial", "Owner", "Purchased", "Source", "Support", "Plan", "Service", "Notes", "ExtraA", "ExtraB", "ExtraC")
    $types = @(1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 1, 1, 1)
    $lengths = @(30, 10, 30, 12, 20, 12, 20, 12, 12, 12, 40, 3, 3, 3)

    $fieldBase = $schema + 96
    $fieldOffset = 0
    for ($index = 0; $index -lt $names.Count; $index++) {
        Set-FieldDefinition $bytes ($fieldBase + $index * 37) $names[$index] $types[$index] $fieldOffset $lengths[$index]
        $fieldOffset += $lengths[$index]
    }

    $recordOffsets = @(($dataPage + 6 + $physicalRecordLength), ($dataPage + 6 + 2 * $physicalRecordLength))
    for ($recordIndex = 0; $recordIndex -lt $recordOffsets.Count; $recordIndex++) {
        $number = $recordIndex + 1
        $base = $recordOffsets[$recordIndex] + 5
        $values = @(
            ("Product{0}" -f $number),
            ("1.{0}" -f $number),
            ("Company {0}" -f $number),
            ("SER{0}" -f $number),
            ("Owner {0}" -f $number),
            "2026-07",
            ("Source {0}" -f $number),
            ("Help {0}" -f $number),
            ("Plan {0}" -f $number),
            ("Svc {0}" -f $number),
            ("Many-field note {0}" -f $number),
            ("A{0}" -f $number),
            ("B{0}" -f $number),
            ("C{0}" -f $number)
        )

        $valueOffset = 0
        for ($index = 0; $index -lt $names.Count; $index++) {
            Set-AsciiField $bytes ($base + $valueOffset) $lengths[$index] $values[$index]
            $valueOffset += $lengths[$index]
        }
    }

    [IO.File]::WriteAllBytes($DestinationPath, $bytes)
}

$plainPath = Join-Path $FixtureDir "plain.BTN"
if (-not (Test-Path -LiteralPath $plainPath)) {
    throw "plain.BTN is required before edge fixtures can be generated: $plainPath"
}

$cardSlots = @(8533, 8868, 9203)
$payloadOffset = 5

Write-PatchedSoftwareFixture $plainPath (Join-Path $FixtureDir "notes_heavy.BTN") {
    param([byte[]]$Bytes)
    Set-AsciiField $Bytes 8209 64 "Notes Heavy Library"
    Set-AsciiField $Bytes ($cardSlots[0] + $payloadOffset + 275) 40 "Line1`r`nLine2`r`nLegacy note edge"
    Set-AsciiField $Bytes ($cardSlots[1] + $payloadOffset + 275) 40 "Tabbed`tand CRLF`r`nnotes sample"
    Set-AsciiField $Bytes ($cardSlots[2] + $payloadOffset + 275) 40 "012345678901234567890123456789012345678"
}

Write-PatchedSoftwareFixture $plainPath (Join-Path $FixtureDir "max_lengths.BTN") {
    param([byte[]]$Bytes)
    Set-AsciiField $Bytes 8209 64 "Max Length Library"
    $fields = @(
        @(0, 30, "PRODUCT-MAX-123456789012345"),
        @(30, 10, "VER-12345"),
        @(40, 50, "COMPANY-MAX-123456789012345678901234567890123"),
        @(90, 25, "SERIAL-1234567890123456"),
        @(115, 50, "REGISTERED-MAX-1234567890123456789012345678901"),
        @(165, 20, "PURCHASED-2026-07"),
        @(185, 30, "FROM-MAX-123456789012345678"),
        @(215, 20, "TECH-SUPPORT-12345"),
        @(235, 20, "SUPPORT-PLAN-12345"),
        @(255, 20, "CUSTOMER-SERVICE-1"),
        @(275, 40, "NOTES-MAX-12345678901234567890123456789")
    )

    foreach ($slot in $cardSlots) {
        foreach ($field in $fields) {
            Set-AsciiField $Bytes ($slot + $payloadOffset + [int]$field[0]) ([int]$field[1]) ([string]$field[2])
        }
    }
}

Write-ManyFieldsFixture (Join-Path $FixtureDir "many_fields.BTN")
Copy-Item -LiteralPath $plainPath -Destination (Join-Path $FixtureDir "security_cycle.BTN") -Force

Get-Item -LiteralPath `
    (Join-Path $FixtureDir "notes_heavy.BTN"), `
    (Join-Path $FixtureDir "many_fields.BTN"), `
    (Join-Path $FixtureDir "max_lengths.BTN"), `
    (Join-Path $FixtureDir "security_cycle.BTN") |
    Select-Object Name, Length |
    Format-Table -AutoSize
