# Building Qt From Source

CardStack can avoid Qt account registration by building open-source Qt from source.
For this project, the practical dependency is `qtbase`: it contains Qt Core, Widgets,
SQL, printing support, and the SQLite SQL driver we need for CardStack 1.0.

## Recommended Policy

- Pin a released Qt tag instead of tracking `dev`.
- Prefer the latest standard-support open-source Qt release for active development.
- Avoid using commercial-only LTS patch releases as the default open-source build target.
- Build only `qtbase` unless a future feature proves another module is required.
- Keep Qt outside the CardStack source tree, usually under `.deps/qt-src`,
  `.deps/qt-build`, and `.deps/qt-install`.
- Cache the installed Qt prefix in CI by OS, compiler, Qt version, and configure flags.
- Point CardStack at the installed Qt with `CMAKE_PREFIX_PATH` or `Qt6_DIR`.

Suggested starting tag:

`6.11.1`, matching the current Qt 6.11 standard-support line. Qt 6.8 LTS remains useful as a compatibility check, but its latest LTS patch stream is commercial-only, so it is not the best default for CardStack's GPL/open-source build path.

## Local Windows Build

Run from an x64 Native Tools Command Prompt for VS 2026 (or the VS 2022 fallback) or a PowerShell session
that has the MSVC environment loaded.

The repo script handles the Visual Studio environment and the bundled Visual
Studio Ninja path automatically. It defaults to `.deps\qt-src`,
`.deps\qt-build`, and `.deps\qt-install`:

```powershell
scripts\build-qt6-windows.ps1 -Clone -ConfigureCardStack
```

For a Qt-docs-style short Windows path, use:

```powershell
scripts\build-qt6-windows.ps1 -Clone -ConfigureCardStack -QtSourceDir C:\dev\qt6 -QtBuildDir C:\dev\qt6-build -QtInstallPrefix C:\dev\qt6-install
```

Without `-Clone`, the script expects an existing Qt checkout at
`.deps\qt-src`. By default it builds into `.deps\qt-build`, installs into
`.deps\qt-install`, then configures CardStack with the `vs2026-local-qt` preset and
`CMAKE_PREFIX_PATH` pointing at that install.

```powershell
git clone https://code.qt.io/qt/qt5.git .deps/qt-src
cd .deps/qt-src
git switch --detach 6.11.1

cd ../..
mkdir .deps/qt-build
cd .deps/qt-build
../qt-src/configure.bat -prefix ../qt-install -release -opensource -confirm-license -nomake examples -nomake tests -submodules qtbase -init-submodules
cmake --build . --parallel
cmake --install .
```

Then configure CardStack:

```powershell
cmake --preset vs2026-local-qt
cmake --build --preset vs2026-local-qt
```

## Running The Windows Build

CardStack runs from the build output directory after Qt deployment. On Windows,
the app target runs `windeployqt` after each successful build by default:

```powershell
cmake --build build\vs2026 --config Debug
.\build\vs2026\Debug\CardStackDeploy\CardStack.exe
```

The deployment behavior is controlled by:

```powershell
cmake -S . -B build\vs2026 -DCARDSTACK_DEPLOY_QT_AFTER_BUILD=ON
cmake --build build\vs2026 --config Debug --target CardStackDeploy
```

The deployed app is intentionally placed in `CardStackDeploy` instead of the
shared CMake `Debug` directory so test executables do not accidentally load the
app-deployed Qt DLLs/plugins.

The default `CARDSTACK_WINDEPLOYQT_MODE=release` matches the recommended
release-only Qt-from-source install. If you build a debug Qt install with debug
Qt DLLs/plugins, configure with `-DCARDSTACK_WINDEPLOYQT_MODE=debug`; use
`auto` to let `windeployqt` infer from the executable.

For distributable Windows zips/installers, build from an x64 Native Tools
Command Prompt so `VCINSTALLDIR` is set and `windeployqt` can also find the
MSVC runtime files. If `VCINSTALLDIR` is missing, the build-tree app will still
get Qt DLLs/plugins copied beside `CardStack.exe`, but a clean machine may also
need the Microsoft Visual C++ Redistributable installed.

## Local Linux Build

Install compiler/build dependencies first. Debian/Ubuntu need packages such as
`build-essential`, `cmake`, `ninja-build`, `python3`, `git`, OpenGL headers,
fontconfig/freetype headers, and XCB/X11 development libraries.

```sh
git clone https://code.qt.io/qt/qt5.git .deps/qt-src
cd .deps/qt-src
git switch --detach 6.11.1

cd ../..
mkdir -p .deps/qt-build
cd .deps/qt-build
../qt-src/configure -prefix ../qt-install -release -opensource -confirm-license -nomake examples -nomake tests -submodules qtbase -init-submodules
cmake --build . --parallel
cmake --install .

cmake --preset default -DCMAKE_PREFIX_PATH="$PWD/../qt-install"
cmake --build --preset default
```

## Local macOS Build

Use Xcode command line tools, CMake, Ninja, Python, and Git.

