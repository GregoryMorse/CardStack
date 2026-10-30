#pragma once

#include "ApplicationAppearance.h"
#include "LegacyInterchangeReader.h"
#include "ReportPrintEngine.h"
#include "ReportTextLayout.h"
#include <QClipboard>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFontDialog>
#include <QMessageBox>
#include <QPainter>
#include <QPdfWriter>
#include <QTableView>
#include <QTextCursor>
#include <algorithm>

namespace CardStack::Tests::RefreshAudit {

inline constexpr int ModalTimeoutMs = 3000;
inline constexpr qreal GeometryTolerance = 0.000001;

class HiddenWindows : public QObject {
public:
    HiddenWindows()
        : m_nativeDialogsDisabled(QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs))
    {
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        qApp->installEventFilter(this);
    }
    ~HiddenWindows() override
    {
        qApp->removeEventFilter(this);
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, m_nativeDialogsDisabled);
    }
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Polish) {
            if (auto* widget = qobject_cast<QWidget*>(watched); widget && widget->isWindow())
                widget->setAttribute(Qt::WA_DontShowOnScreen);
        }
        return false;
    }
private:
    bool m_nativeDialogsDisabled;
};

struct ModalStep {
    QString name;
    std::function<void(QDialog*)> handle;
};

inline void flow(const std::function<void()>& open, const QVector<ModalStep>& steps)
{
    int index = 0;
    bool expired = false;
    std::exception_ptr failure;
    QElapsedTimer deadline;
    deadline.start();
    QTimer timer;
    timer.setInterval(5);
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        // A handler may enter a nested modal loop. Arm the next independent
        // timer event before handling it so nested prompts remain bounded.
        timer.start();
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (failure || deadline.elapsed() > ModalTimeoutMs) {
            expired = !failure;
            if (dialog) dialog->reject();
            return;
        }
        if (!dialog || index >= steps.size()) return;
        const ModalStep step = steps.at(index);
        const QString actual = dialog->property("legacyDialogName").toString();
        const bool matches = actual == step.name
            || (step.name == QStringLiteral("message") && qobject_cast<QMessageBox*>(dialog))
            || (step.name == QStringLiteral("font") && qobject_cast<QFontDialog*>(dialog))
            || (step.name == QStringLiteral("file") && qobject_cast<QFileDialog*>(dialog));
        if (!matches) return;
        ++index;
        try { step.handle(dialog); }
        catch (...) { failure = std::current_exception(); dialog->reject(); }
    });
    timer.start();
    open();
    while (index < steps.size() && !failure && deadline.elapsed() < ModalTimeoutMs) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    timer.stop();
    if (failure) std::rethrow_exception(failure);
    require(!expired && index == steps.size(), QStringLiteral("Modal sequence incomplete: %1/%2").arg(index).arg(steps.size()));
    events();
}

inline void message(QDialog* dialog, QMessageBox::StandardButton choice)
{
    auto* box = qobject_cast<QMessageBox*>(dialog);
    require(box && box->button(choice), QStringLiteral("Expected message choice unavailable"));
    box->button(choice)->click();
}

inline DeckWorkspace* workspace(MainWindow& window)
{
    auto* result = window.findChild<DeckWorkspace*>();
    require(result != nullptr, QStringLiteral("Deck missing"));
    return result;
}

inline void show(MainWindow& window)
{
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.resize(1180, 780);
    window.show();
    events();
}

inline void visibleMdiChild(MainWindow& window, QWidget* widget)
{
    auto* mdi = window.findChild<QMdiArea*>();
    auto* child = windowFor(window, widget);
    require(mdi->viewport()->rect().contains(child->geometry()),
            QStringLiteral("New MDI child is partly outside the viewport"));
}

inline void capture(QWidget& widget, const QString& name)
{
    const QString path = qEnvironmentVariable("CARDSTACK_REFRESH_GALLERY_DIR");
    if (path.isEmpty()) return;
    require(QDir().mkpath(path), QStringLiteral("Gallery directory unavailable"));
    widget.ensurePolished();
    events();
    const QPixmap image = widget.grab();
    require(!image.isNull() && image.save(QDir(path).filePath(name + QStringLiteral(".png"))),
            QStringLiteral("Capture failed: %1").arg(name));
}

inline Deck searchableDeck(const QString& value)
{
    Deck deck(QStringLiteral("Text replacement"));
    deck.addField(FieldDefinition(QStringLiteral("Name"), FieldType::Text, 255));
    deck.addField(FieldDefinition(QStringLiteral("Notes"), FieldType::Notes, 4096));
    deck.addCard(CardRecord({value, QStringLiteral("First line\nLtd appears in notes.")}));
    return deck;
}

