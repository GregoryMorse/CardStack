#include "LegacyDeckReader.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace CardStack;

namespace {

constexpr int RecordPayloadOffset = 5;
constexpr int SchemaFieldCountOffset = 87;
constexpr int FieldDefinitionSize = 37;
constexpr int FieldNameSize = 16;
constexpr int OldFormatOwnerFlagsOffset = 0x37;
constexpr quint8 OldFormatOwnerReadAccessFlag = 0x01;

void putU16(QByteArray* bytes, int offset, quint16 value)
{
    (*bytes)[offset] = static_cast<char>(value & 0xff);
    (*bytes)[offset + 1] = static_cast<char>((value >> 8) & 0xff);
}

void putCString(QByteArray* bytes, int offset, int length, const QByteArray& value)
{
    for (int index = 0; index < length; ++index) {
        (*bytes)[offset + index] = index < value.size() ? value.at(index) : '\0';
    }
}

void putFieldDefinition(
    QByteArray* schema,
    int offset,
    const QByteArray& name,
    quint8 type,
    quint16 recordOffset,
    quint16 length)
{
    putCString(schema, offset, FieldNameSize, name);
    (*schema)[offset + 16] = static_cast<char>(type);
    putU16(schema, offset + 20, recordOffset);
    putU16(schema, offset + 22, length);
}

QByteArray makeSchemaRecord()
{
    QByteArray record(512, '\0');
    putCString(&record, 5, 5, "TN20");
    putCString(&record, 11, 64, "Synthetic Library");
    record[SchemaFieldCountOffset] = 3;

    const int fieldsOffset = 256;
    putFieldDefinition(&record, fieldsOffset, "Product", 1, 0, 30);
    putFieldDefinition(&record, fieldsOffset + FieldDefinitionSize, "Version", 1, 30, 10);
    putFieldDefinition(&record, fieldsOffset + FieldDefinitionSize * 2, "Notes", 3, 40, 40);
    return record;
}

QByteArray makeDeclaredCountSchemaRecord()
{
    QByteArray record(512, '\0');
    putCString(&record, 5, 5, "TN20");
    putCString(&record, 11, 64, "Declared Count Library");
    record[SchemaFieldCountOffset] = 2;

    const int fieldsOffset = 256;
    putFieldDefinition(&record, fieldsOffset, "Alpha", 1, 0, 12);
    putFieldDefinition(&record, fieldsOffset + FieldDefinitionSize, "Beta", 1, 12, 12);
    putFieldDefinition(&record, fieldsOffset + FieldDefinitionSize * 2, "ShouldSkip", 1, 24, 12);
    return record;
}

QByteArray makeCardRecord(const QByteArray& product, const QByteArray& version, const QByteArray& notes)
{
    QByteArray record(RecordPayloadOffset + 80, '\0');
    putCString(&record, RecordPayloadOffset, 30, product);
    putCString(&record, RecordPayloadOffset + 30, 10, version);
    putCString(&record, RecordPayloadOffset + 40, 40, notes);
    return record;
}

} // namespace

