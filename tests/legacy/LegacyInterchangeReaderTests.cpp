#include "LegacyInterchangeReader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace CardStack;

namespace {

QString fixturePath(const QString& fileName)
{
    const QString fixtureDirPath = qEnvironmentVariable("CARDSTACK_WINEVDM_GOLDEN_DIR");
    if (fixtureDirPath.isEmpty() || !QDir(fixtureDirPath).exists()) {
        return {};
    }
    return QDir(fixtureDirPath).filePath(fileName);
}

void requireFixture(const QString& filePath)
{
    if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
        QSKIP("Set CARDSTACK_WINEVDM_GOLDEN_DIR to tests/fixtures/legacy/winevdm with generated interchange fixtures.");
    }
}

QString writeFixtureFile(QTemporaryDir* directory, const QString& fileName, const QByteArray& bytes)
{
    const QString path = directory->filePath(fileName);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(bytes);
    return path;
}

} // namespace

class LegacyInterchangeReaderTests : public QObject {
    Q_OBJECT

private slots:
    void detectsLegacyInterchangeExtensions()
    {
        QVERIFY(isLegacyInterchangePath(QStringLiteral("sample.DBF")));
        QVERIFY(isLegacyInterchangePath(QStringLiteral("sample.crd")));
        QVERIFY(isLegacyInterchangePath(QStringLiteral("sample.TN")));
        QVERIFY(isLegacyInterchangePath(QStringLiteral("sample.wp")));
        QVERIFY(!isLegacyInterchangePath(QStringLiteral("sample.btn")));
        QVERIFY(!isLegacyInterchangePath(QStringLiteral("sample.csv")));
    }

