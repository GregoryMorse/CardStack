#include "DeckWorkspace.h"

#include "Deck.h"
#include "DeckMerge.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSplitter>
#include <QTableView>
#include <QtTest/QtTest>

#include <utility>

using namespace CardStack;

namespace {

Deck createTwoCardDeck()
{
    Deck deck(QStringLiteral("Undo Test"));
    deck.addField(FieldDefinition(QStringLiteral("Name"), FieldType::Text, 255));
    deck.addCard(CardRecord({QStringLiteral("Alpha")}));
    deck.addCard(CardRecord({QStringLiteral("Beta")}));
    return deck;
}

Deck createSmartPasteDeck()
{
    Deck deck(QStringLiteral("Smart Paste Test"));
    deck.addField(FieldDefinition(QStringLiteral("Name"), FieldType::Text, 255));
    deck.addField(FieldDefinition(QStringLiteral("Notes"), FieldType::Text, 255));
    deck.addCard(CardRecord({QStringLiteral("Alpha"), QStringLiteral("Original")}));
    return deck;
}

Deck createConstrainedEditorDeck()
{
    Deck deck(QStringLiteral("Editor Constraint Test"));
    deck.addField(FieldDefinition(QStringLiteral("Short"), FieldType::Text, 5));
    deck.addField(FieldDefinition(QStringLiteral("Memo"), FieldType::Notes, 8));
    deck.addCard(CardRecord({QStringLiteral("Alpha"), QStringLiteral("Original")}));
    return deck;
}

Deck createTemplateLayoutDeck()
{
    Deck deck(QStringLiteral("Template Layout Test"));
    deck.addField(FieldDefinition(QStringLiteral("Name"), FieldType::Text, 255));
    deck.addField(FieldDefinition(QStringLiteral("Notes"), FieldType::Notes, 1024));
    deck.addCard(CardRecord({QStringLiteral("Alpha"), QStringLiteral("Memo")}));

    CardTemplateLayout layout;
    layout.canvasWidth = 6400;
    layout.canvasHeight = 4800;
    CardTemplateFrame nameFrame;
    nameFrame.kind = CardTemplateFrameKind::DataBox;
    nameFrame.fieldIndex = 0;
    nameFrame.text = QStringLiteral("Name");
    nameFrame.bounds = QRect(100, 200, 1000, 240);
    layout.frames.append(nameFrame);

    CardTemplateFrame labelFrame;
    labelFrame.kind = CardTemplateFrameKind::Text;
    labelFrame.text = QStringLiteral("Name label");
    labelFrame.bounds = QRect(100, 80, 1000, 120);
    layout.frames.append(labelFrame);

    CardTemplateFrame notesFrame;
    notesFrame.kind = CardTemplateFrameKind::NotesBox;
    notesFrame.fieldIndex = 1;
    notesFrame.text = QStringLiteral("Notes");
    notesFrame.bounds = QRect(500, 800, 1200, 600);
    layout.frames.append(notesFrame);

    deck.setCardTemplateLayout(layout);
    return deck;
}

Deck createMergeSourceDeck()
{
    Deck deck(QStringLiteral("Merge Source"));
    deck.addField(FieldDefinition(QStringLiteral("Source Name"), FieldType::Text, 255));
    deck.addField(FieldDefinition(QStringLiteral("Source Note"), FieldType::Notes, 255));
    deck.addCard(CardRecord({QStringLiteral("Gamma"), QStringLiteral("Merged note")}));
    return deck;
}

Deck createDeckWithReport()
{
    Deck deck = createTwoCardDeck();
    ReportDefinition report;
    report.name = QStringLiteral("Current Card Report");
    report.formatMagic = QStringLiteral("RPT@#$B");
    report.formWidth = 5000;
    report.formHeight = 3000;
    deck.addReport(report);
    return deck;
}

} // namespace

