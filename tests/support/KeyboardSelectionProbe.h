#pragma once

#include "DeckWorkspace.h"
#include "ReportDesignerWidget.h"
#include "TemplateDesignerWidget.h"
#include "VisualTestSupport.h"
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLabel>
#include "DesignerSelection.h"
#include "DesignerFrameTable.h"
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScopeGuard>
#include <QPointer>
#include <QSignalSpy>
#include <QTableView>
#include <QTableWidget>
#include <QTest>

namespace CardStack::Tests::KeyboardSelectionProbe {

inline void activate(QWidget& widget)
{
    widget.resize(1040, 740);
    widget.show();
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QApplication::setActiveWindow(&widget); // Qt state only; no OS foreground/input API.
    QT_WARNING_POP
    QCoreApplication::processEvents();
}

inline void key(QWidget* target, int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QKeyEvent overrideEvent(QEvent::ShortcutOverride, code, modifiers);
    QCoreApplication::sendEvent(target, &overrideEvent);
    QKeyEvent press(QEvent::KeyPress, code, modifiers);
    QCoreApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, code, modifiers);
    QCoreApplication::sendEvent(target, &release);
    QCoreApplication::processEvents();
}

inline Deck deck(bool empty = false)
{
    Deck value(QStringLiteral("Field selection"));
    value.addField(FieldDefinition(QStringLiteral("Name"), FieldType::Text, 64));
    value.addField(FieldDefinition(QStringLiteral("Category"), FieldType::Text, 64));
    value.addField(FieldDefinition(QStringLiteral("Reference"), FieldType::Text, 10));
    value.addField(FieldDefinition(QStringLiteral("Notes"), FieldType::Notes, 1024));
    if (!empty) {
        value.addCard(CardRecord({QStringLiteral("Alpha"), QStringLiteral("Beta"), QStringLiteral("Gamma"), QStringLiteral("Memo")}));
        value.addCard(CardRecord({QStringLiteral("Zulu"), QStringLiteral("Other"), QStringLiteral("Last"), QStringLiteral("Untouched")}));
    }
    return value;
}

inline void clickField(DeckWorkspace& workspace, int field, Qt::KeyboardModifiers modifiers)
{
    QWidget* target = nullptr;
    QPoint point;
    if (workspace.viewMode() == DeckWorkspace::ViewMode::Table) {
        auto* table = workspace.findChild<QTableView*>();
        target = table->viewport();
        point = table->visualRect(table->model()->index(workspace.currentCardIndex(), field)).center();
    } else {
        target = workspace.findChild<QWidget*>(QStringLiteral("fieldValue_%1").arg(field));
        if (auto* edit = qobject_cast<QPlainTextEdit*>(target)) target = edit->viewport();
        point = target->rect().center();
    }
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(point), QPointF(target->mapToGlobal(point)),
                      Qt::LeftButton, Qt::LeftButton, modifiers);
    QCoreApplication::sendEvent(target, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(point), QPointF(target->mapToGlobal(point)),
                        Qt::LeftButton, Qt::NoButton, modifiers);
    QCoreApplication::sendEvent(target, &release);
    QCoreApplication::processEvents();
}

