#include "SQLiteDeckStore.h"
#include "SQLitePackageStore.h"

#include <QTemporaryDir>
#include <QTest>

namespace CardStack {
namespace {

Deck makeDeckWithCardsLayoutAndReport()
{
    Deck deck(QStringLiteral("Library Template"));
    deck.setDescription(QStringLiteral("Deck used to prove safe package sharing."));
    deck.addField(FieldDefinition(QStringLiteral("Title"), FieldType::Text, 80));
    deck.addField(FieldDefinition(QStringLiteral("Notes"), FieldType::Notes, 4096));
    deck.addCard(CardRecord({QStringLiteral("Invisible Cities"), QStringLiteral("Fixture card")}));

    CardTemplateLayout layout;
    layout.canvasWidth = 4200;
    layout.canvasHeight = 2600;
    CardTemplateFrame title;
    title.kind = CardTemplateFrameKind::DataBox;
    title.fieldIndex = 0;
    title.bounds = QRect(120, 160, 2400, 320);
    title.lineBoxShape = CardTemplateLineBoxShape::Box;
    title.fillPattern = 1;
    layout.frames.append(title);
    CardTemplateFrame notes;
    notes.kind = CardTemplateFrameKind::NotesBox;
    notes.fieldIndex = 1;
    notes.bounds = QRect(120, 560, 3600, 1400);
    notes.lineBoxShape = CardTemplateLineBoxShape::Box;
    notes.cornerRadius = 120;
    layout.frames.append(notes);
    deck.setCardTemplateLayout(layout);

    ReportDefinition report;
    report.name = QStringLiteral("Shelf List");
    report.formatMagic = QStringLiteral("RPT@#$B");
    report.formType = ReportFormType::Report;
    report.formWidth = 8500;
    report.formHeight = 11000;
    ReportFrameDefinition titleFrame;
    titleFrame.kind = ReportFrameKind::Data;
    titleFrame.bounds = QRect(240, 300, 3000, 240);
    titleFrame.text = QStringLiteral("{Title}");
    titleFrame.fieldPlaceholders = {QStringLiteral("Title")};
    report.frames.append(titleFrame);
    deck.addReport(report);

    return deck;
}

ReportDefinition makeReport(const QString& name)
{
    ReportDefinition report;
    report.name = name;
    report.formatMagic = QStringLiteral("RPT@#$B");
    report.formType = ReportFormType::Label;
    report.formWidth = 4000;
    report.formHeight = 2000;
    report.rows = 2;
    report.columns = 1;

    ReportFrameDefinition frame;
    frame.kind = ReportFrameKind::Text;
    frame.bounds = QRect(100, 100, 1500, 200);
    frame.text = QStringLiteral("Shared report");
    frame.lineBoxShape = 'b';
    frame.lineStyle = 1;
    frame.fillPattern = 2;
    report.frames.append(frame);

    return report;
}

} // namespace

class SQLitePackageStoreTests : public QObject {
    Q_OBJECT

private slots:
    void savesTemplatePackagesWithoutCards()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("library.cstemplate"));
        QString error;
        QVERIFY2(SQLitePackageStore::saveTemplatePackage(makeDeckWithCardsLayoutAndReport(), path, &error), qPrintable(error));

        SQLitePackageType type = SQLitePackageType::Unknown;
        QVERIFY2(SQLitePackageStore::packageType(path, &type, &error), qPrintable(error));
        QCOMPARE(type, SQLitePackageType::Template);

