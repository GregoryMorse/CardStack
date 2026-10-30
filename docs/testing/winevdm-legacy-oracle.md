# WineVDM migration oracle and UI comparison

The optional oracle runs a separately supplied ButtonFile installation through the fixed WineVDM build. It creates migration fixtures and captures original UI behavior for comparison with CardStack. Neither binary is a CardStack runtime dependency.

## Non-disruptive execution

Use Windows PowerShell 5.1 (powershell.exe). The launcher creates a transient, non-input Windows desktop and starts its worker and WineVDM there. It never switches desktops, activates a foreground window, moves the system cursor or injects physical input. Modern tests default to Qt's offscreen platform. Native Windows gallery tests require the same private-desktop runner and refuse an ordinary desktop before creating QApplication.

The driver uses bounded window messages, including client-coordinate mouse messages and character/key messages. Stale-output cleanup preserves the staged launch document and its report sidecar, even when the scenario output stem matches the input. Modifier-key probes attach input queues only after verifying both threads belong to the same private desktop; synthetic state is cleared when the probe ends. Every keyboard probe must specify an observable postcondition. A posted command is recorded as command coverage, not proof that a keyboard accelerator works.

The parent owns a kill-on-close process job and a timeout. Cleanup targets only that job and explicitly staged runtime directories, never all processes with a matching name. Interactive and KeepProcess modes are rejected. There is no foreground fallback.

A private desktop isolates UI interaction, not untrusted code. Use your own known runtime and disposable data. These gallery scenarios do not place calls, send output to a physical printer or launch external browsers.

## Runtime prerequisites

