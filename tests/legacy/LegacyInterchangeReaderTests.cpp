#include "LegacyInterchangeReader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QFont>

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

    void decodesDBaseCodePagesAndPreservesMemoWhitespace()
    {
        constexpr int HeaderBytes = 32;
        constexpr int DescriptorBytes = 32;
        constexpr int CharacterWidth = 8;
        constexpr int MemoReferenceWidth = 10;
        constexpr int HeaderLength = HeaderBytes + 2 * DescriptorBytes + 1;
        constexpr int RowLength = 1 + CharacterWidth + MemoReferenceWidth;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        for (const quint8 driver : {quint8(0), quint8(1), quint8(3), quint8(0x57)}) {
            const char accent = driver == 0 || driver == 1 ? char(0x82) : char(0xe9);
            QByteArray bytes(HeaderLength + RowLength, '\0');
            bytes[0] = char(0x83);
            bytes[4] = 1;
            bytes[8] = char(HeaderLength);
            bytes[10] = char(RowLength);
            bytes[29] = char(driver);
            bytes.replace(HeaderBytes, 4, QByteArray("CAF") + accent);
            bytes[HeaderBytes + 11] = 'C';
            bytes[HeaderBytes + 16] = CharacterWidth;
            bytes.replace(HeaderBytes + DescriptorBytes, 5, "NOTES");
            bytes[HeaderBytes + DescriptorBytes + 11] = 'M';
            bytes[HeaderBytes + DescriptorBytes + 16] = MemoReferenceWidth;
            bytes[HeaderLength - 1] = '\r';
            bytes[HeaderLength] = ' ';
            const QByteArray text = QByteArray("  Caf") + accent + "  ";
            bytes.replace(HeaderLength + 1, CharacterWidth, text);
            bytes.replace(HeaderLength + 1 + CharacterWidth, MemoReferenceWidth, "         1");
            const QString stem = QStringLiteral("codepage_%1").arg(driver);
            const QString path = writeFixtureFile(&directory, stem + QStringLiteral(".DBF"), bytes);
            QByteArray memo(512, '\0');
            memo += QByteArray("\r\n  Caf") + accent + " \r\n  ";
            memo += QByteArray(2, char(0x1a));
            QVERIFY(!writeFixtureFile(&directory, stem + QStringLiteral(".DBT"), memo).isEmpty());
            const auto result = LegacyInterchangeReader{}.readFile(path);
            QVERIFY2(result.ok(), qPrintable(result.errorMessage));
            QCOMPARE(result.deck.fieldAt(0).name(), QStringLiteral("CAF") + QChar(0x00e9));
            QCOMPARE(result.deck.cardAt(0).valueAt(0), QStringLiteral("  Caf") + QChar(0x00e9));
            QCOMPARE(result.deck.cardAt(0).valueAt(1), QStringLiteral("\r\n  Caf") + QChar(0x00e9) + QStringLiteral(" \r\n  "));
            bytes[29] = char(0x7f);
            const auto unknown = LegacyInterchangeReader{}.readFile(writeFixtureFile(&directory, stem + QStringLiteral("_unknown.DBF"), bytes));
            QVERIFY(!unknown.ok());
            QVERIFY(unknown.errorMessage.contains(QStringLiteral("language driver")));
        }
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
        const auto& layout = result.deck.cardTemplateLayout();
        const auto& control = result.deck.legacyControlRecord();
        QVERIFY(control.size() >= 0x842);
        QCOMPARE(control.mid(5, 6), QByteArrayLiteral("WINDEX"));
        const int textCount = quint8(control.at(0x5c)) | (quint16(quint8(control.at(0x5d))) << 8);
        QCOMPARE(layout.frames.size(), result.deck.fieldCount() + textCount);
        QVERIFY(layout.canvasWidth > 0 && layout.canvasHeight > 0);
        for (int field = 0; field < result.deck.fieldCount(); ++field) {
            QCOMPARE(result.deck.fieldAt(field).legacyDescriptor().size(), 0x39);
            QVERIFY(layout.frames.at(textCount + field).bounds.width() > 0);
            QVERIFY(layout.frames.at(textCount + field).bounds.height() > 0);
        }
        QFont data;
        QVERIFY(data.fromString(result.deck.appearance().dataFont));
        QCOMPARE(data.family(), QStringLiteral("Arial"));
        QCOMPARE(result.deck.appearance().textFont, result.deck.appearance().dataFont);
        QCOMPARE(result.deck.appearance().indexFont, result.deck.appearance().nameFont);
        QCOMPARE(result.deck.name(), QStringLiteral("Software Library"));
        QCOMPARE(result.deck.fieldCount(), 11);
        QCOMPARE(result.deck.cardCount(), 3);
        QCOMPARE(result.deck.cardAt(0).valueAt(0), QStringLiteral("button") + QStringLiteral("File"));
    }

    void importsNonSoftwareTakeNoteLabelsAndControlRoles()
    {
        const QString path = fixturePath(QStringLiteral("mailing_layout.TN"));
        requireFixture(path);
        const auto result = LegacyInterchangeReader{}.readFile(path);
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QCOMPARE(result.deck.name(), QStringLiteral("Mailing List"));
        QCOMPARE(result.deck.fieldCount(), 10); QCOMPARE(result.deck.cardCount(), 1);
        const auto& layout = result.deck.cardTemplateLayout();
        QCOMPARE(layout.frames.size(), 12);
        QCOMPARE(layout.frames[0].kind, CardTemplateFrameKind::Text);
        QCOMPARE(layout.frames[0].text, QStringLiteral("Name"));
        QCOMPARE(layout.frames[1].text, QStringLiteral("Address"));
        QCOMPARE(layout.frames[0].legacyDescriptor.size(), 0x26);
        QVERIFY(!result.deck.fieldAt(0).showName());
        QVERIFY(result.deck.fieldAt(4).showName());
        QVERIFY(result.deck.fieldAt(7).isPhone());
        QCOMPARE(result.deck.cardAt(0).valueAt(0), QStringLiteral("Alex"));
        QCOMPARE(result.deck.cardAt(0).valueAt(1), QStringLiteral("Morgan"));
        QCOMPARE(result.deck.cardAt(0).valueAt(2), QStringLiteral("42 Maple St"));
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

