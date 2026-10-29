#include "BtrieveAuditReader.h"
#include "BtrieveFileSaverReader.h"
#include "DeckSecurity.h"
#include "DocumentFile.h"
#include "DelimitedText.h"
#include "LegacyDeckReader.h"
#include "LegacyInterchangeReader.h"
#include "LegacyReportReader.h"
#include "PhoneCallLog.h"
#include "SQLiteDeckStore.h"
#include "SQLiteSnapshot.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <atomic>
#include <limits>

#ifdef CARDSTACK_TEST_NATIVE_BTRIEVE
#ifndef NOMINMAX
#define NOMINMAX
#endif
extern "C" {
#include "BtrieveFileSaverLib.h"
}
#undef bool
#undef true
#undef false
#endif

using namespace CardStack;
namespace {
void u16(QByteArray& bytes, qsizetype offset, quint16 value)
{
    bytes[offset] = char(value);
    bytes[offset + 1] = char(value >> 8);
}
void u32(QByteArray& bytes, qsizetype offset, quint32 value)
{
    for (int byte = 0; byte < 4; ++byte) bytes[offset + byte] = char(value >> (byte * 8));
}
QByteArray cardfile(quint32 offset = 80)
{
    QByteArray bytes(128, '\0');
    bytes.replace(0, 3, "MGC");
    u16(bytes, 3, 1);
    u32(bytes, 11, offset);
    bytes.replace(16, 5, "Title");
    u16(bytes, 82, 4);
    bytes.replace(84, 4, "Note");
    return bytes;
}
QByteArray variableFile(int recordCount = 1, int fragmentPages = 1)
{
    constexpr int PageSize = 512, FixedSize = 16, SlotSize = 20;
    QByteArray bytes((fragmentPages + 2) * PageSize, '\0');
    u16(bytes, 6, 0x0500); u16(bytes, 8, PageSize);
    u16(bytes, 0x16, FixedSize); u16(bytes, 0x18, SlotSize);
    u16(bytes, 0x1c, quint16(recordCount)); bytes[0x38] = 1;
    u16(bytes, PageSize + 4, 0x8001);
    for (int record = 0; record < recordCount; ++record) {
        const int offset = PageSize + 6 + record * SlotSize;
        bytes[offset + 8] = 1;
        bytes[offset + FixedSize + 1] = 2;
    }
    for (int page = 0; page < fragmentPages; ++page) {
        const int offset = (page + 2) * PageSize;
        u16(bytes, offset + 510, page + 1 < fragmentPages ? 0x8010 : 16);
        u16(bytes, offset + 508, 500);
        const int dataStart = page + 1 < fragmentPages ? 20 : 16;
        if (page + 1 < fragmentPages) bytes[offset + 17] = char(page + 3);
        for (int index = dataStart; index < 500; ++index) bytes[offset + index] = 'Z';
    }
    return bytes;
}
QString writeFile(QTemporaryDir& directory, const QString& name, const QByteArray& bytes)
{
    const QString path = directory.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
    return path;
}
QString golden(const char* name)
{
    return QDir(qEnvironmentVariable("CARDSTACK_WINEVDM_GOLDEN_DIR")).filePath(QString::fromLatin1(name));
}
struct Database {
    QString name = QUuid::createUuid().toString();
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    Database() { db.setDatabaseName(QStringLiteral(":memory:")); db.open(); }
    ~Database() { db.close(); db = {}; QSqlDatabase::removeDatabase(name); }
};
QJsonObject snapshotCell(const QJsonValue& value)
{
    QJsonArray rows;
    rows.append(QJsonArray{value});
    const QJsonObject table{
        {QStringLiteral("columns"), QJsonArray{QStringLiteral("name")}},
        {QStringLiteral("rows"), rows}};
    return {{QStringLiteral("snapshot_version"), 1},
            {QStringLiteral("tables"), QJsonObject{{QStringLiteral("decks"), table}}}};
}
#ifdef CARDSTACK_TEST_NATIVE_BTRIEVE
int stopNative(void* context, uint64_t units)
{
    auto* remaining = static_cast<quint64*>(context);
    if (units > *remaining) return 0;
    *remaining -= units;
    return 1;
}
#endif
} // namespace