Only the [fixed common-dialog WineVDM branch](https://github.com/GregoryMorse/winevdm/tree/fix-filedlg-fileok-ofn16-sync) is supported. The default installation is .tools/winevdm/pr. Bootstrap it when necessary:

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/bootstrap_winevdm_pr.ps1
~~~

Supply your own executable with -LegacyExe or CARDSTACK_LEGACY_EXE. The fixture launcher also accepts CARDSTACK_LEGACY_DIR and the sanitized optional scripts/winevdm_legacy_manifest.json. No audit resource catalog or decompiled source is needed by these scripts. Keep required DLLs and sample databases/report sidecars beside the executable. Public scenario metadata contains only the control/command IDs, captions and routes needed for migration testing.

## Driver self-test

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/test_winevdm_oracle_driver.ps1
~~~

This tests private-desktop guards, selection-preserving character input, mouse-message activation, an explicitly implemented Ctrl+A test shortcut, PNG capture and owned-process timeout cleanup without using the legacy runtime. Evidence goes to build/oracle-driver-self-test. It does not prove WineVDM implements those routes identically; real scenarios provide separate evidence.

## Migration fixtures

~~~powershell
$env:CARDSTACK_LEGACY_EXE = 'C:\path\to\BUTNFILE.EXE'
& scripts/winevdm_buttonfile_oracle.ps1 -Fixture @('plain-minimal-deck','password-only-deck','password-encrypted-deck')
~~~

Each fixture gets a fresh private-desktop session. The launcher keeps fixture evidence under build/winevdm-oracle/fixtures by default and retains snapshots, per-step evidence, worker logs and isolation metadata. Runtime scratch directories use short, unique paths under docs/audit/wvdm; evidence stays beneath the selected output directory. A real calibration reproduced File not found from a deeper scratch path with the same dataset and input sequence. This is a path-sensitivity finding, not proof that every path component needs an 8.3 alias. Password and encryption cases remain migration tests, not modern security configuration instructions.

Common-dialog acceptance sends the real dialog's IDOK command, allowing the fixed WineVDM FILEOK callback to run with its actual OPENFILENAME state. It does not fabricate a FILEOK callback with a null structure pointer.

## Validated migration corpus

Use the corpus collector for repeatable authoring, save/reopen checks and semantic validation:

~~~powershell
& scripts/collect_winevdm_corpus.ps1 -LegacyExe $env:CARDSTACK_LEGACY_EXE -BuildDir build/vs2026-local-qt -RequireComplete -Promote
~~~

All 33 enabled original-app recipes must run for complete validation or promotion. They cover all 14 stock templates and their report sidecars, a custom forty-field deck, both original owner-cipher schedules, a complete add/remove protection cycle, long notes, original edit limits, multi-page data, Windows text/whitespace and four export formats with nonempty notes. The committed contract currently contains 61 artifacts and 50 semantic cases. Historical synthetic boundaries remain explicitly identified, not relabeled as original UI evidence.

The collector fingerprints its scripts, supplied executable, runtime and compiled validator before and after the run. Published run summaries must contain actual recipe names, successful exit/status, input mode and step counts; complete corpus contracts bind original-UI artifacts to those runs. It requires successful message-only private-desktop traces, nonempty hashed snapshots and importer contracts before promotion. It removes only its exact owned scratch directories unless KeepRuntime is requested. Failed or partial batches cannot be promoted. The published provenance omits local log and executable paths.

The validator checks duplicate/unlisted artifacts, sizes, hashes and case references. It invokes the unified test executable directly, preventing CTest environment settings from substituting another fixture directory. Required corpus tests must run without skips. SkipTests cannot certify completeness. Test the validator itself with:

~~~powershell
& scripts/test_winevdm_fixture_validator.ps1 -BuildDir build/vs2026-local-qt
~~~

Its deliberately incorrect semantic value, null native-run provenance, hash, missing file and unlisted artifact must all be rejected. These checks do not certify every possible malformed file or format variant.

The migration contracts preserve Windows-1252 deck/report text and whitespace. Unmarked original dBASE exports use DOS/OEM text; supported declared Windows code pages are handled separately. Notes reserve forty bytes in descriptors but can grow with the record. The original edit accepts 8191 characters; a witnessed save/reopen preserves 8190, which is recorded as original behavior rather than importer loss. WINDEX TakeNote files are parsed from their own field table, not a hard-coded Software Library schema. Saved WINDEX text labels, field rectangles, grouped/standalone roles, phone flags, sort order and original font conversion are also tested. The corpus includes a non-Software Mailing List export saved and reopened in the original app.

## Side-by-side gallery

Build CardStackTests first. Supply SOFTWARE.BTN and its report sidecar beside your executable. The collector snapshots the input set once, records hashes and runs each original route against its own disposable copy. Use an empty output directory; the default is a unique directory below docs/audit/winevdm-ui.

~~~powershell
& scripts/collect_winevdm_gallery.ps1 -LegacyExe $env:CARDSTACK_LEGACY_EXE -CaptureModern -QtBin 'C:\path\to\Qt\bin'
~~~

A bounded calibration batch can select named cases:

~~~powershell
& scripts/collect_winevdm_gallery.ps1 -LegacyExe $env:CARDSTACK_LEGACY_EXE -OnlyCase @('gallery-newfile','gallery-search','gallery-deck')
~~~

The modern executable defaults to build/vs2026-local-qt/Release/CardStackTests.exe; override ModernTestExe for another build. QtBin is optional when the runtime is deployed beside the tests or already on PATH. Native captures use the production application appearance and record the stylesheet hash and effective fonts.

Open index.html in the output directory for side-by-side images. Modern filename matches must be unambiguous; report Data attributes cannot silently pair with template Data attributes. Workspaces are maximized within the capture so designer property panels are not cropped. The original preview explicitly selects All cards to match the modern capture scope; the separate initial Print dialog still records the original This card default. coverage.md and coverage.json retain the complete requested inventory, including unimplemented routes, failures, modern-only states and images still requiring review. Per-capture JSON records control geometry, visible/enabled/checked state, list contents, fonts and menu state where exposed. Password edits are redacted. KeepRuntime retains disposable per-case runtimes; otherwise only the collector's exact staged runtime paths are removed after execution. Seed files remain for reproducibility inside the ignored audit output.

The inventory covers initial/widest-choice dialog states, Find AND/OR, form types/orientation, startup/deck/template/report toolbars and targeted dialog/MDI keyboard routes.

Report Form and Define Custom Form comparison rows are captured through the initialized MainWindow workflows, including live measurements, form-type changes, portrait/landscape and pixel-widest choices. The modern gallery runs seven capture functions. Raw resource-construction samples remain supplemental: they must not be mistaken for the initialized runtime dialogs or used as their canonical comparison images. Designer captures keep saved frame rectangles; connected text flow and blank-row compaction belong to print/preview, not to the editable designer canvas. Modern galleries additionally capture expanded dropdowns and designer element variants. Routes not yet calibrated on the original remain explicitly pending. RequireComplete fails if any inventory row is not paired; pairing alone does not certify visual or behavioral equivalence.

## Scenario authoring

- command: posts a menu command ID; this is not an accelerator test.
- setText / ansiSetText: set a control value with readback.
- typeText: select and replace text through character messages, including deletion when the requested value is empty.
- mouseClick / click: send client-coordinate button messages without moving the cursor.
- listDoubleClick: uses the selected row's client rectangle, then double-click messages.
- focus: request logical dialog focus and verify it where the scenario requires it.
- key: post a virtual key with explicit modifiers and a required observable expect condition.
- assert: check focus, control text/check state or menu state.
- capture: save a semantic PNG plus control/menu evidence.
- interestingState: select the pixel-widest native choice using GDI text extents and a local clone of the reported control font; measurements are journaled. A control without a reported font explicitly records its DC-default basis.
- snapshot / assertAnyFile: retain and verify migration artifacts.

sendKeys accepts literal text only; global SendKeys syntax is rejected. The old physicalClick action is a compatibility alias for message clicks, not physical input. Prefer mouseClick in new scenarios.

## Evidence limits

PrintWindow asks the target to paint into a bitmap; some custom GDI windows may omit content even when the call succeeds. Uniform captures fail with a diagnostic PNG retained. Non-uniform captures still require visual inspection. Early close-all/open calibration hid the original MDI client. Launching the staged document through its supported command-line route now produces visible deck, template and report workspaces. Old hidden-child renders remain diagnostic only. Do not replace this limitation with foreground input or desktop switching. There is no screen-copy or desktop-switch fallback. Do not mark absent custom drawing as a fidelity pass.

The default gallery excludes the still-unverified F9 and Ctrl+F4 message routes. IncludeUnverifiedKeyboardProbes requests those diagnostics explicitly; their failure must not be reported as successful accelerator coverage. The witnessed New dialog Tab/Shift+Tab route and three-pixel designer arrow moves have observable postconditions. Plain movement is used; the original Shift-resize route polls or moves the global cursor and is deliberately excluded.

Posted input is not hardware input. Paths relying on asynchronous hardware-key state or global cursor polling may not respond faithfully; record unsupported routes instead of changing focus or injecting input. Compare actual state changes, persisted results and command effects, not just API return values.

Original and modern dialogs can intentionally differ (branding, modern security, removed modem settings, accessible spacing). Fonts and geometry should be reviewed with recorded DPI and effective font data; identical Win16 glyph rasterization across operating systems is not promised. Matched mailing-label comparisons cover populated addresses and long names with blank address rows. Regression checks cover the designer-to-page origin, connected text advance, empty-row compaction, alignment and physical grid pitch. Native Print to PDF is an opt-in integration test; portable PDF rendering remains part of the normal suite. Report pairing alone is not a fidelity sign-off. No screenshot-only or zero-pixel-difference metric establishes complete behavior coverage.

## Platform references

- [Desktop inheritance and thread connection](https://learn.microsoft.com/en-us/windows/win32/winstation/thread-connection-to-a-desktop)
- [STARTUPINFO desktop selection](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-startupinfow)
- [PrintWindow rendering contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-printwindow)
- [Keyboard input and thread key-state tables](https://learn.microsoft.com/en-us/windows/win32/inputdev/keyboard-input)
- [Keyboard accelerator translation](https://learn.microsoft.com/en-us/windows/win32/menurc/about-keyboard-accelerators)