class DeckWorkspaceTests : public QObject {
    Q_OBJECT

private slots:
    void cardEditorUsesExactTemplateFrameGeometry()
    {
        DeckWorkspace workspace(createTemplateLayoutDeck());
        auto* nameEditor = workspace.findChild<QLineEdit*>(QStringLiteral("fieldValue_0"));
        auto* notesEditor = workspace.findChild<QPlainTextEdit*>(QStringLiteral("fieldValue_1"));
        auto* label = workspace.findChild<QLabel*>(QStringLiteral("templateTextFrame"));

        QVERIFY(nameEditor != nullptr);
        QVERIFY(notesEditor != nullptr);
        QVERIFY(label != nullptr);
        QCOMPARE(nameEditor->geometry(), QRect(22, 32, 100, 24));
        QCOMPARE(notesEditor->geometry(), QRect(62, 92, 120, 60));
        QCOMPARE(label->text(), QStringLiteral("Name label"));
        QCOMPARE(label->geometry(), QRect(22, 20, 100, 12));
        QVERIFY(!label->isHidden());
        const QList<QLabel*> fieldCaptions = workspace.findChildren<QLabel*>(QRegularExpression(QStringLiteral("^fieldCaption_")));
        QCOMPARE(fieldCaptions.size(), 1);
        QCOMPARE(fieldCaptions.first()->objectName(), QStringLiteral("fieldCaption_1"));
        QCOMPARE(fieldCaptions.first()->alignment(), Qt::AlignRight | Qt::AlignVCenter);
        QVERIFY(!fieldCaptions.first()->autoFillBackground());
        QVERIFY(fieldCaptions.first()->geometry().right() < notesEditor->geometry().left());
    }

    void deleteUndoRestoresCardAndUndeleteState()
    {
        DeckWorkspace workspace(createTwoCardDeck());

        QVERIFY(!workspace.canUndo());
        QVERIFY(!workspace.canUndelete());

        workspace.deleteCurrentCard();
        QCOMPARE(workspace.deck().cardCount(), 1);
        QCOMPARE(workspace.deck().cardAt(0).valueAt(0), QStringLiteral("Beta"));
        QVERIFY(workspace.canUndo());
        QVERIFY(workspace.canUndelete());

        QVERIFY(workspace.undo());
        QCOMPARE(workspace.deck().cardCount(), 2);
        QCOMPARE(workspace.deck().cardAt(0).valueAt(0), QStringLiteral("Alpha"));
        QCOMPARE(workspace.deck().cardAt(1).valueAt(0), QStringLiteral("Beta"));
        QVERIFY(!workspace.canUndelete());
    }

    void undeleteRestoresCardAndCanBeUndone()
    {
        DeckWorkspace workspace(createTwoCardDeck());

        workspace.deleteCurrentCard();
        QVERIFY(workspace.undeleteCard());
        QCOMPARE(workspace.deck().cardCount(), 2);
        QCOMPARE(workspace.deck().cardAt(0).valueAt(0), QStringLiteral("Alpha"));
        QCOMPARE(workspace.deck().cardAt(1).valueAt(0), QStringLiteral("Beta"));
        QVERIFY(!workspace.canUndelete());

        QVERIFY(workspace.undo());
        QCOMPARE(workspace.deck().cardCount(), 1);
        QCOMPARE(workspace.deck().cardAt(0).valueAt(0), QStringLiteral("Beta"));
        QVERIFY(workspace.canUndelete());
    }

    void addDuplicateDeleteAndUndeleteKeepCardLifecycleStable()
    {
        DeckWorkspace workspace(createTwoCardDeck());

        const int initialCount = workspace.deck().cardCount();
        workspace.addCard();
        QCOMPARE(workspace.deck().cardCount(), initialCount + 1);
        QCOMPARE(workspace.currentCardIndex(), workspace.deck().cardCount() - 1);
        QCOMPARE(workspace.deck().cardAt(workspace.currentCardIndex()).valueAt(0), QString());

        workspace.duplicateCurrentCard();
        QCOMPARE(workspace.deck().cardCount(), initialCount + 2);
        QCOMPARE(workspace.currentCardIndex(), workspace.deck().cardCount() - 1);

        workspace.deleteCurrentCard();
        QCOMPARE(workspace.deck().cardCount(), initialCount + 1);
        QVERIFY(workspace.canUndelete());

        QVERIFY(workspace.undeleteCard());
        QCOMPARE(workspace.deck().cardCount(), initialCount + 2);
        QCOMPARE(workspace.currentCardIndex(), workspace.deck().cardCount() - 1);

        while (workspace.deck().cardCount() > 1) {
            workspace.deleteCurrentCard();
        }
        QCOMPARE(workspace.deck().cardCount(), 1);

        workspace.deleteCurrentCard();
        QCOMPARE(workspace.deck().cardCount(), 1);
        QCOMPARE(workspace.currentCardIndex(), 0);
        QCOMPARE(workspace.deck().cardAt(0).valueAt(0), QString());
        QVERIFY(workspace.isDirty());
    }

