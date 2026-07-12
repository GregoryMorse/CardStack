# Dialog Visual Audit

The dialog gallery is an opt-in manual inspection aid. It captures every decoded application dialog twice:

- `initial`: the dialog immediately after construction and initialization.
- `wide`: the dialog after combo boxes select their widest item, list boxes select their last item, edits receive representative long values, and spin boxes move to their maximum.

This is intended to catch release-blocking visual issues that pure geometry tests may miss, including awkward spacing, clipped labels, oversized edits, radio-group crowding, bad default selections, and unreadable wide choices.

Toolbar/control-strip resources are intentionally excluded from this gallery. They are exercised through toolbar/menu workflow tests and should be reviewed in the running main window rather than as standalone modal screenshots.

## Generate the gallery

On Windows, run with the normal Qt platform plugin so captured text uses the same font path as the app:

```powershell
$env:CARDSTACK_DIALOG_GALLERY_DIR = "$PWD\build\manual-dialog-gallery"
$env:QT_PLUGIN_PATH = "C:\dev\qt6-install\plugins"
$env:QT_QPA_PLATFORM = "windows"
$env:PATH = "$PWD\build\vs2022-local-qt\Debug\CardStackDeploy;C:\dev\qt6-install\bin;$env:PATH"
.\build\vs2022-local-qt\Debug\CardStackTests.exe --test UiBuilderTests writesManualDialogInspectionImagesWhenConfigured
```

For CI/headless smoke coverage, leave `CARDSTACK_DIALOG_GALLERY_DIR` unset. The test will skip instead of generating files.

## Review checklist

- Dialog titles and primary action buttons are visible.
- Default radio/check states match expected workflow defaults.
- Wide combo-box selections remain readable without colliding with adjacent controls.
- Numeric edits and micro-scroll/spin controls stay compact.
- Group boxes contain their children without clipping labels.
- Help buttons are present only where useful and do not crowd action buttons.
- Platform-removed options, such as obsolete phone/modem hardware controls, stay hidden.
