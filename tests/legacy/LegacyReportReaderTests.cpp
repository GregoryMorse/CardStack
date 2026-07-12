#include "LegacyReportReader.h"

#include <QByteArray>
#include <QFileInfo>
#include <QTest>

#include "DeckTemplate.h"

using namespace CardStack;

namespace {

constexpr int HeaderSize = 0x3f5;
constexpr int OldHeaderSize = 0x3c9;
constexpr int FrameSize = 0x9b;

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

void putFrame(QByteArray* bytes, int offset, quint16 sourceId, const QRect& bounds, const QByteArray& text)
{
    putU16(bytes, offset, 0xabcd);
    putU16(bytes, offset + 0x02, sourceId);
    putU16(bytes, offset + 0x08, static_cast<quint16>(bounds.left()));
    putU16(bytes, offset + 0x0a, static_cast<quint16>(bounds.top()));
    putU16(bytes, offset + 0x0c, static_cast<quint16>(bounds.right() + 1));
    putU16(bytes, offset + 0x0e, static_cast<quint16>(bounds.bottom() + 1));
    putCString(bytes, offset + 0x10, 0x7a, text);
}

QByteArray makeSyntheticReportStore()
{
    constexpr int frameCount = 2;
    constexpr int entrySize = HeaderSize + frameCount * FrameSize;
    QByteArray bytes(entrySize + 32, '\0');
    putCString(&bytes, 0, 8, "RPT@#$B");
    putU16(&bytes, 0x08, entrySize);
    putCString(&bytes, 0x0a, 64, "Synthetic Report");
    putU16(&bytes, 0x4b, 2000);
    putU16(&bytes, 0x4d, 1000);
    putU16(&bytes, 0x53, frameCount);
    putU16(&bytes, 0x55, 1);
    putU16(&bytes, 0x57, 1);
    putU16(&bytes, 0x59, 4);
    putCString(&bytes, 0x6c, 32, QByteArray(1, '\x12') + QByteArrayLiteral("Arial"));
    putCString(&bytes, 0x9e, 32, QByteArray(1, '\x12') + QByteArrayLiteral("Courier"));

    putFrame(&bytes, HeaderSize, 0, QRect(20, 30, 300, 40), "Product: [Product]");
    putFrame(&bytes, HeaderSize + FrameSize, 0xffff, QRect(20, 90, 300, 40), "{reportname} - {page}");
    return bytes;
}

QByteArray makeOldFormatLineReportStore()
{
    constexpr int frameCount = 1;
    constexpr int entrySize = OldHeaderSize + frameCount * FrameSize;
    QByteArray bytes(entrySize, '\0');
    putCString(&bytes, 0, 8, "RPT@#$A");
    putU16(&bytes, 0x08, entrySize);
    putCString(&bytes, 0x0a, 64, "Old Line Report");
    putU16(&bytes, 0x4b, 2000);
    putU16(&bytes, 0x4d, 1000);
    putU16(&bytes, 0x53, frameCount);
    putU16(&bytes, 0x55, 1);
    putU16(&bytes, 0x57, 1);
    putU16(&bytes, 0x59, 4);

    const int frameOffset = OldHeaderSize;
    putFrame(&bytes, frameOffset, ReportLineStyleDash, QRect(20, 30, 300, 4), "");
    putU16(&bytes, frameOffset + 0x04, 50);
    bytes[frameOffset + 0x8e] = static_cast<char>(ReportLineShapeLegacyAuto);
    return bytes;
}

} // namespace

