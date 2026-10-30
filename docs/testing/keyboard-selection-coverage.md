# Keyboard selection coverage

F8 starts continuous selection. Shift+F8 starts discontinuous selection. In continuous mode, F8 exits; in discontinuous mode, Shift+F8 exits. Escape exits either mode without clearing the selected items. F8 and Shift+F8 can also switch between modes.

While a selection mode is active, Tab/Right/Down advance and Shift+Tab/Left/Up move backward. Home/Page Up and End/Page Down move to the first/last item. Continuous selection extends a range. Discontinuous selection moves a separate cursor; Space/Enter toggle that item. Outside these modes, normal editing and designer arrow movement/resizing are unchanged.

Decks select whole fields on the current card. Field navigation stops at boundaries. Shift-click selects a field range; Ctrl-click toggles a field. These modifier clicks preserve an active deck selection mode; an ordinary click exits it. Rebuilding the field schema clears selection state. Copy uses CRLF between selected values. Cut, Clear and Paste change only selected fields, respect field lengths, retain trailing notes, and undo as one operation. Card navigation clears field selections.

Designers navigate frames in top-to-bottom, left-to-right position order, not storage/list order, and wrap at the ends. Continuous ranges include all frames at their endpoint positions. Both canvas and frame-list routes are covered. Mouse selection ends the keyboard mode, preserving normal modifier selection and dragging.

## Automated evidence

- DeckWorkspaceTests: populated/empty card and table views, range and discontinuous modes, boundaries, cancellation, modal isolation, clipboard, field-length constraints, one-step undo and mouse modifiers.
- ReportDesignerWidgetTests and TemplateDesignerWidgetTests: canvas/frame-list routes, spatial ordering, wrap, range shrinking, toggling, retained selection, modal isolation and unchanged document dirty state.
- Workspace teardown is tested with pending edits and both modes; macOS repeats the workspace suite ten times. UI action/workspace/designer suites also run in the sanitizer job.
- MainWindowActionTests: selection-mode Page Down must not navigate cards or fall through to the application navigation action.
- Optional screenshot functions write production-styled images and metadata when CARDSTACK_SELECTION_GALLERY_DIR is set. Native captures run only through the private-desktop runner. They do not synthesize physical input or request OS foreground activation.

Cross-platform execution and native visual results are recorded separately from this test inventory. The inventory alone is not a claim of a successful run.