inline DeckWorkspace::SearchRequest contains(const QString& text, int field = 0)
{
    DeckWorkspace::SearchRequest request;
    request.first.text = text;
    request.first.fieldIndex = field;
    request.first.type = DeckWorkspace::SearchType::Contains;
    return request;
}

inline void replacements()
{
    const QString original = QStringLiteral("Atlas Ltd, London; Ltd");
    DeckWorkspace work(searchableDeck(original));
    auto request = contains(QStringLiteral("Ltd"));
    require(work.find(request), QStringLiteral("Initial match missing"));
    require(work.replaceCurrent(request, QStringLiteral("LLC")), QStringLiteral("Single replacement failed"));
    require(work.deck().cardAt(0).valueAt(0) == QStringLiteral("Atlas LLC, London; Ltd"), QStringLiteral("Single replacement discarded surrounding text"));
    require(work.undo() && work.deck().cardAt(0).valueAt(0) == original, QStringLiteral("Single replacement Undo corrupted"));
    require(work.replaceAll(request, QStringLiteral("LLC")) == 2, QStringLiteral("Repeated matches not counted"));
    require(work.deck().cardAt(0).valueAt(0) == QStringLiteral("Atlas LLC, London; LLC"), QStringLiteral("Replace All discarded text"));
    require(work.undo() && work.deck().cardAt(0).valueAt(0) == original, QStringLiteral("Replace All Undo corrupted"));
    require(!work.canUndo(), QStringLiteral("Replace All generated multiple Undo steps"));

    DeckWorkspace words(searchableDeck(QStringLiteral("Ltdish Ltd ltd")));
    request.first.wholeWord = true;
    require(words.replaceAll(request, QStringLiteral("LLC")) == 2, QStringLiteral("Whole-word matching failed"));
    require(words.deck().cardAt(0).valueAt(0) == QStringLiteral("Ltdish LLC LLC"), QStringLiteral("Whole-word boundaries damaged"));

    DeckWorkspace exact(searchableDeck(QStringLiteral("Ltd ltd")));
    request.first.caseSensitive = true;
    require(exact.replaceAll(request, QStringLiteral("")) == 1, QStringLiteral("Case-sensitive deletion failed"));
    require(exact.deck().cardAt(0).valueAt(0) == QStringLiteral(" ltd"), QStringLiteral("Deletion lost suffix"));

    DeckWorkspace notes(searchableDeck(QStringLiteral("Atlas")));
    require(notes.replaceAll(contains(QStringLiteral("Ltd"), 1), QStringLiteral("LLC")) == 1, QStringLiteral("Notes replacement failed"));
    require(notes.deck().cardAt(0).valueAt(1) == QStringLiteral("First line\nLLC appears in notes."), QStringLiteral("Notes lines were lost"));

    Deck constrained(QStringLiteral("Limited field"));
    constrained.addField(FieldDefinition(QStringLiteral("Code"), FieldType::Text, 8));
    constrained.addCard(CardRecord({QStringLiteral("ABCDxy")}));
    DeckWorkspace limited(constrained);
    limited.replaceAll(contains(QStringLiteral("xy")), QStringLiteral("123456"));
    require(limited.deck().cardAt(0).valueAt(0) == QStringLiteral("ABCD1234"), QStringLiteral("Replacement length limit ignored"));
}

inline void searchBoundaries()
{
    Deck deck = makeDeck();
    deck.cardAt(0).setValueAt(1, QStringLiteral("Ltd Ltd"));
    deck.cardAt(1).setValueAt(1, QStringLiteral("Ltd"));
    DeckWorkspace work(deck);
    auto request = contains(QStringLiteral("Ltd"), 1);
    require(work.find(request), QStringLiteral("Search failed"));
    auto* edit = work.findChild<QLineEdit*>(QStringLiteral("fieldValue_1"));
    require(edit && edit->selectionStart() == 0, QStringLiteral("First match not selected"));
    require(work.findNext() && work.currentCardIndex() == 0 && edit->selectionStart() == 4, QStringLiteral("Next occurrence skipped"));
    require(work.findNext() && work.currentCardIndex() == 1, QStringLiteral("Next card skipped"));
    require(!work.findNext() && work.currentCardIndex() == 1, QStringLiteral("End search repeated current card"));
    require(work.findNext(request, true) && work.currentCardIndex() == 0, QStringLiteral("Explicit forward wrap failed"));

    work.lastCard();
    request.direction = DeckWorkspace::SearchDirection::BackwardFromCurrent;
    require(work.find(request) && work.findNext() && work.currentCardIndex() == 0, QStringLiteral("Backward search failed"));
    require(edit->selectionStart() == 4, QStringLiteral("Backward search did not select last occurrence"));
    require(work.findNext() && edit->selectionStart() == 0, QStringLiteral("Backward occurrence skipped"));
    require(!work.findNext(), QStringLiteral("Beginning search repeated current card"));
    require(work.findNext(request, true) && work.currentCardIndex() == 1, QStringLiteral("Explicit backward wrap failed"));

    auto comparison = contains(QStringLiteral("zzz"));
    comparison.first.type = DeckWorkspace::SearchType::LessThan;
    require(work.replaceAll(comparison, QStringLiteral("Complete")) == 2, QStringLiteral("Comparison matches failed"));
    require(work.deck().cardAt(0).valueAt(0) == QStringLiteral("Complete"), QStringLiteral("Comparison criterion did not replace complete value"));
}

