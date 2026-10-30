param(
    [string]$BuildDir='build/vs2026-local-qt',
    [string]$Configuration='Release',
    [string]$FixtureDir='tests/fixtures/legacy/winevdm',
    [string]$OutputDir=''
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
function Full([string]$path) { if([IO.Path]::IsPathRooted($path)){return [IO.Path]::GetFullPath($path)};return [IO.Path]::GetFullPath((Join-Path $root $path)) }
$FixtureDir=Full $FixtureDir
if(!$OutputDir){$OutputDir=Join-Path $root ('build/oracle-validator-controls/'+[Guid]::NewGuid().ToString('N'))}
$OutputDir=Full $OutputDir
if(!$OutputDir.StartsWith((Join-Path $root 'build')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Negative controls must stay under build.'}
if(Test-Path -LiteralPath $OutputDir){throw 'Use a fresh negative-control directory.'}
New-Item -ItemType Directory -Path $OutputDir | Out-Null
$results=@()
foreach($kind in @('semantic','provenance','hash','missing','unlisted')){
    $corpus=Join-Path $OutputDir $kind
    New-Item -ItemType Directory -Path $corpus | Out-Null
    Get-ChildItem -LiteralPath $FixtureDir -File | Copy-Item -Destination $corpus
    $manifestPath=Join-Path $corpus 'corpus-manifest.json'
    $manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if($kind -eq 'semantic'){
        $case=@($manifest.cases | Where-Object { $_.rows.Count -gt 0 })[0]
        $case.rows[0][0]='Deliberately incorrect semantic negative control'
    } elseif($kind -eq 'provenance'){
        $proofPath=Join-Path $corpus 'generation-evidence.json'
        $proof=Get-Content -LiteralPath $proofPath -Raw -Encoding UTF8 | ConvertFrom-Json
        $proof.runs[0].status=$null
        $proof | ConvertTo-Json -Depth 64 | Set-Content -LiteralPath $proofPath -Encoding UTF8
    } elseif($kind -eq 'hash'){
        $manifest.files[0].sha256=('0'*64)
    } elseif($kind -eq 'unlisted'){
        Copy-Item -LiteralPath (Join-Path $corpus $manifest.files[0].name) -Destination (Join-Path $corpus 'UNLISTED.BTN')
    } else {
        $manifest.files[0].name='DELIBERATELY_ABSENT.BTN'
    }
    $manifest | ConvertTo-Json -Depth 64 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    $arguments=@('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'validate_winevdm_golden_fixtures.ps1'),'-FixtureDir',$corpus,'-BuildDir',(Full $BuildDir),'-Configuration',$Configuration,'-OutputDir',(Join-Path $OutputDir ($kind+'-result')))
    if($kind -notin @('semantic','provenance')){$arguments+='-SkipTests'}
    $log=Join-Path $OutputDir ($kind+'.log')
    $previousErrorAction=$ErrorActionPreference
    try {
        $ErrorActionPreference='Continue'
        & (Join-Path $PSHOME 'powershell.exe') @arguments *> $log
        $code=$LASTEXITCODE
    } finally { $ErrorActionPreference=$previousErrorAction }
    $results+=[ordered]@{control=$kind;exitCode=$code;rejected=($code -ne 0);log=$log}
    if($code -eq 0){throw "Validator accepted the $kind negative control."}
}
$results | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDir 'controls.json') -Encoding UTF8
Write-Host 'All negative controls were rejected; the requested corpus directory and semantic contracts are enforced.'