class ImportSecurityTests : public QObject {
    Q_OBJECT
private slots:
    void cardfileOffsets_data()
    {
        QTest::addColumn<quint32>("offset");
        QTest::addColumn<bool>("valid");
        QTest::newRow("valid") << quint32(80) << true;
        for (quint32 value : {0U, 4U, 56U, 125U, 128U, 0x7ffffffdU, 0x7ffffffeU, 0x7fffffffU, 0xffffffffU})
            QTest::newRow(qPrintable(QString::number(value, 16))) << value << false;
    }
    void cardfileOffsets()
    {
        QFETCH(quint32, offset);
        QFETCH(bool, valid);
        QTemporaryDir directory;
        const QString path = writeFile(directory, QStringLiteral("bounds.CRD"), cardfile(offset));
        QVERIFY(!path.isEmpty());
        QProcess worker;
        worker.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--security-interchange"), path});
        QVERIFY(worker.waitForStarted(3000));
        const bool finished = worker.waitForFinished(5000);
        if (!finished) { worker.kill(); worker.waitForFinished(); }
        QVERIFY2(finished, worker.readAllStandardError().constData());
        QCOMPARE(worker.exitStatus(), QProcess::NormalExit);
        QCOMPARE(worker.exitCode(), valid ? 0 : 7);
        QFile original(path);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), cardfile(offset));
    }
    void truncatedCardfileNeverPublishesPartialCards()
    {
        QTemporaryDir directory;
        for (int size : {4, 16, 57, 83, 87}) {
            const auto result = LegacyInterchangeReader{}.readMicrosoftCardfile(
                writeFile(directory, QStringLiteral("short.CRD"), cardfile().left(size)));
            QVERIFY(!result.ok());
            QCOMPARE(result.deck.cardCount(), 0);
        }
    }
    void inputRowsValuesAndFieldsAreBounded()
    {
        QTemporaryDir directory;
        const QString path = writeFile(directory, QStringLiteral("bounded.CRD"), cardfile());
        ImportLimits limits;
        limits.maximumInputBytes = 127;
        QVERIFY(LegacyInterchangeReader(limits).readFile(path).processingAborted);
        limits = {};
        limits.maximumRows = 0;
        QVERIFY(LegacyInterchangeReader(limits).readFile(path).processingAborted);
        limits = {};
        limits.maximumValues = 1;
        QVERIFY(LegacyInterchangeReader(limits).readFile(path).processingAborted);
        limits = {};
        limits.maximumFields = 1;
        QVERIFY(LegacyInterchangeReader(limits).readFile(path).processingAborted);
        limits = {};
        limits.maximumDecodedBytes = 32;
        QVERIFY(LegacyInterchangeReader(limits).readFile(path).processingAborted);
        limits = {};
        limits.maximumCellBytes = 2;
        QVERIFY(LegacyInterchangeReader(limits).readFile(path).processingAborted);
    }
    void cancellationDeadlineAndWorkFailClosed()
    {
        std::atomic_bool cancelled{true};
        for (int kind = 0; kind < 3; ++kind) {
            ImportLimits limits;
            if (kind == 0) limits.cancellation = &cancelled;
            if (kind == 1) limits.maximumMilliseconds = 0;
            if (kind == 2) limits.maximumWorkUnits = 1;
            const auto result = BtrieveFileSaverReader(limits).readMemory(variableFile());
            QVERIFY(result.processingAborted);
            QVERIFY(!result.ok());
            QVERIFY(result.records.isEmpty());
            const auto audit = BtrieveAuditReader(limits).readBytes(variableFile());
            QVERIFY(audit.processingAborted);
        }
    }
    void sharedFragmentsCannotAmplifyPastDecodedBudget()
    {
        const auto bytes = variableFile(25, 16);
        const auto full = BtrieveFileSaverReader{}.readMemory(bytes);
        QVERIFY2(full.ok(), qPrintable(full.errorMessage));
        QCOMPARE(full.records.size(), 25);
        quint64 total = 0;
        for (const auto& record : full.records) total += quint64(record.size());
        QVERIFY(total > quint64(bytes.size()) * 10);
        ImportLimits limits;
        limits.maximumDecodedBytes = total / 2;
        const auto limited = BtrieveFileSaverReader(limits).readMemory(bytes);
        QVERIFY(limited.processingAborted);
        QVERIFY(!limited.ok());
        QVERIFY(limited.records.isEmpty());
    }
    void nullVariablePointerPreservesFixedRecord()
    {
        QByteArray bytes = variableFile();
        bytes[512 + 6 + 17] = 0;
        const auto result = BtrieveFileSaverReader{}.readMemory(bytes);
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.records.size(), 1);
        QCOMPARE(result.records.first().size(), 16);
    }
    void nativeMemoryAndFileApisAgreeAndCleanUp()
    {
#ifndef CARDSTACK_TEST_NATIVE_BTRIEVE
        QSKIP("Native Btrieve migration support is disabled.");
#else
        QTemporaryDir directory;
        const auto bytes = variableFile();
        const auto original = bytes;
        const QString path = writeFile(directory, QStringLiteral("memory.BTN"), bytes);
        CLIENT_STRUCT memory{}, file{};
        BF_READ_OPTIONS options{};
        options.maximum_file_bytes = ImportMebibyte;
        QCOMPARE(BF_OPEN_MEMORY(&memory, bytes.constData(), uint64_t(bytes.size()), &options), 0);
        const QByteArray localPath = QFile::encodeName(path);
        QCOMPARE(BF_OPEN_WITH_OPTIONS(&file, localPath.constData(), &options), 0);
        char memoryRecord[1024]{}, fileRecord[1024]{};
        unsigned long memoryLength = sizeof(memoryRecord), fileLength = sizeof(fileRecord);
        QCOMPARE(BF_GET_REC(&memory, memoryRecord, &memoryLength), 0);
        QCOMPARE(BF_GET_REC(&file, fileRecord, &fileLength), 0);
        QCOMPARE(QByteArray(memoryRecord, int(memoryLength)), QByteArray(fileRecord, int(fileLength)));
        QCOMPARE(BF_CLOSE(&memory), 0);
        QCOMPARE(BF_CLOSE(&file), 0);
        QCOMPARE(bytes, original);
        quint64 remaining = 0;
        options.progress = stopNative; options.context = &remaining;
        QCOMPARE(BF_OPEN_MEMORY(&memory, bytes.constData(), uint64_t(bytes.size()), &options), PROCESSING_LIMIT);
        BF_CLOSE(&memory);
        options.progress = nullptr;
        options.maximum_file_bytes = 1;
        QCOMPARE(BF_OPEN_MEMORY(&memory, bytes.constData(), uint64_t(bytes.size()), &options), PROCESSING_LIMIT);
        BF_CLOSE(&memory);
        options.maximum_file_bytes = ImportMebibyte;
        QCOMPARE(BF_OPEN_MEMORY(&memory, bytes.constData(), uint64_t(bytes.size()), &options), 0);
        remaining = 0; memory.progress = stopNative; memory.progressContext = &remaining;
        memoryLength = sizeof(memoryRecord);
        QCOMPARE(BF_GET_REC(&memory, memoryRecord, &memoryLength), PROCESSING_LIMIT);
        BF_CLOSE(&memory);
#endif
    }
    void budgetFailureCannotEnterTakeNoteFallback()
    {
        QTemporaryDir directory;
        const QString path = writeFile(directory, QStringLiteral("bounded.TN"), variableFile());
        ImportLimits limits;
        limits.maximumWorkUnits = 1;
        const auto result = LegacyInterchangeReader(limits).readTakeNoteFile(path);
        QVERIFY(result.processingAborted);
        QVERIFY(!result.ok());
        QCOMPARE(result.deck.cardCount(), 0);
        QVERIFY(!result.passwordRejected);
    }
    void encryptedGoldenImportRemainsPasswordVerified()
    {
        const QString path = golden("crypt.BTN");
        QVERIFY2(QFileInfo::exists(path), qPrintable(path));
        const auto imported = LegacyDeckReader{}.readDeck(path, QStringLiteral("Crypt01"));
        QVERIFY2(imported.ok(), qPrintable(imported.errorMessage));
        QVERIFY(imported.legacyPasswordVerified);
        QVERIFY(imported.legacyDataEncrypted);
        QCOMPARE(imported.deck.cardCount(), 3);
        QVERIFY(LegacyDeckReader{}.readDeck(path, QStringLiteral("Wrong01")).passwordRejected);
        ImportLimits limits;
        limits.maximumDecodedBytes = 16;
        const auto rejected = LegacyDeckReader(limits).readDeck(path, QStringLiteral("Crypt01"));
        QVERIFY(rejected.processingAborted);
        QVERIFY(!rejected.passwordRejected);
        QVERIFY(rejected.rawRecords.isEmpty());
    }
    void dbaseAndMemoOffsetsRejectOverflow()
    {
        QTemporaryDir directory;
        QByteArray bytes(76, '\0');
        bytes[0] = char(0x83);
        u32(bytes, 4, 1); u16(bytes, 8, 65); u16(bytes, 10, 11);
        bytes.replace(32, 4, "MEMO"); bytes[43] = 'M'; bytes[48] = 10; bytes[64] = char(0x0d);
        bytes[65] = ' ';
        bytes.replace(66, 10, "4294967295");
        const QString path = writeFile(directory, QStringLiteral("memo.DBF"), bytes);
        QVERIFY(!writeFile(directory, QStringLiteral("memo.DBT"), QByteArray(512, '\0')).isEmpty());
        QVERIFY(!LegacyInterchangeReader{}.readDBaseFile(path).ok());
        u32(bytes, 4, 0xffffffffU);
        QVERIFY(!LegacyInterchangeReader{}.readDBaseFile(writeFile(directory, QStringLiteral("huge.DBF"), bytes)).ok());
        u32(bytes, 4, 1); bytes[48] = 11;
        QVERIFY(!LegacyInterchangeReader{}.readDBaseFile(writeFile(directory, QStringLiteral("wide.DBF"), bytes)).ok());
    }
    void textAndCallLogBudgetsAreAtomic()
    {
        ImportLimits limits;
        limits.maximumValues = 5;
        Deck output(QStringLiteral("Keep"));
        QString error;
        const auto profile = DelimitedText::csvProfile(ImportExportProfileType::Import);
        QVERIFY(!DelimitedText::readDeck(QStringLiteral("A,B,C\nx\ny\nz\n"), profile, &output, &error, limits));
        QCOMPARE(output.name(), QStringLiteral("Keep"));
        QVERIFY(!error.isEmpty());
        QTemporaryDir directory;
        const QString log = writeFile(directory, QStringLiteral("calls.LOG"), QByteArray("01/01/24,12:00,123,one\n01/01/24,12:01,456,two\n"));
        limits = {}; limits.maximumRows = 1;
        QCOMPARE(PhoneCallLog::importLegacyFile(log, &output, &error, limits), 0);
        QVERIFY(output.phoneCallLogEntries().isEmpty());
        QVERIFY(!error.isEmpty());
    }
    void reportFrameBudgetRejectsBeforeAllocation()
    {
        QFile source(golden("reports.RPT"));
        QVERIFY(source.open(QIODevice::ReadOnly));
        ImportLimits limits; limits.maximumFrames = 0;
        const auto result = LegacyReportReader(limits).readBytes(source.readAll());
        QVERIFY(!result.ok());
        QVERIFY(result.reports.isEmpty());
    }
    void snapshotBudgetAndTypedValuesPreserveExistingRows()
    {
        Database memory;
        QVERIFY(memory.db.isOpen());
        QSqlQuery query(memory.db);
        QVERIFY(query.exec(QStringLiteral("CREATE TABLE decks(name TEXT)")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO decks VALUES('keep')")));
        QString error;
        const QJsonObject text{{QStringLiteral("text"), QStringLiteral("replace")}};
        ImportLimits limits; limits.maximumRows = 0;
        QVERIFY(!SQLiteSnapshot::restore(memory.db, snapshotCell(text), &error, limits));
        for (const QJsonValue& invalid : {
                QJsonValue(QJsonObject{{QStringLiteral("integer"), QStringLiteral("9223372036854775808")}}),
                QJsonValue(QJsonObject{{QStringLiteral("blob"), QStringLiteral("%%%")}}),
                QJsonValue(QJsonObject{{QStringLiteral("text"), QStringLiteral("x")}, {QStringLiteral("integer"), QStringLiteral("1")}}),
                QJsonValue(true)}) {
            QVERIFY(!SQLiteSnapshot::restore(memory.db, snapshotCell(invalid), &error));
        }
        QVERIFY(query.exec(QStringLiteral("SELECT name FROM decks")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("keep"));
        QVERIFY(!query.next());
        query.finish();
        QVERIFY2(SQLiteSnapshot::restore(memory.db, snapshotCell(text), &error), qPrintable(error));
    }
    void snapshotCaptureRejectsViewsGeneratedColumnsAndLargeCells()
    {
        QString error;
        QJsonObject snapshot{{QStringLiteral("unchanged"), true}};
        Database memory;
        QSqlQuery query(memory.db);
        QVERIFY(query.exec(QStringLiteral("CREATE VIEW decks AS SELECT 'x' AS name")));
        QVERIFY(!SQLiteSnapshot::capture(memory.db, &snapshot, &error));
        QVERIFY(snapshot.contains(QStringLiteral("unchanged")));
        QVERIFY(query.exec(QStringLiteral("DROP VIEW decks")));
        QVERIFY(query.exec(QStringLiteral("CREATE /*schema comment*/ VIRTUAL TABLE decks USING fts5(name)")));
        QVERIFY(!SQLiteSnapshot::capture(memory.db, &snapshot, &error));
        QVERIFY(query.exec(QStringLiteral("DROP TABLE decks")));
        QVERIFY(query.exec(QStringLiteral("CREATE TABLE decks(name TEXT, generated TEXT GENERATED ALWAYS AS (name || name))")));
        QVERIFY(!SQLiteSnapshot::capture(memory.db, &snapshot, &error));
        QVERIFY(query.exec(QStringLiteral("DROP TABLE decks")));
        QVERIFY(query.exec(QStringLiteral("CREATE TABLE decks(name TEXT)")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO decks VALUES('0123456789')")));
        ImportLimits limits; limits.maximumCellBytes = 4;
        QVERIFY(!SQLiteSnapshot::capture(memory.db, &snapshot, &error, limits));
        QVERIFY(snapshot.contains(QStringLiteral("unchanged")));
        limits = {}; limits.maximumDecodedBytes = 1;
        QVERIFY(!SQLiteSnapshot::capture(memory.db, &snapshot, &error, limits));
    }
    void jsonStructureIsBudgetedBeforeParsing()
    {
        QString error;
        QVERIFY(SQLiteSnapshot::validateJsonBytes(QByteArray("{\"a\":[1,2,3]}"), &error));
        QVERIFY(!SQLiteSnapshot::validateJsonBytes(QByteArray(129, '[') + QByteArray(129, ']'), &error));
        ImportLimits limits; limits.maximumDecodedBytes = 100;
        QVERIFY(!SQLiteSnapshot::validateJsonBytes(QByteArray("[0,0,0,0,0]"), &error, limits));
        limits = {}; limits.maximumCellBytes = 4;
        QVERIFY(!SQLiteSnapshot::validateJsonBytes(QByteArray("\"0123456789\""), &error, limits));
    }
    void sqliteFileBudgetRejectsWithoutChangingDestination()
    {
        QTemporaryDir directory;
        const auto original = QByteArray("not a database", 14);
        const QString path = writeFile(directory, QStringLiteral("oversized.cardstack"), original);
        ImportLimits limits; limits.maximumInputBytes = 8;
        SQLiteDeckStore store(limits);
        QString error;
        QVERIFY(!store.open(path, &error));
        QFile source(path);
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), original);
    }
    void budgetedSaveKeepsPreviousFileBytes()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("keep.cardstack"));
        Deck original(QStringLiteral("Keep"));
        original.addField(FieldDefinition(QStringLiteral("Name"), FieldType::Text, 255));
        original.addCard(CardRecord({QStringLiteral("Original")}));
        QString error;
        {
            SQLiteDeckStore store;
            QVERIFY2(store.create(path, &error), qPrintable(error));
            QVERIFY2(store.saveDeck(original, &error), qPrintable(error));
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto before = file.readAll();
        file.close();
        ImportLimits limits;
        limits.maximumValues = 500;
        SQLiteDeckStore store(limits);
        QVERIFY2(store.open(path, &error), qPrintable(error));
        Deck larger = original;
        for (int index = 0; index < 1000; ++index) larger.addCard(CardRecord({QStringLiteral("Extra")}));
        QVERIFY(!store.saveDeck(larger, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), before);
        file.close();
        Deck reloaded;
        SQLiteDeckStore reader;
        QVERIFY2(reader.open(path, &error), qPrintable(error));
        QVERIFY2(reader.loadDeck(&reloaded, &error), qPrintable(error));
        QCOMPARE(reloaded.cardCount(), 1);
        QCOMPARE(reloaded.cardAt(0).valueAt(0), QStringLiteral("Original"));
    }
    void encryptedSnapshotBudgetRejectsBeforeJsonConstruction()
    {
        QTemporaryDir directory;
        const QString password = QStringLiteral("Snapshot test secret");
        const auto credentials = DeckSecurity::createCredentials(password);
        QVERIFY(credentials.key);
        const QByteArray json = QByteArray("{\"snapshot_version\":1,\"tables\":{},\"padding\":\"") + QByteArray(2000, 'x') + QByteArray("\"}");
        const QByteArray container = DeckSecurity::encrypt(json, credentials.record, credentials.key);
        const QString path = writeFile(directory, QStringLiteral("limited.cardstack"), container);
        ImportLimits limits;
        limits.maximumCellBytes = 128;
        SQLiteDeckStore store(limits);
        QString error;
        QVERIFY(!store.open(path, &error, password));
        QVERIFY(!error.isEmpty());
        QVERIFY(store.securityError() != DeckSecurity::Error::WrongPassword);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), container);
    }
    void fingerprintReadsHonorLimitsAndCancellation()
    {
        QTemporaryDir directory;
        const QString path = writeFile(directory, QStringLiteral("fingerprint.cardstack"), QByteArray(256, 'x'));
        bool ok = false;
        const auto expected = documentFileFingerprint(path, &ok);
        QVERIFY(ok);
        QVERIFY(!expected.isEmpty());
        ImportLimits limits; limits.maximumInputBytes = 128;
        QVERIFY(documentFileFingerprint(path, &ok, limits).isEmpty());
        QVERIFY(!ok);
        std::atomic_bool cancelled{true};
        limits = {}; limits.cancellation = &cancelled;
        QVERIFY(documentFileFingerprint(path, &ok, limits).isEmpty());
        QVERIFY(!ok);
        QCOMPARE(documentFileFingerprint(path, &ok), expected);
        QVERIFY(ok);
    }
    void deterministicMalformedCorpusStaysBounded()
    {
        QTemporaryDir directory;
        quint32 state = 0x4c494d49;
        for (int index = 0; index < 48; ++index) {
            const bool native = index >= 24;
            QByteArray bytes = native ? variableFile() : cardfile();
            for (int mutation = 0; mutation < 4; ++mutation) {
                state = state * 1664525U + 1013904223U;
                const int offset = int(state % quint32(bytes.size()));
                state = state * 1664525U + 1013904223U;
                bytes[offset] = char(state >> 24);
            }
            if (index % 7 == 0) bytes.truncate(int(state % quint32(bytes.size())));
            const QString path = writeFile(directory, native ? QStringLiteral("mutation.BTN") : QStringLiteral("mutation.CRD"), bytes);
            QVERIFY(!path.isEmpty());
            QProcess worker;
            worker.start(QCoreApplication::applicationFilePath(),
                {native ? QStringLiteral("--security-native") : QStringLiteral("--security-interchange"), path});
            QVERIFY(worker.waitForStarted(3000));
            const bool finished = worker.waitForFinished(5000);
            if (!finished) { worker.kill(); worker.waitForFinished(); }
            QVERIFY2(finished, qPrintable(QStringLiteral("Mutation %1 timed out").arg(index)));
            QCOMPARE(worker.exitStatus(), QProcess::NormalExit);
            QVERIFY2(worker.exitCode() == 0 || worker.exitCode() == 7, worker.readAllStandardError().constData());
        }
    }
};

#ifdef CARDSTACK_TEST_SUITE
int runImportSecurityTests(int argc, char** argv)
{
    ImportSecurityTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#else
QTEST_MAIN(ImportSecurityTests)
#endif
#include "ImportSecurityTests.moc"