inline void replaceThroughGui()
{
    HiddenWindows hidden;
    MainWindow window(nullptr, true, false);
    show(window);
    auto* work = workspace(window);
    work->showTableView();
    auto* table = work->findChild<QTableView*>(QStringLiteral("deckTableView"));
    require(table != nullptr, QStringLiteral("Table missing"));
    require(table->model()->setData(table->model()->index(0, 1), QStringLiteral("Atlas Ltd, London")), QStringLiteral("Edit failed"));
    table->setCurrentIndex(table->model()->index(0, 1));
    flow([&] { command(window, UiIds::Command::SearchReplace)->trigger(); }, {
        {QStringLiteral("REPLACE"), [](QDialog* dialog) {
            require(control<QLineEdit>(dialog, UiIds::Control::ReplaceCurrentData)->text() == QStringLiteral("Atlas Ltd, London"),
                    QStringLiteral("Current data readout was not initialized"));
            control<QComboBox>(dialog, UiIds::Control::SearchText)->setEditText(QStringLiteral("Ltd"));
            auto* fields = control<QComboBox>(dialog, UiIds::Control::SearchAllDataBoxes);
            fields->setCurrentIndex(fields->findText(QStringLiteral("Status")));
            control<QComboBox>(dialog, UiIds::Control::SearchType)->setCurrentIndex(1);
            control<QComboBox>(dialog, UiIds::Control::SearchSecondText)->setEditText(QStringLiteral("LLC"));
            capture(*dialog, QStringLiteral("01_replace_preserves_surrounding_text"));
            dialog->accept();
        }}
    });
    require(work->deck().cardAt(0).valueAt(1) == QStringLiteral("Atlas LLC, London"), QStringLiteral("GUI Replace discarded text"));
    require(work->undo() && work->deck().cardAt(0).valueAt(1) == QStringLiteral("Atlas Ltd, London"), QStringLiteral("GUI replacement Undo failed"));
}


inline void findNavigationThroughGui()
{
    HiddenWindows hidden;
    MainWindow window(nullptr, true, false);
    show(window);
    auto* work = workspace(window);
    work->showTableView();
    auto* table = work->findChild<QTableView*>(QStringLiteral("deckTableView"));
    require(table && work->deck().cardCount() >= 2, QStringLiteral("Search setup missing"));
    for (int row : {0, 1})
        require(table->model()->setData(table->model()->index(row, 1), row == 0 ? QStringLiteral("Ready Alpha") : QStringLiteral("Ready Beta")), QStringLiteral("Search setup edit failed"));
    auto request = contains(QStringLiteral("Ready"), 1);
    require(work->find(request) && work->currentCardIndex() == 0, QStringLiteral("Search setup match failed"));
    flow([&] { command(window, UiIds::Command::SearchReplace)->trigger(); }, {
        {QStringLiteral("REPLACE"), [&](QDialog* dialog) {
            control<QComboBox>(dialog, UiIds::Control::SearchText)->setEditText(QStringLiteral("Ready"));
            auto* fields = control<QComboBox>(dialog, UiIds::Control::SearchAllDataBoxes);
            fields->setCurrentIndex(fields->findText(QStringLiteral("Status")));
            control<QComboBox>(dialog, UiIds::Control::SearchType)->setCurrentIndex(1);
            auto* next = control<QAbstractButton>(dialog, UiIds::Control::SearchDirectionForward);
            next->click();
            require(work->currentCardIndex() == 1, QStringLiteral("Replace Find Next repeated current card"));
            require(control<QLineEdit>(dialog, UiIds::Control::ReplaceCurrentData)->text() == QStringLiteral("Ready Beta"),
                    QStringLiteral("Current data readout did not follow Find Next"));
            next->click();
            require(work->currentCardIndex() == 1, QStringLiteral("Declined wrap moved selection"));
            control<QAbstractButton>(dialog, UiIds::Control::SearchDirectionBackward)->click();
            require(work->currentCardIndex() == 0, QStringLiteral("Replace Find Previous repeated current card"));
            require(control<QLineEdit>(dialog, UiIds::Control::ReplaceCurrentData)->text() == QStringLiteral("Ready Alpha"),
                    QStringLiteral("Current data readout did not follow Find Previous"));
            dialog->reject();
        }},
        {QStringLiteral("message"), [](QDialog* dialog) {
            require(qobject_cast<QMessageBox*>(dialog)->text().contains(QStringLiteral("beginning")), QStringLiteral("Wrong forward wrap prompt"));
            capture(*dialog, QStringLiteral("01a_find_wrap_question"));
            message(dialog, QMessageBox::No);
        }},
        {QStringLiteral("message"), [](QDialog* dialog) { message(dialog, QMessageBox::Ok); }}
    });
    flow([&] { command(window, UiIds::Command::SearchFindNext)->trigger(); }, {
        {QStringLiteral("message"), [](QDialog* dialog) {
            require(qobject_cast<QMessageBox*>(dialog)->text().contains(QStringLiteral("end")), QStringLiteral("Wrong backward wrap prompt"));
            message(dialog, QMessageBox::Yes);
        }}
    });
    require(work->currentCardIndex() == work->deck().cardCount() - 1, QStringLiteral("Accepted backward wrap did not reach last match"));
}

