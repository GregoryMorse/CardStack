#include "SQLiteDeckStore.h"

#include "DeckTemplate.h"
#include "LegacyDeckReader.h"

#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

namespace CardStack {
namespace {

Deck makeRoundTripDeck()
{
    Deck deck(QStringLiteral("Round Trip"));
    deck.setDescription(QStringLiteral("A deck description saved with the native CardStack file."));
    DeckAppearance appearance;
    appearance.dataFont = QStringLiteral("Data Font Serialization");
    appearance.nameFont = QStringLiteral("Name Font Serialization");
    appearance.textFont = QStringLiteral("Text Font Serialization");
    appearance.indexFont = QStringLiteral("Index Font Serialization");
    appearance.customColors = {
        QStringLiteral("#102030"), QStringLiteral("#203040"), QStringLiteral("#304050"),
        QStringLiteral("#405060"), QStringLiteral("#506070"), QStringLiteral("#607080"),
        QStringLiteral("#708090")};
    appearance.useSystemColors = false;
    deck.setAppearance(appearance);
    deck.addField(FieldDefinition(QStringLiteral("Name"), FieldType::Text, 64, true, false, {}, 225));
    deck.addField(FieldDefinition(QStringLiteral("Notes"), FieldType::Notes, 8192));
    deck.setSortKeys({
        {0, false},
        {1, true},
    });
    ImportExportProfile csvExport;
    csvExport.name = QStringLiteral("CSV export");
    csvExport.type = ImportExportProfileType::Export;
    csvExport.format = DelimitedTextFormat::Csv;
    csvExport.delimiter = QLatin1Char(',');
    csvExport.hasHeader = true;
    csvExport.fieldMappings = {0, 1};
    ImportExportProfile tsvImport;
    tsvImport.name = QStringLiteral("TSV import");
    tsvImport.type = ImportExportProfileType::Import;
    tsvImport.format = DelimitedTextFormat::Tsv;
    tsvImport.delimiter = QLatin1Char('\t');
    tsvImport.hasHeader = false;
    tsvImport.fieldMappings = {1, 0};
    deck.setImportExportProfiles({csvExport, tsvImport});
    deck.addCard(CardRecord({QStringLiteral("Project Atlas"), QStringLiteral("Migration-friendly sample")}));
    deck.addCard(CardRecord({QStringLiteral("CardStack"), QStringLiteral("Modern SQLite deck")}));

    CardTemplateLayout layout;
    layout.canvasWidth = 6400;
    layout.canvasHeight = 4800;
    CardTemplateFrame nameLabel;
    nameLabel.kind = CardTemplateFrameKind::Text;
    nameLabel.bounds = QRect(240, 240, 1200, 260);
    nameLabel.text = QStringLiteral("Name");
    nameLabel.styleFlags = 1;
    layout.frames.append(nameLabel);
    CardTemplateFrame nameBox;
    nameBox.kind = CardTemplateFrameKind::DataBox;
    nameBox.fieldIndex = 0;
    nameBox.bounds = QRect(1500, 220, 3600, 320);
    nameBox.lineBoxShape = CardTemplateLineBoxShape::Box;
    nameBox.fillPattern = 1;
    layout.frames.append(nameBox);
    CardTemplateFrame notesBox;
    notesBox.kind = CardTemplateFrameKind::NotesBox;
    notesBox.fieldIndex = 1;
    notesBox.bounds = QRect(240, 720, 5200, 1700);
    notesBox.lineBoxShape = CardTemplateLineBoxShape::Box;
    layout.frames.append(notesBox);
    deck.setCardTemplateLayout(layout);

    ReportDefinition report;
    report.name = QStringLiteral("Project Summary");
    report.formatMagic = QStringLiteral("RPT@#$B");
    report.legacyOffset = 0x153c;
    report.entrySize = 0x0c6f;
    report.headerSize = 0x3f5;
    report.declaredFrameCount = 2;
    report.formType = ReportFormType::Report;
    report.formWidth = 8500;
    report.formHeight = 11000;
    report.rows = 1;
    report.columns = 1;
    report.dataFont.faceName = QStringLiteral("MS Sans Serif");
    report.dataFont.legacyHeight = -11;
    report.textFont.faceName = QStringLiteral("Arial");
    report.textFont.legacyHeight = -13;

    ReportFrameDefinition title;
    title.legacyOffset = 0x1931;
    title.signature = 0x8002;
    title.order = 1;
    title.bounds = QRect(120, 180, 2200, 260);
    title.text = QStringLiteral("Project Summary");
    title.kind = ReportFrameKind::Text;
    title.styleFlags = 3;
    report.frames.append(title);

    ReportFrameDefinition data;
    data.legacyOffset = 0x19cc;
    data.signature = 0x8002;
    data.sourceId = 2;
    data.order = 2;
    data.bounds = QRect(120, 520, 3200, 220);
    data.text = QStringLiteral("{Project}");
    data.fieldPlaceholders = {QStringLiteral("Project")};
    data.systemTokens = {QStringLiteral("PageNumber")};
    data.kind = ReportFrameKind::Data;
    data.printEntireContentsFlag = 1;
    data.validationFlags = 2;
    report.frames.append(data);

    deck.addReport(report);
    return deck;
}

} // namespace

class SQLiteDeckStoreTests : public QObject {
    Q_OBJECT

private slots:
    void roundTripsImportedLegacyAppearanceAndRawDescriptors()
    {
        const QString fixtureDirPath = qEnvironmentVariable("CARDSTACK_WINEVDM_GOLDEN_DIR");
        if (fixtureDirPath.isEmpty() || !QDir(fixtureDirPath).exists()) {
            QSKIP("Set CARDSTACK_WINEVDM_GOLDEN_DIR to the accepted WineVDM fixture directory.");
        }

        const LegacyDeckReader::Result imported = LegacyDeckReader().readDeck(
            QDir(fixtureDirPath).filePath(QStringLiteral("plain.BTN")));
        QVERIFY2(imported.ok(), qPrintable(imported.errorMessage));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("migrated.cardstack"));
        QString error;
        SQLiteDeckStore writer;
        QVERIFY2(writer.open(path, &error), qPrintable(error));
        QVERIFY2(writer.saveDeck(imported.deck, &error), qPrintable(error));
        writer.close();

