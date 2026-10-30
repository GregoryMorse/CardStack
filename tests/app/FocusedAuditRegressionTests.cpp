#include "ApplicationAppearance.h"
#include "DeckWorkspace.h"
#include "DocumentFile.h"
#include "LegacyDeckReader.h"
#include "MainWindow.h"
#include "../support/DesignerSelectionProbe.h"
#include "../support/UiPolishProbe.h"
#include "ReportDesignerWidget.h"
#include "SQLiteDeckStore.h"
#include "SQLitePackageStore.h"
#include "TemplateDesignerWidget.h"
#include "UiBuilder.h"
#include "UiIds.h"
#include "UiResourceData.h"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QFileInfo>
#include <QLockFile>
#include <QPixmap>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QCheckBox>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
#include <functional>
#include <stdexcept>
#include <cstdio>

using namespace CardStack;
namespace {
constexpr int ModalDeadlineMs = 3000;
constexpr int UnsafeProbeDeadlineMs = 5000;
constexpr int MaximumTabSteps = 256;
void require(bool condition, const QString& message)
{
    if (!condition) throw std::runtime_error(message.toStdString());
}
void events() { QCoreApplication::processEvents(); }
QByteArray readBytes(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), file.errorString());
    return file.readAll();
}
void writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), file.errorString());
    require(file.write(bytes) == bytes.size(), file.errorString());
}
Deck makeDeck()
{
    Deck deck(QStringLiteral("Disposable audit deck"));
    deck.addField(FieldDefinition(QStringLiteral("Name"), FieldType::Text, 255));
    deck.addField(FieldDefinition(QStringLiteral("Status"), FieldType::Text, 255));
    deck.addCard(CardRecord({QStringLiteral("Alpha"), QStringLiteral("Ready")}));
    deck.addCard(CardRecord({QStringLiteral("Beta"), QStringLiteral("Pending")}));
    CardTemplateLayout layout;
    layout.canvasWidth = 6400;
    layout.canvasHeight = 4800;
    CardTemplateFrame frame;
    frame.kind = CardTemplateFrameKind::DataBox;
    frame.fieldIndex = 0;
    frame.bounds = QRect(300, 300, 2200, 300);
    frame.text = QStringLiteral("Name");
    layout.frames.append(frame);
    deck.setCardTemplateLayout(layout);
    return deck;
}
ReportDefinition makeReport()
{
    ReportDefinition report;
    report.name = QStringLiteral("Disposable audit report");
    report.formatMagic = QStringLiteral("RPT@#$B");
    report.formType = ReportFormType::Report;
    report.formWidth = 8500;
    report.formHeight = 11000;
    report.rows = report.columns = 1;
    ReportFrameDefinition frame;
    frame.kind = ReportFrameKind::Text;
    frame.text = QStringLiteral("Original frame text");
    frame.bounds = QRect(500, 500, 2200, 300);
    report.frames.append(frame);
    return report;
}
QAction* command(const QList<QAction*>& actions, int id)
{
    for (QAction* action : actions) {
        if (action->data().toInt() == id) return action;
        if (action->menu())
            if (QAction* found = command(action->menu()->actions(), id)) return found;
    }
    return nullptr;
}
QAction* command(MainWindow& window, int id)
{
    QAction* action = command(window.menuBar()->actions(), id);
    require(action && action->isEnabled(), QStringLiteral("Action missing/disabled: %1").arg(id));
    return action;
}
template<class T> T* control(QWidget* parent, int id)
{
    T* result = qobject_cast<T*>(UiBuilder::controlById(parent, id));
    require(result != nullptr, QStringLiteral("Control missing: %1").arg(id));
    return result;
}
void withDialog(const std::function<void()>& open, const QString& name,
                const std::function<void(QDialog*)>& handle)
{
    bool handled = false, timedOut = false;
    std::exception_ptr failure;
    QElapsedTimer deadline;
    deadline.start();
    QTimer timer;
    timer.setInterval(5);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dialog && dialog->property("legacyDialogName").toString() == name) {
            timer.stop();
            handled = true;
            try { handle(dialog); }
            catch (...) { failure = std::current_exception(); dialog->reject(); }
        } else if (deadline.elapsed() >= ModalDeadlineMs) {
            timer.stop();
            timedOut = true;
            if (dialog) dialog->reject();
        }
    });
    timer.start();
    open();
    timer.stop();
    if (failure) std::rethrow_exception(failure);
    require(handled && !timedOut, QStringLiteral("Dialog not handled: %1").arg(name));
    events();
}
QMdiSubWindow* windowFor(MainWindow& window, QWidget* widget)
{
    auto* mdi = window.findChild<QMdiArea*>();
    require(mdi != nullptr, QStringLiteral("MDI missing"));
    for (auto* child : mdi->subWindowList())
        if (child->widget() == widget) return child;
    throw std::runtime_error("MDI child missing");
}
void activate(MainWindow& window, QWidget* widget)
{
    window.findChild<QMdiArea*>()->setActiveSubWindow(windowFor(window, widget));
    events();
}
ReportDesignerWidget* newReport(MainWindow& window)
{
    const auto before = window.findChildren<ReportDesignerWidget*>();
    withDialog([&] { command(window, UiIds::Command::FileNewReport)->trigger(); },
        QStringLiteral("REPORTFORM"), [](QDialog* dialog) { dialog->accept(); });
    for (auto* designer : window.findChildren<ReportDesignerWidget*>())
        if (!before.contains(designer)) return designer;
    throw std::runtime_error("Report designer not created");
}
QWidget* properties(MainWindow& window)
{
    auto* widget = window.findChild<QWidget*>(QStringLiteral("designerPropertyToolbar"));
    require(widget != nullptr, QStringLiteral("Property toolbar missing"));
    return widget;
}
QToolButton* boldButton(MainWindow& window)
{
    for (auto* button : properties(window)->findChildren<QToolButton*>())
        if (button->text() == QStringLiteral("B")) return button;
    throw std::runtime_error("Bold button missing");
}
bool tabTo(QWidget& root, QWidget* target)
{
    root.show();
    QApplication::setActiveWindow(&root);
    events();
    for (int attempt = 0; attempt < MaximumTabSteps; ++attempt) {
        if (QApplication::focusWidget() == target) return true;
        QWidget* current = QApplication::focusWidget();
        if (!current || (current != &root && !root.isAncestorOf(current))) current = &root;
        QTest::keyClick(current, Qt::Key_Tab);
        events();
    }
    return QApplication::focusWidget() == target;
}
QJsonObject outcome(bool reproduced, QJsonObject details)
{
    details.insert(QStringLiteral("status"), reproduced ? QStringLiteral("reproduced") : QStringLiteral("not_reproduced"));
    return details;
}
QJsonObject securityUndo(const QDir& output)
{
    DeckWorkspace workspace(makeDeck());
    workspace.setDeckDescription(QStringLiteral("Content edit before adding security"));
    require(workspace.setSecurity(QStringLiteral("Exact audit password 2026"), true), QStringLiteral("Security setup failed"));
    require(workspace.hasEncryptedSecurity(), QStringLiteral("Encryption not enabled"));
    require(workspace.undo(), QStringLiteral("Undo missing"));
    const QString path = output.filePath(QStringLiteral("undo-security.cardstack"));
    QString error;
    require(SQLitePackageStore::saveDeckPackage(workspace.deck(), path, &error), error);
    const bool envelope = DeckSecurity::isEncryptedContainer(readBytes(path).left(8));
    Deck reloaded;
    SQLiteDeckStore store;
    const bool withoutPassword = store.open(path, &error, {}, SQLiteDeckStore::OpenMode::ReadOnly)
        && store.loadDeck(&reloaded, &error);
    return outcome(!workspace.hasSecurity() && !envelope && withoutPassword,
        {{QStringLiteral("security_after_undo"), workspace.hasSecurity()},
         {QStringLiteral("encrypted_file_after_save"), envelope},
         {QStringLiteral("reopened_without_password"), withoutPassword}});
}
QJsonObject saveUndo(const QDir& output)
{
    const QString path = output.filePath(QStringLiteral("undo-fingerprint.cardstack"));
    QString error;
    qInfo("saveUndo: creating disposable package");
    require(SQLitePackageStore::saveDeckPackage(makeDeck(), path, &error), error);
    qInfo("saveUndo: loading disposable package");
    Deck loaded;
    {
        SQLiteDeckStore store;
        require(store.open(path, &error, {}, SQLiteDeckStore::OpenMode::ReadOnly) && store.loadDeck(&loaded, &error), error);
    }
    qInfo("saveUndo: constructing workspace");
    DeckWorkspace workspace(loaded);
    qInfo("saveUndo: editing and saving revision");
    workspace.setDeckDescription(QStringLiteral("Save this revision"));
    require(SQLitePackageStore::saveDeckPackage(workspace.deck(), path, &error), error);
    workspace.setStorageOrigin(path, documentFileFingerprint(path));
    workspace.clearDirty();
    qInfo("saveUndo: restoring content snapshot");
    require(workspace.undo(), QStringLiteral("Undo missing"));
    const bool fingerprintMatches = workspace.deck().storageFingerprint() == documentFileFingerprint(path);
    qInfo("saveUndo: saving restored snapshot");
    const bool saved = SQLitePackageStore::saveDeckPackage(workspace.deck(), path, &error);
    qInfo("saveUndo: verifying result and destroying workspace");
    return outcome(!fingerprintMatches && !saved,
        {{QStringLiteral("fingerprint_matches_disk"), fingerprintMatches},
         {QStringLiteral("save_succeeded"), saved}, {QStringLiteral("error"), error}});
}
QJsonObject interruptedStore(const QDir& output)
{
    const QString path = output.filePath(QStringLiteral("failed-store-save.cardstack"));
    QString error;
    require(SQLitePackageStore::saveDeckPackage(makeDeck(), path, &error), error);
    const QByteArray before = readBytes(path);
    const QStringList oldNames = QSqlDatabase::connectionNames();
    SQLiteDeckStore store;
    require(store.open(path, &error), error);
    QString memoryConnection;
    for (const auto& name : QSqlDatabase::connectionNames())
        if (!oldNames.contains(name) && QSqlDatabase::database(name).databaseName() == QStringLiteral(":memory:"))
            memoryConnection = name;
    require(!memoryConnection.isEmpty(), QStringLiteral("Memory connection not identified"));
    {
        QSqlQuery fault(QSqlDatabase::database(memoryConnection));
        require(fault.exec(QStringLiteral(
            "CREATE TEMP TRIGGER audit_fail_cards BEFORE INSERT ON cards "
            "BEGIN SELECT RAISE(ABORT, 'audit injected card write failure'); END")), fault.lastError().text());
    }
    Deck updated = makeDeck();
    updated.setDescription(QStringLiteral("Revision that must fail atomically"));
    const bool saved = store.saveDeck(updated, &error);
    const bool unchanged = readBytes(path) == before;
    int remainingCards = -1;
    {
        SQLiteDeckStore reader;
        Deck disk;
        QString readError;
        if (reader.open(path, &readError, {}, SQLiteDeckStore::OpenMode::ReadOnly) && reader.loadDeck(&disk, &readError))
            remainingCards = disk.cardCount();
    }
    return outcome(!saved && !unchanged,
        {{QStringLiteral("save_succeeded"), saved}, {QStringLiteral("bytes_unchanged"), unchanged},
         {QStringLiteral("cards_left_on_disk"), remainingCards}, {QStringLiteral("error"), error}});
}
QJsonObject reportIdentity(const QDir& output)
{
    Deck original = makeDeck();
    original.addReport(makeReport());
    const QString reportPath = output.filePath(QStringLiteral("shared.cardreport"));
    QString error;
    require(SQLitePackageStore::saveReportPackage(original.reports(), QStringLiteral("Audit package"), reportPath, &error), error);
    QVector<ReportDefinition> first, second;
    require(SQLitePackageStore::loadReportPackage(reportPath, &first, nullptr, &error), error);
    require(SQLitePackageStore::loadReportPackage(reportPath, &second, nullptr, &error), error);
    require(first.size() == 1 && second.size() == 1, QStringLiteral("Package setup failed"));
    DeckWorkspace workspace(makeDeck());
    require(workspace.saveReportDefinition(-1, first[0]), QStringLiteral("First append failed"));
    second[0].name += QStringLiteral(" (2)");
    require(workspace.saveReportDefinition(-1, second[0]), QStringLiteral("Second append failed"));
    const QString deckPath = output.filePath(QStringLiteral("reimported-reports.cardstack"));
    require(SQLitePackageStore::saveDeckPackage(workspace.deck(), deckPath, &error), error);
    SQLiteDeckStore reader;
    Deck disk;
    require(reader.open(deckPath, &error, {}, SQLiteDeckStore::OpenMode::ReadOnly) && reader.loadDeck(&disk, &error), error);
    require(disk.reportCount() == 2, QStringLiteral("Report count changed"));
    return outcome(disk.reportAt(0).documentId == disk.reportAt(1).documentId,
        {{QStringLiteral("first_id"), disk.reportAt(0).documentId},
         {QStringLiteral("second_id"), disk.reportAt(1).documentId},
         {QStringLiteral("different_names"), disk.reportAt(0).name != disk.reportAt(1).name}});
}
QJsonObject wrongDesigner()
{
    MainWindow window(nullptr, true, false);
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show(); events();
    auto* deck = window.findChild<DeckWorkspace*>();
    require(deck != nullptr, QStringLiteral("Sample deck missing"));
    auto* first = newReport(window);
    first->addTextFrameWithText(QStringLiteral("Report A text"), 0);
    const int firstIndex = first->selectedFrameIndex();
    activate(window, deck);
    auto* second = newReport(window);
    second->addTextFrameWithText(QStringLiteral("Report B text"), 0);
    const int secondIndex = second->selectedFrameIndex();
    activate(window, first);
    require(window.findChild<QMdiArea*>()->activeSubWindow()->widget() == first, QStringLiteral("A not active"));
    auto* edit = properties(window)->findChild<QLineEdit*>(QStringLiteral("designerReportTextEdit"));
    require(edit != nullptr, QStringLiteral("Toolbar text editor missing"));
    const QString shown = edit->text();
    edit->setText(QStringLiteral("Toolbar edit while A is active"));
    require(QMetaObject::invokeMethod(edit, "editingFinished", Qt::DirectConnection), QStringLiteral("Commit signal missing"));
    events();
    const QString a = first->report().frames.at(firstIndex).text;
    const QString b = second->report().frames.at(secondIndex).text;
    return outcome(a == QStringLiteral("Report A text") && b == QStringLiteral("Toolbar edit while A is active"),
        {{QStringLiteral("toolbar_initial_text"), shown},
         {QStringLiteral("active_A_text"), a}, {QStringLiteral("inactive_B_text"), b}});
}
QJsonObject dataFormatting()
{
    MainWindow window(nullptr, true, false);
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show(); events();
    auto* deck = window.findChild<DeckWorkspace*>();
    require(deck && deck->deck().fieldCount() >= 2, QStringLiteral("Sample fields missing"));
    auto* designer = newReport(window);
    designer->addDataFrameForField(deck->deck().fieldAt(0).name(), 0, false);
    auto* sidebar = designer->findChild<QPlainTextEdit*>();
    require(sidebar != nullptr, QStringLiteral("Sidebar editor missing"));
    const QString expression = QStringLiteral("[%1], [%2]").arg(deck->deck().fieldAt(0).name(), deck->deck().fieldAt(1).name());
    sidebar->setPlainText(expression);
    auto* table = designer->findChild<QTableWidget*>();
    require(table != nullptr, QStringLiteral("Frame table missing"));
    table->setCurrentCell(designer->selectedFrameIndex(), table->currentColumn() == 0 ? 1 : 0);
    events();
    require(designer->selectedFrameDefinition()->text == expression, QStringLiteral("Expression setup failed"));
    boldButton(window)->click();
    const QString after = designer->selectedFrameDefinition()->text;
    return outcome(after != expression,
        {{QStringLiteral("before"), expression}, {QStringLiteral("after_bold_only"), after}});
}
QJsonObject staleToolbar()
{
    MainWindow window(nullptr, true, false);
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show(); events();
    auto* designer = newReport(window);
    designer->addTextFrameWithText(QStringLiteral("Original toolbar text"), 0);
    auto* sidebar = designer->findChild<QPlainTextEdit*>();
    auto* toolbar = properties(window)->findChild<QLineEdit*>(QStringLiteral("designerReportTextEdit"));
    require(sidebar && toolbar, QStringLiteral("Text editors missing"));
    sidebar->setPlainText(QStringLiteral("New sidebar text"));
    const QString modelAfterSidebar = designer->selectedFrameDefinition()->text;
    const QString staleValue = toolbar->text();
    boldButton(window)->click();
    const QString after = designer->selectedFrameDefinition()->text;
    return outcome(modelAfterSidebar == QStringLiteral("New sidebar text") && after != modelAfterSidebar,
        {{QStringLiteral("after_sidebar"), modelAfterSidebar},
         {QStringLiteral("toolbar_before_click"), staleValue}, {QStringLiteral("after_bold_only"), after}});
}
QJsonObject templateUndoSelector()
{
    const Deck source = makeDeck();
    TemplateDesignerWidget designer(source.cardTemplateLayout(), source.fields(), nullptr, source.appearance());
    require(designer.addDataField(QStringLiteral("Temporary field"), 30), QStringLiteral("Field setup failed"));
    designer.undo();
    QComboBox* staleCombo = nullptr;
    int staleIndex = -1;
    for (auto* combo : designer.findChildren<QComboBox*>()) {
        const int found = combo->findText(QStringLiteral("Temporary field"));
        if (found >= 0) { staleCombo = combo; staleIndex = found; break; }
    }
    int boundIndex = -1;
    if (staleCombo) {
        staleCombo->setCurrentIndex(staleIndex);
        if (designer.selectedFrameDefinition()) boundIndex = designer.selectedFrameDefinition()->fieldIndex;
    }
    return outcome(staleCombo && boundIndex >= designer.fieldDefinitions().size(),
        {{QStringLiteral("stale_entry_remains"), staleCombo != nullptr},
         {QStringLiteral("actual_field_count"), designer.fieldDefinitions().size()},
         {QStringLiteral("bound_field_index"), boundIndex}});
}
QJsonObject frameTableEdits()
{
    ReportDesignerWidget designer(makeReport(), {QStringLiteral("Name")});
    designer.show();
    QApplication::setActiveWindow(&designer); events();
    auto* table = designer.findChild<QTableWidget*>();
    require(table && table->rowCount() > 0, QStringLiteral("Frame table missing"));
    table->setCurrentCell(0, 2); table->setFocus();
    if (table->editTriggers() == QAbstractItemView::NoEditTriggers)
        return outcome(false, {{QStringLiteral("editing_disabled"), true}});
    QTest::keyClick(table, Qt::Key_F2); events();
    QLineEdit* editor = nullptr;
    for (auto* candidate : table->findChildren<QLineEdit*>())
        if (candidate->isVisible()) { editor = candidate; break; }
    require(editor != nullptr, QStringLiteral("F2 did not start a table editor"));
    QTest::keyClick(editor, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(editor, QStringLiteral("Keyboard edited table cell"));
    QTest::keyClick(editor, Qt::Key_Return); events();
    const QString visible = table->item(0, 2)->text();
    const QString model = designer.report().frames.at(0).text;
    return outcome(visible == QStringLiteral("Keyboard edited table cell") && model != visible,
        {{QStringLiteral("displayed_cell"), visible}, {QStringLiteral("actual_frame_text"), model},
         {QStringLiteral("dirty"), designer.isDirty()}});
}
QJsonObject dialerCancel()
{
    MainWindow window(nullptr, false, false);
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show(); events();
    int before = -1;
    withDialog([&] { command(window, UiIds::Command::ConfigurePhoneDialer)->trigger(); },
        QStringLiteral("PHNDEF"), [&](QDialog* dialog) {
            before = control<QListWidget>(dialog, UiIds::Control::PhoneQuickDials)->count();
            withDialog([&] { control<QAbstractButton>(dialog, UiIds::Control::PhoneQuickDialAdd)->click(); },
                QStringLiteral("QUICKDIAL"), [](QDialog* quick) {
                    control<QLineEdit>(quick, UiIds::Control::QuickDialDescription)->setText(QStringLiteral("Disposable audit dial"));
                    control<QLineEdit>(quick, UiIds::Control::QuickDialNumber)->setText(QStringLiteral("5550123"));
                    quick->accept();
                });
            dialog->reject();
        });
    int after = -1;
    withDialog([&] { command(window, UiIds::Command::ConfigurePhoneDialer)->trigger(); },
        QStringLiteral("PHNDEF"), [&](QDialog* dialog) {
            after = control<QListWidget>(dialog, UiIds::Control::PhoneQuickDials)->count();
            dialog->reject();
        });
    return outcome(after == before + 1,
        {{QStringLiteral("before_count"), before}, {QStringLiteral("count_after_cancel"), after}});
}
QJsonObject colorKeys()
{
    auto dialog = UiBuilder::createDialog(QStringLiteral("CHOOSECOLOR"));
    require(dialog != nullptr, QStringLiteral("Color dialog missing"));
    QStringList colors;
    for (int index = 0; index < 7; ++index) colors.append(QStringLiteral("#123456"));
    UiBuilder::setColorDialogState(dialog.get(), colors, false);
    QWidget* grid = control<QWidget>(dialog.get(), UiIds::Control::ColorCustomGrid);
    const bool reachable = tabTo(*dialog, grid);
    const QStringList before = UiBuilder::colorDialogColors(dialog.get());
    if (reachable) {
        QTest::keyClick(grid, Qt::Key_Right);
        QTest::keyClick(grid, Qt::Key_Down);
        QTest::keyClick(grid, Qt::Key_Space);
    }
    const bool keyboardChanged = UiBuilder::colorDialogColors(dialog.get()) != before;
    QTest::mouseClick(grid, Qt::LeftButton, Qt::NoModifier, QPoint(12, 12));
    const bool mouseChanged = UiBuilder::colorDialogColors(dialog.get()) != before;
    return outcome(reachable && !keyboardChanged && mouseChanged,
        {{QStringLiteral("tab_reachable"), reachable}, {QStringLiteral("keyboard_changed_color"), keyboardChanged},
         {QStringLiteral("message_based_mouse_changed_color"), mouseChanged}});
}
QJsonObject indexKeys()
{
    MainWindow window(nullptr, true, false);
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show(); events();
    auto* bar = window.findChild<QWidget*>(QStringLiteral("indexBarContainer"));
    require(bar != nullptr, QStringLiteral("Index bar missing"));
    auto buttons = bar->findChildren<QPushButton*>();
    require(!buttons.isEmpty(), QStringLiteral("Index buttons missing"));
    int noFocus = 0, noShortcuts = 0;
    for (auto* button : buttons) {
        noFocus += button->focusPolicy() == Qt::NoFocus;
        noShortcuts += button->shortcut().isEmpty();
    }
    const bool reachable = tabTo(window, buttons.first());
    return outcome(noFocus == buttons.size() && noShortcuts == buttons.size() && !reachable,
        {{QStringLiteral("button_count"), buttons.size()}, {QStringLiteral("no_focus_count"), noFocus},
         {QStringLiteral("no_shortcut_count"), noShortcuts}, {QStringLiteral("first_button_tab_reachable"), reachable}});
}
QJsonObject labelMnemonics()
{
    int expected = 0, lost = 0;
    QJsonArray examples;
    UiBuilder::DialogContext context;
    context.fieldNames = {QStringLiteral("Name"), QStringLiteral("Status")};
    for (std::size_t index = 0; index < UiResourceData::dialogCount(); ++index) {
        const auto& resource = UiResourceData::dialogs()[index];
        int marked = 0;
        for (std::size_t item = 0; item < resource.controlCount; ++item) {
            const auto& raw = resource.controls[item];
            if (QString::fromUtf8(raw.className) == QStringLiteral("static")
                && !QKeySequence::mnemonic(QString::fromUtf8(raw.text)).isEmpty()) ++marked;
        }
        if (!marked) continue;
        auto dialog = UiBuilder::createDialog(QString::fromUtf8(resource.name), nullptr, context);
        require(dialog != nullptr, QStringLiteral("Dialog construction failed"));
        int bound = 0;
        for (auto* label : dialog->findChildren<QLabel*>())
            if (label->buddy() && !QKeySequence::mnemonic(label->text()).isEmpty()) ++bound;
        expected += marked;
        if (!bound) {
            lost += marked;
            if (examples.size() < 8) examples.append(QString::fromUtf8(resource.name));
        }
    }
    require(expected > 0, QStringLiteral("No source label mnemonics to exercise"));
    return outcome(lost > 0, {{QStringLiteral("resource_label_mnemonics"), expected},
        {QStringLiteral("markers_in_dialogs_with_no_bound_label"), lost}, {QStringLiteral("example_dialogs"), examples}});
}
QJsonObject appearanceUndo()
{
    Deck source = makeDeck();
    TemplateDesignerWidget designer(source.cardTemplateLayout(), source.fields(), nullptr, source.appearance());
    DeckAppearance changed = source.appearance();
    changed.useSystemColors = false;
    changed.customColors[0] = QStringLiteral("#123456");
    designer.setAppearance(changed);
    const bool canUndo = designer.canUndo();
    designer.undo();
    return outcome(!canUndo && designer.appearance() == changed,
        {{QStringLiteral("undo_available_after_appearance_change"), canUndo},
         {QStringLiteral("appearance_unchanged_after_undo"), designer.appearance() == changed}});
}
QJsonObject nativeHeader(const QDir& output)
{
    QByteArray bytes(512, '\0');
    bytes[6] = '\0'; bytes[7] = '\x05'; // old-format version 5
    bytes[0x16] = '\x08'; bytes[0x18] = '\x08';
    bytes[0x1c] = '\x01'; // word-swapped record count
    const QString path = output.filePath(QStringLiteral("zero-page.BTN"));
    writeBytes(path, bytes); // page size deliberately zero
    QProcess worker;
    worker.setProcessChannelMode(QProcess::MergedChannels);
    worker.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--unsafe-zero-page"), path});
    require(worker.waitForStarted(ModalDeadlineMs), worker.errorString());
    const bool completed = worker.waitForFinished(UnsafeProbeDeadlineMs);
    if (!completed) { worker.kill(); worker.waitForFinished(ModalDeadlineMs); }
    const bool crashed = worker.exitStatus() == QProcess::CrashExit;
    const bool rejected = completed && !crashed && worker.exitCode() == 7;
    return outcome(!rejected,
        {{QStringLiteral("completed_before_deadline"), completed}, {QStringLiteral("crashed"), crashed},
         {QStringLiteral("exit_code"), QString::number(static_cast<quint32>(worker.exitCode()), 16)},
         {QStringLiteral("cleanly_rejected"), rejected},
         {QStringLiteral("child_output"), QString::fromLocal8Bit(worker.readAll()).left(3000)}});
}
} // namespace