    void deletingLastCardLeavesEditableBlankCard()
    {
        Deck deck(QStringLiteral("Blank Card Test"));
        deck.addField(FieldDefinition(QStringLiteral("Name"), FieldType::Text, 255));
        deck.addCard(CardRecord({QStringLiteral("Only")}));

        DeckWorkspace workspace(deck);
        QCOMPARE(workspace.viewMode(), DeckWorkspace::ViewMode::Card);

        workspace.deleteCurrentCard();
        QCOMPARE(workspace.deck().cardCount(), 1);
        QCOMPARE(workspace.currentCardIndex(), 0);
        QCOMPARE(workspace.deck().cardAt(0).valueAt(0), QString());

        QVERIFY(workspace.undeleteCard());
        QCOMPARE(workspace.deck().cardCount(), 1);
        QCOMPARE(workspace.deck().cardAt(0).valueAt(0), QStringLiteral("Only"));
    }

    void deckDescriptionChangeCanBeUndone()
    {
        DeckWorkspace workspace(createTwoCardDeck());

        workspace.setDeckDescription(QStringLiteral("Deck description"));
        QCOMPARE(workspace.deck().description(), QStringLiteral("Deck description"));
        QVERIFY(workspace.canUndo());

        QVERIFY(workspace.undo());
        QCOMPARE(workspace.deck().description(), QString());
    }

    void smartPasteFillsFieldsAndCreatesCards()
    {
        DeckWorkspace workspace(createSmartPasteDeck());
        QApplication::clipboard()->setText(QStringLiteral("Gamma\tOne\nDelta\tTwo\nEpsilon\tThree"));

        workspace.smartPaste();

        QCOMPARE(workspace.deck().cardCount(), 3);
        QCOMPARE(workspace.deck().cardAt(0).valueAt(0), QStringLiteral("Gamma"));
        QCOMPARE(workspace.deck().cardAt(0).valueAt(1), QStringLiteral("One"));
        QCOMPARE(workspace.deck().cardAt(1).valueAt(0), QStringLiteral("Delta"));
        QCOMPARE(workspace.deck().cardAt(1).valueAt(1), QStringLiteral("Two"));
        QCOMPARE(workspace.deck().cardAt(2).valueAt(0), QStringLiteral("Epsilon"));
        QCOMPARE(workspace.deck().cardAt(2).valueAt(1), QStringLiteral("Three"));

        QVERIFY(workspace.undo());
        QCOMPARE(workspace.deck().cardCount(), 1);
        QCOMPARE(workspace.deck().cardAt(0).valueAt(0), QStringLiteral("Alpha"));
        QCOMPARE(workspace.deck().cardAt(0).valueAt(1), QStringLiteral("Original"));
    }

    void cardEditorHonorsFieldLengthLimits()
    {
        DeckWorkspace workspace(createConstrainedEditorDeck());
        workspace.showCardView();

        auto* shortEditor = workspace.findChild<QLineEdit*>(QStringLiteral("fieldValue_0"));
        auto* memoEditor = workspace.findChild<QPlainTextEdit*>(QStringLiteral("fieldValue_1"));
        QVERIFY(shortEditor != nullptr);
        QVERIFY(memoEditor != nullptr);
        QVERIFY(workspace.findChild<QWidget*>(QStringLiteral("cardDetailPanel")) != nullptr);
        QVERIFY(workspace.findChild<QScrollArea*>(QStringLiteral("cardEditorScrollArea")) != nullptr);

        QCOMPARE(shortEditor->maxLength(), 5);
        shortEditor->setText(QStringLiteral("TooLongForField"));
        memoEditor->setPlainText(QStringLiteral("Long memo text"));
        workspace.commitPendingEdits();

        QCOMPARE(workspace.deck().cardAt(0).valueAt(0), QStringLiteral("TooLo"));
        QCOMPARE(workspace.deck().cardAt(0).valueAt(1), QStringLiteral("Long mem"));
    }