        Deck loaded;
        QVERIFY2(SQLitePackageStore::loadTemplatePackage(path, &loaded, &error), qPrintable(error));
        QCOMPARE(loaded.name(), QStringLiteral("Library Template"));
        QCOMPARE(loaded.description(), QStringLiteral("Deck used to prove safe package sharing."));
        QCOMPARE(loaded.fieldCount(), 2);
        QCOMPARE(loaded.cardCount(), 0);
        QCOMPARE(loaded.reportCount(), 1);
        QCOMPARE(loaded.cardTemplateLayout().canvasWidth, 4200);
        QCOMPARE(loaded.cardTemplateLayout().frames.size(), 2);
        QCOMPARE(loaded.cardTemplateLayout().frames.at(0).fieldIndex, 0);
        QCOMPARE(loaded.cardTemplateLayout().frames.at(1).cornerRadius, 120);
        QCOMPARE(loaded.reportAt(0).name, QStringLiteral("Shelf List"));
        QCOMPARE(loaded.reportAt(0).frames.at(0).fieldPlaceholders, QVector<QString>({QStringLiteral("Title")}));
    }

    void savesReportPackagesWithoutDeckData()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QVector<ReportDefinition> reports = {
            makeReport(QStringLiteral("Labels")),
            makeReport(QStringLiteral("Summary")),
        };
        const QString path = directory.filePath(QStringLiteral("reports.csreport"));
        QString error;
        QVERIFY2(SQLitePackageStore::saveReportPackage(reports, QStringLiteral("Library Reports"), path, &error), qPrintable(error));

        SQLitePackageType type = SQLitePackageType::Unknown;
        QVERIFY2(SQLitePackageStore::packageType(path, &type, &error), qPrintable(error));
        QCOMPARE(type, SQLitePackageType::Report);

        QVector<ReportDefinition> loadedReports;
        QString packageName;
        QVERIFY2(SQLitePackageStore::loadReportPackage(path, &loadedReports, &packageName, &error), qPrintable(error));
        QCOMPARE(packageName, QStringLiteral("Library Reports"));
        QCOMPARE(loadedReports.size(), 2);
        QCOMPARE(loadedReports.at(0).name, QStringLiteral("Labels"));
        QCOMPARE(loadedReports.at(0).formType, ReportFormType::Label);
        QCOMPARE(loadedReports.at(0).frames.size(), 1);
        QCOMPARE(loadedReports.at(0).frames.at(0).text, QStringLiteral("Shared report"));
        QCOMPARE(loadedReports.at(0).frames.at(0).lineBoxShape, static_cast<int>('b'));
        QCOMPARE(loadedReports.at(0).frames.at(0).fillPattern, 2);

        Deck rawDeck;
        SQLiteDeckStore rawStore;
        QVERIFY2(rawStore.open(path, &error), qPrintable(error));
        QVERIFY2(rawStore.loadDeck(&rawDeck, &error), qPrintable(error));
        QCOMPARE(rawDeck.fieldCount(), 0);
        QCOMPARE(rawDeck.cardCount(), 0);
        QCOMPARE(rawDeck.reportCount(), 2);
    }

    void savesCardsWithMissingValuesAsBlanks()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        Deck deck(QStringLiteral("Blank Cards"));
        deck.addField(FieldDefinition(QStringLiteral("Name"), FieldType::Text, 80));
        deck.addField(FieldDefinition(QStringLiteral("Notes"), FieldType::Notes, 4096));
        deck.addCard(CardRecord());
        deck.addCard(CardRecord({QStringLiteral("Only first field")}));

        const QString path = directory.filePath(QStringLiteral("blank-cards.cardstack"));
        QString error;
        QVERIFY2(SQLitePackageStore::saveDeckPackage(deck, path, &error), qPrintable(error));

        Deck loaded;
        SQLiteDeckStore store;
        QVERIFY2(store.open(path, &error), qPrintable(error));
        QVERIFY2(store.loadDeck(&loaded, &error), qPrintable(error));

        QCOMPARE(loaded.cardCount(), 2);
        QCOMPARE(loaded.cardAt(0).valueAt(0), QString());
        QCOMPARE(loaded.cardAt(0).valueAt(1), QString());
        QCOMPARE(loaded.cardAt(1).valueAt(0), QStringLiteral("Only first field"));
        QCOMPARE(loaded.cardAt(1).valueAt(1), QString());
    }

    void rejectsCrossLoadingPackageTypes()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString templatePath = directory.filePath(QStringLiteral("deck.cstemplate"));
        const QString reportPath = directory.filePath(QStringLiteral("report.csreport"));
        QString error;
        QVERIFY2(SQLitePackageStore::saveTemplatePackage(makeDeckWithCardsLayoutAndReport(), templatePath, &error), qPrintable(error));
        QVERIFY2(SQLitePackageStore::saveReportPackage({makeReport(QStringLiteral("Report"))}, QStringLiteral("Reports"), reportPath, &error), qPrintable(error));

        Deck templateDeck;
        QVERIFY(!SQLitePackageStore::loadTemplatePackage(reportPath, &templateDeck, &error));
        QVERIFY(error.contains(QStringLiteral("Report packages")));

        QVector<ReportDefinition> reports;
        QVERIFY(!SQLitePackageStore::loadReportPackage(templatePath, &reports, nullptr, &error));
        QVERIFY(error.contains(QStringLiteral("Template packages")));
    }

    void rejectsDeckPackageAsTemplate()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString deckPath = directory.filePath(QStringLiteral("deck.cardstack"));
        QString error;
        QVERIFY2(SQLitePackageStore::saveDeckPackage(makeDeckWithCardsLayoutAndReport(), deckPath, &error), qPrintable(error));

        Deck templateDeck;
        QVERIFY(!SQLitePackageStore::loadTemplatePackage(deckPath, &templateDeck, &error));
        QVERIFY(error.contains(QStringLiteral("Only CardStack template packages")));
    }

    void rejectsDeckPackageAsReport()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString deckPath = directory.filePath(QStringLiteral("deck.cardstack"));
        QString error;
        QVERIFY2(SQLitePackageStore::saveDeckPackage(makeDeckWithCardsLayoutAndReport(), deckPath, &error), qPrintable(error));

        QVector<ReportDefinition> reports;
        QVERIFY(!SQLitePackageStore::loadReportPackage(deckPath, &reports, nullptr, &error));
        QVERIFY(error.contains(QStringLiteral("Only CardStack report packages")));
    }
};

} // namespace CardStack

#ifdef CARDSTACK_TEST_SUITE
int runSQLitePackageStoreTests(int argc, char** argv)
{
    CardStack::SQLitePackageStoreTests test;
    return QTest::qExec(&test, argc, argv);
}
#else
QTEST_GUILESS_MAIN(CardStack::SQLitePackageStoreTests)
#endif

#include "SQLitePackageStoreTests.moc"

