# Release validation

## Automated regression gate

Build `CardStack` and `CardStackTests`, then run CTest with
`--output-on-failure`. For a multi-configuration build, include `-C Release`.
Tests use disposable files and direct Qt events, not physical keyboard or mouse
input. `CardStackStartupSmoke` runs the application executable as well as the
consolidated test suites.

## Packaged application check

Run `CardStack --smoke-test` from the deployment directory. The check renders
every packaged PNG/SVG icon and a hidden widget, then creates, writes and reads
an in-memory SQLite table. It does not construct the main window, restore a
session, open decks or modify application preferences.

For an isolated Windows check, use:

```powershell
scripts/test-deployed-cardstack-windows.ps1 -DeployDir build/vs2026-local-qt/Release/CardStackDeploy
```

The runner removes development Qt paths from the child environment, checks its
exit code within a bounded timeout, and confirms the platform, SQLite and SVG
plugins load from the deployment. It does not change the caller's environment
or use physical keyboard/mouse input. Diagnostic logs go to
`build/deployment-smoke` by default. Windows release packaging runs this check
before creating its zip.

Qt SVG is required, not optional. Include `qtsvg` with `qtbase` when building Qt
from source. Deployment tools must include its SVG icon engine alongside the
platform and SQLite plugins. A headless runner may use
`QT_QPA_PLATFORM=offscreen` only if that plugin is present in the package being
tested.

## Designer and display-scale checks

Designer regressions check keyboard movement/resizing, all resize handles,
marquee/group selection, clipboard placement, scrolling, font fallback and
unchanged document geometry after navigation. Run native platform probes at
100%, 150% and 200% scaling before visual sign-off; assert actual widget DPR so a
misconfigured environment cannot silently mislabel the screenshots.

`CARDSTACK_GRID_GALLERY_DIR`, `CARDSTACK_GROUP_GALLERY_DIR` and
`CARDSTACK_VIEWPORT_GALLERY_DIR` enable opt-in screenshots in the corresponding
designer tests. These images use the application appearance and must be reviewed
for text, property panels, toolbar state, selection handles and clipping.

Preserve stored font requests and geometry. Where a requested font is absent,
validate the Qt substitute and required glyph coverage rather than claiming
identical cross-platform rasterization.

## Publishing gate

Require green Windows, Linux and macOS CI and packaging jobs for the exact
release commit. Local Windows evidence alone is not cross-platform release
sign-off. Do not replace a release tag or its downloads without an explicit
release decision.
