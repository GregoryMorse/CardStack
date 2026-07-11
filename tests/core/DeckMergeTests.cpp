#include "DeckMerge.h"

#include <QtTest/QtTest>

using namespace CardStack;

namespace {

Deck destinationDeck()
{
    Deck deck(QStringLiteral("Destination"));
    deck.addField(FieldDefinition(QStringLiteral("Product"), FieldType::Text, 7));
    deck.addField(FieldDefinition(QStringLiteral("Company"), FieldType::Text, 20));
    deck.addField(FieldDefinition(QStringLiteral("Notes"), FieldType::Notes, 12));
    deck.addCard(CardRecord({QStringLiteral("Existing"), QStringLiteral("Already here"), QString()}));
    return deck;
}

Deck sourceDeck()
{
    Deck deck(QStringLiteral("Source"));
    deck.addField(FieldDefinition(QStringLiteral("Name"), FieldType::Text, 30));
    deck.addField(FieldDefinition(QStringLiteral("Vendor"), FieldType::Text, 30));
    deck.addField(FieldDefinition(QStringLiteral("Memo"), FieldType::Notes, 80));
    deck.addCard(CardRecord({QStringLiteral("ProjectKit"), QStringLiteral("Open Tools"), QStringLiteral("First note")}));
    deck.addCard(CardRecord({QStringLiteral("Windows"), QStringLiteral("Microsoft"), QStringLiteral("Second note")}));
    return deck;
}

DeckMergeOptions defaultMapping()
{
    DeckMergeOptions options;
    options.fieldMappings = {
        {0, 0},
        {1, 1},
        {2, 2},
    };
    return options;
}

} // namespace

class DeckMergeTests : public QObject {
    Q_OBJECT

private slots:
    void appendsAllMappedSourceCards()
    {
        Deck destination = destinationDeck();
        const Deck source = sourceDeck();

        const DeckMergeResult result = mergeDecks(&destination, source, defaultMapping());
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));

        QCOMPARE(result.cardsMerged, 2);
        QCOMPARE(result.valuesMapped, 6);
        QCOMPARE(destination.cardCount(), 3);
        QCOMPARE(destination.cardAt(0).valueAt(0), QStringLiteral("Existing"));
        QCOMPARE(destination.cardAt(1).valueAt(0), QStringLiteral("Project"));
        QCOMPARE(destination.cardAt(1).valueAt(1), QStringLiteral("Open Tools"));
        QCOMPARE(destination.cardAt(1).valueAt(2), QStringLiteral("First note"));
        QCOMPARE(destination.cardAt(2).valueAt(0), QStringLiteral("Windows"));
    }

    void canMergeOnlySelectedSourceCards()
    {
        Deck destination = destinationDeck();
        const Deck source = sourceDeck();
        DeckMergeOptions options = defaultMapping();
        options.scope = DeckMergeOptions::Scope::SelectedCards;
        options.selectedSourceCardIndexes = {1};

        const DeckMergeResult result = mergeDecks(&destination, source, options);
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));

        QCOMPARE(result.cardsMerged, 1);
        QCOMPARE(destination.cardCount(), 2);
        QCOMPARE(destination.cardAt(1).valueAt(0), QStringLiteral("Windows"));
        QCOMPARE(destination.cardAt(1).valueAt(1), QStringLiteral("Microsoft"));
        QCOMPARE(destination.cardAt(1).valueAt(2), QStringLiteral("Second note"));
    }

    void rejectsMissingValidMappings()
    {
        Deck destination = destinationDeck();
        const Deck source = sourceDeck();
        DeckMergeOptions options;
        options.fieldMappings = {{99, 0}, {0, 99}};

        const DeckMergeResult result = mergeDecks(&destination, source, options);
        QVERIFY(!result.ok());
        QCOMPARE(destination.cardCount(), 1);
    }

    void rejectsEmptySelectedScope()
    {
        Deck destination = destinationDeck();
        const Deck source = sourceDeck();
        DeckMergeOptions options = defaultMapping();
        options.scope = DeckMergeOptions::Scope::SelectedCards;
        options.selectedSourceCardIndexes = {42};

        const DeckMergeResult result = mergeDecks(&destination, source, options);
        QVERIFY(!result.ok());
        QCOMPARE(destination.cardCount(), 1);
    }
};

#ifdef CARDSTACK_TEST_SUITE
int runDeckMergeTests(int argc, char** argv)
{
    DeckMergeTests test;
    return QTest::qExec(&test, argc, argv);
}
#else
QTEST_GUILESS_MAIN(DeckMergeTests)
#endif

#include "DeckMergeTests.moc"