inline void bulkOrdering()
{
    auto request = contains(QStringLiteral("Alpha"));
    DeckWorkspace replacement(makeDeck());
    require(replacement.replaceAll(request, QStringLiteral("Zulu")) == 1, QStringLiteral("Replace order setup failed"));
    require(replacement.deck().cardAt(0).valueAt(0) == QStringLiteral("Beta")
        && replacement.deck().cardAt(1).valueAt(0) == QStringLiteral("Zulu"), QStringLiteral("Replace did not reindex"));
    require(replacement.currentCardIndex() == 1, QStringLiteral("Replacement lost active card identity"));
    require(replacement.undo() && replacement.deck().cardAt(0).valueAt(0) == QStringLiteral("Alpha"), QStringLiteral("Sorted replacement Undo failed"));

    DeckWorkspace pasted(makeDeck());
    QApplication::clipboard()->setText(QStringLiteral("Zulu\tReady\nAble\tPending"));
    pasted.smartPaste();
    require(pasted.deck().cardAt(0).valueAt(0) == QStringLiteral("Able")
        && pasted.deck().cardAt(1).valueAt(0) == QStringLiteral("Zulu"), QStringLiteral("Smart Paste did not reindex"));
    require(pasted.currentCardIndex() == 1, QStringLiteral("Smart Paste lost active card"));
    require(pasted.undo() && pasted.deck().cardAt(0).valueAt(0) == QStringLiteral("Alpha"), QStringLiteral("Paste Undo failed"));

    Deck source = makeDeck();
    source.setCards({CardRecord({QStringLiteral("Able"), QStringLiteral("Merged")})});
    DeckWorkspace merged(makeDeck());
    DeckMergeOptions options;
    options.fieldMappings = {{0, 0}, {1, 1}};
    const auto result = merged.mergeFromDeck(source, options);
    require(result.ok() && result.cardsMerged == 1, QStringLiteral("Merge failed"));
    require(merged.deck().cardAt(0).valueAt(0) == QStringLiteral("Able")
        && merged.deck().cardAt(merged.currentCardIndex()).valueAt(0) == QStringLiteral("Alpha"), QStringLiteral("Merge order/selection incorrect"));
    require(merged.undo() && merged.deck().cardCount() == 2, QStringLiteral("Merge Undo failed"));
}

inline void reusedStore(const QDir& directory)
{
    QElapsedTimer totalTime;
    QElapsedTimer stageTime;
    totalTime.start();
    stageTime.start();
    const auto checkpoint = [&](const char* stage) {
        qInfo().noquote().nospace() << "[reused-store] " << stage << ": +"
            << stageTime.restart() << " ms, total " << totalTime.elapsed() << " ms";
    };
    checkpoint("start");
    const QString path = directory.filePath(QStringLiteral("reused.cardstack"));
    QString error;
    require(SQLitePackageStore::saveDeckPackage(makeDeck(), path, &error), error);
    checkpoint("initial package saved");
    SQLiteDeckStore store;
    Deck loaded;
    require(store.open(path, &error), error);
    checkpoint("store opened");
    require(store.loadDeck(&loaded, &error), error);
    checkpoint("initial deck loaded");
    Deck stale = loaded;
    loaded.setDescription(QStringLiteral("First edit"));
    require(store.saveDeck(loaded, &error), error);
    checkpoint("first edit saved");
    require(store.loadDeck(&loaded, &error), error);
    checkpoint("first edit reloaded");
    loaded.setDescription(QStringLiteral("Second edit"));
    require(store.saveDeck(loaded, &error), error);
    checkpoint("second edit saved");
    require(!store.saveDeck(stale, &error), QStringLiteral("Truly stale snapshot accepted after reload"));
    checkpoint("stale snapshot rejected");
    SQLiteDeckStore disk;
    Deck saved;
    require(disk.open(path, &error, {}, SQLiteDeckStore::OpenMode::ReadOnly), error);
    checkpoint("read-only store opened");
    require(disk.loadDeck(&saved, &error), error);
    checkpoint("saved deck reloaded");
    require(saved.description() == QStringLiteral("Second edit"), QStringLiteral("Second save not persisted"));
    checkpoint("persisted value verified");
}

