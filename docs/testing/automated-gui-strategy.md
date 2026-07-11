# Phase 7 Automated GUI Strategy

CardStack is now past the recovery-scaffolding stage: the remaining confidence gap is not whether the old Win16 facts exist, but whether the modern Qt app exercises them repeatably.

## Automated first

These checks should be covered by Qt Test or small helper tools before the final manual pass:

- GUI automation should be message-driven: invoke `QAction::trigger()`, signals/slots, model APIs, and direct widget state inspection rather than physical keyboard or mouse input.
- Tests should prefer `QT_QPA_PLATFORM=offscreen` whenever possible so they can run headlessly without stealing focus from the user's desktop.
- Close/save prompts should be handled through modal-dialog object inspection and direct standard-button clicks, not keyboard shortcuts. Tests should assert the state consequences of `Save`, `Discard`, and `Cancel` where each path is deterministic.
- Main-window tests should cover command exposure, toolbar contents, enabled-state rules, and safe shell actions. Designer internals should be tested at the widget level unless a purpose-built non-modal MDI harness is added.
- Menu coverage: every supported command in `command-coverage.md` has a QAction, stable command id, expected shortcut, expected enabled/disabled state, and a routed handler.
- Toolbar coverage: every primary action has a toolbar affordance, stable icon name, tooltip, and matching command route.
- Dialog construction: every decoded dialog can be instantiated through `UiBuilder`, has expected control ids, expected control classes, and translated user-visible text.
- Dialog geometry smoke tests: decoded Win16 dialog units produce stable Qt positions and sizes, with tolerance for font and platform differences.
- Deck workflow smoke tests: create, open, save, import, export, field edit, card edit, search, sort, merge, and security prompts run without relying on mouse focus.
- Template designer tests: add text/data/notes/line/box frames, edit geometry/style, save template, reload template, and apply template to a deck.
- Report designer tests: add/edit text/data/system/line frames, save report, reload report, preview pages, and invoke print-preview routing without requiring a real printer.
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