inline void deckModes(bool tableMode, bool empty)
{
    DeckWorkspace workspace(deck(empty));
    activate(workspace);
    if (tableMode) workspace.showTableView();
    workspace.focusFieldBoundary(DeckWorkspace::FieldBoundary::First);
    QWidget* target = tableMode ? static_cast<QWidget*>(workspace.findChild<QTableView*>()) : &workspace;
    const int cards = workspace.deck().cardCount();
    key(target, Qt::Key_F8);
    QCOMPARE(workspace.fieldSelectionMode(), KeyboardSelection::Mode::Continuous);
    QCOMPARE(workspace.selectedFieldIndexes(), QVector<int>({0}));
    key(target, Qt::Key_Tab);
    QCOMPARE(workspace.selectedFieldIndexes(), QVector<int>({0, 1}));
    key(target, Qt::Key_Right);
    QCOMPARE(workspace.selectedFieldIndexes(), QVector<int>({0, 1, 2}));
    key(target, Qt::Key_Left);
    QCOMPARE(workspace.selectedFieldIndexes(), QVector<int>({0, 1}));
    key(target, Qt::Key_PageDown);
    QCOMPARE(workspace.selectedFieldIndexes(), QVector<int>({0, 1, 2, 3}));
    QCOMPARE(workspace.currentCardIndex(), 0);
    key(target, Qt::Key_PageUp);
    QCOMPARE(workspace.selectedFieldIndexes(), QVector<int>({0}));
    key(target, Qt::Key_Up);
    QCOMPARE(workspace.selectedFieldIndexes(), QVector<int>({0})); // Fields do not wrap.
    key(target, Qt::Key_End);
    key(target, Qt::Key_F8, Qt::ShiftModifier);
    QCOMPARE(workspace.fieldSelectionMode(), KeyboardSelection::Mode::Discontinuous);
    key(target, Qt::Key_Space);
    QCOMPARE(workspace.selectedFieldIndexes(), QVector<int>({0, 1, 2}));
    key(target, Qt::Key_Home);
    QCOMPARE(workspace.selectedFieldIndexes(), QVector<int>({0, 1, 2}));
    key(target, Qt::Key_Return);
    QCOMPARE(workspace.selectedFieldIndexes(), QVector<int>({1, 2}));
    key(target, Qt::Key_F8, Qt::ShiftModifier);
    QCOMPARE(workspace.fieldSelectionMode(), KeyboardSelection::Mode::None);
    QCOMPARE(workspace.selectedFieldIndexes(), QVector<int>({1, 2}));
    QCOMPARE(workspace.deck().cardCount(), cards);
    QVERIFY(!workspace.isDirty());
    QDialog modal(&workspace);
    modal.setWindowModality(Qt::ApplicationModal);
    modal.show();
    QCoreApplication::processEvents();
    key(target, Qt::Key_F8);
    QCOMPARE(workspace.fieldSelectionMode(), KeyboardSelection::Mode::None);
    modal.reject();
    QCoreApplication::processEvents();
    if (empty) {
        key(target, Qt::Key_Delete);
        QVERIFY(!workspace.isDirty());
        QVERIFY(!workspace.canUndo());
        return;
    }
    const QString previousClipboard = QApplication::clipboard()->text();
    const auto restoreClipboard = qScopeGuard([&] { QApplication::clipboard()->setText(previousClipboard); });
    key(target, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("Beta\r\nGamma"));
    key(target, Qt::Key_X, Qt::ControlModifier);
    QCOMPARE(workspace.deck().cardAt(0).valueAt(0), QStringLiteral("Alpha"));
    QVERIFY(workspace.deck().cardAt(0).valueAt(1).isEmpty());
    QVERIFY(workspace.deck().cardAt(0).valueAt(2).isEmpty());
    QCOMPARE(workspace.deck().cardAt(0).valueAt(3), QStringLiteral("Memo"));
    key(target, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(workspace.deck().cardAt(0).valueAt(1), QStringLiteral("Beta"));
    QCOMPARE(workspace.deck().cardAt(0).valueAt(2), QStringLiteral("Gamma"));
    QCOMPARE(workspace.fieldSelectionMode(), KeyboardSelection::Mode::None);
    workspace.focusFieldBoundary(DeckWorkspace::FieldBoundary::First);
    clickField(workspace, 2, Qt::ShiftModifier);
    QCOMPARE(workspace.selectedFieldIndexes(), QVector<int>({0, 1, 2}));
    clickField(workspace, 1, Qt::ControlModifier);
    QCOMPARE(workspace.selectedFieldIndexes(), QVector<int>({0, 2}));
    QApplication::clipboard()->setText(QStringLiteral("Omega\r\n123456789012345"));
    key(target, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(workspace.deck().cardAt(workspace.currentCardIndex()).valueAt(0), QStringLiteral("Omega"));
    QCOMPARE(workspace.deck().cardAt(workspace.currentCardIndex()).valueAt(1), QStringLiteral("Beta"));
    QCOMPARE(workspace.deck().cardAt(workspace.currentCardIndex()).valueAt(2), QStringLiteral("1234567890"));
    workspace.undo();
    QCOMPARE(workspace.deck().cardAt(0).valueAt(0), QStringLiteral("Alpha"));
    QCOMPARE(workspace.deck().cardAt(1).valueAt(3), QStringLiteral("Untouched"));
    workspace.focusFieldBoundary(DeckWorkspace::FieldBoundary::First);
    key(target, Qt::Key_F8);
    key(target, Qt::Key_End);
    key(target, Qt::Key_Escape);
    QCOMPARE(workspace.fieldSelectionMode(), KeyboardSelection::Mode::None);
    QCOMPARE(workspace.selectedFieldIndexes().size(), 4);
    workspace.nextCard();
    QVERIFY(workspace.selectedFieldIndexes().isEmpty());
    QVERIFY(!workspace.findChild<QWidget*>(QStringLiteral("fieldValue_0"))->property("wholeFieldSelected").toBool());
}

inline ReportDefinition report()
{
    ReportDefinition result;
    result.name = QStringLiteral("Keyboard selection");
    result.formWidth = 6500;
    result.formHeight = 4500;
    for (const QRect bounds : {QRect(400, 1300, 2300, 300), QRect(2700, 400, 2300, 300), QRect(400, 400, 1800, 300)}) {
        ReportFrameDefinition frame;
        frame.kind = ReportFrameKind::Text;
        frame.bounds = bounds;
        frame.text = result.frames.size() == 0 ? QStringLiteral("Third position")
            : result.frames.size() == 1 ? QStringLiteral("Second position") : QStringLiteral("First position");
        result.frames.append(frame);
    }
    return result;
}

inline CardTemplateLayout layout()
{
    CardTemplateLayout result;
    result.canvasWidth = 6500;
    result.canvasHeight = 4500;
    for (const auto& source : report().frames)
        result.frames.append({CardTemplateFrameKind::Text, source.bounds, source.text, -1});
    return result;
}

template <class Designer>
void designerModes(Designer& designer, bool tableMode, const QString& canvasName)
{
    activate(designer);
    auto* table = designer.template findChild<QTableWidget*>();
    auto* canvas = designer.template findChild<QWidget*>(canvasName);
    QVERIFY(table && canvas);
    table->setCurrentCell(2, 0);
    table->selectRow(2);
    QWidget* target = tableMode ? static_cast<QWidget*>(table) : canvas;
    target->setFocus();
    key(target, Qt::Key_F8);
    auto* summary = designer.template findChild<QLabel*>(QStringLiteral("designerSelectionSummary"));
    QVERIFY(summary && summary->isVisible());
    QVERIFY(summary->text().startsWith(QStringLiteral("Continuous selection:")));
    QCOMPARE(designer.selectedFrameIndices(), QVector<int>({2}));
    key(target, Qt::Key_Tab);
    QCOMPARE(designer.selectedFrameIndices(), QVector<int>({1, 2}));
    QCOMPARE(canvas->property("keyboardSelectionCursor").toInt(), 1);
    key(target, Qt::Key_PageDown);
    QCOMPARE(designer.selectedFrameIndices(), QVector<int>({0, 1, 2}));
    key(target, Qt::Key_Home);
    QCOMPARE(designer.selectedFrameIndices(), QVector<int>({2}));
    key(target, Qt::Key_Up); // Frame navigation wraps in spatial, not storage, order.
    QCOMPARE(designer.selectedFrameIndices(), QVector<int>({0, 1, 2}));
    key(target, Qt::Key_Escape);
    QCOMPARE(canvas->property("keyboardSelectionMode").toInt(), int(KeyboardSelection::Mode::None));
    key(target, Qt::Key_F8, Qt::ShiftModifier);
    QVERIFY(summary->text().startsWith(QStringLiteral("Discontinuous selection:")));
    key(target, Qt::Key_Space);
    QCOMPARE(designer.selectedFrameIndices(), QVector<int>({1, 2}));
    key(target, Qt::Key_Up);
    QCOMPARE(designer.selectedFrameIndices(), QVector<int>({1, 2}));
    QCOMPARE(canvas->property("keyboardSelectionCursor").toInt(), 1);
    if (tableMode) QCOMPARE(table->currentRow(), 1);
    key(target, Qt::Key_Return);
    QCOMPARE(designer.selectedFrameIndices(), QVector<int>({2}));
    key(target, Qt::Key_Tab);
    key(target, Qt::Key_Return);
    QCOMPARE(designer.selectedFrameIndices(), QVector<int>({2, 0}));
    key(target, Qt::Key_F8, Qt::ShiftModifier);
    QCOMPARE(canvas->property("keyboardSelectionMode").toInt(), int(KeyboardSelection::Mode::None));
    QVERIFY(!designer.isDirty());
    QDialog modal(&designer);
    modal.setWindowModality(Qt::ApplicationModal);
    modal.show();
    QCoreApplication::processEvents();
    key(target, Qt::Key_F8);
    QCOMPARE(canvas->property("keyboardSelectionMode").toInt(), int(KeyboardSelection::Mode::None));
    modal.reject();
    QCoreApplication::processEvents();
    key(target, Qt::Key_F8);
    key(target, Qt::Key_F8);
    QCOMPARE(canvas->property("keyboardSelectionMode").toInt(), int(KeyboardSelection::Mode::None));
}

template <class Designer>
void designerTeardown(Designer* designer, const QString& canvasName)
{
    activate(*designer);
    auto* canvas = designer->template findChild<QWidget*>(canvasName);
    QVERIFY(canvas);
    key(canvas, Qt::Key_F8);
    key(canvas, Qt::Key_Tab);
    int dirtyDuringTeardown = 0;
    QObject receiver;
    QObject::connect(designer, &Designer::dirtyChanged, &receiver, [&] { ++dirtyDuringTeardown; });
    QPointer<QObject> guardedCanvas(canvas);
    delete designer;
    QCoreApplication::processEvents();
    QVERIFY(guardedCanvas.isNull());
    QCOMPARE(dirtyDuringTeardown, 0);
}

inline void frameTableIgnoresLifecycleEvents()
{
    int calls = 0;
    DesignerFrameTable table;
    table.selectionKeyHandler = [&](QEvent*) { ++calls; return false; };
    QEvent other(QEvent::User);
    QCoreApplication::sendEvent(&table, &other);
    QCOMPARE(calls, 0);
    QKeyEvent key(QEvent::ShortcutOverride, Qt::Key_F8, Qt::NoModifier);
    QCoreApplication::sendEvent(&table, &key);
    QCOMPARE(calls, 1);
}

inline void capture(QWidget& widget, const QString& name)
{
    const QString path = qEnvironmentVariable("CARDSTACK_SELECTION_GALLERY_DIR");
    if (path.isEmpty()) QSKIP("Set CARDSTACK_SELECTION_GALLERY_DIR for production-styled selection screenshots.");
    QDir directory(path);
    QVERIFY(directory.exists() || directory.mkpath(QStringLiteral(".")));
    QVERIFY(Tests::saveWidgetImage(widget, directory, name));
}

inline void emptyDiscontinuousSelectionAndNotes()
{
    DeckWorkspace workspace(deck());
    activate(workspace);
    workspace.focusFieldBoundary(DeckWorkspace::FieldBoundary::First);
    auto* name = workspace.findChild<QLineEdit*>(QStringLiteral("fieldValue_0"));
    QVERIFY(name);
    name->setSelection(1, 2);
    key(name, Qt::Key_F8, Qt::ShiftModifier);
    QCOMPARE(workspace.fieldSelectionMode(), KeyboardSelection::Mode::Discontinuous);
    QCOMPARE(QApplication::focusWidget(), name);
    QCOMPARE(name->selectedText(), QStringLiteral("lp"));
    const QString previousClipboard = QApplication::clipboard()->text();
    const auto restoreClipboard = qScopeGuard([&] { QApplication::clipboard()->setText(previousClipboard); });
    workspace.copy();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("lp"));
    QKeyEvent typed(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, QStringLiteral("X"));
    QCoreApplication::sendEvent(name, &typed);
    QCOMPARE(workspace.fieldSelectionMode(), KeyboardSelection::Mode::None);
    QCOMPARE(name->text(), QStringLiteral("AXha"));
    workspace.commitPendingEdits();
    workspace.focusFieldBoundary(DeckWorkspace::FieldBoundary::First);
    clickField(workspace, 1, Qt::ControlModifier);
    clickField(workspace, 3, Qt::ControlModifier);
    QApplication::clipboard()->setText(QStringLiteral("Changed\r\nline one\r\nline two"));
    workspace.paste();
    QCOMPARE(workspace.deck().cardAt(workspace.currentCardIndex()).valueAt(1), QStringLiteral("Changed"));
    QCOMPARE(workspace.deck().cardAt(workspace.currentCardIndex()).valueAt(3), QStringLiteral("line one\r\nline two"));
    workspace.undo();
    QCOMPARE(workspace.deck().cardAt(workspace.currentCardIndex()).valueAt(1), QStringLiteral("Beta"));
    QCOMPARE(workspace.deck().cardAt(workspace.currentCardIndex()).valueAt(3), QStringLiteral("Memo"));
}

inline void modifierClicksPreserveModeAndSchemaClearsSelection()
{
    DeckWorkspace workspace(deck());
    activate(workspace);
    workspace.focusFieldBoundary(DeckWorkspace::FieldBoundary::First);
    key(&workspace, Qt::Key_F8);
    clickField(workspace, 2, Qt::ControlModifier);
    QCOMPARE(workspace.fieldSelectionMode(), KeyboardSelection::Mode::Continuous);
    QCOMPARE(workspace.selectedFieldIndexes(), QVector<int>({0, 2}));
    key(&workspace, Qt::Key_Left);
    QCOMPARE(workspace.selectedFieldIndexes(), QVector<int>({1, 2}));
    clickField(workspace, 0, Qt::ShiftModifier);
    QCOMPARE(workspace.fieldSelectionMode(), KeyboardSelection::Mode::Continuous);
    QCOMPARE(workspace.selectedFieldIndexes(), QVector<int>({0, 1, 2}));
    key(&workspace, Qt::Key_F8, Qt::ShiftModifier);
    clickField(workspace, 1, Qt::ControlModifier);
    QCOMPARE(workspace.fieldSelectionMode(), KeyboardSelection::Mode::Discontinuous);
    QCOMPARE(workspace.selectedFieldIndexes(), QVector<int>({0, 2}));
    clickField(workspace, 0, Qt::NoModifier);
    QCOMPARE(workspace.fieldSelectionMode(), KeyboardSelection::Mode::None);
    QVERIFY(workspace.selectedFieldIndexes().isEmpty());
    key(&workspace, Qt::Key_F8);
    key(&workspace, Qt::Key_End);
    QString error;
    QVERIFY2(workspace.redefineFields({FieldDefinition(QStringLiteral("Name"), FieldType::Text, 64)}, {0}, &error), qPrintable(error));
    QCOMPARE(workspace.fieldSelectionMode(), KeyboardSelection::Mode::None);
    QVERIFY(workspace.selectedFieldIndexes().isEmpty());
    QCOMPARE(workspace.deck().fieldCount(), 1);
}

inline void undoRebuildDoesNotResetModelReentrantly()
{
    DeckWorkspace workspace(deck());
    activate(workspace);
    workspace.setDeckDescription(QStringLiteral("Snapshot before editor rebuild"));
    workspace.focusFieldBoundary(DeckWorkspace::FieldBoundary::First);
    QPointer<QLineEdit> abandoned = workspace.findChild<QLineEdit*>(QStringLiteral("fieldValue_0"));
    QVERIFY(abandoned);
    abandoned->setText(QStringLiteral("Abandoned editor text"));
    abandoned->setModified(true);
    QVERIFY(!abandoned->isUndoAvailable());
    auto* table = workspace.findChild<QTableView*>();
    QVERIFY(table);
    QSignalSpy resets(table->model(), &QAbstractItemModel::modelReset);
    QVERIFY(workspace.undo());
    QVERIFY(abandoned.isNull());
    QCOMPARE(workspace.deck().cardAt(0).valueAt(0), QStringLiteral("Alpha"));
    QCOMPARE(resets.count(), 1);
}

inline void destructionDoesNotCommitPendingEditors()
{
    for (int iteration = 0; iteration < 12; ++iteration) {
        auto* workspace = new DeckWorkspace(deck(iteration % 2 == 0));
        activate(*workspace);
        if (iteration % 3 == 0) workspace->showTableView();
        workspace->focusFieldBoundary(DeckWorkspace::FieldBoundary::First);
        key(workspace, Qt::Key_F8, iteration % 2 ? Qt::ShiftModifier : Qt::NoModifier);
        int editsDuringTeardown = 0;
        QObject receiver;
        QObject::connect(workspace, &DeckWorkspace::dirtyChanged, &receiver,
                         [&] { ++editsDuringTeardown; });
        auto* edit = workspace->findChild<QLineEdit*>(QStringLiteral("fieldValue_0"));
        QVERIFY(edit);
        edit->setText(QStringLiteral("Uncommitted edit"));
        delete workspace;
        QCoreApplication::processEvents();
        QCOMPARE(editsDuringTeardown, 0);
    }
}

inline void bindingMatrixAndCoordinateTies()
{
    using Command = KeyboardSelection::Command;
    const QVector<QPair<int, Command>> navigation{
        {Qt::Key_Tab, Command::Next}, {Qt::Key_Right, Command::Next}, {Qt::Key_Down, Command::Next},
        {Qt::Key_Left, Command::Previous}, {Qt::Key_Up, Command::Previous},
        {Qt::Key_PageDown, Command::Last}, {Qt::Key_End, Command::Last},
        {Qt::Key_PageUp, Command::First}, {Qt::Key_Home, Command::First}, {Qt::Key_Escape, Command::Cancel}
    };
    for (auto mode : {KeyboardSelection::Mode::Continuous, KeyboardSelection::Mode::Discontinuous}) {
        KeyboardSelection selection;
        selection.apply(mode == KeyboardSelection::Mode::Continuous ? Command::Continuous : Command::Discontinuous,
                        {0, 1, 2}, {}, 1, false);
        for (const auto& binding : navigation) {
            QKeyEvent event(QEvent::KeyPress, binding.first, Qt::NoModifier);
            QCOMPARE(selection.command(event), binding.second);
            QKeyEvent control(QEvent::KeyPress, binding.first, Qt::ControlModifier);
            QCOMPARE(selection.command(control), Command::None);
        }
        for (int tab : {int(Qt::Key_Tab), int(Qt::Key_Backtab)}) {
            QKeyEvent event(QEvent::KeyPress, tab, Qt::ShiftModifier);
            QCOMPARE(selection.command(event), Command::Previous);
        }
        for (int toggle : {int(Qt::Key_Return), int(Qt::Key_Enter), int(Qt::Key_Space)}) {
            QKeyEvent event(QEvent::KeyPress, toggle, Qt::NoModifier);
            QCOMPARE(selection.command(event), mode == KeyboardSelection::Mode::Discontinuous ? Command::Toggle : Command::None);
        }
    }
    auto frames = report().frames;
    frames.append(frames.at(2));
    QCOMPARE(DesignerSelection::positionalOrder(frames, 3), QVector<int>({3, 1, 0}));
    QCOMPARE(DesignerSelection::positionalRange(frames, 3, 1), QVector<int>({1, 2, 3}));
    KeyboardSelection empty;
    const auto result = empty.apply(Command::Continuous, {}, {}, -1, false);
    QVERIFY(result.selected.isEmpty());
    QCOMPARE(empty.mode(), KeyboardSelection::Mode::None);
}

inline void deckGallery()
{
    DeckWorkspace workspace(deck());
    activate(workspace);
    workspace.focusFieldBoundary(DeckWorkspace::FieldBoundary::First);
    key(&workspace, Qt::Key_F8);
    key(&workspace, Qt::Key_Down);
    capture(workspace, QStringLiteral("deck_card_continuous.png"));
    workspace.showTableView();
    auto* target = workspace.findChild<QTableView*>();
    key(target, Qt::Key_F8, Qt::ShiftModifier);
    key(target, Qt::Key_End);
    key(target, Qt::Key_Space);
    capture(workspace, QStringLiteral("deck_table_discontinuous.png"));
}

template <class Designer>
void designerGallery(Designer& designer, const QString& canvasName, const QString& prefix)
{
    activate(designer);
    auto* table = designer.template findChild<QTableWidget*>();
    auto* canvas = designer.template findChild<QWidget*>(canvasName);
    QVERIFY(table && canvas);
    table->setCurrentCell(2, 0);
    table->selectRow(2);
    canvas->setFocus();
    key(canvas, Qt::Key_F8);
    key(canvas, Qt::Key_Tab);
    capture(designer, prefix + QStringLiteral("_continuous.png"));
    key(table, Qt::Key_F8, Qt::ShiftModifier);
    key(table, Qt::Key_End);
    capture(designer, prefix + QStringLiteral("_discontinuous.png"));
}

} // namespace CardStack::Tests::KeyboardSelectionProbe