    void tableEditingUsesDeckValidationAndUndo()
    {
        DeckWorkspace workspace(createConstrainedEditorDeck());
        workspace.showTableView();

        auto* tableView = workspace.findChild<QTableView*>(QStringLiteral("deckTableView"));
        QVERIFY(tableView != nullptr);
        QVERIFY(tableView->model()->setData(tableView->model()->index(0, 0), QStringLiteral("TableTooLong"), Qt::EditRole));

        QCOMPARE(workspace.deck().cardAt(0).valueAt(0), QStringLiteral("Table"));
        QVERIFY(workspace.canUndo());

        QVERIFY(workspace.undo());
        QCOMPARE(workspace.deck().cardAt(0).valueAt(0), QStringLiteral("Alpha"));
    }

    void cardViewUsesCardsAsRotatingClickableStack()
    {
        DeckWorkspace workspace(createTwoCardDeck());
        workspace.show();
        workspace.showCardView();
        QCoreApplication::processEvents();

        QVERIFY(workspace.findChild<QWidget*>(QStringLiteral("cardStackNavigator")) == nullptr);
        auto* cardPanel = workspace.findChild<QWidget*>(QStringLiteral("cardDetailPanel"));
        QVERIFY(cardPanel != nullptr);

        QTest::mouseClick(cardPanel, Qt::LeftButton, Qt::NoModifier, QPoint(70, 20));
        QCoreApplication::processEvents();
        QCOMPARE(workspace.currentCardIndex(), 1);

        QTest::mouseClick(cardPanel, Qt::LeftButton, Qt::NoModifier, QPoint(70, 20));
        QCoreApplication::processEvents();
        QCOMPARE(workspace.currentCardIndex(), 0);
    }

    void cardViewLeavesBlankTitlesBlank()
    {
        Deck deck(QStringLiteral("Blank Title Test"));
        deck.addField(FieldDefinition(QStringLiteral("Name"), FieldType::Text, 255));
        deck.addCard(CardRecord({QString()}));
        deck.addCard(CardRecord({QStringLiteral("Alpha")}));

        DeckWorkspace workspace(std::move(deck));
        workspace.showCardView();
        QCoreApplication::processEvents();

        auto* cardPanel = workspace.findChild<QWidget*>(QStringLiteral("cardDetailPanel"));
        QVERIFY(cardPanel != nullptr);
        QVERIFY(cardPanel->accessibleName().isEmpty());
    }

    void indexPrefixJumpUsesNextPopulatedBucket()
    {
        Deck deck(QStringLiteral("Index Test"));
        deck.addField(FieldDefinition(QStringLiteral("Name"), FieldType::Text, 255));
        deck.addCard(CardRecord({QStringLiteral("Alpha")}));
        deck.addCard(CardRecord({QStringLiteral("Delta")}));
        deck.addCard(CardRecord({QStringLiteral("3D Printer")}));

        DeckWorkspace workspace(std::move(deck));

        QVERIFY(workspace.jumpToIndexPrefix(QStringLiteral("B")));
        QCOMPARE(workspace.currentCardIndex(), 1);

        QVERIFY(workspace.jumpToIndexPrefix(QStringLiteral("Z")));
        QCOMPARE(workspace.currentCardIndex(), 2);

        QVERIFY(workspace.jumpToIndexPrefix(QStringLiteral("4")));
        QCOMPARE(workspace.currentCardIndex(), 0);
    }