#include "../support/RefreshAuditProbe.h"

class FocusedAuditRegressionTests : public QObject {
    Q_OBJECT
    QString m_originalOrganization, m_originalApplication;
    QSettings::Format m_originalFormat = QSettings::NativeFormat;
    QTemporaryDir m_settings;

    void verify(const std::function<QJsonObject(const QDir&)>& check)
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        try {
            const QJsonObject result = check(QDir(directory.path()));
            const QByteArray details = QJsonDocument(result).toJson(QJsonDocument::Compact);
            QVERIFY2(result.value(QStringLiteral("status")) == QStringLiteral("not_reproduced"), details.constData());
        } catch (const std::exception& error) {
            QFAIL(error.what());
        }
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_settings.isValid());
        m_originalOrganization = QCoreApplication::organizationName();
        m_originalApplication = QCoreApplication::applicationName();
        m_originalFormat = QSettings::defaultFormat();
        QCoreApplication::setOrganizationName(QStringLiteral("CardStackFocusedRegression"));
        QCoreApplication::setApplicationName(QUuid::createUuid().toString(QUuid::WithoutBraces));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        MainWindow initializeResources(nullptr, false, false);
    }
    void cleanupTestCase()
    {
        QCoreApplication::setOrganizationName(m_originalOrganization);
        QCoreApplication::setApplicationName(m_originalApplication);
        QSettings::setDefaultFormat(m_originalFormat);
    }
    void committedTableClipboardUndoRestoresPreviousValue()
    {
        Tests::RefreshAudit::HiddenWindows hidden;
        for (int operation : {UiIds::Command::EditCut, UiIds::Command::EditPaste}) {
            MainWindow window(nullptr, true, false);
            Tests::RefreshAudit::show(window); QApplication::setActiveWindow(&window);
            auto* workspace = Tests::RefreshAudit::workspace(window);
            workspace->showTableView();
            auto* table = workspace->findChild<QTableView*>(QStringLiteral("deckTableView"));
            QVERIFY(table);
            const QModelIndex index = table->model()->index(0, 1);
            const QString original = QStringLiteral("Alpha Beta Gamma");
            QVERIFY(table->model()->setData(index, original));
            table->setCurrentIndex(index); table->edit(index); events();
            auto* edit = table->findChild<QLineEdit*>();
            QVERIFY(edit); edit->setFocus(); edit->setSelection(6, 4); events();
            QApplication::clipboard()->setText(QStringLiteral("Z"));
            command(window, operation)->trigger(); events();
            const QString expected = operation == UiIds::Command::EditCut ? QStringLiteral("Alpha  Gamma") : QStringLiteral("Alpha Z Gamma");
            QCOMPARE(edit->text(), expected);
            QTest::keyClick(edit, Qt::Key_Tab); events();
            QCOMPARE(workspace->deck().cardAt(workspace->currentCardIndex()).valueAt(1), expected);
            command(window, UiIds::Command::EditUndo)->trigger(); events();
            QCOMPARE(workspace->deck().cardAt(workspace->currentCardIndex()).valueAt(1), original);
            workspace->clearDirty();
        }
    }

    void destroyingWindowWithPendingTableEditDoesNotCallDeadShell()
    {
        Tests::RefreshAudit::HiddenWindows hidden;
        auto* window = new MainWindow(nullptr, true, false);
        Tests::RefreshAudit::show(*window);
        QApplication::setActiveWindow(window);
        auto* workspace = Tests::RefreshAudit::workspace(*window);
        workspace->showTableView();
        auto* table = workspace->findChild<QTableView*>(QStringLiteral("deckTableView"));
        QVERIFY(table);
        table->setCurrentIndex(table->model()->index(0, 0));
        table->edit(table->currentIndex()); events();
        auto* edit = table->findChild<QLineEdit*>();
        QVERIFY(edit); edit->setFocus(); events();
        edit->setText(QStringLiteral("Uncommitted shutdown edit"));
        QPointer<MainWindow> guard(window);
        delete window;
        QVERIFY(guard.isNull());
        events();
    }

    void deckClipboardMenuMatchesNativeSelection_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<int>("operation");
        QTest::addColumn<bool>("selection");
        for (int mode = 0; mode < 3; ++mode)
            for (int operation = 0; operation < 3; ++operation)
                for (bool selection : {false, true})
                    QTest::newRow(qPrintable(QStringLiteral("%1-%2-%3").arg(mode).arg(operation).arg(selection)))
                        << mode << operation << selection;
    }
    void deckClipboardMenuMatchesNativeSelection()
    {
        QFETCH(int, mode); QFETCH(int, operation); QFETCH(bool, selection);
        Tests::RefreshAudit::HiddenWindows hidden;
        const QString previousStyle = qApp->styleSheet();
        initializeApplicationAppearance(*qApp);
        const auto restoreStyle = qScopeGuard([&] { qApp->setStyleSheet(previousStyle); });
        MainWindow window(nullptr, true, false);
        Tests::RefreshAudit::show(window);
        QApplication::setActiveWindow(&window);
        auto* workspace = Tests::RefreshAudit::workspace(window);
        QWidget* editor = nullptr;
        if (mode == 0) {
            command(window, UiIds::Command::ViewTable)->trigger();
            auto* table = workspace->findChild<QTableView*>(QStringLiteral("deckTableView"));
            QVERIFY(table);
            table->setCurrentIndex(table->model()->index(0, 0));
            table->edit(table->currentIndex());
            events();
            editor = table->findChild<QLineEdit*>();
        } else {
            workspace->showCardView();
            editor = mode == 1 ? static_cast<QWidget*>(workspace->findChild<QLineEdit*>(QStringLiteral("fieldValue_0")))
                              : static_cast<QWidget*>(workspace->findChild<QPlainTextEdit*>(QStringLiteral("fieldValue_5")));
        }
        QVERIFY(editor);
        editor->setFocus(); events();
        QTRY_COMPARE(QApplication::focusWidget(), editor);
        const QString original = QStringLiteral("Alpha Beta Gamma");
        const int start = 6;
        const int length = selection ? 4 : 0;
        auto* line = qobject_cast<QLineEdit*>(editor);
        auto* notes = qobject_cast<QPlainTextEdit*>(editor);
        if (line) { line->setText(original); line->setSelection(start, length); }
        else {
            notes->setPlainText(original);
            auto cursor = notes->textCursor(); cursor.setPosition(start);
            cursor.setPosition(start + length, QTextCursor::KeepAnchor); notes->setTextCursor(cursor);
        }
        if (mode == 0) {
            auto* table = workspace->findChild<QTableView*>(QStringLiteral("deckTableView"));
            QVERIFY(table->visualRect(table->currentIndex()).contains(editor->geometry()));
        }
        QApplication::clipboard()->setText(operation == 2 ? QStringLiteral("Z") : QStringLiteral("Keep clipboard"));
        if (selection && operation == 2) Tests::RefreshAudit::capture(window, QStringLiteral("round2_%1_selection").arg(mode));
        const int id = operation == 0 ? UiIds::Command::EditCopy : operation == 1 ? UiIds::Command::EditCut : UiIds::Command::EditPaste;
        auto* action = command(window, id);
        if (mode == 0 && selection) {
            QMenu* popup = nullptr;
            for (auto* menu : window.menuBar()->findChildren<QMenu*>())
                if (menu->actions().contains(action)) { popup = menu; break; }
            QVERIFY(popup);
            popup->setAttribute(Qt::WA_DontShowOnScreen);
            QSignalSpy triggered(action, &QAction::triggered);
            popup->popup(window.mapToGlobal(QPoint(80, 70))); events();
            popup->setActiveAction(action);
            QTest::keyClick(popup, Qt::Key_Return); events();
            QCOMPARE(triggered.count(), 1);
        } else action->trigger();
        events();
        const QString expected = operation == 2 ? original.left(start) + QStringLiteral("Z") + original.mid(start + length)
            : operation == 1 && selection ? original.left(start) + original.mid(start + length) : original;
        QCOMPARE(line ? line->text() : notes->toPlainText(), expected);
        if (operation != 2) QCOMPARE(QApplication::clipboard()->text(), selection ? QStringLiteral("Beta") : QStringLiteral("Keep clipboard"));
        if (selection && operation == 2) Tests::RefreshAudit::capture(window, QStringLiteral("round2_%1_pasted").arg(mode));
        if (operation == 2 || (operation == 1 && selection)) {
            command(window, UiIds::Command::EditUndo)->trigger(); events();
            QCOMPARE(line ? line->text() : notes->toPlainText(), original);
        }
        // Commit through the real focus route, not by changing the model directly.
        if (mode == 0) QTest::keyClick(editor, Qt::Key_Tab);
        else workspace->setFocus();
        events();
        workspace->clearDirty();
    }

    void refreshReplacementSpansAndUndo() { verify([](const QDir&) { Tests::RefreshAudit::replacements(); return outcome(false, {}); }); }
    void refreshSearchBoundariesAndOccurrences() { verify([](const QDir&) { Tests::RefreshAudit::searchBoundaries(); return outcome(false, {}); }); }
    void refreshReplacementThroughGui() { verify([](const QDir&) { Tests::RefreshAudit::replaceThroughGui(); return outcome(false, {}); }); }
    void refreshFindNavigationThroughGui() { verify([](const QDir&) { Tests::RefreshAudit::findNavigationThroughGui(); return outcome(false, {}); }); }
    void refreshBulkMutationsMaintainIndex() { verify([](const QDir&) { Tests::RefreshAudit::bulkOrdering(); return outcome(false, {}); }); }
    void refreshReusedStoreSaveReloadSave() { verify([](const QDir& d) { Tests::RefreshAudit::reusedStore(d); return outcome(false, {}); }); }
    void refreshCustomGridMatchesPrintAndPersistence() { verify([](const QDir& d) { Tests::RefreshAudit::grids(d); return outcome(false, {}); }); }
    void refreshDataAttributesPreserveExpressions() { verify([](const QDir&) { Tests::RefreshAudit::attributes(); return outcome(false, {}); }); }
    void refreshFontDialogsPreserveDescriptors() { verify([](const QDir&) { Tests::RefreshAudit::fonts(); return outcome(false, {}); }); }
    void refreshTemplateOwnerIsExclusive() { verify([](const QDir&) { Tests::RefreshAudit::templateOwnership(); return outcome(false, {}); }); }
    void refreshBulkCloseIncludesNewDraftDeck() { verify([](const QDir&) { Tests::RefreshAudit::closeDrafts(); return outcome(false, {}); }); }
    void refreshDraftDescriptionPersistsAndUndoes() { verify([](const QDir& d) { Tests::RefreshAudit::draftDescription(d); return outcome(false, {}); }); }
    void refreshEmptySchemaImportsAreEditable() { verify([](const QDir& d) { Tests::RefreshAudit::emptySchemas(d); return outcome(false, {}); }); }
    void contentUndoCannotRemoveEncryption() { verify([](const QDir& d) { return securityUndo(d); }); }
    void savingAfterUndoKeepsCurrentStorageOrigin() { verify([](const QDir& d) { return saveUndo(d); }); }
    void failedReusedStoreSavePreservesOriginalBytes() { verify([](const QDir& d) { return interruptedStore(d); }); }
    void importedReportCopiesHaveIndependentPersistentIdentities() { verify([](const QDir& d) { return reportIdentity(d); }); }
    void sameModeActivationRebindsToolbarToActiveReport() { verify([](const QDir&) { return wrongDesigner(); }); }
    void reportFormattingPreservesCompleteExpressions() { verify([](const QDir&) { return dataFormatting(); }); }
    void sidebarAndToolbarStaySynchronized() { verify([](const QDir&) { return staleToolbar(); }); }
    void undoRebuildsTemplateFieldSelectors() { verify([](const QDir&) { return templateUndoSelector(); }); }
    void frameTablesDoNotAcceptDiscardedEdits() { verify([](const QDir&) { return frameTableEdits(); }); }
    void cancellingDialerConfigurationRollsBackQuickDials() { verify([](const QDir&) { return dialerCancel(); }); }
    void paletteArrowsWorkWithoutMouseInput() { verify([](const QDir&) { return colorKeys(); }); }
    void indexBarHasNonConflictingKeyboardAccess()
    {
        verify([](const QDir&) { return indexKeys(); });
        MainWindow window(nullptr, true, false);
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.show();
        events();
        auto* bar = window.findChild<QWidget*>(QStringLiteral("indexBarContainer"));
        QVERIFY(bar);
        const auto buttons = bar->findChildren<QPushButton*>();
        QCOMPARE(buttons.size(), 37);
        QCOMPARE(buttons.first()->text(), QString());
        QCOMPARE(buttons.first()->focusPolicy(), Qt::TabFocus);
        QSignalSpy activated(buttons.at(1), &QPushButton::clicked);
        QApplication::setActiveWindow(&window);
        buttons.first()->setFocus();
        QTest::keyClick(buttons.first(), Qt::Key_Right);
        QCOMPARE(window.focusWidget(), buttons.at(1));
        QCOMPARE(buttons.at(1)->focusPolicy(), Qt::TabFocus);
        QCOMPARE(buttons.first()->focusPolicy(), Qt::NoFocus);
        QTest::keyClick(buttons.at(1), Qt::Key_Return);
        QCOMPARE(activated.size(), 1);
        QTest::keyClick(buttons.at(1), Qt::Key_Space);
        QCOMPARE(activated.size(), 2);
        buttons.at(1)->click();
        QCOMPARE(activated.size(), 3);
        QTest::keyClick(buttons.at(1), Qt::Key_Left);
        QCOMPARE(window.focusWidget(), buttons.first());
        QTest::keyClick(buttons.first(), Qt::Key_Left);
        QCOMPARE(window.focusWidget(), buttons.first());
    }
    void groupSelectionKeepsBulkToolbarAndSingleAttributesDistinct()
    {
        MainWindow window(nullptr, true, false);
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.resize(1180, 760);
        window.show();
        events();
        auto* designer = newReport(window);
        QVERIFY(designer);
        designer->addTextFrameWithText(QStringLiteral("Bulk-format heading"), 0);
        const int textIndex = designer->selectedFrameIndex();
        designer->addLineBoxFrame();
        const int lineIndex = designer->selectedFrameIndex();
        const auto lineBefore = designer->report().frames.at(lineIndex);
        Tests::DesignerSelectionProbe::selectRows(*designer, {textIndex, lineIndex});
        events();
        auto* attributes = command(window.menuBar()->actions(), UiIds::Command::ToolFrameAttributes);
        QVERIFY(attributes && !attributes->isEnabled());
        QVERIFY(command(window.menuBar()->actions(), UiIds::Command::EditCopy)->isEnabled());
        QVERIFY(command(window.menuBar()->actions(), UiIds::Command::EditClear)->isEnabled());
        auto* bold = properties(window)->findChild<QToolButton*>(QStringLiteral("designerGroupBoldButton"));
        QVERIFY(bold);
        bold->click();
        QVERIFY((designer->report().frames.at(textIndex).styleFlags & ReportStyleFlagBold) != 0);
        QCOMPARE(designer->report().frames.at(lineIndex), lineBefore);
        designer->undo();
        Tests::DesignerSelectionProbe::expectSelection(*designer, {textIndex, lineIndex});
        Tests::DesignerSelectionProbe::selectRows(*designer, {textIndex});
        events();
        QVERIFY(attributes->isEnabled());
        QVERIFY(properties(window)->findChild<QLineEdit*>(QStringLiteral("designerReportTextEdit")));
        if (!qEnvironmentVariable("CARDSTACK_GROUP_GALLERY_DIR").isEmpty()) {
            auto* mdi = window.findChild<QMdiArea*>();
            QVERIFY(mdi && mdi->activeSubWindow());
            mdi->activeSubWindow()->showMaximized();
            events();
            auto* canvas = designer->findChild<QWidget*>(QStringLiteral("reportDesignCanvas"));
            QVERIFY(canvas);
            const auto moveForCapture = [&](int index, QPoint destination) {
                Tests::DesignerSelectionProbe::selectRows(*designer, {index});
                events();
                const QRect bounds = designer->report().frames.at(index).bounds;
                const QSize size(designer->report().formWidth, designer->report().formHeight);
                const QPoint anchor(bounds.width() / 3, bounds.height() / 3);
                const QPoint start = Tests::DesignerSelectionProbe::point(canvas, size, 28, bounds.topLeft() + anchor);
                const QPoint end = Tests::DesignerSelectionProbe::point(canvas, size, 28,
                    destination + anchor);
                Tests::DesignerSelectionProbe::pointer(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
                Tests::DesignerSelectionProbe::pointer(canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
                Tests::DesignerSelectionProbe::pointer(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
            };
            moveForCapture(textIndex, QPoint(900, 3000));
            moveForCapture(lineIndex, QPoint(3600, 4500));
            Tests::DesignerSelectionProbe::selectRows(*designer, {textIndex, lineIndex});
            events();
            Tests::DesignerSelectionProbe::capture(window, QStringLiteral("app_report_group_toolbar"));
        }
        auto* workspace = window.findChild<DeckWorkspace*>();
        QVERIFY(workspace);
        activate(window, workspace);
        command(window, UiIds::Command::FileRedefine)->trigger();
        events();
        auto* templateDesigner = window.findChild<TemplateDesignerWidget*>();
        QVERIFY(templateDesigner);
        QVector<int> selected;
        templateDesigner->addTextFrameWithText(QStringLiteral("Template bulk caption"));
        selected.append(templateDesigner->selectedFrameIndex());
        templateDesigner->addTextFrameWithText(QStringLiteral("Second template caption"));
        selected.append(templateDesigner->selectedFrameIndex());
        QCOMPARE(selected.size(), 2);
        Tests::DesignerSelectionProbe::selectRows(*templateDesigner, selected);
        events();
        auto* templateAttributes = command(window.menuBar()->actions(), UiIds::Command::ToolFrameAttributes);
        QVERIFY(templateAttributes && !templateAttributes->isEnabled());
        auto* templateBold = properties(window)->findChild<QToolButton*>(QStringLiteral("designerGroupBoldButton"));
        QVERIFY(templateBold);
        const auto before = templateDesigner->layoutDefinition().frames;
        templateBold->click();
        if (!qEnvironmentVariable("CARDSTACK_GROUP_GALLERY_DIR").isEmpty()) {
            auto* mdi = window.findChild<QMdiArea*>();
            QVERIFY(mdi && mdi->activeSubWindow());
            mdi->activeSubWindow()->showMaximized();
            events();
            Tests::DesignerSelectionProbe::capture(window, QStringLiteral("app_template_group_toolbar"));
        }
        templateDesigner->undo();
        QCOMPARE(templateDesigner->layoutDefinition().frames, before);
        Tests::DesignerSelectionProbe::expectSelection(*templateDesigner, selected);
    }

    void reportAlignmentToolbarIconsContainVisibleInk()
    {
        MainWindow window(nullptr, true, false);
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.show();
        events();
        auto* designer = newReport(window);
        QVERIFY(designer);
        designer->addTextFrameWithText(QStringLiteral("Alignment icon check"), 0);
        for (const QString& name : {QStringLiteral("align-left"),
                                    QStringLiteral("align-center"), QStringLiteral("align-right")}) {
            auto* button = window.findChild<QAbstractButton*>(QStringLiteral("designer-%1").arg(name));
            QVERIFY2(button, qPrintable(name));
            const auto raster = button->icon().pixmap(QSize(24, 24)).toImage();
            QVERIFY2(!raster.isNull(), qPrintable(name));
            int darkPixels = 0;
            for (int y = 0; y < raster.height(); ++y) {
                for (int x = 0; x < raster.width(); ++x) {
                    const QColor pixel = raster.pixelColor(x, y);
                    darkPixels += pixel.alpha() > 0 && pixel.red() < 120
                        && pixel.green() < 120 && pixel.blue() < 120;
                }
            }
            QVERIFY2(darkPixels > 10, qPrintable(name));
        }
    }
    void dialerConfigurationFitsItsVisibleControls()
    {
        auto dialog = UiBuilder::createDialog(QStringLiteral("PHNDEF"));
        QVERIFY(dialog);
        int contentBottom = 0;
        for (QWidget* child : dialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
            if (!child->isHidden()) contentBottom = qMax(contentBottom, child->geometry().bottom());
        }
        QVERIFY(contentBottom > 0);
        QVERIFY(dialog->height() - contentBottom <= 32);
    }
    void dialogLabelMnemonicsAreBound() { verify([](const QDir&) { return labelMnemonics(); }); }
    void templateAppearanceParticipatesInUndo() { verify([](const QDir&) { return appearanceUndo(); }); }
    void malformedHeaderIsRejectedWithoutCrashing() { verify([](const QDir& d) { return nativeHeader(d); }); }

    void removingSecurityCannotBeReversedByContentUndo()
    {
        Deck source = makeDeck();
        QVERIFY(source.setSecurity(QStringLiteral("Old audit security"), true));
        DeckWorkspace workspace(source);
        workspace.setDeckDescription(QStringLiteral("A content snapshot"));
        workspace.clearSecurity();
        QVERIFY(workspace.undo());
        QVERIFY(!workspace.hasSecurity());
        QVERIFY(!workspace.hasEncryptedSecurity());
        QVERIFY(workspace.deck().securityKey() == nullptr);
    }

    void paletteNavigationMatchesOriginalClampedArrowRules()
    {
        auto dialog = UiBuilder::createDialog(QStringLiteral("CHOOSECOLOR"));
        QVERIFY(dialog);
        QStringList colors;
        for (int index = 0; index < 7; ++index) colors.append(QStringLiteral("#123456"));
        UiBuilder::setColorDialogState(dialog.get(), colors, false);
        auto* grid = UiBuilder::controlById(dialog.get(), UiIds::Control::ColorCustomGrid);
        QVERIFY(grid);
        QTest::keyClick(grid, Qt::Key_Left);
        QCOMPARE(UiBuilder::colorDialogColors(dialog.get()).at(5), QStringLiteral("#ff80ff"));
        QTest::keyClick(grid, Qt::Key_Right);
        QCOMPARE(UiBuilder::colorDialogColors(dialog.get()).at(5), QStringLiteral("#ff80ff"));
        UiBuilder::setColorDialogState(dialog.get(), colors, false);
        QTest::keyClick(grid, Qt::Key_Up);
        QCOMPARE(UiBuilder::colorDialogColors(dialog.get()).at(5), QStringLiteral("#000000"));
        QTest::keyClick(grid, Qt::Key_Up);
        QCOMPARE(UiBuilder::colorDialogColors(dialog.get()).at(5), QStringLiteral("#400000"));
    }

    void labelAltKeysFocusTheirVisibleBuddies()
    {
        UiBuilder::DialogContext context;
        context.fieldNames = {QStringLiteral("Name"), QStringLiteral("Status")};
        auto dialog = UiBuilder::createDialog(QStringLiteral("SEARCH"), nullptr, context);
        QVERIFY(dialog);
        dialog->show();
        QApplication::setActiveWindow(dialog.get());
        events();
        int exercised = 0;
        for (auto* label : dialog->findChildren<QLabel*>()) {
            QWidget* buddy = label->buddy();
            if (!label->isVisible() || !buddy || !buddy->isEnabled() || !buddy->isVisible()) continue;
            const QKeySequence sequence = QKeySequence::mnemonic(label->text());
            if (sequence.isEmpty()) continue;
            QWidget* focused = nullptr;
            for (int attempt = 0; attempt < 8; ++attempt) {
                QTest::keySequence(dialog.get(), sequence);
                events();
                focused = dialog->focusWidget();
                if (focused == buddy || (focused && buddy->isAncestorOf(focused))) break;
            }
            const QString diagnostic = QStringLiteral("%1: buddy=%2, dialog focus=%3, app focus=%4, active=%5")
                .arg(label->text(), buddy->objectName(),
                     focused ? focused->objectName() : QStringLiteral("<null>"),
                     QApplication::focusWidget() ? QApplication::focusWidget()->objectName() : QStringLiteral("<null>"),
                     QApplication::activeWindow() ? QApplication::activeWindow()->objectName() : QStringLiteral("<null>"));
            QVERIFY2(focused == buddy || (focused && buddy->isAncestorOf(focused)), qPrintable(diagnostic));
            ++exercised;
        }
        QVERIFY(exercised > 0);
    }

    void reportCanvasArrowsMoveAndShiftArrowsResize()
    {
        ReportDesignerWidget designer(makeReport(), {QStringLiteral("Name")});
        QWidget* canvas = designer.findChild<QWidget*>(QStringLiteral("reportDesignCanvas"));
        QVERIFY(canvas);
        const QRect before = designer.selectedFrameDefinition()->bounds;
        QTest::keyClick(canvas, Qt::Key_Right);
        QCOMPARE(designer.selectedFrameDefinition()->bounds.left(), before.left() + 31);
        QTest::keyClick(canvas, Qt::Key_Down, Qt::ShiftModifier);
        QCOMPARE(designer.selectedFrameDefinition()->bounds.height(), before.height() + 31);
        designer.undo();
        QCOMPARE(designer.selectedFrameDefinition()->bounds.height(), before.height());
        designer.undo();
        QCOMPARE(designer.selectedFrameDefinition()->bounds, before);
        QTest::keyClick(canvas, Qt::Key_Delete);
        QVERIFY(designer.report().frames.isEmpty());
    }

    void allContextHelpUsesReadableDialogTitles() { Tests::UiPolishProbe::helpTitles(); }
    void customFormHelpIsAvailableFromKeyboard() { Tests::UiPolishProbe::customFormKeyboardHelp(); }
    void customFormGeometryUsesPageMarginsAndGrid() { Tests::UiPolishProbe::formGeometry(); }
    void microScrollArrowsAreSubstantialWithoutResizing() { Tests::UiPolishProbe::microScroll(); }

    void frameTableEnterOpensAttributesInsteadOfEditingCells()
    {
        const auto verify = [](auto& designer, auto signal) {
            auto* table = designer.template findChild<QTableWidget*>();
            QVERIFY(table);
            QCOMPARE(table->editTriggers(), QAbstractItemView::NoEditTriggers);
            QSignalSpy commands(&designer, signal);
            table->setCurrentCell(0, 2);
            const QString originalText = table->item(0, 2)->text();
            for (Qt::Key key : {Qt::Key_Return, Qt::Key_Enter}) {
                commands.clear();
                QTest::keyClick(table, key);
                QCOMPARE(commands.size(), 1);
                QCOMPARE(commands.last().at(0).toInt(), UiIds::Command::ToolFrameAttributes);
                QCOMPARE(table->item(0, 2)->text(), originalText);
                QVERIFY(!table->isPersistentEditorOpen(table->item(0, 2)));
                QVERIFY(!designer.isDirty());
            }
        };
        ReportDesignerWidget report(makeReport(), {QStringLiteral("Name")});
        verify(report, &ReportDesignerWidget::commandRequested);
        QCOMPARE(report.report().frames.at(0).text, QStringLiteral("Original frame text"));

        CardTemplateLayout layout;
        CardTemplateFrame text;
        text.kind = CardTemplateFrameKind::Text;
        text.text = QStringLiteral("Original template text");
        text.bounds = QRect(300, 300, 1800, 400);
        layout.frames.append(text);
        TemplateDesignerWidget cardTemplate(layout, {});
        verify(cardTemplate, &TemplateDesignerWidget::commandRequested);
        QCOMPARE(cardTemplate.layoutDefinition().frames.at(0).text, text.text);
    }

    void canonicalAliasesShareOneDocumentLock()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("original.cardstack"));
        const QString alias = directory.filePath(QStringLiteral("alias.cardstack"));
        QString error;
        QVERIFY2(SQLitePackageStore::saveDeckPackage(makeDeck(), path, &error), qPrintable(error));
#ifdef Q_OS_WIN
        if (!CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(alias.utf16()),
                                 reinterpret_cast<LPCWSTR>(path.utf16()), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
            QSKIP("File symlink creation is not permitted on this Windows runner; Unix CI exercises this alias case.");
        }
#else
        QVERIFY(QFile::link(path, alias));
#endif
        QCOMPARE(documentLockPath(path), documentLockPath(alias));
        QLockFile lock(documentLockPath(path));
        lock.setStaleLockTime(0);
        QVERIFY(lock.tryLock(0));
        QProcess worker;
        worker.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--hold-document-lock"), alias});
        QVERIFY(worker.waitForStarted(3000));
        const bool finished = worker.waitForFinished(3000);
        if (!finished) { worker.kill(); worker.waitForFinished(3000); }
        QVERIFY(finished);
        QCOMPARE(worker.exitStatus(), QProcess::NormalExit);
        QCOMPARE(worker.exitCode(), 1);
    }

    void malformedNativeInputsFailClosed_data()
    {
        QTest::addColumn<QString>("mutation");
        for (const char* name : {"zero-page", "tiny-page", "zero-record", "oversized-record", "invalid-fragment",
                                 "fragment-outside-page", "unterminated-table", "cyclic-fragment", "valid-fragment"})
            QTest::newRow(name) << QString::fromLatin1(name);
    }
    void malformedNativeInputsFailClosed()
    {
        QFETCH(QString, mutation);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QByteArray bytes(1536, '\0');
        const auto u16 = [&bytes](int offset, quint16 value) {
            bytes[offset] = static_cast<char>(value & 0xff);
            bytes[offset + 1] = static_cast<char>(value >> 8);
        };
        u16(6, 0x0500); u16(8, 512); u16(0x16, 16); u16(0x18, 20);
        u16(0x1c, 1); bytes[0x38] = 1;
        u16(512 + 4, 0x8001);
        bytes[512 + 6 + 8] = 1;
        bytes[512 + 6 + 16 + 1] = 2;
        u16(1024 + 510, 16);
        u16(1024 + 508, 32);
        for (int index = 16; index < 32; ++index) bytes[1024 + index] = 'Z';
        if (mutation == QStringLiteral("zero-page")) u16(8, 0);
        else if (mutation == QStringLiteral("tiny-page")) u16(8, 1);
        else if (mutation == QStringLiteral("zero-record")) u16(0x18, 0);
        else if (mutation == QStringLiteral("oversized-record")) u16(0x16, 600);
        else if (mutation == QStringLiteral("invalid-fragment")) bytes[512 + 6 + 19] = static_cast<char>(255);
        else if (mutation == QStringLiteral("fragment-outside-page")) {
            u16(1024 + 510, 0x7000); u16(1024 + 508, 0x7010);
        } else if (mutation == QStringLiteral("unterminated-table")) {
            for (int index = 0; index < 510; ++index) bytes[1024 + index] = static_cast<char>(0xff);
        } else if (mutation == QStringLiteral("cyclic-fragment")) {
            u16(1024 + 510, 0x8010);
            bytes[1024 + 16] = 0; bytes[1024 + 17] = 2; bytes[1024 + 18] = 0; bytes[1024 + 19] = 0;
        }
        const QString path = directory.filePath(QStringLiteral("synthetic.BTN"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        QProcess worker;
        worker.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--read-native-btrieve"), path});
        QVERIFY(worker.waitForStarted(3000));
        const bool finished = worker.waitForFinished(5000);
        if (!finished) { worker.kill(); worker.waitForFinished(3000); }
        QVERIFY(finished);
        QCOMPARE(worker.exitStatus(), QProcess::NormalExit);
        QCOMPARE(worker.exitCode(), mutation == QStringLiteral("valid-fragment") ? 0 : 7);
    }

    void writesProductionStyledClosureGalleryWhenConfigured()
    {
        const QString path = qEnvironmentVariable("CARDSTACK_AUDIT_GALLERY_DIR");
        if (path.isEmpty()) QSKIP("Set CARDSTACK_AUDIT_GALLERY_DIR for opt-in visual inspection captures.");
        verify([path](const QDir&) {
            QDir directory(path);
            require(QDir().mkpath(directory.absolutePath()), QStringLiteral("Gallery directory unavailable"));
            QString html = QStringLiteral("<!doctype html><meta charset=\"utf-8\"><title>Audit closure gallery</title>"
                "<style>body{font:16px Segoe UI,sans-serif;margin:24px;background:#eef1f5}"
                "figure{margin:0 0 30px;padding:16px;background:white}img{max-width:100%;height:auto}</style>"
                "<h1>Production-styled audit closure states</h1>");
            const auto capture = [&](QWidget& widget, const QString& name) {
                widget.setAttribute(Qt::WA_DontShowOnScreen);
                widget.show();
                events();
                const QPixmap image = widget.grab();
                require(!image.isNull() && image.save(directory.filePath(name + QStringLiteral(".png"))),
                        QStringLiteral("Capture failed: %1").arg(name));
                html += QStringLiteral("<figure><figcaption>%1</figcaption><img src=\"%1.png\"></figure>").arg(name);
            };
            MainWindow window(nullptr, true, false);
            window.setAttribute(Qt::WA_DontShowOnScreen);
            window.resize(1180, 760);
            window.show(); events();
            auto* deck = window.findChild<DeckWorkspace*>();
            require(deck != nullptr, QStringLiteral("Gallery deck missing"));
            capture(window, QStringLiteral("01_deck_card_toolbar"));
            auto* indexBar = window.findChild<QWidget*>(QStringLiteral("indexBarContainer"));
            require(indexBar != nullptr, QStringLiteral("Gallery index bar missing"));
            const auto indexButtons = indexBar->findChildren<QPushButton*>();
            require(!indexButtons.isEmpty(), QStringLiteral("Gallery index buttons missing"));
            capture(window, QStringLiteral("01a_index_bar"));
            deck->showTableView();
            capture(window, QStringLiteral("02_deck_table_toolbar"));
            auto* first = newReport(window);
            first->setReportName(QStringLiteral("Report A"));
            first->addTextFrameWithText(QStringLiteral("Current report heading"), 0);
            capture(window, QStringLiteral("03_report_text_toolbar"));
            capture(*first, QStringLiteral("03a_report_text_designer"));
            first->addLineBoxFrame();
            capture(window, QStringLiteral("04_report_line_toolbar"));
            capture(*first, QStringLiteral("04a_report_line_designer"));
            activate(window, deck);
            auto* second = newReport(window);
            second->setReportName(QStringLiteral("Report B"));
            second->addTextFrameWithText(QStringLiteral("Other report heading"), 0);
            activate(window, first);
            capture(window, QStringLiteral("05_same_mode_active_report_A"));
            const Deck templateDeck = makeDeck();
            TemplateDesignerWidget templateDesigner(templateDeck.cardTemplateLayout(), templateDeck.fields(), nullptr, templateDeck.appearance());
            templateDesigner.setAttribute(Qt::WA_DontShowOnScreen);
            templateDesigner.resize(940, 620);
            capture(templateDesigner, QStringLiteral("06_template_data_properties"));
            templateDesigner.addTextFrameWithText(QStringLiteral("Template caption"), 0);
            capture(templateDesigner, QStringLiteral("07_template_text_properties"));
            templateDesigner.addLineBoxFrame();
            capture(templateDesigner, QStringLiteral("08_template_line_properties"));
            auto colors = UiBuilder::createDialog(QStringLiteral("CHOOSECOLOR"));
            colors->setAttribute(Qt::WA_DontShowOnScreen);
            colors->show();
            QApplication::setActiveWindow(colors.get());
            auto* grid = control<QWidget>(colors.get(), UiIds::Control::ColorCustomGrid);
            grid->setFocus();
            QTest::keyClick(grid, Qt::Key_Right);
            capture(*colors, QStringLiteral("09_color_keyboard_focus"));
            UiBuilder::DialogContext context;
            context.fieldNames = {QStringLiteral("Name"), QStringLiteral("Status")};
            for (const auto& name : {QStringLiteral("SEARCH"), QStringLiteral("REPLACE"), QStringLiteral("PHNDEF")}) {
                auto dialog = UiBuilder::createDialog(name, nullptr, context);
                capture(*dialog, QStringLiteral("10_") + name.toLower());
                if (name == QStringLiteral("SEARCH")) {
                    control<QAbstractButton>(dialog.get(), UiIds::Control::SearchCompareAnd)->click();
                    capture(*dialog, QStringLiteral("11_search_and"));
                }
            }
            writeBytes(directory.filePath(QStringLiteral("index.html")), html.toUtf8());
            return outcome(false, {{QStringLiteral("gallery"), directory.absolutePath()}});
        });
    }
};

int runFocusedAuditRegressionTests(int argc, char** argv)
{
    FocusedAuditRegressionTests test;
    return QTest::qExec(&test, argc, argv);
}

#include "FocusedAuditRegressionTests.moc"

