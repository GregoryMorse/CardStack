# CardStack manual GUI acceptance checklist

Use this checklist for end-to-end manual testing once a fresh build is available. It is intentionally workflow-oriented: each item should either pass, fail with a concrete bug, or be explicitly deferred.

## Release stage

- For beta, prioritize confirming automated workflow coverage is trustworthy and manually inspect visual fidelity.
- For 1.0, repeat the full checklist against installed release packages on Windows, macOS, and Linux.
- Track any failure as P0/P1/P2 so beta blockers and 1.0 blockers are obvious.

## Startup and identity

- Launch `CardStack.exe` from the build/deploy folder.
- Confirm the window icon is the CardStack icon at normal and high-DPI scale.
- Open Help > About and confirm the transparent CardStack logo, open-source positioning, and version text.
- Confirm README-visible branding matches the app identity.
- On Windows, confirm the executable icon comes from `src/app/assets/cardstack.ico`.
- On macOS/Linux package builds, confirm the package metadata uses the CardStack PNG icon family or a derived platform icon, not recovered legacy art.

## Main toolbar and menus

- Confirm the main toolbar uses CardStack icons, not default Qt placeholder icons.
- Toggle Configure > Show Button Bar and confirm the toolbar hides/shows.
- Confirm each toolbar action matches its tooltip and menu command:
  - New deck
  - Open deck
  - Save deck
  - Print report
  - Find
  - Replace
  - Add card
  - Duplicate card
  - Delete card
  - Undelete card
  - First/previous/next/last card
  - New report
  - Open report designer
  - Add template text/data/notes/line-box tools

## Deck creation and templates

- Create a deck from each built-in template.
- Confirm field names, field order, field types, and visible card-detail layout.
- Create a deck from scratch.
- Create a deck patterned after an existing template.
- Create a deck patterned after an existing deck.
- Save, close, reopen, and confirm schema/card data persists through SQLite.

## Card/list detail editor

- Add cards from card view.
- Add cards from table/list view.
- Edit every field type visible in the selected template.
- Confirm notes fields accept multi-line text and preserve CR/LF behavior on save/reopen.
- Duplicate a card and confirm copied values.
- Delete a card and confirm undelete behavior.
- Use first/previous/next/last navigation and confirm selection sync between table and detail panel.
- Test field-aware paste and smart paste with tabular clipboard text.

## Search, replace, sort, and merge

- Run basic Find.
- Run Find Next.
- Run field-scoped Find.
- Run Replace current/all where supported.
- Confirm phonetic/sounds-like search behavior on sample names.
- Configure sort profiles and confirm stable order after save/reopen.
- Merge two decks with matching fields.
- Merge two decks with different fields and confirm mapping behavior.

## Legacy import

- Import each WineVDM golden fixture:
  - `plain.BTN`
  - `pwonly.BTN`
  - `crypt.BTN`
  - `notes_heavy.BTN`
  - `many_fields.BTN`
  - `max_lengths.BTN`
  - `security_cycle.BTN`
  - `reports.BTN` with `reports.RPT`
- Confirm password prompts for protected decks.
- Confirm wrong passwords show `ACCESS DENIED: Wrong Password` and re-prompt.
- Confirm correct legacy passwords import and are marked as verified source metadata.
- Confirm encrypted legacy deck records import correctly.
- Confirm imported legacy deck can be saved as modern SQLite and reopened without legacy password prompts unless modern security is explicitly added.

## Modern security

- Add modern security to a SQLite deck.
- Confirm empty passwords and passwords containing spaces are rejected where the recovered compatibility dialog requires that rule.
- Confirm verify-password mismatch is rejected.
- Confirm remove-security requires the correct password.
- Confirm modern app security behavior is clearly separated from migrated Btrieve owner metadata.

## Template designer

- Open the template designer for a built-in template.
- Add text, data box, notes box, and line/box objects from toolbar/menu commands.
- Move and resize objects.
- Edit object text, alignment, font style, line style, fill pattern, and corner radius where applicable.
- Save the template/deck and confirm layout persists.
- Reopen and confirm absolute positions/sizes match the saved design.
- Confirm designer controls use modern CardStack assets or Qt-drawn controls, not legacy bitmap art.

## Report designer and preview

- Create a new report.
- Add text, data box, system box, and line/box frames.
- Move and resize frames.
- Confirm selected frames show a visible resize handle and cursor feedback for both move and resize.
- Test line style, fill style, outline style, alignment, bold/italic/underline, and print-entire-contents behavior.
- Preview the report for one card, all cards, and selected cards.
- Test page navigation in preview.
- Print to PDF or a virtual printer.
- Save, close, reopen, and confirm report metadata and layout persist.
- Load `reports.RPT` from the WineVDM golden fixtures and compare each recovered report preview against the legacy output by visual inspection.
- Confirm the automated `ReportPreviewRendererTests` real `.RPT` smoke check runs when `CARDSTACK_LEGACY_REPORT_SAMPLE` points at the golden report file.

## Import/export

- Export delimited text.
- Import delimited text into a new deck.
- Import delimited text into an existing deck where field mapping is required.
- Confirm non-CSV formats are either implemented or explicitly disabled with helpful messaging.

## Window management and application state

- Open multiple decks/reports.
- Test Cascade, Tile Vertical, Tile Horizontal, Close All.
- Confirm intentionally obsolete Arrange Icons behavior shows the documented Qt-native message.
- Confirm unsaved-change prompts fire for deck edits, template edits, report edits, and security changes.

## Remaining visual QA

- Test at 100%, 150%, and 200% display scaling.
- Confirm toolbar icons remain sharp.
- Confirm the README/about logo blends cleanly on light and non-white backgrounds.
- Confirm card-detail layout is comfortable and not cramped.
- Confirm report designer handles are visible and precise.
- Confirm report preview drawing is suitable for real printed output.
