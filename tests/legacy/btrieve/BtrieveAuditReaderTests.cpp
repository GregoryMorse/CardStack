#include "BtrieveAuditReader.h"
#include "BtrieveFileSaverReader.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace CardStack;

namespace {

void putU16(QByteArray* bytes, int offset, quint16 value)
{
    (*bytes)[offset] = static_cast<char>(value & 0xff);
    (*bytes)[offset + 1] = static_cast<char>((value >> 8) & 0xff);
}

void putU32(QByteArray* bytes, int offset, quint32 value)
{
    putU16(bytes, offset, static_cast<quint16>(value & 0xffff));
    putU16(bytes, offset + 2, static_cast<quint16>((value >> 16) & 0xffff));
}

void putWordSwappedU32(QByteArray* bytes, int offset, quint32 value)
{
    putU16(bytes, offset, static_cast<quint16>((value >> 16) & 0xffff));
    putU16(bytes, offset + 2, static_cast<quint16>(value & 0xffff));
}

void putText(QByteArray* bytes, int offset, int length, const QByteArray& value)
{
    for (int index = 0; index < length; ++index) {
        (*bytes)[offset + index] = index < value.size() ? value.at(index) : '\0';
    }
}

void putKeySpec(
    QByteArray* bytes,
    int index,
    quint32 rootPage,
    quint32 uniqueCount,
    quint16 flags,
    quint16 totalSegmentLength,
    quint16 keyLength,
    quint16 recordOffset,
    quint16 segmentLength)
{
    const int offset = 0x110 + index * 0x1c;
    putU16(bytes, offset, static_cast<quint16>((rootPage >> 16) & 0xffff));
    putU16(bytes, offset + 2, static_cast<quint16>(rootPage & 0xffff));
    putWordSwappedU32(bytes, offset + 4, uniqueCount);
    putU16(bytes, offset + 8, flags);
    putU16(bytes, offset + 10, totalSegmentLength);
    putU16(bytes, offset + 12, keyLength);
    putU16(bytes, offset + 14, 100);
    putU16(bytes, offset + 16, 50);
    putU16(bytes, offset + 18, 0);
    putU16(bytes, offset + 20, recordOffset);
    putU16(bytes, offset + 22, segmentLength);
}

QString writeSyntheticBtrieveFile(const QString& path)
{
    constexpr int pageSize = 512;
    constexpr int physicalRecordLength = 64;
    QByteArray bytes(pageSize * 2, '\0');

    putU16(&bytes, 4, 1);
    putU16(&bytes, 6, 0x0500);
    putU16(&bytes, 8, pageSize);
    putU16(&bytes, 0x14, 1);
    putU16(&bytes, 0x16, 60);
    putU16(&bytes, 0x18, physicalRecordLength);
    putWordSwappedU32(&bytes, 0x1a, 1);
    putU16(&bytes, 0x22, 0xffff);
    bytes[0x38] = static_cast<char>(0xfd);
    putText(&bytes, 0x3c, 8, "CASEINSX");
    putU16(&bytes, 0x106, 0x1234);
    putKeySpec(&bytes, 0, 1, 1, 0x0000, 5, 13, 0, 5);

    const int dataPageOffset = pageSize;
    putU16(&bytes, dataPageOffset, 0);
    putU16(&bytes, dataPageOffset + 2, 1);
    putU16(&bytes, dataPageOffset + 4, 0x8002);
    putText(&bytes, dataPageOffset + 6 + 8, 16, "record");

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return QStringLiteral("Could not write synthetic Btrieve file.");
    }
    file.write(bytes);
    return {};
}

int pageCountOfType(const BtrieveAuditReader::Audit& audit, BtrieveAuditReader::PageType type)
{
    int count = 0;
    for (const BtrieveAuditReader::Page& page : audit.pages) {
        if (page.type == type) {
            ++count;
        }
    }
    return count;
}

} // namespace

