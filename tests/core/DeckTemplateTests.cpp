#include "DeckTemplate.h"

#include <QTest>

using namespace CardStack;

class DeckTemplateTests : public QObject {
    Q_OBJECT

private slots:
    void exposesBuiltInTemplateCatalog()
    {
        const QStringList names = builtInDeckTemplateNames();
        QCOMPARE(names.size(), 14);
        QCOMPARE(names.first(), QStringLiteral("Book Library"));
        QVERIFY(names.contains(QStringLiteral("Business Cards")));
        QVERIFY(names.contains(QStringLiteral("Credit Cards")));
        QVERIFY(names.contains(QStringLiteral("Data/Boxes")));
        QVERIFY(names.contains(QStringLiteral("Software Library")));
        QVERIFY(names.contains(QStringLiteral("Video Library")));

        const QVector<DeckTemplate>& templates = builtInDeckTemplates();
        QCOMPARE(templates.first().legacyResourceId, 101);
        QCOMPARE(templates.last().legacyResourceId, 114);
        for (const DeckTemplate& deckTemplate : templates) {
            QVERIFY2(deckTemplate.schemaFromLegacyResource, qPrintable(deckTemplate.name));
            QVERIFY2(deckTemplate.layoutFromLegacyResource, qPrintable(deckTemplate.name));
            QVERIFY2(!deckTemplate.layoutGeneratedFromSchema, qPrintable(deckTemplate.name));
            QCOMPARE(deckTemplate.legacyFields.size(), deckTemplate.fields.size());
            QVERIFY2(!deckTemplate.layout.frames.isEmpty(), qPrintable(deckTemplate.name));
            QVERIFY2(deckTemplate.layout.frames.size() >= deckTemplate.fields.size(), qPrintable(deckTemplate.name));
        }
    }

    void createsDecksFromEachTemplate()
    {
        for (const DeckTemplate& deckTemplate : builtInDeckTemplates()) {
            const Deck deck = createDeckFromTemplate(deckTemplate);
            QCOMPARE(deck.name(), deckTemplate.name);
            QCOMPARE(deck.cardCount(), 0);
            QCOMPARE(deck.fieldCount(), deckTemplate.fields.size());
            QCOMPARE(deck.cardTemplateLayout(), deckTemplate.layout);
            QVERIFY2(deck.fieldCount() > 0, qPrintable(deckTemplate.name));
        }

        const Deck software = createDeckFromTemplateName(QStringLiteral("Software Library"));
        QCOMPARE(software.fieldCount(), 11);
        QCOMPARE(software.fieldAt(0).name(), QStringLiteral("Product"));
        QCOMPARE(software.fieldAt(0).maxLength(), 30);
        QCOMPARE(software.fieldAt(10).name(), QStringLiteral("Notes"));
        QCOMPARE(software.fieldAt(10).type(), FieldType::Notes);
        QCOMPARE(software.fieldAt(10).maxLength(), 40);

        const std::optional<DeckTemplate> softwareTemplate = findBuiltInDeckTemplate(QStringLiteral("Software Library"));
        QVERIFY(softwareTemplate.has_value());
        QCOMPARE(softwareTemplate->legacyFields.at(0).recordOffset, 0);
        QCOMPARE(softwareTemplate->legacyFields.at(0).dataLength, 30);
        QCOMPARE(softwareTemplate->legacyFields.at(0).legacyBounds640, QRect(108, 27, 135, 21));
        QCOMPARE(softwareTemplate->legacyFields.at(0).controlRole, LegacyTemplateControlRole::Standalone);
        QCOMPARE(softwareTemplate->legacyFields.at(0).textAlignment, LegacyTemplateTextAlignment::Left);
        QCOMPARE(softwareTemplate->legacyFields.at(10).legacyFlags, quint16(0x0003));
        QCOMPARE(softwareTemplate->legacyFields.at(10).recordOffset, 275);
        QCOMPARE(softwareTemplate->legacyFields.at(10).dataLength, 40);
        QCOMPARE(softwareTemplate->layout.frames.last().bounds, QRect(1080, 2190, 2250, 750));

        const Deck creditCards = createDeckFromTemplateName(QStringLiteral("Credit Cards"));
        QCOMPARE(creditCards.fieldAt(0).name(), QStringLiteral("Type"));
        QVERIFY(creditCards.fields().last().isNotes());

        const std::optional<DeckTemplate> businessCardsTemplate = findBuiltInDeckTemplate(QStringLiteral("Business Cards"));
        QVERIFY(businessCardsTemplate.has_value());
        QCOMPARE(businessCardsTemplate->legacyFields.at(11).dialableMarker, quint16(1));
        QCOMPARE(businessCardsTemplate->legacyTextFrames.size(), 2);
        QCOMPARE(businessCardsTemplate->legacyTextFrames.at(0).text, QStringLiteral("Name"));
    }

    void supportsScratchAndPatternedDeckCreation()
    {
        const Deck scratch = createDeckFromScratch();
        QCOMPARE(scratch.name(), QStringLiteral("Untitled Deck"));
        QCOMPARE(scratch.fieldCount(), 1);
        QCOMPARE(scratch.fieldAt(0).name(), QStringLiteral("New Data Box"));
        QVERIFY(!scratch.cardTemplateLayout().frames.isEmpty());

        Deck source(QStringLiteral("Source"));
        source.addField(FieldDefinition(QStringLiteral("Name"), FieldType::Text, 64));
        source.addField(FieldDefinition(QStringLiteral("Memo"), FieldType::Notes, 8192));
        source.addCard(CardRecord({QStringLiteral("Existing"), QStringLiteral("Do not copy records")}));
        CardTemplateLayout sourceLayout;
        CardTemplateFrame memoFrame;
        memoFrame.kind = CardTemplateFrameKind::NotesBox;
        memoFrame.fieldIndex = 1;
        memoFrame.bounds = QRect(100, 200, 3000, 1200);
        sourceLayout.frames.append(memoFrame);
        source.setCardTemplateLayout(sourceLayout);

        const Deck patterned = createDeckPatternedAfterDeck(source, QStringLiteral("Patterned"));
        QCOMPARE(patterned.name(), QStringLiteral("Patterned"));
        QCOMPARE(patterned.fieldCount(), 2);
        QCOMPARE(patterned.cardCount(), 0);
        QCOMPARE(patterned.fieldAt(1).type(), FieldType::Notes);
        QCOMPARE(patterned.cardTemplateLayout(), sourceLayout);
    }
};

#ifdef CARDSTACK_TEST_SUITE
int runDeckTemplateTests(int argc, char** argv)
{
    DeckTemplateTests test;
    return QTest::qExec(&test, argc, argv);
}
#else
QTEST_GUILESS_MAIN(DeckTemplateTests)
#endif

#include "DeckTemplateTests.moc"

