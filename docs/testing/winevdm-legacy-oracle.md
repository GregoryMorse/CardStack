# WineVDM Legacy Oracle Harness

This harness runs a legacy card database application through WineVDM so CardStack can generate
and preserve golden legacy `.BTN`/`.RPT` migration fixtures.

The oracle requires the fixed WineVDM PR branch/build that synchronizes Win16 common-dialog
`FILEOK` callback state:

`https://github.com/GregoryMorse/winevdm/tree/fix-filedlg-fileok-ofn16-sync`

Other WineVDM builds are intentionally unsupported. The script defaults to:

`.tools\winevdm\pr`

and fails loudly if `otvdmw.exe` is absent instead of downloading a known-broken release.

The runner stages the legacy executable/runtime into an ignored build scratch workspace by default:

`build/winevdm-oracle/fixtures/runtime`

and copies fixture snapshots into sibling `snapshot-*` directories.

## Bootstrap fixed WineVDM

Build or update the fixed WineVDM PR checkout with:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\bootstrap_winevdm_pr.ps1
```

The bootstrap script:

- clones `GregoryMorse/winevdm` branch `fix-filedlg-fileok-ofn16-sync` into `.tools\winevdm\pr\winevdm` if missing
- generates the local `PropertySheet.props`
- uses the installed Windows SDK and VS 2022 toolset
- uses MSYS2 GNU binutils from `C:\msys64`
- skips optional WHPX/flex/bison-dependent tools not needed by the legacy oracle

To build and immediately run the launch smoke fixture:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\bootstrap_winevdm_pr.ps1 -RunSmokeFixture
```

## Inputs

The legacy executable is not stored in this repository. Provide it with one of:

- `-LegacyExe D:\path\to\LEGACY.EXE`
- `$env:CARDSTACK_LEGACY_EXE`
- `$env:CARDSTACK_LEGACY_DIR`, containing the legacy executable
- `scripts\winevdm_legacy_manifest.json`, a sanitized optional local manifest

Keep the legacy runtime files beside the executable; the script copies sibling `.DLL`,
`.INI`, `.HLP`, `.BTN`, `.RPT`, `.BTR`, and `.DAT` files into the staged runtime directory.

## Run

Smoke launch:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\winevdm_buttonfile_oracle.ps1 -Fixture smoke-launch
```

Generate protected deck fixtures:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\winevdm_buttonfile_oracle.ps1 -Fixture plain-minimal-deck,password-only-deck,password-encrypted-deck
```

Calibration mode, one step at a time:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\winevdm_buttonfile_oracle.ps1 -Fixture password-encrypted-deck -Interactive -KeepProcess
```

## Scenario editing

Automation steps live in:

`scripts/winevdm_buttonfile_oracle.scenarios.json`

Supported actions:

- `waitWindow`: waits for a visible WineVDM-hosted window title fragment.
- `command`: posts a recovered `WM_COMMAND` menu ID, such as `2000` for File > New.
- `setText`: sets a recovered dialog control by numeric control ID.
- `click`: clicks a recovered dialog control by numeric control ID.
- `sendKeys`: sends fallback keyboard text through `WScript.Shell.SendKeys`.
- `snapshot`: copies generated legacy files from the staged runtime.
- `assertAnyFile`: fails if expected fixture files were not generated.
- `sleep`: waits for legacy UI/database work.

The first three enabled fixture targets are intentionally high value:

- plain default deck
- password-only owner-protected deck
- password plus legacy data-encryption checkbox deck

The notes-heavy/redefine flow is left disabled until the first WineVDM calibration pass confirms
the exact redefine dialog/control sequence on the local legacy runtime.

## Why this stays outside CardStack runtime

WineVDM is an oracle and fixture generator. CardStack should not depend on WineVDM to import decks.
The fixtures produced here let the Qt/SQLite importer prove compatibility against real legacy output,
including owner-password and encryption cases.