```sh
git clone https://code.qt.io/qt/qt5.git .deps/qt-src
cd .deps/qt-src
git switch --detach 6.11.1

cd ../..
mkdir -p .deps/qt-build
cd .deps/qt-build
../qt-src/configure -prefix ../qt-install -release -opensource -confirm-license -nomake examples -nomake tests -submodules qtbase -init-submodules
cmake --build . --parallel
cmake --install .

cmake --preset default -DCMAKE_PREFIX_PATH="$PWD/../qt-install"
cmake --build --preset default
```

## CI Sketch

The CI workflow should have two logical jobs.

1. Build or restore Qt:
   - key: `${{ runner.os }}-${{ matrix.compiler }}-qt-6.11.1-qtbase-release`
   - restore `.deps/qt-install`
   - if missing, clone Qt, switch to the pinned tag, configure `qtbase`, build,
     install, and save the cache

2. Build CardStack:
   - configure with `-DCMAKE_PREFIX_PATH=${{ github.workspace }}/.deps/qt-install`
   - build with CMake
   - run tests when test targets exist
   - package per OS later:
     - Windows: `windeployqt`, then CPack/NSIS or WiX
     - macOS: bundle plus `macdeployqt`
     - Linux: AppImage/Flatpak or distro packages

Minimal workflow shape:

```yaml
name: build

on:
  push:
  pull_request:

jobs:
  build:
    strategy:
      fail-fast: false
      matrix:
        os: [windows-latest, ubuntu-latest, macos-latest]
        qt-version: ["6.11.1"]
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4

      - name: Restore Qt cache
        uses: actions/cache@v4
        with:
          path: .deps/qt-install
          key: ${{ runner.os }}-qt-${{ matrix.qt-version }}-qtbase-release

      - name: Build Qt from source on cache miss
        shell: bash
        run: |
          test -d .deps/qt-install && exit 0
          git clone https://code.qt.io/qt/qt5.git .deps/qt-src
          git -C .deps/qt-src switch --detach ${{ matrix.qt-version }}
          cmake -E make_directory .deps/qt-build
          cd .deps/qt-build
          ../qt-src/configure -prefix ../qt-install -release -opensource -confirm-license -nomake examples -nomake tests -submodules qtbase -init-submodules
          cmake --build . --parallel
          cmake --install .

      - name: Configure CardStack
        run: cmake -S . -B build -DCMAKE_PREFIX_PATH=${{ github.workspace }}/.deps/qt-install

      - name: Build CardStack
        run: cmake --build build --parallel
```

The Windows Qt build step will probably need a dedicated `cmd`/PowerShell variant
that runs inside the Visual Studio developer environment. Keep the high-level job
shape the same, but split OS-specific shell details when the workflow is made real.

## References

- Qt Wiki: Building Qt 6 from Git
- Qt docs: Building Qt Sources
- Qt docs: Qt for Linux requirements

## Phase 7 translation and deployment notes

The current minimal Qt-from-source route did not use `-skip qttranslations` or `-skip qttools`; it used a narrow Qt module set. In practice, that means a `qtbase`-focused build can be good enough for CardStack development while still lacking Qt's translation catalog metadata.

If `windeployqt` warns that `translations/catalogs.json` is missing, that is expected for a minimal Qt build without `qttranslations`. CardStack currently supports an English-first packaging mode, but a multilingual release build should add:

- `qttools`, for Qt Linguist tools such as `lupdate` and `lrelease`.
- `qttranslations`, for Qt's own translated runtime catalogs.

CardStack's CMake deployment path defaults to English-only bundles and passes `--no-translations` to `windeployqt`. For a multilingual bundle, configure CardStack with:

```powershell
cmake -S . -B build\vs2026 -DCARDSTACK_DEPLOY_QT_TRANSLATIONS=ON
```

Then install Qt translations into the Qt prefix used by the build:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\scripts\install-qt6-translations-windows.ps1 -QtSourceDir C:\dev\qt6 -QtBuildDir C:\dev\qt6-build -QtInstallDir C:\dev\qt6-install -BuildQtTools
```

If the existing Qt build tree was configured with only `-submodules qtbase`, the `qttranslations` target may not exist yet. In that case, reconfigure or rebuild Qt with `qttranslations` included instead of only updating the git submodule.

For the current Windows source tree, the generated Ninja target is `qttranslations/install`, not plain `qttranslations`. If an incremental build fails with stale precompiled-header errors after switching Visual Studio toolchain versions or expanding the Qt module set, use a fresh Qt build directory for the expanded configuration. That is safer than deleting build artifacts piecemeal from an existing Qt tree.

The `VCINSTALLDIR` warning is not fundamental. It means the deployment step was run outside a Visual Studio developer environment, so `windeployqt` may not find and bundle the MSVC runtime. For direct CardStack rebuilds from a plain PowerShell prompt, use:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\scripts\build-cardstack-windows.ps1 -BuildDir build\vs2026 -Config Debug -Target CardStack
```

That wrapper calls `VsDevCmd.bat` before invoking CMake.