        Deck loaded;
        SQLiteDeckStore reader;
        QVERIFY2(reader.open(path, &error), qPrintable(error));
        QVERIFY2(reader.loadDeck(&loaded, &error), qPrintable(error));

        QCOMPARE(loaded.appearance(), imported.deck.appearance());
        QCOMPARE(loaded.legacyControlRecord(), imported.deck.legacyControlRecord());
        QCOMPARE(loaded.fieldCount(), imported.deck.fieldCount());
        for (int fieldIndex = 0; fieldIndex < loaded.fieldCount(); ++fieldIndex) {
            QCOMPARE(
                loaded.fieldAt(fieldIndex).legacyDescriptor(),
                imported.deck.fieldAt(fieldIndex).legacyDescriptor());
        }
    }

    void savesAndLoadsDeckFieldsAndCards()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("deck.cardstack"));
        QString error;
        SQLiteDeckStore writer;
        QVERIFY2(writer.open(path, &error), qPrintable(error));
        QVERIFY2(writer.saveDeck(makeRoundTripDeck(), &error), qPrintable(error));
        writer.close();

        Deck loaded;
        SQLiteDeckStore reader;
        QVERIFY2(reader.open(path, &error), qPrintable(error));
        QVERIFY2(reader.loadDeck(&loaded, &error), qPrintable(error));

        QCOMPARE(loaded.name(), QStringLiteral("Round Trip"));
        QCOMPARE(loaded.description(), QStringLiteral("A deck description saved with the native CardStack file."));
        QCOMPARE(loaded.appearance(), makeRoundTripDeck().appearance());
        QCOMPARE(loaded.fieldCount(), 2);
        QCOMPARE(loaded.fieldAt(0).name(), QStringLiteral("Name"));
        QCOMPARE(loaded.fieldAt(0).type(), FieldType::Text);
        QCOMPARE(loaded.fieldAt(0).maxLength(), 64);
        QCOMPARE(loaded.fieldAt(0).displayWidth(), 225);
        QCOMPARE(loaded.fieldAt(1).name(), QStringLiteral("Notes"));
        QCOMPARE(loaded.fieldAt(1).type(), FieldType::Notes);
        QCOMPARE(loaded.fieldAt(1).maxLength(), 8192);
        QCOMPARE(loaded.sortKeyCount(), 2);
        QCOMPARE(loaded.sortKeys().at(0).fieldIndex, 0);
        QCOMPARE(loaded.sortKeys().at(0).descending, false);
        QCOMPARE(loaded.sortKeys().at(1).fieldIndex, 1);
        QCOMPARE(loaded.sortKeys().at(1).descending, true);
        QCOMPARE(loaded.importExportProfileCount(), 2);
        QCOMPARE(loaded.importExportProfiles().at(0).name, QStringLiteral("CSV export"));
        QCOMPARE(loaded.importExportProfiles().at(0).type, ImportExportProfileType::Export);
        QCOMPARE(loaded.importExportProfiles().at(0).format, DelimitedTextFormat::Csv);
        QCOMPARE(loaded.importExportProfiles().at(0).delimiter, QLatin1Char(','));
        QCOMPARE(loaded.importExportProfiles().at(0).hasHeader, true);
        QCOMPARE(loaded.importExportProfiles().at(0).fieldMappings, QVector<int>({0, 1}));
        QCOMPARE(loaded.importExportProfiles().at(1).name, QStringLiteral("TSV import"));
        QCOMPARE(loaded.importExportProfiles().at(1).type, ImportExportProfileType::Import);
        QCOMPARE(loaded.importExportProfiles().at(1).format, DelimitedTextFormat::Tsv);
        QCOMPARE(loaded.importExportProfiles().at(1).delimiter, QLatin1Char('\t'));
        QCOMPARE(loaded.importExportProfiles().at(1).hasHeader, false);
        QCOMPARE(loaded.importExportProfiles().at(1).fieldMappings, QVector<int>({1, 0}));
        QCOMPARE(loaded.cardCount(), 2);
        QCOMPARE(loaded.cardAt(0).valueAt(0), QStringLiteral("Project Atlas"));
        QCOMPARE(loaded.cardAt(0).valueAt(1), QStringLiteral("Migration-friendly sample"));
        QCOMPARE(loaded.cardAt(1).valueAt(0), QStringLiteral("CardStack"));
        QCOMPARE(loaded.cardAt(1).valueAt(1), QStringLiteral("Modern SQLite deck"));
        QCOMPARE(loaded.cardTemplateLayout().canvasWidth, 6400);
        QCOMPARE(loaded.cardTemplateLayout().canvasHeight, 4800);
        QCOMPARE(loaded.cardTemplateLayout().frames.size(), 3);
        QCOMPARE(loaded.cardTemplateLayout().frames.at(0).kind, CardTemplateFrameKind::Text);
        QCOMPARE(loaded.cardTemplateLayout().frames.at(0).bounds, QRect(240, 240, 1200, 260));
        QCOMPARE(loaded.cardTemplateLayout().frames.at(0).text, QStringLiteral("Name"));
        QCOMPARE(loaded.cardTemplateLayout().frames.at(1).kind, CardTemplateFrameKind::DataBox);
        QCOMPARE(loaded.cardTemplateLayout().frames.at(1).fieldIndex, 0);
        QCOMPARE(loaded.cardTemplateLayout().frames.at(1).fillPattern, 1);
        QCOMPARE(loaded.cardTemplateLayout().frames.at(2).kind, CardTemplateFrameKind::NotesBox);
        QCOMPARE(loaded.reportCount(), 1);
        QCOMPARE(loaded.reportAt(0).name, QStringLiteral("Project Summary"));
        QCOMPARE(loaded.reportAt(0).formatMagic, QStringLiteral("RPT@#$B"));
        QCOMPARE(loaded.reportAt(0).legacyOffset, 0x153c);
        QCOMPARE(loaded.reportAt(0).entrySize, 0x0c6f);
        QCOMPARE(loaded.reportAt(0).headerSize, 0x3f5);
        QCOMPARE(loaded.reportAt(0).declaredFrameCount, 2);
        QCOMPARE(loaded.reportAt(0).formType, ReportFormType::Report);
        QCOMPARE(loaded.reportAt(0).formWidth, 8500);
        QCOMPARE(loaded.reportAt(0).formHeight, 11000);
        QCOMPARE(loaded.reportAt(0).dataFont.faceName, QStringLiteral("MS Sans Serif"));
        QCOMPARE(loaded.reportAt(0).textFont.faceName, QStringLiteral("Arial"));
        QCOMPARE(loaded.reportAt(0).frames.size(), 2);
        QCOMPARE(loaded.reportAt(0).frames.at(0).text, QStringLiteral("Project Summary"));
        QCOMPARE(loaded.reportAt(0).frames.at(0).bounds, QRect(120, 180, 2200, 260));
        QCOMPARE(loaded.reportAt(0).frames.at(0).kind, ReportFrameKind::Text);
        QCOMPARE(loaded.reportAt(0).frames.at(1).text, QStringLiteral("{Project}"));
        QCOMPARE(loaded.reportAt(0).frames.at(1).fieldPlaceholders, QVector<QString>({QStringLiteral("Project")}));
        QCOMPARE(loaded.reportAt(0).frames.at(1).systemTokens, QVector<QString>({QStringLiteral("PageNumber")}));
        QCOMPARE(loaded.reportAt(0).frames.at(1).kind, ReportFrameKind::Data);
    }

    void overwritesExistingDeckCleanly()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("deck.cardstack"));
        QString error;
        SQLiteDeckStore store;
        QVERIFY2(store.open(path, &error), qPrintable(error));
        QVERIFY2(store.saveDeck(makeRoundTripDeck(), &error), qPrintable(error));

        Deck replacement(QStringLiteral("Replacement"));
        replacement.setDescription(QStringLiteral("Replacement description"));
        replacement.addField(FieldDefinition(QStringLiteral("Only Field"), FieldType::Text, 12));
        replacement.addCard(CardRecord({QStringLiteral("Only Card")}));
        QVERIFY2(store.saveDeck(replacement, &error), qPrintable(error));

        Deck loaded;
        QVERIFY2(store.loadDeck(&loaded, &error), qPrintable(error));
        QCOMPARE(loaded.name(), QStringLiteral("Replacement"));
        QCOMPARE(loaded.description(), QStringLiteral("Replacement description"));
        QCOMPARE(loaded.fieldCount(), 1);
        QCOMPARE(loaded.cardCount(), 1);
        QCOMPARE(loaded.reportCount(), 0);
        QCOMPARE(loaded.fieldAt(0).name(), QStringLiteral("Only Field"));
        QCOMPARE(loaded.cardAt(0).valueAt(0), QStringLiteral("Only Card"));
    }

    void preservesBuiltInTemplateReportPresets()
    {
        for (const DeckTemplate& deckTemplate : builtInDeckTemplates()) {
            QTemporaryDir directory;
            QVERIFY2(directory.isValid(), qPrintable(deckTemplate.name));
            const Deck expected = createDeckFromTemplate(deckTemplate);
            const QString path = directory.filePath(QStringLiteral("template.cardstack"));
            QString error;
            SQLiteDeckStore writer;
            QVERIFY2(writer.open(path, &error), qPrintable(error));
            QVERIFY2(writer.saveDeck(expected, &error), qPrintable(QStringLiteral("%1: %2").arg(deckTemplate.name, error)));
            writer.close();

            Deck loaded;
            SQLiteDeckStore reader;
            QVERIFY2(reader.open(path, &error), qPrintable(error));
            QVERIFY2(reader.loadDeck(&loaded, &error), qPrintable(QStringLiteral("%1: %2").arg(deckTemplate.name, error)));
            QCOMPARE(loaded.name(), expected.name());
            QCOMPARE(loaded.fieldCount(), expected.fieldCount());
            QCOMPARE(loaded.cardTemplateLayout(), expected.cardTemplateLayout());
            QCOMPARE(loaded.reportCount(), expected.reportCount());
            for (int fieldIndex = 0; fieldIndex < expected.fieldCount(); ++fieldIndex) {
                const FieldDefinition& actual = loaded.fieldAt(fieldIndex);
                const FieldDefinition& oracle = expected.fieldAt(fieldIndex);
                QCOMPARE(actual.name(), oracle.name());
                QCOMPARE(actual.type(), oracle.type());
                QCOMPARE(actual.maxLength(), oracle.maxLength());
                QCOMPARE(actual.showName(), oracle.showName());
                QCOMPARE(actual.isPhone(), oracle.isPhone());
                QCOMPARE(actual.legacyDescriptor(), oracle.legacyDescriptor());
                QCOMPARE(actual.displayWidth(), oracle.displayWidth());
            }
            for (int reportIndex = 0; reportIndex < expected.reportCount(); ++reportIndex) {
                const ReportDefinition& actual = loaded.reportAt(reportIndex);
                const ReportDefinition& oracle = expected.reportAt(reportIndex);
                QCOMPARE(actual.name, oracle.name);
                QCOMPARE(actual.formatMagic, oracle.formatMagic);
                QCOMPARE(actual.formType, oracle.formType);
                QCOMPARE(actual.formWidth, oracle.formWidth);
                QCOMPARE(actual.formHeight, oracle.formHeight);
                QCOMPARE(actual.rows, oracle.rows);
                QCOMPARE(actual.columns, oracle.columns);
                QCOMPARE(actual.frames.size(), oracle.frames.size());
                for (int frameIndex = 0; frameIndex < oracle.frames.size(); ++frameIndex) {
                    const ReportFrameDefinition& actualFrame = actual.frames.at(frameIndex);
                    const ReportFrameDefinition& oracleFrame = oracle.frames.at(frameIndex);
                    QCOMPARE(actualFrame.bounds, oracleFrame.bounds);
                    QCOMPARE(actualFrame.text, oracleFrame.text);
                    QCOMPARE(actualFrame.kind, oracleFrame.kind);
                    QCOMPARE(actualFrame.printEntireContentsFlag, oracleFrame.printEntireContentsFlag);
                    QCOMPARE(actualFrame.validationFlags, oracleFrame.validationFlags);
                    QCOMPARE(actualFrame.styleFlags, oracleFrame.styleFlags);
                    QCOMPARE(actualFrame.lineBoxShape, oracleFrame.lineBoxShape);
                    QCOMPARE(actualFrame.lineStyle, oracleFrame.lineStyle);
                    QCOMPARE(actualFrame.fillPattern, oracleFrame.fillPattern);
                    QCOMPARE(actualFrame.cornerRadius, oracleFrame.cornerRadius);
                    QCOMPARE(actualFrame.legacyDescriptor, oracleFrame.legacyDescriptor);
                }
            }
        }
    }

    void createsVersionedSpecSchema()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("schema.cardstack"));
        QString error;
        SQLiteDeckStore store;
        QVERIFY2(store.open(path, &error), qPrintable(error));
        QVERIFY2(store.saveDeck(makeRoundTripDeck(), &error), qPrintable(error));
        store.close();

        const QString connectionName = QStringLiteral("schema_check_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(path);
        QVERIFY(database.open());

        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral(
            "SELECT name FROM sqlite_master WHERE type = 'table' AND name IN ("
            "'schema_migrations', 'app_settings', 'decks', 'templates', 'template_fields',"
            "'template_frames', 'cards', 'card_values', 'deck_sort_keys', 'deck_formatting', 'reports',"
            "'report_frames', 'import_export_profiles', 'phone_profiles', 'phone_call_log')")));

        QStringList tables;
        while (query.next()) {
            tables.append(query.value(0).toString());
        }

        QCOMPARE(tables.size(), 15);
        QVERIFY(tables.contains(QStringLiteral("decks")));
        QVERIFY(tables.contains(QStringLiteral("templates")));
        QVERIFY(tables.contains(QStringLiteral("template_fields")));
        QVERIFY(tables.contains(QStringLiteral("template_frames")));
        QVERIFY(tables.contains(QStringLiteral("deck_sort_keys")));
        QVERIFY(tables.contains(QStringLiteral("deck_formatting")));
        QVERIFY(tables.contains(QStringLiteral("import_export_profiles")));
        QVERIFY(tables.contains(QStringLiteral("phone_profiles")));
        QVERIFY(tables.contains(QStringLiteral("phone_call_log")));

        QVERIFY(query.exec(QStringLiteral(
            "SELECT description FROM templates WHERE id = 1")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(),
                 QStringLiteral("A deck description saved with the native CardStack file."));
        QVERIFY(query.exec(QStringLiteral(
            "SELECT description FROM decks WHERE id = 1")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QString());
        QVERIFY(query.exec(QStringLiteral(
            "SELECT count(*) FROM deck_formatting WHERE scope NOT LIKE 'template-%'")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 0);

        QVERIFY(query.exec(QStringLiteral("SELECT value FROM app_settings WHERE key = 'schema_version'")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("2"));

        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 2);

        database.close();
        QSqlDatabase::removeDatabase(connectionName);
    }

    void migratesDeckDescriptionColumn()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("legacy-schema.cardstack"));
        const QString connectionName = QStringLiteral("legacy_schema_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        {
            QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            database.setDatabaseName(path);
            QVERIFY(database.open());

            QSqlQuery query(database);
            QVERIFY(query.exec(QStringLiteral("CREATE TABLE deck(id INTEGER PRIMARY KEY CHECK(id = 1), name TEXT NOT NULL)")));
            QVERIFY(query.exec(QStringLiteral("CREATE TABLE fields(id INTEGER PRIMARY KEY, ordinal INTEGER NOT NULL, name TEXT NOT NULL, type TEXT NOT NULL, max_length INTEGER NOT NULL)")));
            QVERIFY(query.exec(QStringLiteral("CREATE TABLE cards(id INTEGER PRIMARY KEY, ordinal INTEGER NOT NULL)")));
            QVERIFY(query.exec(QStringLiteral("CREATE TABLE card_values(card_id INTEGER NOT NULL, field_id INTEGER NOT NULL, value TEXT NOT NULL, PRIMARY KEY(card_id, field_id))")));
            QVERIFY(query.exec(QStringLiteral("INSERT INTO deck(id, name) VALUES(1, 'Old Deck')")));
            database.close();
        }
        QSqlDatabase::removeDatabase(connectionName);

        QString error;
        SQLiteDeckStore store;
        QVERIFY2(store.open(path, &error), qPrintable(error));

        Deck loaded;
        QVERIFY2(store.loadDeck(&loaded, &error), qPrintable(error));
        QCOMPARE(loaded.name(), QStringLiteral("Old Deck"));
        QCOMPARE(loaded.description(), QString());
        QCOMPARE(loaded.reportCount(), 0);

        loaded.setDescription(QStringLiteral("Migrated description"));
        QVERIFY2(store.saveDeck(loaded, &error), qPrintable(error));

        Deck reloaded;
        QVERIFY2(store.loadDeck(&reloaded, &error), qPrintable(error));
        QCOMPARE(reloaded.description(), QStringLiteral("Migrated description"));
    }
};

} // namespace CardStack

#ifdef CARDSTACK_TEST_SUITE
int runSQLiteDeckStoreTests(int argc, char** argv)
{
    CardStack::SQLiteDeckStoreTests test;
    return QTest::qExec(&test, argc, argv);
}
#else
QTEST_GUILESS_MAIN(CardStack::SQLiteDeckStoreTests)
#endif

#include "SQLiteDeckStoreTests.moc"