class BtrieveAuditReaderTests : public QObject {
    Q_OBJECT

private slots:
    void parsesOldFormatFcrPagesAndKeys()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("synthetic.btn"));
        const QString error = writeSyntheticBtrieveFile(path);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        const BtrieveAuditReader reader;
        const BtrieveAuditReader::Result result = reader.readFile(path);
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));

        QCOMPARE(result.audit.oldFormat, true);
        QCOMPARE(result.audit.version, 0x0500);
        QCOMPARE(result.audit.pageSize, 512);
        QCOMPARE(result.audit.pageCount, 2);
        QCOMPARE(result.audit.fixedRecordLength, 60);
        QCOMPARE(result.audit.physicalRecordLength, 64);
        QCOMPARE(result.audit.declaredRecordCount, 1);
        QCOMPARE(result.audit.keyCount, 1);
        QCOMPARE(result.audit.acsName, QStringLiteral("CASEINSX"));
        QCOMPARE(result.audit.variableRecordsAllowed, true);
        QCOMPARE(result.audit.userFlags, 0x1234);
        QCOMPARE(pageCountOfType(result.audit, BtrieveAuditReader::PageType::Data), 1);
        QCOMPARE(result.audit.occupiedFixedRecordSlots, 1);

        QCOMPARE(result.audit.keySpecs.size(), 1);
        QCOMPARE(result.audit.keySpecs.at(0).rootPage, 1U);
        QCOMPARE(result.audit.keySpecs.at(0).uniqueValueCount, 1U);
        QCOMPARE(result.audit.keySpecs.at(0).recordOffset, 0);
        QCOMPARE(result.audit.keySpecs.at(0).segmentLength, 5);
    }

    void auditsSoftwareSampleWhenConfigured()
    {
        const QString samplePath = qEnvironmentVariable("CARDSTACK_LEGACY_DECK_SAMPLE");
        if (samplePath.isEmpty() || !QFileInfo::exists(samplePath)) {
            QSKIP("Set CARDSTACK_LEGACY_DECK_SAMPLE to a legacy .BTN file to run the real Btrieve audit check.");
        }

        const BtrieveAuditReader auditReader;
        const BtrieveAuditReader::Result audit = auditReader.readFile(samplePath);
        QVERIFY2(audit.ok(), qPrintable(audit.errorMessage));

        QCOMPARE(audit.audit.oldFormat, true);
        QCOMPARE(audit.audit.version, 0x0500);
        QCOMPARE(audit.audit.pageSize, 4096);
        QCOMPARE(audit.audit.pageCount, 6);
        QCOMPARE(audit.audit.fixedRecordLength, 329);
        QCOMPARE(audit.audit.physicalRecordLength, 335);
        QCOMPARE(audit.audit.declaredRecordCount, 5);
        QCOMPARE(audit.audit.keyCount, 2);
        QCOMPARE(audit.audit.acsName, QStringLiteral("CASEINSX"));
        QCOMPARE(pageCountOfType(audit.audit, BtrieveAuditReader::PageType::FileControlRecord), 1);
        QCOMPARE(pageCountOfType(audit.audit, BtrieveAuditReader::PageType::Data), 1);
        QCOMPARE(audit.audit.occupiedFixedRecordSlots, 5);

        QCOMPARE(audit.audit.keySpecs.size(), 2);
        QCOMPARE(audit.audit.keySpecs.at(0).recordOffset, 0);
        QCOMPARE(audit.audit.keySpecs.at(0).segmentLength, 5);
        QCOMPARE(audit.audit.keySpecs.at(0).uniqueValueCount, 5U);
        QCOMPARE(audit.audit.keySpecs.at(1).fcrOffset, 0x12e);
        QCOMPARE(audit.audit.keySpecs.at(1).flags, 0x00b2);
        QCOMPARE(audit.audit.keySpecs.at(1).recordOffset, 5);
        QCOMPARE(audit.audit.keySpecs.at(1).segmentLength, 20);

        const BtrieveFileSaverReader recordReader;
        const BtrieveFileSaverReader::Result records = recordReader.readAllRecords(samplePath);
        QVERIFY2(records.ok(), qPrintable(records.errorMessage));
        QCOMPARE(records.metadata.version, audit.audit.version);
        QCOMPARE(records.metadata.pageSize, audit.audit.pageSize);
        QCOMPARE(records.metadata.fixedRecordLength, audit.audit.fixedRecordLength);
        QCOMPARE(records.metadata.internalFixedRecordLength, audit.audit.physicalRecordLength);
        QCOMPARE(records.metadata.declaredRecordCount, audit.audit.declaredRecordCount);
        QCOMPARE(records.records.size(), audit.audit.occupiedFixedRecordSlots);
    }
};

#ifdef CARDSTACK_TEST_SUITE
int runBtrieveAuditReaderTests(int argc, char** argv)
{
    BtrieveAuditReaderTests test;
    return QTest::qExec(&test, argc, argv);
}
#else
QTEST_GUILESS_MAIN(BtrieveAuditReaderTests)
#endif

#include "BtrieveAuditReaderTests.moc"