inline ReportDesignerWidget* customGrid(MainWindow& window, int rows, int columns, bool margins)
{
    const auto before = window.findChildren<ReportDesignerWidget*>();
    flow([&] { command(window, UiIds::Command::FileNewReport)->trigger(); }, {
        {QStringLiteral("REPORTFORM"), [](QDialog* dialog) {
            control<QAbstractButton>(dialog, UiIds::Control::ReportFormCustom)->click();
        }},
        {QStringLiteral("DEFINEFORM"), [=](QDialog* dialog) {
            control<QAbstractButton>(dialog, UiIds::Control::DefineFormLabel)->click();
            control<QAbstractButton>(dialog, UiIds::Control::DefineFormLandscape)->click();
            const auto edit = [&](int id, const QString& text) { control<QLineEdit>(dialog, id)->setText(text); };
            edit(UiIds::Control::DefineFormWidth, QStringLiteral("6.000"));
            edit(UiIds::Control::DefineFormHeight, QStringLiteral("4.000"));
            for (int id : {UiIds::Control::DefineFormMarginLeft, UiIds::Control::DefineFormMarginRight,
                           UiIds::Control::DefineFormMarginTop, UiIds::Control::DefineFormMarginBottom})
                edit(id, margins ? QStringLiteral("0.250") : QStringLiteral("0"));
            for (int id : {UiIds::Control::DefineFormHorizontalGutter, UiIds::Control::DefineFormVerticalGutter})
                edit(id, margins ? QStringLiteral("0.100") : QStringLiteral("0"));
            edit(UiIds::Control::DefineFormRows, QString::number(rows));
            edit(UiIds::Control::DefineFormColumns, QString::number(columns));
            events();
            auto* sample = UiBuilder::controlById(dialog, UiIds::Control::DefineFormSample);
            require(sample && sample->property("formSampleRows").toInt() == rows
                && sample->property("formSampleColumns").toInt() == columns, QStringLiteral("Sample grid axes incorrect"));
            capture(*dialog, margins ? QStringLiteral("03_custom_grid_with_margins") : QStringLiteral("02_custom_grid_asymmetric"));
            require(control<QAbstractButton>(dialog, UiIds::Control::Ok)->isEnabled(), QStringLiteral("Valid grid rejected"));
            dialog->accept();
        }}
    });
    for (auto* designer : window.findChildren<ReportDesignerWidget*>())
        if (!before.contains(designer)) return designer;
    throw std::runtime_error("Custom report missing");
}

