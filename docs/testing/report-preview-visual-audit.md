# Report Preview Visual Audit

This note tracks manual inspection of generated report-preview images. The gallery is intended as a human review aid for spacing, clipping, font fallback, label grids, and line/fill fidelity.

## How to generate the gallery

Run the `ReportPreviewRendererTests` suite with `CARDSTACK_REPORT_PREVIEW_IMAGE_DIR` set to a scratch output directory.

On Windows, prefer the normal desktop Qt platform for manual gallery generation. The offscreen platform is useful for CI, but it can render text through a reduced font path and make otherwise valid text look like missing-glyph boxes.

Example:

```powershell
$env:CARDSTACK_REPORT_PREVIEW_IMAGE_DIR = "$PWD\build\manual-report-preview-gallery"
$env:QT_PLUGIN_PATH = "C:\dev\qt6-install\plugins"
$env:QT_QPA_PLATFORM = "windows"
$env:PATH = "$PWD\build\vs2022-local-qt\Debug\CardStackDeploy;C:\dev\qt6-install\bin;$env:PATH"
.\build\vs2022-local-qt\Debug\CardStackTests.exe --test ReportPreviewRendererTests writesManualInspectionPreviewImagesWhenConfigured
```

## Findings from the current gallery

Resolved:

- Card/label grid pagination now treats label/card report dimensions as across-then-down, so 2-across by 10-down labels render as two columns and ten rows.
- Built-in and imported report frame coordinates now scale into the modern mil-based form size instead of being pinned into a tiny upper-left area.
- Report text now has a safe font-family fallback and is capped to the available frame height, avoiding missing-glyph output and clipped tops in normal desktop rendering.
- Line and fill style previews render distinct solid, dash, dot, dash-dot, hairline, thick, clear, solid, density, hatch, grid, and trellis-style approximations.
- `Mailing List > Mailing Labels` now interprets old-format empty-text frames as address data fields where the resource encodes field identity in the frame metadata.

Needs follow-up inspection:

- Very small label presets such as 3-by-15/16-inch address labels are readable at full image size but are intentionally dense. Manual print-preview and PDF checks should decide whether the preview should default to a larger zoom for comfort.
- Several page reports intentionally occupy the top band of a full page. This appears structurally correct, but manual preview should confirm that page zoom and page centering make them comfortable to inspect.

Recommended manual review set:

- Style matrix for line/fill fidelity.
- Credit card information for text/font fallback and header fill.
- Home inventory 3-by-5 laser cards for multi-card layout.
- Mailing labels for legacy preset correctness.
- Address label presets for dense-label readability.
