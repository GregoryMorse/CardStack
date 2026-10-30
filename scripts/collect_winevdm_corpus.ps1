param(
    [string]$LegacyExe=$env:CARDSTACK_LEGACY_EXE,
    [string]$WineVdmDir='',
    [string]$BuildDir='build/vs2026-local-qt',
    [string]$Configuration='Release',
    [string[]]$Fixture=@(),
    [string]$OutputDir='',
    [switch]$KeepRuntime,
    [switch]$Promote,
    [switch]$RequireComplete
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
function Full([string]$path){if([IO.Path]::IsPathRooted($path)){return [IO.Path]::GetFullPath($path)};return [IO.Path]::GetFullPath((Join-Path $root $path))}
function Read-Json([string]$path){Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json}
function Write-Json($value,[string]$path){$value | ConvertTo-Json -Depth 64 | Set-Content -LiteralPath $path -Encoding UTF8}
function Leaf([string]$name){if(!$name -or [IO.Path]::GetFileName($name) -ne $name -or $name -in @('.','..')){throw 'Artifact names must be leaf filenames.'};return $name}
if(!$LegacyExe -or !(Test-Path -LiteralPath (Full $LegacyExe) -PathType Leaf)){throw 'Provide your own legacy executable.'}
$LegacyExe=Full $LegacyExe
if(!$WineVdmDir){$WineVdmDir=Join-Path $root '.tools/winevdm/pr'}
$WineVdmDir=Full $WineVdmDir
if(!$OutputDir){$OutputDir=Join-Path $root ('docs/audit/winevdm-corpus/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,6))}
$OutputDir=Full $OutputDir
$allowed=@((Join-Path $root 'build'),(Join-Path $root 'docs/audit'))
if(!@($allowed | Where-Object {$OutputDir.StartsWith($_+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)}).Count){throw 'Output must stay under build or docs/audit.'}
if(Test-Path -LiteralPath $OutputDir){throw 'Choose a fresh output directory; evidence must not be reused.'}
$planPath=Join-Path $PSScriptRoot 'winevdm_corpus.scenarios.json'
$plan=Read-Json $planPath
$Fixture=@($Fixture | ForEach-Object {$_ -split ','} | Where-Object {$_})
foreach($name in $Fixture){if($name -notin @($plan.fixtures.name)){throw "Unknown corpus recipe: $name"}}
$selected=@($plan.fixtures | Where-Object {$_.enabled -and (!$Fixture.Count -or $Fixture -contains $_.name)})
if(!$selected.Count){throw 'No corpus recipes selected.'}
if(($RequireComplete -or $Promote) -and $selected.Count -ne @($plan.fixtures | Where-Object {$_.enabled}).Count){throw 'Complete validation or promotion requires every enabled original-app recipe.'}
$baseline=Join-Path $root 'tests/fixtures/legacy/winevdm'
$manifest=Read-Json (Join-Path $baseline 'corpus-manifest.json')
if ('custom-forty-field-save-reopen' -notin @($manifest.coverage.id)) {
    $manifest.coverage+=@([ordered]@{id='custom-forty-field-save-reopen';status='pending';reason='Requires forty original-app-authored, arranged fields, saved and reopened with schema/layout contracts.'})
}
function Generator-Fingerprint {
    $validatorExe=Join-Path (Join-Path (Full $BuildDir) $Configuration) 'CardStackTests.exe'
    if(!(Test-Path -LiteralPath $validatorExe -PathType Leaf)){throw 'Build CardStackTests before collecting fixtures.'}
    $paths=@($planPath,$LegacyExe,$validatorExe)
    foreach($name in @('collect_winevdm_corpus.ps1','winevdm_buttonfile_oracle.ps1','WineVdmOracleNative.cs','invoke_private_desktop.ps1','validate_winevdm_golden_fixtures.ps1')){$paths+=Join-Path $PSScriptRoot $name}
    $runtimeExe=Join-Path $WineVdmDir 'winevdm/Release/otvdmw.exe'
    if(!(Test-Path -LiteralPath $runtimeExe -PathType Leaf)){throw 'Build the fixed WineVDM PR runtime before collecting fixtures.'}
    $paths+=@(Get-ChildItem -LiteralPath (Split-Path -Parent $runtimeExe) -File | Where-Object {$_.Extension -in @('.exe','.dll','.dll16')} | ForEach-Object {$_.FullName})
    return @($paths | Sort-Object -Unique | ForEach-Object {$full=[IO.Path]::GetFullPath($_);$name=if($full -eq $LegacyExe){'supplied-executable'}elseif($full.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){$full.Substring($root.Length+1)}else{[IO.Path]::GetFileName($full)};[ordered]@{name=$name;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()}})
}
$fingerprint=Generator-Fingerprint
$corpus=Join-Path $OutputDir 'corpus'
New-Item -ItemType Directory -Path $corpus -Force | Out-Null
foreach($file in $manifest.files){Copy-Item -LiteralPath (Join-Path $baseline (Leaf $file.name)) -Destination $corpus}
$sourceHash=(Get-FileHash -LiteralPath $LegacyExe -Algorithm SHA256).Hash.ToLowerInvariant()
$planHash=(Get-FileHash -LiteralPath $planPath -Algorithm SHA256).Hash.ToLowerInvariant()
$runs=@()
foreach($spec in $selected){
    $caseDir=Join-Path (Join-Path $OutputDir 'generation') $spec.name
    New-Item -ItemType Directory -Path $caseDir -Force | Out-Null
    $log=Join-Path $caseDir 'launcher.log'
    $runtimeRoot=[IO.Path]::GetFullPath((Join-Path $root 'docs/audit/wvdm'))
    $runtime=Join-Path $runtimeRoot ([Guid]::NewGuid().ToString('N').Substring(0,8))
    if(Test-Path -LiteralPath $runtime){throw 'Scratch name collision.'}
    if(!@($spec.artifacts).Count){throw "Recipe has no declared artifacts: $($spec.name)"}
    $previous=$ErrorActionPreference
    try{
        $ErrorActionPreference='Continue'
        & (Join-Path $PSHOME 'powershell.exe') -NoProfile -NonInteractive -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'winevdm_buttonfile_oracle.ps1') -LegacyExe $LegacyExe -WineVdmDir $WineVdmDir -ScenarioPath $planPath -Fixture $spec.name -StageOnlyDeck SOFTWARE.BTN -OutputDir $caseDir -RuntimeDir $runtime -TimeoutSeconds 180 *> $log
        $code=$LASTEXITCODE
    }finally{$ErrorActionPreference=$previous}
    $run=[ordered]@{fixture=$spec.name;status='failed';exitCode=$code;log=$log}
    try{
        if($code -ne 0){throw 'Original application authoring failed.'}
        $trace=Read-Json (Join-Path $caseDir 'run-evidence.json')
        if($trace.status -ne 'passed' -or $trace.inputMode -ne 'window-messages-only' -or $trace.desktop -notlike 'CardStackOracle_*'){throw 'A complete private-desktop message-only trace is required.'}
        foreach($artifact in $spec.artifacts){
            $snapshot=Join-Path $caseDir ('snapshot-'+$artifact.snapshot)
            $proof=Read-Json (Join-Path $snapshot 'snapshot.json')
            $entry=@($proof.files | Where-Object {$_.name -eq $artifact.file})
            if($entry.Count -ne 1){throw "Missing snapshot artifact: $($artifact.file)"}
            $source=Join-Path $snapshot (Leaf $artifact.file)
            $hash=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
            $size=(Get-Item -LiteralPath $source).Length
            if($size -le 0 -or $size -ne $entry[0].size -or $hash -ne $entry[0].sha256.ToLowerInvariant()){throw 'Snapshot identity changed before validation.'}
            $target=Leaf $artifact.target
            Copy-Item -LiteralPath $source -Destination (Join-Path $corpus $target) -Force
            $manifest.files=@($manifest.files | Where-Object {$_.name -ne $target})+@([ordered]@{name=$target;size=$size;sha256=$hash;origin='original-ui';recipe=$spec.name;sourceExeSha256=$sourceHash;scenarioSha256=$planHash})
            $manifest.cases=@($manifest.cases | Where-Object {$_.file -ne $target})
            foreach($contract in $artifact.cases){$contract | Add-Member -NotePropertyName file -NotePropertyValue $target -Force;$manifest.cases+=@($contract)}
        }
        $run.status='passed'
        $run['inputMode']=$trace.inputMode
        $run['stepCount']=@($trace.steps).Count
        $run['reopenedFieldChecks']=@($trace.steps | Where-Object {$_.action -eq 'assertField' -and $_.readbackVerified}).Count
    }catch{$run['error']=$_.Exception.Message}
    if(!$KeepRuntime -and (Test-Path -LiteralPath $runtime)) {
        $actual=[IO.Path]::GetFullPath($runtime)
        if(!$actual.StartsWith($runtimeRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or (Split-Path -Parent $actual) -ne $runtimeRoot){throw 'Refusing out-of-scope scratch cleanup.'}
        Remove-Item -LiteralPath $actual -Recurse -Force
    }
    $runs+=@($run)
    Write-Json $runs (Join-Path $OutputDir 'generation.json')
    Write-Host ($spec.name+': '+$run.status)
}
$passed=@($runs | Where-Object {$_.status -eq 'passed'} | ForEach-Object {$_.fixture})
foreach($gate in $manifest.coverage){
    $complete=$false
    switch($gate.id){
        'real-security-cycle' {$complete=$passed -contains 'oracle-security-cycle'}
        'real-long-notes-and-boundary-input' {$complete=$passed -contains 'oracle-long-notes'}
        'all-stock-template-save-reopen' {$complete=@($plan.fixtures | Where-Object {$_.name -like 'oracle-stock-*' -and $_.name -notin $passed}).Count -eq 0}
        'variable-and-multi-page-storage' {$complete=($passed -contains 'oracle-long-notes') -and ($passed -contains 'oracle-many-records')}
        'nonempty-export-memos' {$complete=$passed -contains 'oracle-export-dbase-notes'}
        'custom-forty-field-save-reopen' {$complete=$passed -contains 'oracle-custom-forty-fields'}
    }
    if($complete){$gate.status='verified';$gate.reason='Original-app generation and native reopen checks; importer contracts must also pass before promotion.'}
    else{$gate.status='pending';$gate.reason='This coverage gate was not exercised in this generation batch.'}
}
if($passed -contains 'oracle-security-cycle'){$manifest.notes=@($manifest.notes | Where-Object {$_ -notlike '*historical security-cycle*'})}
if(($fingerprint | ConvertTo-Json -Depth 8 -Compress) -ne ((Generator-Fingerprint) | ConvertTo-Json -Depth 8 -Compress)){throw 'Generator, source executable or runtime changed during collection; nothing was promoted.'}
Write-Json $manifest (Join-Path $corpus 'corpus-manifest.json')
$publishedRuns=@($runs | ForEach-Object {
    [ordered]@{fixture=$_.fixture;status=$_.status;exitCode=$_.exitCode;inputMode=$_.inputMode;stepCount=$_.stepCount;reopenedFieldChecks=$_.reopenedFieldChecks}
})
Write-Json ([ordered]@{version=1;sourceExeSha256=$sourceHash;scenarioSha256=$planHash;inputMode='window-messages-only';generatorFingerprint=$fingerprint;runs=$publishedRuns}) (Join-Path $corpus 'generation-evidence.json')
if(@($runs | Where-Object {$_.status -ne 'passed'}).Count){throw 'Some original-app recipes failed; nothing was promoted. Inspect generation.json.'}
$validationArguments=@{FixtureDir=$corpus;BuildDir=(Full $BuildDir);Configuration=$Configuration;OutputDir=(Join-Path $OutputDir 'validation')}
if($RequireComplete -or $Promote){$validationArguments['RequirePending']=$true}
& (Join-Path $PSScriptRoot 'validate_winevdm_golden_fixtures.ps1') @validationArguments
if($Promote){
    foreach($file in $manifest.files){Copy-Item -LiteralPath (Join-Path $corpus (Leaf $file.name)) -Destination $baseline -Force}
    Copy-Item -LiteralPath (Join-Path $corpus 'generation-evidence.json') -Destination $baseline -Force
    # Publish the new contract last, after every binary and every semantic test succeeded.
    Copy-Item -LiteralPath (Join-Path $corpus 'corpus-manifest.json') -Destination $baseline -Force
    Write-Host 'Validated corpus promoted to tests/fixtures/legacy/winevdm.'
}else{Write-Host ('Validated staging corpus: '+$corpus)}
