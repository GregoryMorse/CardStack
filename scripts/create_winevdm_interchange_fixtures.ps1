param(
    [string[]]$Fixture=@('export-dbase-deck','export-cardfile-deck','export-wordperfect-deck','export-takenote-deck'),
    [string]$LegacyExe=$env:CARDSTACK_LEGACY_EXE,
    [string]$OutputDir='',
    [string]$WineVdmDir='',
    [string]$BuildDir='build/vs2026-local-qt',
    [switch]$NoDownload,
    [switch]$SkipValidation,
    [switch]$NoPromote
)
$ErrorActionPreference='Stop'
if($SkipValidation){throw 'Fixture promotion may no longer bypass semantic validation. Use -NoPromote for a validated staging-only run.'}
$mapping=@{'export-dbase-deck'='oracle-export-dbase';'export-cardfile-deck'='oracle-export-cardfile';'export-wordperfect-deck'='oracle-export-wordperfect';'export-takenote-deck'='oracle-export-takenote'}
$recipes=@()
foreach($name in $Fixture){if(!$mapping.ContainsKey($name)){throw "Unknown interchange recipe: $name"};$recipes+=$mapping[$name]}
if($NoDownload){
    $root=Split-Path -Parent $PSScriptRoot
    $runtime=if($WineVdmDir){$WineVdmDir}else{Join-Path $root '.tools/winevdm/pr'}
    if(!(Test-Path -LiteralPath (Join-Path $runtime 'winevdm/Release/otvdmw.exe'))){throw 'Build the supported WineVDM PR runtime first.'}
}
$parameters=@{LegacyExe=$LegacyExe;BuildDir=$BuildDir;Fixture=$recipes;Promote=(!$NoPromote)}
if($OutputDir){$parameters.OutputDir=$OutputDir}
if($WineVdmDir){$parameters.WineVdmDir=$WineVdmDir}
& (Join-Path $PSScriptRoot 'collect_winevdm_corpus.ps1') @parameters