    void titleEditsKeepDeckInIndexOrder()
    {
        Deck deck(QStringLiteral("Title Sort Test"));
        deck.addField(FieldDefinition(QStringLiteral("Name"), FieldType::Text, 255));
        deck.addCard(CardRecord({QStringLiteral("Alpha")}));
        deck.addCard(CardRecord({QStringLiteral("Charlie")}));
        deck.addCard(CardRecord({QStringLiteral("Delta")}));

        DeckWorkspace workspace(std::move(deck));
        workspace.showCardView();
        QCoreApplication::processEvents();

        auto* editor = workspace.findChild<QLineEdit*>(QStringLiteral("fieldValue_0"));
        QVERIFY(editor != nullptr);
        QCOMPARE(workspace.currentCardIndex(), 0);

        editor->setText(QStringLiteral("Zulu"));
        emit editor->editingFinished();
        QCoreApplication::processEvents();

        QCOMPARE(workspace.deck().cardAt(0).valueAt(0), QStringLiteral("Charlie"));
        QCOMPARE(workspace.deck().cardAt(1).valueAt(0), QStringLiteral("Delta"));
        QCOMPARE(workspace.deck().cardAt(2).valueAt(0), QStringLiteral("Zulu"));
        QCOMPARE(workspace.currentCardIndex(), 2);

        editor->setText(QStringLiteral("3D Printer"));
        emit editor->editingFinished();
        QCoreApplication::processEvents();

        QCOMPARE(workspace.deck().cardAt(0).valueAt(0), QStringLiteral("Charlie"));
        QCOMPARE(workspace.deck().cardAt(1).valueAt(0), QStringLiteral("Delta"));
        QCOMPARE(workspace.deck().cardAt(2).valueAt(0), QStringLiteral("3D Printer"));
        QCOMPARE(workspace.currentCardIndex(), 2);
    }

    void tableSelectionControlsCardEditorRecord()
    {
        DeckWorkspace workspace(createTwoCardDeck());
        workspace.showTableView();

        auto* tableView = workspace.findChild<QTableView*>(QStringLiteral("deckTableView"));
        auto* splitter = workspace.findChild<QSplitter*>(QStringLiteral("deckListDetailSplitter"));
        QVERIFY(tableView != nullptr);
        QVERIFY(splitter != nullptr);
        QCOMPARE(splitter->count(), 1);
        tableView->setCurrentIndex(tableView->model()->index(1, 0));

        workspace.showCardView();
        auto* editor = workspace.findChild<QLineEdit*>(QStringLiteral("fieldValue_0"));
        QVERIFY(editor != nullptr);
        QCOMPARE(editor->text(), QStringLiteral("Beta"));
    }

    void writesManualDeckInspectionImagesWhenConfigured()
    {
        const QString outputPath = qEnvironmentVariable("CARDSTACK_DECK_GALLERY_DIR");
        if (outputPath.isEmpty()) {
            QSKIP("Set CARDSTACK_DECK_GALLERY_DIR to write deck card/table PNGs for manual inspection.");
        }

        QDir outputDirectory(outputPath);
        QVERIFY2(outputDirectory.exists() || outputDirectory.mkpath(QStringLiteral(".")), qPrintable(outputPath));

        DeckWorkspace workspace(createTemplateLayoutDeck());
        workspace.resize(900, 620);
        workspace.show();
        QVERIFY(QTest::qWaitForWindowExposed(&workspace));

        workspace.showCardView();
        QCoreApplication::processEvents();
        QTest::qWait(30);
        QVERIFY(workspace.grab().save(outputDirectory.filePath(QStringLiteral("deck_card_view_template_layout.png"))));

        workspace.showTableView();
        QCoreApplication::processEvents();
        QTest::qWait(30);
        QVERIFY(workspace.grab().save(outputDirectory.filePath(QStringLiteral("deck_table_view.png"))));
    }

    void fieldSearchMovesToMatchingCard()
    {
        Deck deck = createSmartPasteDeck();
        deck.addCard(CardRecord({QStringLiteral("Beta"), QStringLiteral("Needle")}));
        DeckWorkspace workspace(std::move(deck));

        DeckWorkspace::SearchRequest request;
        request.first.text = QStringLiteral("Needle");
        request.first.fieldIndex = 1;
        request.first.type = DeckWorkspace::SearchType::Contains;

        QVERIFY(workspace.find(request));
        QCOMPARE(workspace.currentCardIndex(), 1);
    }