    void importsDBaseGoldenFixture()
    {
        const QString path = fixturePath(QStringLiteral("EXDBF.DBF"));
        requireFixture(path);

        const LegacyInterchangeReader reader;
        const LegacyInterchangeReader::Result result = reader.readFile(path);
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.format, LegacyInterchangeReader::Format::DBase);
        QCOMPARE(result.deck.fieldCount(), 11);
        QCOMPARE(result.deck.cardCount(), 3);
        QCOMPARE(result.deck.fieldAt(0).name(), QStringLiteral("PRODUCT"));
        QCOMPARE(result.deck.fieldAt(0).maxLength(), 30);
        QCOMPARE(result.deck.fieldAt(10).name(), QStringLiteral("NOTES"));
        QCOMPARE(result.deck.fieldAt(10).type(), FieldType::Notes);
        QCOMPARE(result.deck.cardAt(0).valueAt(0), QStringLiteral("button") + QStringLiteral("File"));
        QCOMPARE(result.deck.cardAt(0).valueAt(1), QStringLiteral("1.0"));
        QCOMPARE(result.deck.cardAt(0).valueAt(2), QStringLiteral("Button") + QStringLiteral("Ware Incorporated"));
        QCOMPARE(result.deck.cardAt(1).valueAt(0), QStringLiteral("other software"));
        QCOMPARE(result.deck.cardAt(2).valueAt(0), QStringLiteral("WindowsT"));
        QCOMPARE(result.deck.cardAt(0).valueAt(10), QString());
    }

    void importsMicrosoftCardfileGoldenFixture()
    {
        const QString path = fixturePath(QStringLiteral("EXCRD.CRD"));
        requireFixture(path);

        const LegacyInterchangeReader reader;
        const LegacyInterchangeReader::Result result = reader.readFile(path);
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.format, LegacyInterchangeReader::Format::MicrosoftCardfile);
        QCOMPARE(result.deck.fieldCount(), 2);
        QCOMPARE(result.deck.cardCount(), 3);
        QCOMPARE(result.deck.fieldAt(0).name(), QStringLiteral("Title"));
        QCOMPARE(result.deck.fieldAt(1).type(), FieldType::Notes);
        QCOMPARE(result.deck.cardAt(0).valueAt(0), QStringLiteral("button") + QStringLiteral("File"));
        QVERIFY(result.deck.cardAt(0).valueAt(1).contains(QStringLiteral("Button") + QStringLiteral("Ware Incorporated")));
        QCOMPARE(result.deck.cardAt(1).valueAt(0), QStringLiteral("other software"));
        QCOMPARE(result.deck.cardAt(2).valueAt(0), QStringLiteral("Windows") + QChar(0x2122));
        QVERIFY(result.deck.cardAt(2).valueAt(1).contains(QStringLiteral("Microsoft Corporation")));
    }

    void importsWordPerfectMergeGoldenFixture()
    {
        const QString path = fixturePath(QStringLiteral("EXWP.WP"));
        requireFixture(path);

        const LegacyInterchangeReader reader;
        const LegacyInterchangeReader::Result result = reader.readFile(path);
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.format, LegacyInterchangeReader::Format::WordPerfectMerge);
        QCOMPARE(result.deck.fieldCount(), 11);
        QCOMPARE(result.deck.cardCount(), 3);
        QCOMPARE(result.deck.cardAt(0).valueAt(0), QStringLiteral("button") + QStringLiteral("File"));
        QCOMPARE(result.deck.cardAt(0).valueAt(1), QStringLiteral("1.0"));
        QCOMPARE(result.deck.cardAt(0).valueAt(2), QStringLiteral("Button") + QStringLiteral("Ware Incorporated"));
        QCOMPARE(result.deck.cardAt(1).valueAt(0), QStringLiteral("other software"));
        QCOMPARE(result.deck.cardAt(2).valueAt(0), QStringLiteral("Windows") + QChar(0x2122));
        QCOMPARE(result.deck.cardAt(2).valueAt(2), QStringLiteral("Microsoft Corporation"));
    }

    void importsTakeNoteGoldenFixtureThroughBtrieveReader()
    {
        const QString path = fixturePath(QStringLiteral("EXTN.TN"));
        requireFixture(path);

        const LegacyInterchangeReader reader;
        const LegacyInterchangeReader::Result result = reader.readFile(path);
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.format, LegacyInterchangeReader::Format::TakeNote);
        QCOMPARE(result.deck.name(), QStringLiteral("Software Library"));
        QCOMPARE(result.deck.fieldCount(), 11);
        QCOMPARE(result.deck.cardCount(), 3);
        QCOMPARE(result.deck.cardAt(0).valueAt(0), QStringLiteral("button") + QStringLiteral("File"));
    }

    void importsPasswordProtectedTakeNoteGoldenFixture()
    {
        const QString path = fixturePath(QStringLiteral("PWTN.TN"));
        requireFixture(path);

        const LegacyInterchangeReader reader;
        const LegacyInterchangeReader::Result missing = reader.readFile(path);
        QVERIFY(!missing.ok());
        QVERIFY(missing.passwordRequired);
        QVERIFY(missing.legacyPasswordProtected);

        const LegacyInterchangeReader::Result wrong = reader.readFile(path, QStringLiteral("Wrong01"));
        QVERIFY(!wrong.ok());
        QVERIFY(wrong.passwordRejected);

        const LegacyInterchangeReader::Result result = reader.readFile(path, QStringLiteral("Legacy01"));
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.format, LegacyInterchangeReader::Format::TakeNote);
        QVERIFY(result.legacyPasswordProtected);
        QVERIFY(result.legacyPasswordVerified);
        QCOMPARE(result.deck.name(), QStringLiteral("Software Library"));
        QCOMPARE(result.deck.fieldCount(), 11);
        QCOMPARE(result.deck.cardCount(), 3);
        QCOMPARE(result.deck.cardAt(0).valueAt(0), QStringLiteral("button") + QStringLiteral("File"));
    }

    void rejectsMalformedInterchangeFiles()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const LegacyInterchangeReader reader;

        const LegacyInterchangeReader::Result unsupported = reader.readFile(directory.filePath(QStringLiteral("broken.xyz")));
        QVERIFY(!unsupported.ok());
        QVERIFY(unsupported.errorMessage.contains(QStringLiteral("Unsupported legacy interchange extension")));

        const LegacyInterchangeReader::Result dbf = reader.readFile(writeFixtureFile(&directory, QStringLiteral("broken.dbf"), QByteArray("tiny")));
        QVERIFY(!dbf.ok());
        QCOMPARE(dbf.format, LegacyInterchangeReader::Format::DBase);
        QVERIFY(dbf.errorMessage.contains(QStringLiteral("too small")));

        const LegacyInterchangeReader::Result crd = reader.readFile(writeFixtureFile(&directory, QStringLiteral("broken.crd"), QByteArray("BAD")));
        QVERIFY(!crd.ok());
        QCOMPARE(crd.format, LegacyInterchangeReader::Format::MicrosoftCardfile);
        QVERIFY(crd.errorMessage.contains(QStringLiteral("MGC")));

        const LegacyInterchangeReader::Result wp = reader.readFile(writeFixtureFile(&directory, QStringLiteral("broken.wp"), QByteArray("NOTWPC")));
        QVERIFY(!wp.ok());
        QCOMPARE(wp.format, LegacyInterchangeReader::Format::WordPerfectMerge);
        QVERIFY(wp.errorMessage.contains(QStringLiteral("WPC")));

        const LegacyInterchangeReader::Result tn = reader.readFile(writeFixtureFile(&directory, QStringLiteral("broken.tn"), QByteArray("not a btrieve deck")));
        QVERIFY(!tn.ok());
        QCOMPARE(tn.format, LegacyInterchangeReader::Format::TakeNote);
        QVERIFY(tn.errorMessage.contains(QStringLiteral("Could not import legacy TakeNote-compatible file")));
    }
};

#ifdef CARDSTACK_TEST_SUITE
int runLegacyInterchangeReaderTests(int argc, char** argv)
{
    LegacyInterchangeReaderTests test;
    return QTest::qExec(&test, argc, argv);
}
#else
QTEST_GUILESS_MAIN(LegacyInterchangeReaderTests)
#endif

#include "LegacyInterchangeReaderTests.moc"

