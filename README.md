<p align="center">
  <img src="docs/assets/cardstack-logo-wide-transparent.png" alt="CardStack" width="640">
</p>

# CardStack

CardStack is a modern open-source Qt 6 card database application. It uses SQLite
for new files and includes migration support for legacy card/deck data.

License: GPLv3-or-later.

## Features

- Qt 6 Widgets desktop application.
- SQLite-backed deck storage.
- Card and table views with keyboard navigation.
- Visual deck/template and report design workflows.
- Search, replace, sort, merge, import, and report preview/printing paths.
- Legacy migration readers for checked fixture formats including Btrieve decks,
  reports, TakeNote, DBF/DBT, Microsoft Cardfile, and WordPerfect merge files.
- Automated Qt test suite with message-driven GUI coverage.

## Repository layout

```text
src/             Application, core model, storage, UI widgets, and legacy readers
tests/           Unit, migration, storage, and offscreen GUI tests
tests/fixtures/  Canonical public test fixtures
scripts/         Build, deploy, translation, geometry, and fixture-generation helpers
translations/    Qt Linguist translation sources
docs/            Public build/testing notes and product assets
third_party/     Vendored third-party source used by the migration layer
```

## Requirements

- CMake 3.24 or newer.
- C++20 compiler.
- Qt 6 with Widgets, SQL, Test, and PrintSupport modules.
- SQLite Qt SQL driver.

Windows development prefers Visual Studio 2026 and Qt 6.11.x. Visual Studio 2022 remains an explicit fallback.

## Build

If Qt 6 is already installed and discoverable by CMake:

```powershell
cmake -S . -B build/default
cmake --build build/default
```

Using the provided Visual Studio/local Qt preset:

```powershell
cmake --preset vs2026-local-qt
cmake --build --preset vs2026-local-qt
```

To build Qt from source on Windows and configure CardStack against it:

```powershell
scripts\build-qt6-windows.ps1 -Clone -ConfigureCardStack -QtSourceDir C:\dev\qt6 -QtBuildDir C:\dev\qt6-build -QtInstallPrefix C:\dev\qt6-install
```

## Test

CardStack uses one consolidated Qt test executable.

```powershell
ctest --test-dir build\vs2026 -C Debug --output-on-failure
```

Run a single test group directly:

```powershell
build\vs2026\Debug\CardStackTests.exe --test MainWindowActionTests
```

The GUI tests use Qt's offscreen platform and avoid focus-stealing mouse or
keyboard automation.

## Deploy a local Windows test build

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-cardstack-windows.ps1 -BuildDir build\vs2026 -Config Debug -Target CardStack
```

The deployed executable is written beside the build output, for example:

```text
build\vs2026\Debug\CardStackDeploy\CardStack.exe
```

## Regenerate the Windows visual galleries

Gallery generation is intentionally opt-in and is not part of the normal build or CI. The wrapper builds and deploys the application, builds the gallery test harness, discovers the Qt runtime from the build's CMake cache, and regenerates the app, deck, designer, dialog, and report-preview galleries:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\generate-visual-galleries-windows.ps1
```

Use `-SkipBuild` to recapture from an existing build, or pass `-BuildDir`, `-Config`, `-QtRoot`, and `-OutputRoot` when using a non-default local setup.

## CI and release packages

GitHub Actions builds and tests CardStack on Windows, Linux, and macOS using
prebuilt Qt packages. Tagged releases matching `v*` produce downloadable
artifacts:

- Windows: a `.zip` containing `CardStack.exe` and the Qt runtime deployed by
  `windeployqt`.
- macOS: a `.zip` containing `CardStack.app` deployed by `macdeployqt`.
- Linux: a `.zip` containing a Qt-bundled AppImage.

## Legacy migration fixtures

Checked fixtures live in:

```text
tests\fixtures\legacy\winevdm
```

WineVDM helper scripts remain in `scripts/` because they regenerate migration
fixtures. They require a local copy of the original legacy executable, which is
not stored in this repository.

## Third-party code

`third_party/BtrieveFileSaver-Code` is used by the GPL-compatible legacy Btrieve
migration path. See `THIRD_PARTY_NOTICES.md` for details.