class LegacyReportReaderTests : public QObject {
    Q_OBJECT

private slots:
    void parsesSyntheticCurrentFormatReportStore()
    {
        const LegacyReportReader reader;
        const LegacyReportReader::Result result = reader.readBytes(makeSyntheticReportStore());
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));

        QCOMPARE(result.reports.size(), 1);
        const ReportDefinition& report = result.reports.first();
        QCOMPARE(report.formatMagic, QStringLiteral("RPT@#$B"));
        QCOMPARE(report.name, QStringLiteral("Synthetic Report"));
        QCOMPARE(report.entrySize, HeaderSize + 2 * FrameSize);
        QCOMPARE(report.headerSize, HeaderSize);
        QCOMPARE(report.declaredFrameCount, 2);
        QCOMPARE(report.formType, ReportFormType::Report);
        QCOMPARE(report.formWidth, 2000);
        QCOMPARE(report.formHeight, 1000);
        QCOMPARE(report.rows, 1);
        QCOMPARE(report.columns, 1);
        QCOMPARE(report.dataFont.faceName, QStringLiteral("Arial"));
        QCOMPARE(report.textFont.faceName, QStringLiteral("Courier"));
        QCOMPARE(report.frames.size(), 2);
        QCOMPARE(report.frames.at(0).signature, 0xabcd);
        QCOMPARE(report.frames.at(0).kind, ReportFrameKind::Data);
        QCOMPARE(report.frames.at(0).bounds, QRect(200, 300, 3000, 400));
        QCOMPARE(report.frames.at(0).fieldPlaceholders, QVector<QString>{QStringLiteral("Product")});
        QCOMPARE(report.frames.at(1).kind, ReportFrameKind::SystemText);
        QCOMPARE(report.frames.at(1).systemTokens, (QVector<QString>{QStringLiteral("reportname"), QStringLiteral("page")}));
    }

    void importsSoftwareReportSampleWhenConfigured()
    {
        const QString samplePath = qEnvironmentVariable("CARDSTACK_LEGACY_REPORT_SAMPLE");
        if (samplePath.isEmpty() || !QFileInfo::exists(samplePath)) {
            QSKIP("Set CARDSTACK_LEGACY_REPORT_SAMPLE to SOFTWARE.RPT to run the real report-store check.");
        }

        const LegacyReportReader reader;
        const LegacyReportReader::Result result = reader.readFile(samplePath);
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));

        QCOMPARE(result.reports.size(), 3);
        const Deck expected = createDeckFromTemplateName(QStringLiteral("Software Library"));
        QCOMPARE(expected.reportCount(), result.reports.size());
        for (int reportIndex = 0; reportIndex < result.reports.size(); ++reportIndex) {
            const ReportDefinition& actual = result.reports.at(reportIndex);
            const ReportDefinition& oracle = expected.reportAt(reportIndex);
            QCOMPARE(actual.name, oracle.name);
            QCOMPARE(actual.formType, oracle.formType);
            QCOMPARE(actual.formWidth, oracle.formWidth);
            QCOMPARE(actual.formHeight, oracle.formHeight);
            QCOMPARE(actual.frames.size(), oracle.frames.size());
            QCOMPARE(actual.legacyHeader.size(), actual.headerSize);
            for (int frameIndex = 0; frameIndex < actual.frames.size(); ++frameIndex) {
                QCOMPARE(actual.frames.at(frameIndex).text, oracle.frames.at(frameIndex).text);
                QCOMPARE(actual.frames.at(frameIndex).bounds, oracle.frames.at(frameIndex).bounds);
                QCOMPARE(actual.frames.at(frameIndex).kind, oracle.frames.at(frameIndex).kind);
                QCOMPARE(actual.frames.at(frameIndex).legacyDescriptor.size(), 0x9b);
            }
        }
        QCOMPARE(result.reports.at(0).legacyOffset, 0);
        QCOMPARE(result.reports.at(0).entrySize, 0x0a9e);
        QCOMPARE(result.reports.at(0).name, QStringLiteral("Index Card (3 x 5 - laser)"));
        QCOMPARE(result.reports.at(0).formType, ReportFormType::Card);
        QCOMPARE(result.reports.at(0).formWidth, 5000);
        QCOMPARE(result.reports.at(0).formHeight, 3000);
        QCOMPARE(result.reports.at(0).declaredFrameCount, 11);
        QCOMPARE(result.reports.at(0).frames.at(0).text, QStringLiteral("[Product]"));
        QCOMPARE(result.reports.at(0).frames.at(0).fieldPlaceholders, QVector<QString>{QStringLiteral("Product")});
        QCOMPARE(result.reports.at(0).frames.at(10).fieldPlaceholders, QVector<QString>{QStringLiteral("Notes")});

        QCOMPARE(result.reports.at(1).legacyOffset, 0x0a9e);
        QCOMPARE(result.reports.at(1).name, QStringLiteral("Index Card (3 x 5 - pin)"));
        QCOMPARE(result.reports.at(1).declaredFrameCount, 11);

        QCOMPARE(result.reports.at(2).legacyOffset, 0x153c);
        QCOMPARE(result.reports.at(2).entrySize, 0x0c6f);
        QCOMPARE(result.reports.at(2).name, QStringLiteral("Software Reg") + QStringLiteral("istrations"));
        QCOMPARE(result.reports.at(2).formType, ReportFormType::Report);
        QCOMPARE(result.reports.at(2).formWidth, 8500);
        QCOMPARE(result.reports.at(2).formHeight, 11000);
        QCOMPARE(result.reports.at(2).declaredFrameCount, 14);
        QCOMPARE(result.reports.at(2).frames.at(12).systemTokens, QVector<QString>{QStringLiteral("reportname")});
        QCOMPARE(result.reports.at(2).frames.at(13).systemTokens, QVector<QString>{QStringLiteral("page")});
    }

    void normalizesOldFormatLineFrameFillWords()
    {
        const LegacyReportReader reader;
        const LegacyReportReader::Result result = reader.readBytes(makeOldFormatLineReportStore());
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));

        QCOMPARE(result.reports.size(), 1);
        const ReportFrameDefinition& frame = result.reports.first().frames.first();
        QCOMPARE(frame.kind, ReportFrameKind::LineOrBox);
        QCOMPARE(frame.lineBoxShape, ReportLineShapeLegacyAuto);
        QCOMPARE(frame.lineStyle, ReportLineStyleDash);
        QCOMPARE(frame.order, 50);
        QCOMPARE(frame.fillPattern, ReportFillPatternClear);
        QCOMPARE(frame.bounds, QRect(200, 300, 3000, 40));
    }
};

#ifdef CARDSTACK_TEST_SUITE
int runLegacyReportReaderTests(int argc, char** argv)
{
    LegacyReportReaderTests test;
    return QTest::qExec(&test, argc, argv);
}
#else
QTEST_GUILESS_MAIN(LegacyReportReaderTests)
#endif

#include "LegacyReportReaderTests.moc"