    void searchTypesAndDirectionsFollowDialogSemantics()
    {
        Deck deck(QStringLiteral("Search Type Test"));
        deck.addField(FieldDefinition(QStringLiteral("Name"), FieldType::Text, 255));
        deck.addCard(CardRecord({QStringLiteral("Alpha One")}));
        deck.addCard(CardRecord({QStringLiteral("Beta Two")}));
        deck.addCard(CardRecord({QStringLiteral("Gamma Three")}));

        DeckWorkspace workspace(std::move(deck));

        DeckWorkspace::SearchRequest beginsWith;
        beginsWith.first.text = QStringLiteral("beta");
        beginsWith.first.fieldIndex = 0;
        beginsWith.first.type = DeckWorkspace::SearchType::BeginsWith;
        QVERIFY(workspace.find(beginsWith));
        QCOMPARE(workspace.currentCardIndex(), 1);

        workspace.lastCard();
        DeckWorkspace::SearchRequest backward = beginsWith;
        backward.direction = DeckWorkspace::SearchDirection::BackwardFromCurrent;
        QVERIFY(workspace.find(backward));
        QCOMPARE(workspace.currentCardIndex(), 1);

        DeckWorkspace::SearchRequest doesNotContain;
        doesNotContain.first.text = QStringLiteral("Two");
        doesNotContain.first.fieldIndex = 0;
        doesNotContain.first.type = DeckWorkspace::SearchType::DoesNotContain;
        QVERIFY(workspace.find(doesNotContain));
        QCOMPARE(workspace.currentCardIndex(), 0);

        DeckWorkspace::SearchRequest greaterThan;
        greaterThan.first.text = QStringLiteral("Beta Two");
        greaterThan.first.fieldIndex = 0;
        greaterThan.first.type = DeckWorkspace::SearchType::GreaterThan;
        QVERIFY(workspace.find(greaterThan));
        QCOMPARE(workspace.currentCardIndex(), 2);
    }

    void sortCardsUpdatesProfileAndCanBeUndone()
    {
        Deck deck(QStringLiteral("Sort Test"));
        deck.addField(FieldDefinition(QStringLiteral("Name"), FieldType::Text, 255));
        deck.addField(FieldDefinition(QStringLiteral("Group"), FieldType::Text, 255));
        deck.addCard(CardRecord({QStringLiteral("Charlie"), QStringLiteral("B")}));
        deck.addCard(CardRecord({QStringLiteral("Alpha"), QStringLiteral("A")}));
        deck.addCard(CardRecord({QStringLiteral("Beta"), QStringLiteral("A")}));

        DeckWorkspace workspace(std::move(deck));
        workspace.sortCards({
            {1, false},
            {0, true},
        });

        QCOMPARE(workspace.deck().cardAt(0).valueAt(0), QStringLiteral("Beta"));
        QCOMPARE(workspace.deck().cardAt(1).valueAt(0), QStringLiteral("Alpha"));
        QCOMPARE(workspace.deck().cardAt(2).valueAt(0), QStringLiteral("Charlie"));
        QCOMPARE(workspace.deck().sortKeyCount(), 2);
        QCOMPARE(workspace.deck().sortKeys().at(0).fieldIndex, 1);
        QCOMPARE(workspace.deck().sortKeys().at(0).descending, false);
        QCOMPARE(workspace.deck().sortKeys().at(1).fieldIndex, 0);
        QCOMPARE(workspace.deck().sortKeys().at(1).descending, true);

        QVERIFY(workspace.undo());
        QCOMPARE(workspace.deck().cardAt(0).valueAt(0), QStringLiteral("Charlie"));
        QCOMPARE(workspace.deck().sortKeyCount(), 0);
    }

    void mergeFromDeckCanBeUndone()
    {
        DeckWorkspace workspace(createSmartPasteDeck());
        DeckMergeOptions options;
        options.fieldMappings = {
            {0, 0},
            {1, 1},
        };

        const DeckMergeResult result = workspace.mergeFromDeck(createMergeSourceDeck(), options);
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.cardsMerged, 1);
        QCOMPARE(workspace.deck().cardCount(), 2);
        QCOMPARE(workspace.deck().cardAt(1).valueAt(0), QStringLiteral("Gamma"));
        QCOMPARE(workspace.deck().cardAt(1).valueAt(1), QStringLiteral("Merged note"));