inline void grids(const QDir& directory)
{
    HiddenWindows hidden;
    for (bool margins : {false, true}) {
        MainWindow window(nullptr, true, false);
        show(window);
        auto* designer = customGrid(window, 2, 3, margins);
        visibleMdiChild(window, designer);
        const auto report = designer->report();
        for (const auto& frame : report.frames) {
            const auto& font = frame.kind == ReportFrameKind::Data ? report.dataFont : report.textFont;
            require(frame.bounds.height() >= qCeil(QFontMetricsF(ReportTextLayout::font(font, frame.styleFlags)).height()),
                    QStringLiteral("Starter frame clips its configured font"));
        }
        require(report.rows == 3 && report.columns == 2, QStringLiteral("Across/down persistence adapter incorrect"));
        require(report.formWidth == (margins ? 1766 : 2000)
            && report.formHeight == (margins ? 1700 : 2000), QStringLiteral("Cell dimensions incorrect"));
        const auto pages = ReportPrintEngine::paginate(report, 6);
        require(pages.size() == 1 && pages.first().cells.size() == 6, QStringLiteral("Grid pagination incorrect"));
        for (int i = 0; i < pages.first().cells.size(); ++i) {
            const auto& cell = pages.first().cells.at(i);
            require(cell.recordIndex == i && cell.target.left() >= 0 && cell.target.top() >= 0
                && cell.target.right() <= 1.0 + GeometryTolerance && cell.target.bottom() <= 1.0 + GeometryTolerance,
                QStringLiteral("Grid cell outside page or records repeated"));
            for (int j = 0; j < i; ++j)
                require(!cell.target.intersects(pages.first().cells.at(j).target), QStringLiteral("Grid cells overlap"));
        }
        Deck deck = makeDeck();
        deck.addReport(report);
        const QString path = directory.filePath(margins ? QStringLiteral("grid-margins.cardstack") : QStringLiteral("grid.cardstack"));
        QString error;
        require(SQLitePackageStore::saveDeckPackage(deck, path, &error), error);
        SQLiteDeckStore store;
        Deck reloaded;
        require(store.open(path, &error, {}, SQLiteDeckStore::OpenMode::ReadOnly) && store.loadDeck(&reloaded, &error), error);
        require(reloaded.reportAt(0).rows == 3 && reloaded.reportAt(0).columns == 2
            && reloaded.reportAt(0).formWidth == report.formWidth, QStringLiteral("Grid changed on round trip"));

        if (!margins && !qEnvironmentVariable("CARDSTACK_REFRESH_GALLERY_DIR").isEmpty()) {
            QVector<ReportPreviewData> records;
            for (int i = 0; i < 6; ++i) {
                ReportPreviewData data;
                data.fieldValues.insert(QStringLiteral("Project"), QStringLiteral("Cell %1").arg(i + 1));
                records.append(data);
            }
            designer->addDataFrameForField(QStringLiteral("Project"), 0, false);
            auto rendered = designer->report();
            rendered.frames = {rendered.frames.last()};
            rendered.frames.last().bounds = QRect(100, 100, 1700, 350);
            require(QRect(0, 0, rendered.formWidth, rendered.formHeight).contains(designer->selectedFrameDefinition()->bounds),
                    QStringLiteral("New frame starts outside the form"));
            QImage image(960, 640, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image);
            ReportPrintEngine::renderPage(&painter, rendered, records, pages.first(), QRectF(image.rect()));
            painter.end();
            require(image.save(QDir(qEnvironmentVariable("CARDSTACK_REFRESH_GALLERY_DIR")).filePath(QStringLiteral("04_grid_print_render.png"))),
                    QStringLiteral("Grid print capture failed"));
            capture(window, QStringLiteral("05_custom_report_designer"));
        }
    }
}

inline void attributes()
{
    HiddenWindows hidden;
    MainWindow window(nullptr, true, false);
    show(window);
    auto* designer = newReport(window);
    designer->addDataFrameForField(QStringLiteral("Project"), 0, false);
    const QString expression = QStringLiteral("[Project], [Status] [Lead]");
    designer->updateSelectedFrameFromToolbar(expression, ReportStyleFlagItalic, true, 0, 0, 0, 0);
    flow([&] { command(window, UiIds::Command::ToolFrameAttributes)->trigger(); }, {
        {QStringLiteral("DATAFRAME"), [](QDialog* dialog) {
            capture(*dialog, QStringLiteral("06_data_expression_attributes"));
            dialog->accept();
        }}
    });
    require(designer->selectedFrameDefinition()->text == expression, QStringLiteral("No-op attributes discarded expression"));
    flow([&] { command(window, UiIds::Command::ToolFrameAttributes)->trigger(); }, {
        {QStringLiteral("DATAFRAME"), [](QDialog* dialog) {
            control<QAbstractButton>(dialog, UiIds::Control::DataFramePrintEntireContents)->setChecked(false);
            dialog->accept();
        }}
    });
    require(designer->selectedFrameDefinition()->text == expression
        && !designer->selectedFrameDefinition()->printEntireContentsFlag, QStringLiteral("Style-only attributes discarded expression"));
    flow([&] { command(window, UiIds::Command::ToolFrameAttributes)->trigger(); }, {
        {QStringLiteral("DATAFRAME"), [](QDialog* dialog) {
            auto* fields = control<QComboBox>(dialog, UiIds::Control::DataFrameFieldList);
            fields->setCurrentIndex(fields->findText(QStringLiteral("Status")));
            require(QMetaObject::invokeMethod(fields, "activated", Q_ARG(int, fields->currentIndex())), QStringLiteral("Field activation failed"));
            dialog->accept();
        }}
    });
    require(designer->selectedFrameDefinition()->text == QStringLiteral("[Status]"), QStringLiteral("Explicit field change ignored"));
    designer->undo();
    require(designer->selectedFrameDefinition()->text == expression, QStringLiteral("Attribute Undo discarded expression"));
}