class LegacyDeckReaderTests : public QObject {
    Q_OBJECT

private slots:
    void mapsLegacyFieldDefinitionsAndFixedRecords()
    {
        const QVector<QByteArray> records = {
            makeSchemaRecord(),
            makeCardRecord("ProjectKit", "1.0", "Legacy card"),
            makeCardRecord("Windows", "3.x", "Second card"),
            QByteArray("\0\0\0\0\0~BT~LO2", 12),
        };

        const LegacyDeckReader reader;
        const LegacyDeckReader::Result result = reader.readRecords(records, QStringLiteral("Fallback"));
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));

        QCOMPARE(result.deck.name(), QStringLiteral("Synthetic Library"));
        QCOMPARE(result.deck.fieldCount(), 3);
        QCOMPARE(result.deck.fieldAt(0).name(), QStringLiteral("Product"));
        QCOMPARE(result.deck.fieldAt(0).type(), FieldType::Text);
        QCOMPARE(result.deck.fieldAt(0).maxLength(), 30);
        QCOMPARE(result.deck.fieldAt(2).name(), QStringLiteral("Notes"));
        QCOMPARE(result.deck.fieldAt(2).type(), FieldType::Notes);
        QCOMPARE(result.deck.cardCount(), 2);
        QCOMPARE(result.deck.cardAt(0).valueAt(0), QStringLiteral("ProjectKit"));
        QCOMPARE(result.deck.cardAt(0).valueAt(1), QStringLiteral("1.0"));
        QCOMPARE(result.deck.cardAt(0).valueAt(2), QStringLiteral("Legacy card"));
        QCOMPARE(result.deck.cardAt(1).valueAt(0), QStringLiteral("Windows"));
        QCOMPARE(result.deck.cardAt(1).valueAt(2), QStringLiteral("Second card"));
    }

    void honorsDeclaredFieldCountWithoutProductAnchor()
    {
        QByteArray card(RecordPayloadOffset + 36, '\0');
        putCString(&card, RecordPayloadOffset, 12, "first");
        putCString(&card, RecordPayloadOffset + 12, 12, "second");
        putCString(&card, RecordPayloadOffset + 24, 12, "ignored");

        const LegacyDeckReader reader;
        const LegacyDeckReader::Result result = reader.readRecords(
            {makeDeclaredCountSchemaRecord(), card},
            QStringLiteral("Fallback"));
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));

        QCOMPARE(result.deck.name(), QStringLiteral("Declared Count Library"));
        QCOMPARE(result.deck.fieldCount(), 2);
        QCOMPARE(result.deck.fieldAt(0).name(), QStringLiteral("Alpha"));
        QCOMPARE(result.deck.fieldAt(1).name(), QStringLiteral("Beta"));
        QCOMPARE(result.deck.cardCount(), 1);
        QCOMPARE(result.deck.cardAt(0).valueAt(0), QStringLiteral("first"));
        QCOMPARE(result.deck.cardAt(0).valueAt(1), QStringLiteral("second"));
    }

    void importsSoftwareSampleWhenConfigured()
    {
        const QString samplePath = qEnvironmentVariable("CARDSTACK_LEGACY_DECK_SAMPLE");
        if (samplePath.isEmpty() || !QFileInfo::exists(samplePath)) {
            QSKIP("Set CARDSTACK_LEGACY_DECK_SAMPLE to a legacy .BTN file to run the real legacy import check.");
        }

        const LegacyDeckReader reader;
        const LegacyDeckReader::Result result = reader.readDeck(samplePath);
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));

        QCOMPARE(result.btrieveMetadata.version, 0x0500);
        QCOMPARE(result.btrieveMetadata.pageSize, 4096);
        QCOMPARE(result.btrieveMetadata.fixedRecordLength, 329);
        QCOMPARE(result.rawRecords.size(), 5);
        QCOMPARE(result.deck.name(), QStringLiteral("Software Library"));
        QCOMPARE(result.deck.fieldCount(), 11);
        QStringList importedProducts;
        for (int index = 0; index < result.deck.cardCount(); ++index) {
            importedProducts.append(result.deck.cardAt(index).valueAt(0));
        }
        QVERIFY2(
            result.deck.cardCount() == 3,
            qPrintable(QStringLiteral("Imported Product values: %1").arg(importedProducts.join(QStringLiteral(" | ")))));
        QCOMPARE(result.deck.fieldAt(0).name(), QStringLiteral("Product"));
        QCOMPARE(result.deck.fieldAt(10).name(), QStringLiteral("Notes"));
        QCOMPARE(result.deck.cardAt(0).valueAt(0), QStringLiteral("button") + QStringLiteral("File"));
        QCOMPARE(result.deck.cardAt(0).valueAt(1), QStringLiteral("1.0"));
        QCOMPARE(result.deck.cardAt(0).valueAt(2), QStringLiteral("Button") + QStringLiteral("Ware Incorporated"));
        QCOMPARE(result.deck.cardAt(1).valueAt(0), QStringLiteral("Windows") + QChar(0x2122));
        QCOMPARE(result.deck.cardAt(2).valueAt(0), QStringLiteral("other software"));
    }

    void importsWineVdmGoldenFixturesWhenConfigured()
    {
        const QString fixtureDirPath = qEnvironmentVariable("CARDSTACK_WINEVDM_GOLDEN_DIR");
        if (fixtureDirPath.isEmpty() || !QDir(fixtureDirPath).exists()) {
            QSKIP("Set CARDSTACK_WINEVDM_GOLDEN_DIR to tests/fixtures/legacy/winevdm or another WineVDM fixture directory.");
        }

        const QDir fixtureDir(fixtureDirPath);
        const QString plainPath = fixtureDir.filePath(QStringLiteral("plain.BTN"));
        const QString passwordOnlyPath = fixtureDir.filePath(QStringLiteral("pwonly.BTN"));
        const QString encryptedPath = fixtureDir.filePath(QStringLiteral("crypt.BTN"));
        const QString notesHeavyPath = fixtureDir.filePath(QStringLiteral("notes_heavy.BTN"));
        const QString manyFieldsPath = fixtureDir.filePath(QStringLiteral("many_fields.BTN"));
        const QString maxLengthsPath = fixtureDir.filePath(QStringLiteral("max_lengths.BTN"));
        const QString securityCyclePath = fixtureDir.filePath(QStringLiteral("security_cycle.BTN"));
        QVERIFY2(QFileInfo::exists(plainPath), qPrintable(QStringLiteral("Missing %1").arg(plainPath)));
        QVERIFY2(QFileInfo::exists(passwordOnlyPath), qPrintable(QStringLiteral("Missing %1").arg(passwordOnlyPath)));
        QVERIFY2(QFileInfo::exists(encryptedPath), qPrintable(QStringLiteral("Missing %1").arg(encryptedPath)));
        QVERIFY2(QFileInfo::exists(notesHeavyPath), qPrintable(QStringLiteral("Missing %1").arg(notesHeavyPath)));
        QVERIFY2(QFileInfo::exists(manyFieldsPath), qPrintable(QStringLiteral("Missing %1").arg(manyFieldsPath)));
        QVERIFY2(QFileInfo::exists(maxLengthsPath), qPrintable(QStringLiteral("Missing %1").arg(maxLengthsPath)));
        QVERIFY2(QFileInfo::exists(securityCyclePath), qPrintable(QStringLiteral("Missing %1").arg(securityCyclePath)));

        const LegacyDeckReader reader;
        const LegacyDeckReader::Result plain = reader.readDeck(plainPath);
        QVERIFY2(plain.ok(), qPrintable(plain.errorMessage));
        QCOMPARE(plain.deck.name(), QStringLiteral("Software Library"));
        QCOMPARE(plain.deck.fieldCount(), 11);
        QCOMPARE(plain.deck.cardCount(), 3);
        QCOMPARE(plain.deck.fieldAt(10).name(), QStringLiteral("Notes"));

        const LegacyDeckReader::Result passwordMissing = reader.readDeck(passwordOnlyPath);
        QVERIFY(passwordMissing.passwordRequired);
        QVERIFY(!passwordMissing.passwordRejected);

        const LegacyDeckReader::Result passwordOnly = reader.readDeck(passwordOnlyPath, QStringLiteral("Legacy01"));
        QVERIFY2(passwordOnly.ok(), qPrintable(passwordOnly.errorMessage));
        QVERIFY(passwordOnly.legacyPasswordProtected);
        QVERIFY(passwordOnly.legacyPasswordVerified);
        QVERIFY(!passwordOnly.legacyPasswordVerificationUnavailable);
        QVERIFY(passwordOnly.deck.description().contains(QStringLiteral("password-protected")));
        QCOMPARE(passwordOnly.deck.name(), QStringLiteral("Software Library"));
        QCOMPARE(passwordOnly.deck.fieldCount(), 11);
        QCOMPARE(passwordOnly.deck.cardCount(), 3);

        const LegacyDeckReader::Result passwordWrong = reader.readDeck(passwordOnlyPath, QStringLiteral("Wrong01"));
        QVERIFY(!passwordWrong.ok());
        QVERIFY(passwordWrong.legacyPasswordProtected);
        QVERIFY(passwordWrong.passwordRejected);
        QVERIFY(!passwordWrong.legacyPasswordVerified);
        QVERIFY(!passwordWrong.legacyPasswordVerificationUnavailable);

        QTemporaryDir readAccessDirectory;
        QVERIFY(readAccessDirectory.isValid());
        const QString readAccessPath = readAccessDirectory.filePath(QStringLiteral("readflag.BTN"));
        QVERIFY(QFile::copy(passwordOnlyPath, readAccessPath));
        QFile readAccessFile(readAccessPath);
        QVERIFY(readAccessFile.open(QIODevice::ReadWrite));
        QVERIFY(readAccessFile.seek(OldFormatOwnerFlagsOffset));
        const QByteArray ownerFlags = readAccessFile.read(1);
        QCOMPARE(ownerFlags.size(), 1);
        QVERIFY(readAccessFile.seek(OldFormatOwnerFlagsOffset));
        QCOMPARE(
            readAccessFile.write(QByteArray(1, static_cast<char>(
                static_cast<quint8>(ownerFlags.at(0)) | OldFormatOwnerReadAccessFlag))),
            qsizetype(1));
        readAccessFile.close();

        const LegacyDeckReader::Result readAllowedWithoutPassword = reader.readDeck(readAccessPath);
        QVERIFY2(readAllowedWithoutPassword.ok(), qPrintable(readAllowedWithoutPassword.errorMessage));
        QVERIFY(readAllowedWithoutPassword.legacyPasswordProtected);
        QVERIFY(!readAllowedWithoutPassword.passwordRequired);
        QVERIFY(!readAllowedWithoutPassword.passwordRejected);
        QVERIFY(!readAllowedWithoutPassword.legacyPasswordVerified);
        QVERIFY(readAllowedWithoutPassword.legacyPasswordVerificationUnavailable);
        QCOMPARE(readAllowedWithoutPassword.deck.name(), QStringLiteral("Software Library"));
        QCOMPARE(readAllowedWithoutPassword.deck.cardCount(), 3);

        const LegacyDeckReader::Result readAllowedWithWrongPassword = reader.readDeck(readAccessPath, QStringLiteral("Wrong01"));
        QVERIFY2(readAllowedWithWrongPassword.ok(), qPrintable(readAllowedWithWrongPassword.errorMessage));
        QVERIFY(readAllowedWithWrongPassword.legacyPasswordProtected);
        QVERIFY(!readAllowedWithWrongPassword.passwordRejected);
        QVERIFY(!readAllowedWithWrongPassword.legacyPasswordVerified);
        QVERIFY(readAllowedWithWrongPassword.legacyPasswordVerificationUnavailable);

        const LegacyDeckReader::Result encryptedMissing = reader.readDeck(encryptedPath);
        QVERIFY(encryptedMissing.passwordRequired);
        QVERIFY(!encryptedMissing.passwordRejected);

        const LegacyDeckReader::Result encrypted = reader.readDeck(encryptedPath, QStringLiteral("Crypt01"));
        QVERIFY2(encrypted.ok(), qPrintable(encrypted.errorMessage));
        QVERIFY(encrypted.legacyPasswordProtected);
        QVERIFY(encrypted.legacyDataEncrypted);
        QVERIFY(encrypted.legacyPasswordVerified);
        QVERIFY(!encrypted.legacyPasswordVerificationUnavailable);
        QCOMPARE(encrypted.deck.name(), QStringLiteral("Software Library"));
        QCOMPARE(encrypted.deck.fieldCount(), 11);
        QCOMPARE(encrypted.deck.cardCount(), 3);
        QCOMPARE(encrypted.deck.cardAt(0).valueAt(0), QStringLiteral("button") + QStringLiteral("File"));

        const LegacyDeckReader::Result encryptedWrong = reader.readDeck(encryptedPath, QStringLiteral("Wrong01"));
        QVERIFY(!encryptedWrong.ok());
        QVERIFY(encryptedWrong.legacyPasswordProtected);
        QVERIFY(encryptedWrong.legacyDataEncrypted);
        QVERIFY(encryptedWrong.passwordRejected);
        QVERIFY(!encryptedWrong.legacyPasswordVerified);
        QVERIFY(!encryptedWrong.legacyPasswordVerificationUnavailable);

        const LegacyDeckReader::Result notesHeavy = reader.readDeck(notesHeavyPath);
        QVERIFY2(notesHeavy.ok(), qPrintable(notesHeavy.errorMessage));
        QCOMPARE(notesHeavy.deck.name(), QStringLiteral("Notes Heavy Library"));
        QCOMPARE(notesHeavy.deck.cardAt(0).valueAt(10), QStringLiteral("Line1\r\nLine2\r\nLegacy note edge"));
        QCOMPARE(notesHeavy.deck.cardAt(1).valueAt(10), QStringLiteral("Tabbed\tand CRLF\r\nnotes sample"));
        QCOMPARE(notesHeavy.deck.cardAt(2).valueAt(10), QStringLiteral("012345678901234567890123456789012345678"));

        const LegacyDeckReader::Result maxLengths = reader.readDeck(maxLengthsPath);
        QVERIFY2(maxLengths.ok(), qPrintable(maxLengths.errorMessage));
        QCOMPARE(maxLengths.deck.name(), QStringLiteral("Max Length Library"));
        QCOMPARE(maxLengths.deck.cardAt(0).valueAt(0), QStringLiteral("PRODUCT-MAX-123456789012345"));
        QCOMPARE(maxLengths.deck.cardAt(0).valueAt(10), QStringLiteral("NOTES-MAX-12345678901234567890123456789"));

        const LegacyDeckReader::Result manyFields = reader.readDeck(manyFieldsPath);
        QVERIFY2(manyFields.ok(), qPrintable(manyFields.errorMessage));
        QCOMPARE(manyFields.deck.name(), QStringLiteral("Many Fields Library"));
        QCOMPARE(manyFields.deck.fieldCount(), 14);
        QCOMPARE(manyFields.deck.fieldAt(11).name(), QStringLiteral("ExtraA"));
        QCOMPARE(manyFields.deck.fieldAt(12).name(), QStringLiteral("ExtraB"));
        QCOMPARE(manyFields.deck.fieldAt(13).name(), QStringLiteral("ExtraC"));
        QCOMPARE(manyFields.deck.cardAt(0).valueAt(11), QStringLiteral("A1"));
        QCOMPARE(manyFields.deck.cardAt(0).valueAt(12), QStringLiteral("B1"));
        QCOMPARE(manyFields.deck.cardAt(0).valueAt(13), QStringLiteral("C1"));

        const LegacyDeckReader::Result securityCycle = reader.readDeck(securityCyclePath);
        QVERIFY2(securityCycle.ok(), qPrintable(securityCycle.errorMessage));
        QVERIFY(!securityCycle.passwordRequired);
        QVERIFY(!securityCycle.passwordRejected);
        QCOMPARE(securityCycle.deck.name(), QStringLiteral("Software Library"));
        QCOMPARE(securityCycle.deck.cardCount(), 3);
    }
};

#ifdef CARDSTACK_TEST_SUITE
int runLegacyDeckReaderTests(int argc, char** argv)
{
    LegacyDeckReaderTests test;
    return QTest::qExec(&test, argc, argv);
}
#else
QTEST_GUILESS_MAIN(LegacyDeckReaderTests)
#endif

#include "LegacyDeckReaderTests.moc"