        QVERIFY(workspace.canUndo());
        QVERIFY(workspace.undo());
        QCOMPARE(workspace.deck().cardCount(), 1);
        QCOMPARE(workspace.deck().cardAt(0).valueAt(0), QStringLiteral("Alpha"));
    }

    void redefineFieldsMigratesValuesAndCanBeUndone()
    {
        DeckWorkspace workspace(createConstrainedEditorDeck());

        QString error;
        const bool changed = workspace.redefineFields(
            {
                FieldDefinition(QStringLiteral("Memo"), FieldType::Notes, 8),
                FieldDefinition(QStringLiteral("Shorter"), FieldType::Text, 3),
                FieldDefinition(QStringLiteral("Added"), FieldType::Text, 10),
            },
            {1, 0, -1},
            &error);

        QVERIFY2(changed, qPrintable(error));
        QCOMPARE(workspace.deck().fieldCount(), 3);
        QCOMPARE(workspace.deck().fieldAt(0).name(), QStringLiteral("Memo"));
        QCOMPARE(workspace.deck().fieldAt(1).name(), QStringLiteral("Shorter"));
        QCOMPARE(workspace.deck().cardAt(0).valueAt(0), QStringLiteral("Original"));
        QCOMPARE(workspace.deck().cardAt(0).valueAt(1), QStringLiteral("Alp"));
        QCOMPARE(workspace.deck().cardAt(0).valueAt(2), QString());

        QVERIFY(workspace.canUndo());
        QVERIFY(workspace.undo());
        QCOMPARE(workspace.deck().fieldCount(), 2);
        QCOMPARE(workspace.deck().fieldAt(0).name(), QStringLiteral("Short"));
        QCOMPARE(workspace.deck().cardAt(0).valueAt(0), QStringLiteral("Alpha"));
        QCOMPARE(workspace.deck().cardAt(0).valueAt(1), QStringLiteral("Original"));
    }

    void cardMutationsPreserveImportedReports()
    {
        DeckWorkspace workspace(createDeckWithReport());

        workspace.deleteCurrentCard();
        QCOMPARE(workspace.deck().reportCount(), 1);
        QCOMPARE(workspace.deck().reportAt(0).name, QStringLiteral("Current Card Report"));

        workspace.undo();
        QCOMPARE(workspace.deck().reportCount(), 1);
        QCOMPARE(workspace.deck().reportAt(0).formatMagic, QStringLiteral("RPT@#$B"));
    }

    void reportDefinitionsSaveDeleteAndUndo()
    {
        DeckWorkspace workspace(createTwoCardDeck());

        ReportDefinition report;
        report.name = QStringLiteral("Designer Report");
        report.formatMagic = QStringLiteral("RPT@#$B");
        report.formWidth = 8500;
        report.formHeight = 11000;

        QVERIFY(workspace.saveReportDefinition(-1, report));
        QCOMPARE(workspace.deck().reportCount(), 1);
        QCOMPARE(workspace.deck().reportAt(0).name, QStringLiteral("Designer Report"));

        report.name = QStringLiteral("Renamed Report");
        QVERIFY(workspace.saveReportDefinition(0, report));
        QCOMPARE(workspace.deck().reportCount(), 1);
        QCOMPARE(workspace.deck().reportAt(0).name, QStringLiteral("Renamed Report"));

        QVERIFY(workspace.removeReportDefinition(0));
        QCOMPARE(workspace.deck().reportCount(), 0);

        QVERIFY(workspace.insertReportDefinition(0, report));
        QCOMPARE(workspace.deck().reportCount(), 1);
        QCOMPARE(workspace.deck().reportAt(0).name, QStringLiteral("Renamed Report"));

        QVERIFY(workspace.undo());
        QCOMPARE(workspace.deck().reportCount(), 0);
    }
};

#ifdef CARDSTACK_TEST_SUITE
int runDeckWorkspaceTests(int argc, char** argv)
{
    DeckWorkspaceTests test;
    return QTest::qExec(&test, argc, argv);
}
#else
QTEST_MAIN(DeckWorkspaceTests)
#endif

#include "DeckWorkspaceTests.moc"

