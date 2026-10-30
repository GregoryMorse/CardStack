param(
    [string]$FixtureDir='', [string]$BuildDir='', [string]$Configuration='Release',
    [string]$TestExecutable='', [string]$QtBin='', [string]$OutputDir='',
    [switch]$SkipTests, [switch]$RequirePending
)
$ErrorActionPreference='Stop'
if($SkipTests -and $RequirePending){throw 'Completeness cannot be certified while semantic tests are disabled.'}
$root=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
if (!$FixtureDir) { $FixtureDir=Join-Path $root 'tests\fixtures\legacy\winevdm' }
$FixtureDir=[IO.Path]::GetFullPath($FixtureDir)
$manifestPath=Join-Path $FixtureDir 'corpus-manifest.json'
if (!(Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'A corpus-manifest.json with provenance and semantic cases is required.' }
$manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.version -ne 1 -or !@($manifest.files).Count -or !@($manifest.cases).Count) { throw 'Unsupported or empty corpus contract.' }
$declared=@{}
foreach ($file in $manifest.files) {
    if($declared.ContainsKey([string]$file.name)){throw "Duplicate fixture: $($file.name)"}
    $declared[[string]$file.name]=$true
    if (!$file.name -or [IO.Path]::GetFileName($file.name) -ne $file.name -or $file.name -eq '..') { throw 'Corpus filenames must be relative leaf names.' }
    $path=Join-Path $FixtureDir $file.name
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing fixture: $($file.name)" }
    if ((Get-Item -LiteralPath $path).Length -ne $file.size -or $file.size -le 0) { throw "Wrong fixture size: $($file.name)" }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256) { throw "Wrong fixture hash: $($file.name)" }
    if (!$file.origin) { throw "Missing provenance category: $($file.name)" }
}
foreach($file in Get-ChildItem -LiteralPath $FixtureDir -File){
    if($file.Extension -match '^\.(BTN|RPT|TN|DBF|DBT|CRD|WP|CSV|BTR|DAT)$' -and !$declared.ContainsKey($file.Name)){throw "Unlisted binary artifact: $($file.Name)"}
}
foreach($case in $manifest.cases){
    if(!$declared.ContainsKey([string]$case.file)){throw "Case references an undeclared artifact: $($case.id)"}
    if($case.reportFile -and !$declared.ContainsKey([string]$case.reportFile)){throw "Case references an undeclared sidecar: $($case.id)"}
}
$pending=@($manifest.coverage | Where-Object { $_.status -ne 'verified' })
$manifest.files | Group-Object origin | Select-Object Name,Count | Format-Table -AutoSize
if (!$OutputDir) { $OutputDir=Join-Path $root ('build\oracle-validation\'+[Guid]::NewGuid().ToString('N')) }
$OutputDir=[IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$results=New-Object 'System.Collections.Generic.List[object]'
if (!$SkipTests) {
    if (!$BuildDir) {
        foreach ($candidate in @('build\vs2026-local-qt','build\vs2026','build\vs2022','build\default','build')) {
            if (Test-Path -LiteralPath (Join-Path (Join-Path $root $candidate) 'CMakeCache.txt')) { $BuildDir=Join-Path $root $candidate;break }
        }
    }
    if (!$BuildDir) { throw 'Pass BuildDir or TestExecutable for the unified test binary.' }
    $BuildDir=[IO.Path]::GetFullPath($BuildDir)
    if (!$TestExecutable) {
        foreach ($candidate in @((Join-Path $BuildDir ($Configuration+'\CardStackTests.exe')),(Join-Path $BuildDir 'CardStackTests.exe'),(Join-Path $BuildDir 'tests\CardStackTests'))) {
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { $TestExecutable=$candidate;break }
        }
    }
    if (!$TestExecutable) { throw 'CardStackTests was not found; build the requested configuration first.' }
    if (!$QtBin) {
        $cache=Join-Path $BuildDir 'CMakeCache.txt'
        if (Test-Path -LiteralPath $cache) {
            $qt=Get-Content -LiteralPath $cache | Where-Object { $_ -match '^Qt6_DIR:[^=]+=' } | Select-Object -First 1
            if ($qt) { $prefix=($qt -split '=',2)[1];for($i=0;$i -lt 3;$i++){$prefix=Split-Path -Parent $prefix};$QtBin=Join-Path $prefix 'bin' }
        }
    }
    $names=@('PATH','QT_PLUGIN_PATH','QT_QPA_PLATFORM','CARDSTACK_WINEVDM_GOLDEN_DIR','CARDSTACK_LEGACY_DECK_SAMPLE','CARDSTACK_LEGACY_REPORT_SAMPLE')
    $saved=@{};foreach($name in $names){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
    $required=@{
        LegacyDeckReaderTests=@('importsWineVdmGoldenFixturesWhenConfigured','validatesFixtureCorpusManifestWhenConfigured','decodesWin16TextAndPreservesCardWhitespace')
        LegacyInterchangeReaderTests=@('importsDBaseGoldenFixture','importsMicrosoftCardfileGoldenFixture','importsWordPerfectMergeGoldenFixture','importsTakeNoteGoldenFixtureThroughBtrieveReader','importsPasswordProtectedTakeNoteGoldenFixture')
        LegacyReportReaderTests=@('importsSoftwareReportSampleWhenConfigured')
        BtrieveAuditReaderTests=@()
    }
    try {
        $env:QT_QPA_PLATFORM='offscreen'
        $env:CARDSTACK_WINEVDM_GOLDEN_DIR=$FixtureDir
        $env:CARDSTACK_LEGACY_DECK_SAMPLE=Join-Path $FixtureDir 'plain.BTN'
        $env:CARDSTACK_LEGACY_REPORT_SAMPLE=Join-Path $FixtureDir 'reports.RPT'
        if ($QtBin) { $env:PATH=$QtBin+';'+$env:PATH;$env:QT_PLUGIN_PATH=Join-Path (Split-Path -Parent $QtBin) 'plugins' }
        foreach ($suite in @('LegacyDeckReaderTests','LegacyInterchangeReaderTests','LegacyReportReaderTests','BtrieveAuditReaderTests')) {
            $xml=Join-Path $OutputDir ($suite+'.xml')
            $text=Join-Path $OutputDir ($suite+'.txt')
            & $TestExecutable --test $suite -o ($xml+',xml') -o ($text+',txt')
            $code=$LASTEXITCODE
            if ($code -ne 0) { throw "Fixture semantics failed in $suite (exit $code); see $text" }
            [xml]$report=Get-Content -LiteralPath $xml -Raw
            foreach ($name in $required[$suite]) {
                $functions=@($report.SelectNodes("//TestFunction[@name='$name']"))
                if ($functions.Count -ne 1 -or $functions[0].SelectNodes(".//*[@type='skip' or @type='fail' or @type='xfail' or @type='xpass']").Count) { throw "Required corpus test did not pass: $suite/$name" }
            }
            $results.Add([ordered]@{suite=$suite;exitCode=$code;requiredTests=$required[$suite];xml=$xml})
        }
    } finally { foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')} }
}
[ordered]@{fixtureDirectory=$FixtureDir;manifestSha256=(Get-FileHash -LiteralPath $manifestPath).Hash;fileCount=@($manifest.files).Count;caseCount=@($manifest.cases).Count;testSuites=$results.ToArray();testsSkipped=[bool]$SkipTests;pendingCoverage=$pending} |
    ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $OutputDir 'validation.json') -Encoding UTF8
if ($pending.Count) {
    $pending | Format-Table id,status,reason -AutoSize
    if ($RequirePending) { throw 'Importer contracts passed, but the declared oracle completeness gates are not closed.' }
    Write-Warning 'Corpus data checks passed; oracle completeness is NOT certified. Synthetic and pending cases are listed explicitly.'
}
Write-Host ('Validated requested corpus: '+$FixtureDir)
Write-Host ('Evidence: '+$OutputDir)
