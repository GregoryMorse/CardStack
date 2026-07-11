# Phase 7 Automated GUI Strategy

CardStack is now past the recovery-scaffolding stage: the remaining confidence gap is not whether the old Win16 facts exist, but whether the modern Qt app exercises them repeatably.

## Release confidence gates

### Beta gate

CardStack can move to `v0.2.0-beta.1` when automated tests cover the normal user workflows and the remaining manual pass is mostly visual judgment:

- File workflows: New, Open, Save, Save As, Close, Close All, Exit, and unsaved-change prompts with Save/Discard/Cancel behavior.
- Deck workflows: card view default, editable blank-card behavior, add/delete/undelete/duplicate cards, table/card synchronization, index bar navigation, search, replace, sort, merge, and security add/remove.
- Template workflows: create from template, create from scratch, pattern after template, pattern after deck file, edit layout, save, close, use template to create deck, save/reopen.
- Report workflows: Available Reports always opens, Add Defaults, New, Modify, Delete, Undo Del, report designer frame creation/editing, preview routing, save/reopen.
- Print workflows: report output uses a deterministic PDF target for automation, with native print dialogs reserved for manual smoke testing.
- Phone workflows: configure dialing defaults/prefixes/quick dials, then verify Phone > Dial reflects configured data and current-card phone fields.
- Legacy workflows: golden fixtures import deterministically, including password/encryption, notes-heavy, many-fields, reports/sidecars, and supported interchange formats.
- Packaging workflows: CI builds and release packages pass on Windows, macOS, and Linux.

Current automated status:

- Covered by `MainWindowActionTests`: startup/deck/designer menu and toolbar routing, startup checked menu state for the button bar, dynamic MDI window list, view-mode checkmarks and navigation shortcuts, safe shell/help/about routes, close prompts for dirty child windows, new-deck design routes, open/save/save-as, modern security prompt on open, CSV import, merge, redefine/template-designer persistence, report form presets, custom Define Form state and applied dimensions, report save-as, and dirty report-designer close behavior.
- Covered by `MainWindowWorkflowTests`: Close All prompt sequencing, app Exit prompt sequencing, dirty deck close-with-save persistence, MDI cascade/tile/arrange command reachability, Available Reports opening on empty report lists, Add Defaults, Delete/Undo Del, report manager New/Modify, report print-preview routing from Available Reports through the recovered Print dialog, report designer text/data/system/line tool routing with customized text/data/line-frame attributes and save, modern security add/remove with encrypted flag and wrong-password retry, Find/Replace/Replace All/Index dialog workflows, design-from-scratch close/use-template flow, pattern-after-deck-file flow, phone quick-dial configuration feeding Phone > Dial, current-card phone numbers with configured dialing prefixes, and persistence of modern phone defaults/prefixes/local-area/log-call settings.
- Covered by widget/core/storage/legacy suites: blank editable card behavior, add/duplicate/delete/undelete card lifecycle behavior, last-card delete behavior, undelete, search operator/direction semantics, title sort refresh, deck merge, template definitions, recovered built-in template report presets, decoded dialog/resource construction, color role/system-palette dialog state, initial radio-button defaults, no-overlap/out-of-bounds dialog geometry smoke checks, combobox popup width/row geometry smoke checks, modernized phone/print/new-dialog sizing regressions, report pagination/rendering, deterministic single-page and multi-page PDF rendering through `QPdfWriter`, SQLite deck/package persistence, and WineVDM fixture import smoke checks when fixture environment variables are present.
- Remaining beta verification is intentionally manual/visual: report preview fidelity against golden output, printed/PDF visual quality, card-stack comfort at display scaling, native file/print/phone integration smoke checks, and installed package smoke testing on each OS.

### 1.0 gate

CardStack can move to `v1.0.0` when beta has survived real use and:

- No known data-loss bugs remain.
- All P0/P1 beta bugs are closed.
- Legacy import/export support is documented and fixture-tested.
- Report preview/PDF output has passed manual visual comparison.
- Template/card layout has passed manual visual comparison.
- Cross-platform release packages have been installed and smoke-tested outside the build tree.
- Help and README content are sufficient for a normal user who has no project history context.

