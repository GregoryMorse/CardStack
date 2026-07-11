# CardStack Help

CardStack is an open-source card database for modern desktops. It is designed for small structured databases: contacts, inventories, research notes, recipes, collections, report lists, and other information that benefits from both a table view and a comfortable card view.

## Quick start

1. Choose **File > New**.
2. Select a built-in template, design a new template, or pattern a deck after an existing deck.
3. Add cards with **Card > Add** or the toolbar.
4. Use **View > Card** for focused editing and **View > Table** for list editing.
5. Save with **File > Save As**. CardStack stores modern decks as SQLite-backed package files.

## Decks, cards, and data boxes

A deck is a database. A card is one record in that database. A data box is a field, such as Name, Phone, Category, Serial Number, or Notes.

## Creating decks

**File > New** offers four creation routes:

- **New deck from template** creates a ready-to-use deck from the selected built-in template.
- **Design deck from scratch** creates a minimal deck and opens the visual template designer.
- **Design deck patterned after template** starts from a built-in template and opens the designer immediately.
- **Design deck patterned after deck** clones the active deck schema/layout so you can design a related deck.

## Redefining fields and designing templates

**File > Redefine** changes the deck schema: field names, field order, field type, field length, and how existing card data maps into the new schema.

The template designer controls the visual card layout. It can add and edit text labels, data boxes, notes boxes, lines, and boxes. Saving the designer applies the layout to the owning deck. Template packages can be exported for sharing without sending a full deck.

## Editing cards

- **Card > Add** appends a new blank card.
- **Card > Duplicate** copies the current card.
- **Card > Delete** removes the current card and makes it available to undelete.
- **Edit > Undo** restores the previous deck edit when available.
- **Edit > Smart Paste** can paste tabular clipboard text into consecutive fields/cards.

## Searching, replacing, and indexing

**Search > Find** supports field scope, whole-word matching, case sensitivity, phonetic matching, and combined criteria. **Search > Replace** uses the same match controls and can replace the current match or all matches.

**Configure > Change Deck Index** chooses up to three sort/index levels. Each level can sort ascending or descending. Sorting is a deck edit and can be saved with the deck.

## Import, export, and merge

**File > Open** opens modern CardStack packages and imports supported legacy/interchange files. Password-protected legacy data prompts for a password when required.

- **Merge** imports cards from another deck into the active deck using field mappings.
- **Export** writes selected fields/cards to modern interchange formats such as CSV or tab-separated text.
- **Template packages** share layout/schema designs.
- **Report packages** share report definitions independently of a deck.

## Reports and printing

Reports are visual print designs stored with a deck or shared as report packages. Use **File > New Report** or the report manager to create and modify reports.

- Add text frames, data frames, system boxes, lines, and boxes.
- Use form settings to configure card, label, report, or custom page layouts. New page reports default to half-inch margins; imported legacy reports keep their stored margins exactly.
- Use preview before printing to inspect pagination and rendering.
- Save report designs back into the deck or export them as reusable report packages.

## Dialog reference

### Find

The Find dialog searches without changing card data. Choose the text to search for, optionally limit the search to one data box, select the match type, and use whole-word, case-sensitive, or sounds-like matching when needed. A second criterion can be combined with And or Or.

### Replace

Replace uses the same matching controls as Find, then updates either the current match or every match in the deck. Replacement text is still constrained by the destination data box length.

### Change Deck Index

Change Deck Index sorts the deck by up to three data boxes. Each level can be reversed for descending order. The selected index is saved with the deck.

### Merge Mapping

Merge copies cards from another deck into the active deck. Map each source data box to a destination data box; unmapped destination boxes remain empty on merged cards.

### Export

Export writes cards and selected fields to an interchange file such as CSV or tab-separated text. Use the field lists to choose export order and scope.

### Print

The Print dialog selects the report design and card scope. Print Preview uses the same renderer as printing so page breaks, margins, and frame placement can be reviewed before sending output to a printer.

### Available Reports and Save Report Design

Available Reports manages report designs stored in the active deck. Save Report Design names the current report and can replace an existing design or save a new one.

### Report Form and Define Custom Form

Report Form chooses between card, label, page report, and custom form types. Define Custom Form edits page size, orientation, rows, columns, margins, and gutters. Page-report defaults use half-inch margins; card and label forms default to zero internal margins because their form size is already the printable cell.

### Add System Box

System boxes insert generated values such as dates, page numbers, deck names, report names, or card counts. Alignment and text style options affect the generated text frame.

### Colors

The Colors dialog changes deck color roles. On Windows, system palette mode follows desktop control colors; other platforms use the normal Qt palette.

### Security prompts

Legacy password prompts use the old import-compatible eight-character uppercase rules only when reading protected legacy files. Modern deck security is separate and can use stronger rules.

### User name

User name prompts collect display metadata for workflows that need an operator or author name.

## Security and passwords

Legacy password handling is used only for importing protected legacy decks. Modern CardStack security is separate from those legacy limits and should use contemporary password expectations.

## Phone dialing

**Phone > Dial** copies the chosen number and asks the operating system to open a `tel:` link.

## Window management

CardStack uses an MDI workspace. Use the Window menu to cascade, tile, arrange, activate, or close deck/report/template windows.

## Manual visual review

Automated tests cover command routing, decoded dialogs, help buttons, core deck editing, import/export persistence, and designer APIs. Visual fidelity for report rendering, print preview, and detailed layout spacing should still be reviewed manually before release.
