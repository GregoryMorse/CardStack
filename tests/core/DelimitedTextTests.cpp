#include "DelimitedText.h"

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>

using namespace CardStack;

namespace {

Deck makeDelimitedDeck()
{
    Deck deck(QStringLiteral("Delimited"));
    deck.addField(FieldDefinition(QStringLiteral("Name"), FieldType::Text, 255));
    deck.addField(FieldDefinition(QStringLiteral("Notes"), FieldType::Notes, 8192));
    deck.addCard(CardRecord({QStringLiteral("Alpha"), QStringLiteral("Plain")}));
    deck.addCard(CardRecord({QStringLiteral("Beta, Inc."), QStringLiteral("Line one\nLine two")}));
    deck.addCard(CardRecord({QStringLiteral("Quote \"Test\""), QStringLiteral("Tabbed\tvalue")}));
    return deck;
}

} // namespace

class DelimitedTextTests : public QObject {
    Q_OBJECT

private slots:
    void mappingsReorderSelectAndRejectInvalidColumns()
    {
        const Deck source = makeDelimitedDeck();
        auto profile = DelimitedText::csvProfile(ImportExportProfileType::Export);
        profile.fieldMappings = {1, 0};
        const QString text = DelimitedText::writeDeck(source, profile);
        QVERIFY(text.startsWith(QStringLiteral("Notes,Name\nPlain,Alpha\n")));
        profile.fieldMappings = {1};
        QVERIFY(DelimitedText::writeDeck(source, profile).startsWith(QStringLiteral("Notes\nPlain\n")));
        profile.type = ImportExportProfileType::Import;
        Deck imported;
        QString error;
        QVERIFY2(DelimitedText::readDeck(QStringLiteral("Name,Notes\nAlpha,Plain\n"), profile, &imported, &error), qPrintable(error));
        QCOMPARE(imported.fieldCount(), 1);
        QCOMPARE(imported.fieldAt(0).name(), QStringLiteral("Notes"));
        QCOMPARE(imported.cardAt(0).valueAt(0), QStringLiteral("Plain"));
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("keep.csv"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("keep"), qint64(4)); file.close();
        for (int invalid : {-1, 2, (std::numeric_limits<int>::max)()}) {
            profile.fieldMappings = {invalid};
            QVERIFY(DelimitedText::writeDeck(source, profile).isNull());
            QVERIFY(!DelimitedText::writeDeckFile(source, profile, path, &error));
            QVERIFY(!DelimitedText::readDeck(QStringLiteral("Name,Notes\nAlpha,Plain\n"), profile, &imported, &error));
            QCOMPARE(imported.cardAt(0).valueAt(0), QStringLiteral("Plain"));
            QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("keep")); file.close();
        }
    }

    void writesCsvWithHeaderAndEscaping()
    {
        const QString text = DelimitedText::writeDeck(
            makeDelimitedDeck(),
            DelimitedText::csvProfile(ImportExportProfileType::Export));

        QVERIFY(text.startsWith(QStringLiteral("Name,Notes\n")));
        QVERIFY(text.contains(QStringLiteral("\"Beta, Inc.\",\"Line one\nLine two\"")));
        QVERIFY(text.contains(QStringLiteral("\"Quote \"\"Test\"\"\",Tabbed\tvalue")));
    }

    void readsCsvIntoTemplateDrivenDeck()
    {
        Deck deck;
        QString error;
        QVERIFY2(
            DelimitedText::readDeck(
                QStringLiteral("Name,Notes\nAlpha,One\nBeta,\"Two, with comma\"\n"),
                DelimitedText::csvProfile(ImportExportProfileType::Import),
                &deck,
                &error),
            qPrintable(error));

        QCOMPARE(deck.name(), QStringLiteral("Imported Text Deck"));
        QCOMPARE(deck.fieldCount(), 2);
        QCOMPARE(deck.fieldAt(0).name(), QStringLiteral("Name"));
        QCOMPARE(deck.fieldAt(1).type(), FieldType::Notes);
        QCOMPARE(deck.cardCount(), 2);
        QCOMPARE(deck.cardAt(1).valueAt(1), QStringLiteral("Two, with comma"));
        QCOMPARE(deck.importExportProfileCount(), 1);
        QCOMPARE(deck.importExportProfiles().at(0).type, ImportExportProfileType::Import);
    }

    void readsTsvWithoutHeader()
    {
        ImportExportProfile profile = DelimitedText::tsvProfile(ImportExportProfileType::Import);
        profile.hasHeader = false;

        Deck deck;
        QString error;
        QVERIFY2(DelimitedText::readDeck(QStringLiteral("Alpha\tOne\nBeta\tTwo\n"), profile, &deck, &error), qPrintable(error));

        QCOMPARE(deck.fieldCount(), 2);
        QCOMPARE(deck.fieldAt(0).name(), QStringLiteral("Field 1"));
        QCOMPARE(deck.cardAt(1).valueAt(1), QStringLiteral("Two"));
    }
};

#ifdef CARDSTACK_TEST_SUITE
int runDelimitedTextTests(int argc, char** argv)
{
    DelimitedTextTests test;
    return QTest::qExec(&test, argc, argv);
}
#else
QTEST_GUILESS_MAIN(DelimitedTextTests)
#endif

#include "DelimitedTextTests.moc"