inline void fonts()
{
    HiddenWindows hidden;
    MainWindow window(nullptr, true, false);
    show(window);
    auto* designer = newReport(window);
    for (bool dataFont : {true, false}) {
        for (int height : {-90, 120}) {
            ReportFontDefinition original;
            original.faceName = QStringLiteral("Arial");
            original.legacyHeight = height;
            original.width = 30;
            original.weight = 700;
            original.italic = original.underline = original.strikeOut = true;
            designer->setReportFont(dataFont, original);
            flow([&] { command(window, dataFont ? UiIds::Command::ConfigureDataFont : UiIds::Command::ConfigureTextFont)->trigger(); }, {
                {QStringLiteral("font"), [=](QDialog* dialog) {
                    auto* font = qobject_cast<QFontDialog*>(dialog);
                    require(font && font->currentFont().bold() && font->currentFont().italic()
                        && font->currentFont().underline() && font->currentFont().strikeOut(),
                        QStringLiteral("Font traits not initialized: role=%1 height=%2 font=%3 bold=%4 italic=%5 underline=%6 strike=%7")
                            .arg(dataFont).arg(height).arg(font ? font->currentFont().toString() : QString())
                            .arg(font && font->currentFont().bold()).arg(font && font->currentFont().italic())
                            .arg(font && font->currentFont().underline()).arg(font && font->currentFont().strikeOut()));
                    capture(*dialog, QStringLiteral("07_report_%1_font_%2").arg(dataFont ? QStringLiteral("data") : QStringLiteral("text")).arg(height));
                    dialog->accept();
                }}
            });
            require((dataFont ? designer->report().dataFont : designer->report().textFont) == original,
                    QStringLiteral("No-op font dialog changed stored descriptor"));
        }
    }
}

inline TemplateDesignerWidget* draft(MainWindow& window)
{
    flow([&] { command(window, UiIds::Command::FileNew)->trigger(); }, {
        {QStringLiteral("NEWFILE"), [](QDialog* dialog) {
            control<QComboBox>(dialog, UiIds::Control::NewFileSourceCombo)->setCurrentIndex(2);
            dialog->accept();
        }}
    });
    auto* designer = window.findChild<TemplateDesignerWidget*>();
    require(designer != nullptr && window.findChildren<DeckWorkspace*>().isEmpty(), QStringLiteral("Draft opened an unwanted deck"));
    return designer;
}

inline void templateOwnership()
{
    HiddenWindows hidden;
    MainWindow window(nullptr, true, false);
    show(window);
    auto* work = workspace(window);
    auto* owner = windowFor(window, work);
    command(window, UiIds::Command::FileRedefine)->trigger();
    events();
    auto* designer = window.findChild<TemplateDesignerWidget*>();
    require(designer && !owner->isVisible(), QStringLiteral("Template owner not hidden"));
    visibleMdiChild(window, designer);
    window.findChild<QMdiArea*>()->setActiveSubWindow(owner);
    events();
    require(!owner->isVisible() && window.findChild<QMdiArea*>()->activeSubWindow() == windowFor(window, designer),
            QStringLiteral("Hidden owner was reopened during design"));
    int listedWindows = 0;
    for (QAction* top : window.menuBar()->actions()) {
        if (!top->menu()) continue;
        for (QAction* action : top->menu()->actions()) {
            if (action->property("cardstackDynamicWindowAction").toBool() && !action->isSeparator()) {
                ++listedWindows;
                require(action->isChecked(), QStringLiteral("Designer window not active in menu"));
            }
        }
    }
    require(listedWindows == 1, QStringLiteral("Hidden owner listed as a separate window"));
    capture(window, QStringLiteral("08_template_single_owner_toolbar"));
}

inline void closeDrafts()
{
    HiddenWindows hidden;
    MainWindow window(nullptr, false, false);
    show(window);
    draft(window);
    flow([&] { command(window, UiIds::Command::WindowCloseAll)->trigger(); }, {
        {QStringLiteral("message"), [](QDialog* dialog) { message(dialog, QMessageBox::Yes); }},
        {QStringLiteral("message"), [](QDialog* dialog) { message(dialog, QMessageBox::Cancel); }}
    });
    require(window.findChildren<TemplateDesignerWidget*>().isEmpty() && workspace(window)->isDirty(),
            QStringLiteral("Close All ignored new dirty deck / Cancel"));
    flow([&] { command(window, UiIds::Command::WindowCloseAll)->trigger(); }, {
        {QStringLiteral("message"), [](QDialog* dialog) { message(dialog, QMessageBox::Discard); }}
    });
    require(window.findChild<QMdiArea*>()->subWindowList().isEmpty(), QStringLiteral("Close All left a window"));

    draft(window);
    bool closed = true;
    flow([&] { closed = window.close(); }, {
        {QStringLiteral("message"), [](QDialog* dialog) { message(dialog, QMessageBox::Yes); }},
        {QStringLiteral("message"), [](QDialog* dialog) { message(dialog, QMessageBox::Cancel); }}
    });
    require(!closed && workspace(window)->isDirty(), QStringLiteral("Exit ignored new dirty deck / Cancel"));
}

