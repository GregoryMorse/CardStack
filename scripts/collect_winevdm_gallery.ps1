param(
    [string]$LegacyExe=$env:CARDSTACK_LEGACY_EXE,
    [string]$WineVdmDir='', [string]$OutputDir='',
    [string[]]$OnlyCase=@(), [switch]$CaptureModern,
    [string]$ModernTestExe='', [string]$QtBin='',
    [switch]$KeepRuntime, [switch]$RequireComplete,
    [switch]$IncludeUnverifiedKeyboardProbes,
    [switch]$ModernWorker, [string]$RequestFile=''
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
function Write-Json($Value,[string]$Path) { $Value | ConvertTo-Json -Depth 32 | Set-Content -LiteralPath $Path -Encoding UTF8 }
function Read-Json([string]$Path) { Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json }
function Full-Path([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $root $Path))
}
if ($ModernWorker) {
    Add-Type -Path (Join-Path $PSScriptRoot 'WineVdmOracleNative.cs') -ReferencedAssemblies System.dll,System.Core.dll,System.Drawing.dll,System.Windows.Forms.dll
    [CardStack.Oracle.Native]::RequirePrivate()
    $request=Read-Json $RequestFile
    $env:QT_QPA_PLATFORM='windows'
    $env:CARDSTACK_GALLERY_LEGACY_DECK=$request.deck
    if ($request.qtBin) {
        $env:PATH=$request.qtBin+';'+$env:PATH
        $env:QT_PLUGIN_PATH=Join-Path (Split-Path -Parent $request.qtBin) 'plugins'
    }
    $results=New-Object 'System.Collections.Generic.List[object]'
    foreach ($spec in $request.suites) {
        $directory=Join-Path $request.output $spec.directory
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
        [Environment]::SetEnvironmentVariable($spec.variable,$directory,'Process')
        $log=Join-Path $request.output ($spec.suite+'.'+($spec.function -replace '[^A-Za-z0-9_-]','_')+'.txt')
        $process=[CardStack.Oracle.Native]::StartChild($request.executable,@('--test',$spec.suite,$spec.function,'-o',($log+',txt')),$root)
        $completion=[CardStack.Oracle.Native]::WaitChild($process,120000)
        $results.Add([ordered]@{suite=$spec.suite;function=$spec.function;exitCode=$completion.ExitCode;timedOut=$completion.TimedOut;log=$log})
        $process.Dispose()
        [Environment]::SetEnvironmentVariable($spec.variable,$null,'Process')
    }
    Write-Json ($results.ToArray()) (Join-Path $request.output 'suites.json')
    if (@($results | Where-Object { $_.timedOut -or $_.exitCode -ne 0 }).Count) { exit 1 }
    exit 0
}
if (!$OutputDir) { $OutputDir=Join-Path $root ('docs\audit\winevdm-ui\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,6)) }
$OutputDir=Full-Path $OutputDir
$allowed = @((Join-Path $root 'build'), (Join-Path $root 'docs\audit')) | ForEach-Object { $_ + [IO.Path]::DirectorySeparatorChar }
if (-not @($allowed | Where-Object { $OutputDir.StartsWith($_,[StringComparison]::OrdinalIgnoreCase) }).Count) { throw 'Gallery output must be below build or docs/audit.' }
if ((Test-Path -LiteralPath $OutputDir) -and @(Get-ChildItem -LiteralPath $OutputDir -Force).Count) { throw 'Choose a new, empty output directory; stale images must not enter a comparison.' }
if (!$LegacyExe -or !(Test-Path -LiteralPath $LegacyExe -PathType Leaf)) { throw 'Provide -LegacyExe pointing to your own legacy executable.' }
$LegacyExe=Full-Path $LegacyExe
if (!$WineVdmDir) { $WineVdmDir=Join-Path $root '.tools\winevdm\pr' }
$WineVdmDir=Full-Path $WineVdmDir
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$seed=Join-Path $OutputDir 'seed'
New-Item -ItemType Directory -Path $seed | Out-Null
$provenance=New-Object 'System.Collections.Generic.List[object]'
foreach ($file in Get-ChildItem -LiteralPath (Split-Path -Parent $LegacyExe) -File) {
    if ($file.Extension -notin @('.exe','.dll','.ini','.hlp','.btn','.rpt','.btr','.dat')) { continue }
    Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $seed $file.Name)
    $provenance.Add([ordered]@{name=$file.Name;size=$file.Length;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash})
}
$seedExe=Join-Path $seed (Split-Path -Leaf $LegacyExe)
if (!(Test-Path -LiteralPath $seedExe)) { throw 'The executable was not included in the disposable seed.' }
$deck=Join-Path $seed 'SOFTWARE.BTN'
if (!(Test-Path -LiteralPath $deck)) { throw 'Place the SOFTWARE.BTN sample (and its report sidecar) beside the executable for this comparison plan.' }
$planPath=Join-Path $PSScriptRoot 'winevdm_gallery.scenarios.json'
$plan=Read-Json $planPath
$OnlyCase=@($OnlyCase | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$selected=@($plan.fixtures | Where-Object { (!$OnlyCase.Count -or $OnlyCase -contains $_.name) -and (!$_.diagnosticOnly -or $IncludeUnverifiedKeyboardProbes -or $OnlyCase -contains $_.name) })
if (!$selected.Count) { throw 'No gallery cases matched.' }
foreach ($name in $OnlyCase) { if ($name -notin @($plan.fixtures.name)) { throw "Unknown gallery case: $name" } }
Write-Json ([ordered]@{createdUtc=[DateTime]::UtcNow.ToString('o');dataset='SOFTWARE.BTN';inputs=($provenance.ToArray());planSha256=(Get-FileHash -LiteralPath $planPath).Hash;inputMode='private-desktop-window-messages';switchDesktop=$false;physicalInput=$false}) (Join-Path $OutputDir 'provenance.json')
$results=New-Object 'System.Collections.Generic.List[object]'
$ps=Join-Path $PSHOME 'powershell.exe'
foreach ($spec in $selected) {
    $caseDir=Join-Path (Join-Path $OutputDir 'legacy') $spec.name
    $runtimeRoot=Join-Path $root 'docs\audit\wvdm'
    $runtimeLeaf=[Guid]::NewGuid().ToString('N').Substring(0,8)
    $runtime=Join-Path $runtimeRoot $runtimeLeaf
    if (Test-Path -LiteralPath $runtime) { throw 'Scratch name collision; rerun with a fresh name.' }
    New-Item -ItemType Directory -Force -Path $caseDir | Out-Null
    $log=Join-Path $caseDir 'launcher.log'
    $priorErrorAction=$ErrorActionPreference
    try {
        $ErrorActionPreference='Continue'
    & $ps -NoProfile -NonInteractive -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'winevdm_buttonfile_oracle.ps1') -LegacyExe $seedExe -WineVdmDir $WineVdmDir -ScenarioPath $planPath -Fixture $spec.name -OutputDir $caseDir -RuntimeDir $runtime -StageOnlyDeck $deck -TimeoutSeconds 75 *> $log
        $exitCode=$LASTEXITCODE
    } finally { $ErrorActionPreference=$priorErrorAction }
    $results.Add([ordered]@{fixture=$spec.name;exitCode=$exitCode;status=$(if ($exitCode -eq 0) {'passed'} else {'failed'});log=$log})
    Write-Host ($spec.name+': '+$exitCode)
    if (!$KeepRuntime -and (Test-Path -LiteralPath $runtime)) {
        $actual=[IO.Path]::GetFullPath($runtime)
        $expected=[IO.Path]::GetFullPath((Join-Path $runtimeRoot $runtimeLeaf))
        if ($actual -ne $expected -or !$actual.StartsWith($runtimeRoot+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Refusing out-of-scope runtime cleanup.' }
        Remove-Item -LiteralPath $actual -Recurse -Force
    }
}
Write-Json ($results.ToArray()) (Join-Path $OutputDir 'legacy-runs.json')
$modernDir=Join-Path $OutputDir 'modern'
$modernStatus='not-requested'
if ($CaptureModern) {
    if (!$ModernTestExe) { $ModernTestExe=Join-Path $root 'build\vs2026-local-qt\Release\CardStackTests.exe' }
    $ModernTestExe=Full-Path $ModernTestExe
    if (!(Test-Path -LiteralPath $ModernTestExe -PathType Leaf)) { throw 'Build CardStackTests before collecting modern images.' }
    if (!$QtBin) { $QtBin=$env:QT_HOST_PATH; if ($QtBin) { $QtBin=Join-Path $QtBin 'bin' } }
    if ($QtBin) { $QtBin=Full-Path $QtBin }
    New-Item -ItemType Directory -Force -Path $modernDir | Out-Null
    $request=Join-Path $modernDir 'request.json'
    Write-Json ([ordered]@{executable=$ModernTestExe;qtBin=$QtBin;deck=$deck;output=$modernDir;suites=$plan.modernSuites}) $request
    try {
        & (Join-Path $PSScriptRoot 'invoke_private_desktop.ps1') -Executable $ps -ArgumentList @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$PSCommandPath,'-ModernWorker','-RequestFile',$request) -WorkingDirectory $root -OutputDir (Join-Path $modernDir 'worker') -TimeoutSeconds 650
        $modernStatus='passed'
    } catch { $modernStatus='failed'; $_ | Out-String | Set-Content -LiteralPath (Join-Path $modernDir 'failure.txt') }
}
function Relative-Link([string]$Path) { return $Path.Substring($OutputDir.Length+1).Replace('\','/') }
function Html([object]$Text) { return [Net.WebUtility]::HtmlEncode([string]$Text) }
$modernImages=@()
if (Test-Path -LiteralPath $modernDir) { $modernImages=@(Get-ChildItem -LiteralPath $modernDir -Filter '*.png' -Recurse -File) }
$rows=New-Object 'System.Collections.Generic.List[object]'
$used=New-Object 'System.Collections.Generic.HashSet[string]'
foreach ($item in $plan.inventory) {
    $legacyPath=''
    if ($item.fixture) { $candidate=Join-Path (Join-Path (Join-Path $OutputDir 'legacy') $item.fixture) ('captures\'+$item.key+'.png'); if (Test-Path -LiteralPath $candidate) { $legacyPath=$candidate } }
    $modern=@($modernImages | Where-Object { $_.Name -like $item.modernMatch })
    if($modern.Count -gt 1){throw "Ambiguous modern capture for '$($item.key)': $($item.modernMatch)"}
    $modernPath=''; if ($modern.Count) { $modernPath=$modern[0].FullName; [void]$used.Add($modernPath) }
    $status='pending-route'
    if ($legacyPath -and $modernPath) { $status='paired-needs-review' }
    elseif ($legacyPath) { $status='legacy-only' }
    elseif ($item.fixture -in @($selected.name)) { $status='routing-failed' }
    elseif ($modernPath) { $status='modern-only' }
    elseif ($item.routing -eq 'configured') { $status='not-requested' }
    if ($legacyPath) {
        $metadata=[IO.Path]::ChangeExtension($legacyPath,'.json')
        if ((Test-Path -LiteralPath $metadata) -and (Read-Json $metadata).capture.partial) { $status='incomplete-legacy-capture' }
    }
    $rows.Add([ordered]@{key=$item.key;status=$status;legacy=$(if ($legacyPath) {Relative-Link $legacyPath} else {''});modern=$(if ($modernPath) {Relative-Link $modernPath} else {''});notes=@($item.notes);behaviorCompared=$false;visuallyReviewed=$false})
}
$knownLegacy=@($rows | Where-Object { $_.legacy } | ForEach-Object { $_.legacy })
foreach ($image in Get-ChildItem -LiteralPath (Join-Path $OutputDir 'legacy') -Filter '*.png' -Recurse -File) {
    $relative=Relative-Link $image.FullName
    if ($relative -notin $knownLegacy) {
        $rows.Add([ordered]@{key=$image.BaseName;status='legacy-diagnostic-only';legacy=$relative;modern='';notes=@('Supplemental original capture; hidden-workspace images do not prove visible-window fidelity.');behaviorCompared=$false;visuallyReviewed=$false})
    }
}
foreach ($image in $modernImages) { if (!$used.Contains($image.FullName)) { $rows.Add([ordered]@{key=$image.BaseName;status='modern-only';legacy='';modern=(Relative-Link $image.FullName);notes=@('No calibrated legacy route yet.');behaviorCompared=$false;visuallyReviewed=$false}) } }
$summary=[ordered]@{version=1;legacyRuns=($results.ToArray());modern=$modernStatus;counts=@($rows | Group-Object { $_['status'] } | ForEach-Object { @{status=$_.Name;count=$_.Count} });inventory=($rows.ToArray());limitations=@('Paired screenshots are review candidates, not equivalence assertions.','Message-delivered commands are not proof of accelerator routing.','No real printing, phone calls or external browser launches are performed.','Win16 and Qt font rasterization may differ.','Pending routes and capture failures remain uncovered.')}
Write-Json $summary (Join-Path $OutputDir 'coverage.json')
$markdown=New-Object 'System.Collections.Generic.List[string]'
$markdown.Add('# Legacy / modern UI evidence')
$markdown.Add('')
$markdown.Add('No physical input, foreground activation or desktop switching. A paired row still requires visual and behavioral review. See coverage.json and per-capture metadata.')
$markdown.Add('')
$markdown.Add('| State | Coverage | Original | Modern |')
$markdown.Add('| --- | --- | --- | --- |')
$html=New-Object Text.StringBuilder
[void]$html.Append('<!doctype html><meta charset="utf-8"><title>Legacy / modern UI evidence</title><style>body{font:16px Segoe UI,sans-serif;margin:24px;background:#f4f5f6;color:#20252b}section{background:white;padding:16px;margin:20px 0;border:1px solid #c6cbd1}article{display:flex;gap:20px;align-items:flex-start}figure{margin:0;flex:1;min-width:0}img{max-width:100%;height:auto;border:1px solid #888}figcaption{font-weight:600;margin-bottom:8px}.status{font-family:monospace}</style><h1>Legacy / modern UI evidence</h1><p>Paired images are review candidates, not proof of equivalence. All windows ran on private, non-input desktops. Missing routes are explicit.</p>')
foreach ($row in $rows) {
    $left=''; $right=''
    if ($row.legacy) { $left='[PNG]('+ $row.legacy +')' }
    if ($row.modern) { $right='[PNG]('+ $row.modern +')' }
    $markdown.Add('| '+$row.key+' | '+$row.status+' | '+$left+' | '+$right+' |')
    [void]$html.Append('<section><h2>'+ (Html $row.key) +'</h2><p class="status">'+(Html $row.status)+'</p><p>'+ (Html ($row.notes -join ' ')) +'</p><article>')
    foreach ($side in @('legacy','modern')) { [void]$html.Append('<figure><figcaption>'+(Html $side)+'</figcaption>'); if ($row[$side]) { [void]$html.Append('<a href="'+(Html $row[$side])+'"><img loading="lazy" src="'+(Html $row[$side])+'"></a>') } else { [void]$html.Append('<p>Not captured</p>') }; [void]$html.Append('</figure>') }
    [void]$html.Append('</article></section>')
}
$markdown | Set-Content -LiteralPath (Join-Path $OutputDir 'coverage.md') -Encoding UTF8
$html.ToString() | Set-Content -LiteralPath (Join-Path $OutputDir 'index.html') -Encoding UTF8
Write-Host ('Gallery: '+(Join-Path $OutputDir 'index.html'))
if (@($results | Where-Object { $_.exitCode -ne 0 }).Count -or $modernStatus -eq 'failed') { throw 'One or more requested routes failed. Evidence and coverage inventory were preserved.' }
if ($RequireComplete -and @($rows | Where-Object { $_.status -ne 'paired-needs-review' }).Count) { throw 'The full inventory is not paired yet; consult coverage.md.' }