## Automated first

These checks should be covered by Qt Test or small helper tools before the final manual pass:

- GUI automation should be message-driven: invoke `QAction::trigger()`, signals/slots, model APIs, and direct widget state inspection rather than physical keyboard or mouse input.
- Tests should prefer `QT_QPA_PLATFORM=offscreen` whenever possible so they can run headlessly without stealing focus from the user's desktop.
- Close/save prompts should be handled through modal-dialog object inspection and direct standard-button clicks, not keyboard shortcuts. Tests should assert the state consequences of `Save`, `Discard`, and `Cancel` where each path is deterministic.
- Main-window tests should cover command exposure, toolbar contents, enabled-state rules, and safe shell actions. Designer internals should be tested at the widget level unless a purpose-built non-modal MDI harness is added.
- Menu coverage: every supported command in `command-coverage.md` has a QAction, stable command id, expected shortcut, expected enabled/disabled state, and a routed handler.
- Toolbar coverage: every primary action has a toolbar affordance, stable icon name, tooltip, and matching command route.
- Dialog construction: every decoded dialog can be instantiated through `UiBuilder`, has expected control ids, expected control classes, and translated user-visible text.
- Dialog geometry smoke tests: decoded Win16 dialog units produce stable Qt positions and sizes, with tolerance for font and platform differences; visible controls must stay within their dialog and avoid material sibling overlap except for expected group-box/frame containment. Populated comboboxes also need popup width and visible-row capacity checked separately from the collapsed control geometry.
- Deck workflow smoke tests: create, open, save, import, export, field edit, card edit, search, sort, merge, and security prompts run without relying on mouse focus.
- Template designer tests: add text/data/notes/line/box frames, edit geometry/style, save template, reload template, and apply template to a deck.
- Report designer tests: add/edit text/data/system/line frames, save report, reload report, preview pages, and invoke print-preview routing without requiring a real printer.
- Report preview image gallery: set `CARDSTACK_REPORT_PREVIEW_IMAGE_DIR` while running `ReportPreviewRendererTests` to write a PNG gallery of the line/fill style matrix and built-in report presets for manual visual inspection.
- Legacy importer tests: golden `.BTN`, report, template, text, cardfile, and WordPerfect fixtures import into deterministic core models.
- Packaging smoke tests: a deployed Windows bundle starts, finds Qt plugins, loads icons, and reports useful diagnostics when optional translations are absent.

## Manual by design

These are still best handled by focused human review, because pixel-perfect acceptability depends on taste, printer drivers, and display scaling:

- Report preview and print output compared with WineVDM-generated golden reports.
- Template/card layout comfort compared with the legacy built-in templates.
- Toolbar icon readability at 100%, 150%, and 200% scaling.
- About box/logo transparency on light and dark desktop themes.
- Native file dialog, print dialog, and phone-link behavior on Windows, macOS, and Linux.

Keyboard/mouse scripting is intentionally reserved for legacy-oracle work such as WineVDM fixture generation. CardStack's modern tests should not move the user's pointer, type into the active desktop, or require foreground focus.

## WinAPI-assisted review

For Windows manual passes, a helper can query HWND geometry and class names with `EnumChildWindows`, `GetWindowRect`, and `GetClassNameW`. This gives an objective control-position table while still leaving visual judgment to the reviewer.

Recommended output columns:

| Window | Control text | Class | X | Y | Width | Height | Enabled | Visible |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |

## Translation readiness target

The app should be considered translation-ready when:

- User-visible strings use `tr()` or `QCoreApplication::translate()`.
- Non-user-visible keys, resource ids, SQL identifiers, and fixture names remain stable ASCII literals.
- Qt Linguist extraction is wired through CMake when `Qt6::LinguistTools` is available.
- Release builds either deploy `qttranslations` or intentionally disable Qt catalog deployment without noisy warnings.
- Magic constants in UI, import, report, and storage code are named constants or scoped enums.