inline void draftDescription(const QDir& directory)
{
    HiddenWindows hidden;
    MainWindow window(nullptr, false, false);
    show(window);
    auto* designer = draft(window);
    const QString original = designer->description();
    flow([&] { command(window, UiIds::Command::ConfigureDeckDescription)->trigger(); }, {
        {QStringLiteral("CHANGEDESC"), [](QDialog* dialog) {
            control<QLineEdit>(dialog, UiIds::Control::DeckDescriptionText)->setText(QStringLiteral("Reusable project template"));
            capture(*dialog, QStringLiteral("09_draft_description"));
            dialog->accept();
        }}
    });
    require(designer->description() == QStringLiteral("Reusable project template") && designer->isDirty(), QStringLiteral("Draft description not staged"));
    designer->undo();
    require(designer->description() == original, QStringLiteral("Draft description Undo failed"));
    designer->setDescription(QStringLiteral("Reusable project template"));
    const QString path = directory.filePath(QStringLiteral("custom.cardstack-template"));
    designer->setProperty("templatePackagePath", path);
    command(window, UiIds::Command::FileSave)->trigger();
    events();
    require(!designer->isDirty(), QStringLiteral("Draft save failed"));
    SQLiteDeckStore store;
    Deck saved;
    QString error;
    require(store.open(path, &error, {}, SQLiteDeckStore::OpenMode::ReadOnly) && store.loadDeck(&saved, &error), error);
    require(saved.description() == designer->description(), QStringLiteral("Draft description not persisted"));
    flow([&] { command(window, UiIds::Command::FileClose)->trigger(); }, {
        {QStringLiteral("message"), [](QDialog* dialog) { message(dialog, QMessageBox::Yes); }}
    });
    require(workspace(window)->deck().description() == QStringLiteral("Reusable project template"), QStringLiteral("New deck lost template description"));
}

inline void emptySchemas(const QDir& directory)
{
    QByteArray schema(512, '\0');
    schema.replace(5, 4, QByteArrayLiteral("TN20"));
    schema.replace(11, 14, QByteArrayLiteral("Empty template"));
    // A schema record is a valid deck even when it has no user records.
    constexpr int FieldCountOffset = 87;
    constexpr int FieldTableOffset = 256;
    schema[FieldCountOffset] = 1;
    schema.replace(FieldTableOffset, 4, QByteArrayLiteral("Name"));
    schema[FieldTableOffset + 16] = 1;
    schema[FieldTableOffset + 22] = 30;
    const LegacyDeckReader reader;
    const auto imported = reader.readRecords({schema}, QStringLiteral("Empty deck"));
    require(imported.ok() && imported.deck.fieldCount() == 1 && imported.deck.cardCount() == 0, imported.errorMessage);
    DeckWorkspace work(imported.deck);
    auto* edit = work.findChild<QLineEdit*>(QStringLiteral("fieldValue_0"));
    require(edit && edit->isEnabled() && !edit->isReadOnly(), QStringLiteral("Empty import is not editable"));
    edit->setText(QStringLiteral("First card"));
    work.commitPendingEdits();
    require(work.deck().cardCount() == 1, QStringLiteral("Empty import cannot create its first card"));
    require(!reader.readRecords({QByteArray(512, '\0')}).ok(), QStringLiteral("Invalid schema accepted"));

    QByteArray dbf(65, '\0');
    dbf[0] = 3;
    dbf[8] = 65;
    dbf[10] = 31;
    dbf.replace(32, 4, QByteArrayLiteral("NAME"));
    dbf[43] = 'C';
    dbf[48] = 30;
    dbf[64] = '\r';
    const QString path = directory.filePath(QStringLiteral("empty.dbf"));
    writeBytes(path, dbf);
    const auto base = LegacyInterchangeReader().readDBaseFile(path);
    require(base.ok() && base.deck.fieldCount() == 1 && base.deck.cardCount() == 0, base.errorMessage);
    dbf[4] = 1;
    writeBytes(path, dbf);
    require(!LegacyInterchangeReader().readDBaseFile(path).ok(), QStringLiteral("Truncated declared card was mistaken for an empty deck"));
}
} // namespace CardStack::Tests::RefreshAudit
