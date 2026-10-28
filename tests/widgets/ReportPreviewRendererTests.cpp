#include "ReportPreviewRenderer.h"
#include "FrameDrawingStyle.h"
#include "ReportTextLayout.h"
#include <QPrinter>
#include <QPrinterInfo>
#include <QFile>

#include <QAbstractButton>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPdfWriter>
#include <QTest>
#include <QTemporaryDir>
#include <QWheelEvent>

#include "DeckTemplate.h"
#include "BuiltInReportTemplates.h"
#include "LegacyReportReader.h"
#include "UiIds.h"
#include "UiBuilder.h"
#include "ReportPrintEngine.h"
#include "ReportPreviewDialog.h"

using namespace CardStack;

namespace {

constexpr int ManualPreviewImageWidth = 960;
constexpr int ManualPreviewImageHeight = 720;

ReportDefinition makeReport()
{
    ReportDefinition report;
    report.name = QStringLiteral("Preview");
    report.formWidth = 1000;
    report.formHeight = 1000;
    report.textFont.faceName = QStringLiteral("Arial");

    ReportFrameDefinition frame;
    frame.bounds = QRect(100, 100, 600, 160);
    frame.text = QStringLiteral("Project: [Project] / {page}");
    frame.fieldPlaceholders = {QStringLiteral("Project")};
    frame.systemTokens = {QStringLiteral("page")};
    frame.kind = ReportFrameKind::Data;
    report.frames.append(frame);

    ReportFrameDefinition box;
    box.bounds = QRect(80, 80, 640, 220);
    box.kind = ReportFrameKind::LineOrBox;
    report.frames.append(box);
    return report;
}

ReportDefinition makeStyleMatrixReport()
{
    ReportDefinition report;
    report.name = QStringLiteral("Style Matrix");
    report.formWidth = 12000;
    report.formHeight = 9000;
    report.textFont.faceName = QStringLiteral("Arial");

    for (int lineStyle = 0; lineStyle < ReportLineStyleCount; ++lineStyle) {
        ReportFrameDefinition frame;
        frame.kind = ReportFrameKind::LineOrBox;
        frame.lineBoxShape = ReportLineShapeBox;
        frame.lineStyle = lineStyle;
        frame.fillPattern = ReportFillPatternClear;
        frame.bounds = QRect(200 + lineStyle * 850, 200, 650, 420);
        report.frames.append(frame);
    }

    for (int fillPattern = 0; fillPattern < ReportFillPatternCount; ++fillPattern) {
        ReportFrameDefinition frame;
        frame.kind = ReportFrameKind::LineOrBox;
        frame.lineBoxShape = ReportLineShapeBox;
        frame.lineStyle = ReportLineStyleSolid;
        frame.fillPattern = fillPattern;
        frame.bounds = QRect(200 + (fillPattern % 13) * 850, 900 + (fillPattern / 13) * 620, 650, 420);
        report.frames.append(frame);
    }

    ReportFrameDefinition horizontal;
    horizontal.kind = ReportFrameKind::LineOrBox;
    horizontal.lineBoxShape = ReportLineShapeHorizontal;
    horizontal.lineStyle = ReportLineStyleDashDot;
    horizontal.bounds = QRect(200, 2300, 2500, 120);
    report.frames.append(horizontal);

    ReportFrameDefinition vertical;
    vertical.kind = ReportFrameKind::LineOrBox;
    vertical.lineBoxShape = ReportLineShapeVertical;
    vertical.lineStyle = ReportLineStyleDashDotDot;
    vertical.bounds = QRect(3000, 2200, 120, 1100);
    report.frames.append(vertical);

    ReportFrameDefinition legacyAutoLine;
    legacyAutoLine.kind = ReportFrameKind::LineOrBox;
    legacyAutoLine.lineBoxShape = ReportLineShapeLegacyAuto;
    legacyAutoLine.lineStyle = ReportLineStyleThickDash;
    legacyAutoLine.bounds = QRect(3400, 2300, 2500, 120);
    report.frames.append(legacyAutoLine);

    return report;
}

ReportDefinition makeCardGridReport()
{
    ReportDefinition report = makeReport();
    report.formType = ReportFormType::Card;
    report.rows = 2;
    report.columns = 2;
    return report;
}

ReportDefinition makeBandedReport()
{
    ReportDefinition report = makeReport();
    report.formType = ReportFormType::Report;
    report.frames.clear();

    ReportFrameDefinition header;
    header.kind = ReportFrameKind::SystemText;
    header.band = 1;
    header.bounds = QRect(50, 20, 900, 80);
    header.text = QStringLiteral("Page {page}");
    header.systemTokens = {QStringLiteral("page")};
    report.frames.append(header);

    ReportFrameDefinition body;
    body.kind = ReportFrameKind::Data;
    body.band = 0;
    body.bounds = QRect(50, 120, 900, 140);
    body.text = QStringLiteral("[Project]");
    body.fieldPlaceholders = {QStringLiteral("Project")};
    report.frames.append(body);

    ReportFrameDefinition footer;
    footer.kind = ReportFrameKind::Text;
    footer.band = 2;
    footer.bounds = QRect(50, 900, 900, 60);
    footer.text = QStringLiteral("Footer");
    report.frames.append(footer);
    return report;
}

bool hasInk(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y) != Qt::white) {
                return true;
            }
        }
    }
    return false;
}

QString safeFileStem(QString text)
{
    text = text.trimmed();
    QString result;
    result.reserve(text.size());
    for (const QChar character : text) {
        result.append(character.isLetterOrNumber() ? character.toLower() : QLatin1Char('_'));
    }
    while (result.contains(QStringLiteral("__"))) {
        result.replace(QStringLiteral("__"), QStringLiteral("_"));
    }
    while (result.startsWith(QLatin1Char('_'))) {
        result.remove(0, 1);
    }
    while (result.endsWith(QLatin1Char('_'))) {
        result.chop(1);
    }
    return result.isEmpty() ? QStringLiteral("preview") : result;
}

ReportPreviewData previewDataForTemplate(const DeckTemplate& deckTemplate, int sampleIndex)
{
    ReportPreviewData data;
    for (const FieldDefinition& field : deckTemplate.fields) {
        const QString key = field.name().toLower();
        QString value = QStringLiteral("Sample");
        if (key.contains(QLatin1String("name"))) value = key.contains(QLatin1String("last")) ? QStringLiteral("Morgan") : QStringLiteral("Alex");
        else if (key.contains(QLatin1String("title")) || key == QLatin1String("product")) value = QStringLiteral("Field Notes");
        else if (key.contains(QLatin1String("author")) || key.contains(QLatin1String("artist"))) value = QStringLiteral("A. Morgan");
        else if (key.contains(QLatin1String("company")) || key.contains(QLatin1String("publisher"))) value = QStringLiteral("North Press");
        else if (key.contains(QLatin1String("initial"))) value = QStringLiteral("A.");
        else if (key.contains(QLatin1String("year"))) value = QStringLiteral("2026");
        else if (key.contains(QLatin1String("pages"))) value = QStringLiteral("184");
        else if (key.contains(QLatin1String("phone")) || key.contains(QLatin1String("fax"))) value = QStringLiteral("555-0123");
        else if (key.contains(QLatin1String("state"))) value = QStringLiteral("CA");
        else if (key.contains(QLatin1String("zip"))) value = QStringLiteral("94000");
        else if (key.contains(QLatin1String("city"))) value = QStringLiteral("Oakland");
        else if (key.contains(QLatin1String("address"))) value = QStringLiteral("42 Maple St");
        else if (field.isNotes()) value = QStringLiteral("A short note.");
        data.fieldValues.insert(field.name(), value.left(std::max(1, field.maxLength())));
    }
    data.systemValues.insert(QStringLiteral("page"), QString::number(sampleIndex));
    return data;
}

bool writePreviewImage(const ReportDefinition& report, const ReportPreviewData& data, const QString& filePath)
{
    QImage image(ManualPreviewImageWidth, ManualPreviewImageHeight, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);

    const int recordCount =
        report.formType == ReportFormType::Card || report.formType == ReportFormType::Label
        ? std::max(1, report.rows * report.columns)
        : 1;
    QVector<ReportPreviewData> records;
    records.reserve(recordCount);
    for (int index = 0; index < recordCount; ++index) {
        ReportPreviewData record = data;
        for (auto iterator = record.fieldValues.begin(); iterator != record.fieldValues.end(); ++iterator) {
            if (iterator.value().isEmpty()) iterator.value() = QString::number(index + 1);
        }
        records.append(record);
    }
    const QVector<ReportPrintPage> pages = ReportPrintEngine::paginate(report, records);

    QPainter painter(&image);
    const QSizeF paper = ReportPrintEngine::pageSizeMils(report);
    const qreal scale = std::min((image.width() - 40.0) / paper.width(), (image.height() - 40.0) / paper.height());
    const QRectF page((image.width() - paper.width() * scale) / 2.0,
                      (image.height() - paper.height() * scale) / 2.0,
                      paper.width() * scale, paper.height() * scale);
    if (!pages.isEmpty()) {
        ReportPrintEngine::renderPage(
            &painter,
            report,
            records,
            pages.first(),
            page);
    } else {
        ReportPreviewRenderer::render(
            &painter,
            report,
            QRectF(20, 20, image.width() - 40, image.height() - 40),
            data);
    }
    painter.end();

    return hasInk(image) && image.save(filePath);
}

} // namespace

class ReportPreviewRendererTests : public QObject {
    Q_OBJECT

private slots:

    void fillPatternsHaveCorrectDensityDirectionAndWeight()
    {
        int previous = -1;
        for (int pattern = ReportFillPattern5Percent; pattern <= ReportFillPattern90Percent; ++pattern) {
            const QImage image = FrameDrawingStyle::fillBrush(pattern).textureImage();
            QVERIFY(!image.isNull());
            int filled = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    filled += image.pixelColor(x, y).alpha() > 0 ? 1 : 0;
            QVERIFY(filled > previous);
            previous = filled;
        }
        const QImage horizontal = FrameDrawingStyle::fillBrush(ReportFillPatternLightHorizontal).textureImage();
        const QImage vertical = FrameDrawingStyle::fillBrush(ReportFillPatternLightVertical).textureImage();
        QVERIFY(horizontal.pixelColor(4, 0).alpha() > 0);
        QCOMPARE(horizontal.pixelColor(0, 4).alpha(), 0);
        QVERIFY(vertical.pixelColor(0, 4).alpha() > 0);
        QCOMPARE(vertical.pixelColor(4, 0).alpha(), 0);
        QVERIFY(FrameDrawingStyle::fillBrush(ReportFillPatternDarkHorizontal).textureImage()
            != FrameDrawingStyle::fillBrush(ReportFillPatternLightHorizontal).textureImage());
    }

    void builtInReportsUseRealFontsPaperAndDerivedBands()
    {
        bool bibliographySeen = false;
        for (const auto& deckTemplate : builtInDeckTemplates()) {
            for (const auto& report : deckTemplate.reports) {
                QVERIFY(report.dataFont.legacyHeight < 0);
                QVERIFY(report.textFont.legacyHeight < 0);
                QVERIFY(report.pageWidth > 0);
                QVERIFY(report.pageHeight > 0);
                const auto pages = ReportPrintEngine::paginate(report, 1);
                QVERIFY(!pages.isEmpty());
                QImage rendered(480, 640, QImage::Format_ARGB32_Premultiplied);
                rendered.fill(Qt::white);
                QPainter painter(&rendered);
                ReportPreviewData record = previewDataForTemplate(deckTemplate, 1);
                ReportPrintEngine::renderPage(&painter, report, {record}, pages.first(), QRectF(0, 0, 480, 640));
                painter.end();
                QVERIFY2(hasInk(rendered), qPrintable(report.name));
                if (deckTemplate.legacyResourceId == 105 && report.legacyOffset == 0) {
                    QCOMPARE(report.marginTop, 62);
                    QCOMPARE(report.marginLeft, 500);
                    QCOMPARE(report.marginRight, 500);
                    QCOMPARE(report.marginBottom, 0);
                    QCOMPARE(report.pageHeight - report.marginTop - report.marginBottom, report.formHeight);
                }
                for (const auto& frame : report.frames) QVERIFY(frame.band <= ReportFooterBand);
                if (report.name == QLatin1String("Bibliography")) {
                    bibliographySeen = true;
                    QCOMPARE(report.frames[0].band, ReportBodyBand);
                    QCOMPARE(report.frames[1].band, ReportBodyBand);
                    QCOMPARE(report.frames[2].band, ReportBodyBand);
                    QCOMPARE(report.frames[5].band, ReportHeaderBand);
                    QCOMPARE(report.frames.last().band, ReportFooterBand);
                    QCOMPARE(report.dataFont.legacyHeight, -98);
                    QCOMPARE(ReportPrintEngine::pageSizeMils(report), QSizeF(8500, 11000));
                }
            }
        }
        QVERIFY(bibliographySeen);
    }

    void continuationFieldsAreCompleteAndExhaustedFieldsStayBlank()
    {
        ReportDefinition report;
        report.formType = ReportFormType::Report;
        report.formWidth = 8500;
        report.formHeight = 11000;
        report.dataFont = {QStringLiteral("Arial"), -100};
        for (int index = 0; index < 2; ++index) {
            ReportFrameDefinition frame;
            frame.kind = ReportFrameKind::Data;
            frame.bounds = QRect(500 + index * 2500, 500, 1800, 400);
            frame.text = index == 0 ? QStringLiteral("[Short]") : QStringLiteral("[Long]");
            frame.fieldPlaceholders = {index == 0 ? QStringLiteral("Short") : QStringLiteral("Long")};
            frame.printEntireContentsFlag = 1;
            report.frames.append(frame);
        }
        ReportPreviewData values;
        values.fieldValues.insert(QStringLiteral("Short"), QStringLiteral("short field"));
        values.fieldValues.insert(QStringLiteral("Long"), QStringLiteral("Long paragraph with Unicode text. ").repeated(80));
        const auto pages = ReportPrintEngine::paginate(report, QVector<ReportPreviewData>{values});
        QString shortOutput, longOutput;
        int continuationCount = 0;
        for (const auto& page : pages) {
            for (const auto& cell : page.cells) {
                ++continuationCount;
                QVERIFY(cell.fieldValueOverrides.contains(QStringLiteral("Short")));
                QVERIFY(cell.fieldValueOverrides.contains(QStringLiteral("Long")));
                shortOutput += cell.fieldValueOverrides.value(QStringLiteral("Short"));
                longOutput += cell.fieldValueOverrides.value(QStringLiteral("Long"));
            }
        }
        QVERIFY(continuationCount > 1);
        QCOMPARE(shortOutput, values.fieldValues.value(QStringLiteral("Short")));
        QCOMPARE(longOutput, values.fieldValues.value(QStringLiteral("Long")));
    }

    void paperLayoutResetsPortraitAndHonorsPhysicalDimensions()
    {
        ReportDefinition report;
        report.formWidth = 8500;
        report.formHeight = 14000;
        report.pageWidth = 8500;
        report.pageHeight = 14000;
        const auto portrait = ReportPrintEngine::pageLayout(report);
        QCOMPARE(portrait.orientation(), QPageLayout::Portrait);
        const QSizeF dimensions = portrait.fullRect(QPageLayout::Inch).size();
        QVERIFY(qAbs(dimensions.width() - 8.5) < 0.01);
        QVERIFY(qAbs(dimensions.height() - 14.0) < 0.01);
        report.orientation = 1;
        const auto landscape = ReportPrintEngine::pageLayout(report);
        QCOMPARE(landscape.orientation(), QPageLayout::Landscape);
        report.orientation = 0;
        QCOMPARE(ReportPrintEngine::pageLayout(report).orientation(), QPageLayout::Portrait);
    }

    void fontSizingUsesTenthsOfPointsNotDevicePixelCaps()
    {
        const QFont font = ReportTextLayout::font({QStringLiteral("Arial"), -100}, ReportStyleFlagBold);
        QCOMPARE(font.pixelSize(), 139);
        QVERIFY(font.bold());
        ReportDefinition report;
        report.formWidth = 8500;
        report.formHeight = 11000;
        report.textFont = {QStringLiteral("Arial"), -100};
        ReportFrameDefinition frame;
        frame.kind = ReportFrameKind::Text;
        frame.bounds = QRect(500, 500, 3000, 500);
        frame.text = QStringLiteral("Physical font size");
        report.frames.append(frame);
        const auto inkBounds = [&](int scale) {
            QImage image(816 * scale, 1056 * scale, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image);
            ReportPreviewRenderer::render(&painter, report, image.rect());
            painter.end();
            QRect bounds;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    if (image.pixelColor(x, y).lightness() < 200)
                        bounds = bounds.isNull() ? QRect(x, y, 1, 1) : bounds.united(QRect(x, y, 1, 1));
            return bounds;
        };
        const QRect one = inkBounds(1), two = inkBounds(2);
        QVERIFY(!one.isEmpty());
        QVERIFY(qAbs(two.width() - one.width() * 2) <= 4);
        QVERIFY(qAbs(two.height() - one.height() * 2) <= 4);
    }

    void nativePrintToPdfWhenExplicitlyEnabledAndAvailable()
    {
        if (!qEnvironmentVariableIsSet("CARDSTACK_TEST_NATIVE_PDF"))
            QSKIP("Set CARDSTACK_TEST_NATIVE_PDF for the optional native PDF-printer integration test.");
        QString printerName;
        for (const auto& info : QPrinterInfo::availablePrinters()) {
            if (info.printerName().contains(QStringLiteral("Print to PDF"), Qt::CaseInsensitive)) {
                printerName = info.printerName();
                break;
            }
        }
        if (printerName.isEmpty()) QSKIP("A native Print to PDF printer is not available.");
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("native-print.pdf"));
        QPrinter printer(QPrinter::HighResolution);
        printer.setPrinterName(printerName);
        printer.setOutputFileName(path);
        printer.setOutputFormat(QPrinter::NativeFormat);
        ReportDefinition report = makeReport();
        report.formType = ReportFormType::Report;
        report.formWidth = report.pageWidth = 8500;
        report.formHeight = report.pageHeight = 11000;
        report.frames.resize(1);
        report.frames[0].bounds = QRect(500, 500, 7500, 500);
        printer.setPageLayout(ReportPrintEngine::pageLayout(report));
        printer.setFullPage(true);
        ReportPreviewData data;
        data.fieldValues.insert(QStringLiteral("Project"), QStringLiteral("Native PDF integration"));
        const QVector<ReportPreviewData> records{data};
        const auto pages = ReportPrintEngine::paginate(report, records);
        QPainter painter;
        QVERIFY(painter.begin(&printer));
        ReportPrintEngine::renderPage(&painter, report, records, pages.first(), painter.viewport());
        QVERIFY(painter.end());
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(path).size() > 0, 10000);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.read(5) == QByteArray("%PDF-"));
    }
    void resolvesAbbreviatedNamesAndOmitsMissingOptionalFields()
    {
        ReportFrameDefinition frame;
        frame.text = QStringLiteral("Service: [Customer servic]; optional: [Title]; {MISSING}");
        ReportPreviewData data;
        data.fieldValues.insert(QStringLiteral("Customer service"), QStringLiteral("555-0100"));
        data.fieldValues.insert(QStringLiteral("CUSTOMER SERVICE"), QStringLiteral("555-0100"));
        QCOMPARE(ReportPreviewRenderer::resolveFrameText(frame, data), QStringLiteral("Service: 555-0100; optional: ; "));
        data.fieldValues.insert(QStringLiteral("Customer services"), QStringLiteral("Other"));
        QCOMPARE(ReportPreviewRenderer::resolveFrameText(frame, data), QStringLiteral("Service: ; optional: ; "));
        for (const auto& deckTemplate : builtInDeckTemplates()) {
            auto sample = previewDataForTemplate(deckTemplate, 1);
            for (const auto& report : deckTemplate.reports) {
                sample.systemValues.insert(QStringLiteral("reportname"), report.name);
                for (const auto& item : report.frames) {
                    const QString text = ReportPreviewRenderer::resolveFrameText(item, sample);
                    QVERIFY2(!text.contains(QLatin1Char('[')) && !text.contains(QLatin1Char('{')),
                             qPrintable(deckTemplate.name + QStringLiteral(" / ") + report.name + QStringLiteral(": ") + text));
                }
            }
        }
    }

    void resolvesFieldAndSystemPlaceholders()
    {
        const ReportDefinition report = makeReport();
        ReportPreviewData data;
        data.fieldValues.insert(QStringLiteral("Project"), QStringLiteral("ProjectKit"));
        data.systemValues.insert(QStringLiteral("page"), QStringLiteral("1"));

        QCOMPARE(
            ReportPreviewRenderer::resolveFrameText(report.frames.first(), data),
            QStringLiteral("Project: ProjectKit / 1"));
    }

    void rendersReportIntoImage()
    {
        QImage image(320, 240, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);

        QPainter painter(&image);
        ReportPreviewData data;
        data.fieldValues.insert(QStringLiteral("Project"), QStringLiteral("ProjectKit"));
        data.systemValues.insert(QStringLiteral("page"), QStringLiteral("1"));
        ReportPreviewRenderer::render(&painter, makeReport(), QRectF(0, 0, image.width(), image.height()), data);
        painter.end();

        QVERIFY(hasInk(image));
    }

    void rendersConfiguredLegacyReportSampleWhenConfigured()
    {
        const QString samplePath = qEnvironmentVariable("CARDSTACK_LEGACY_REPORT_SAMPLE");
        if (samplePath.isEmpty() || !QFileInfo::exists(samplePath)) {
            QSKIP("Set CARDSTACK_LEGACY_REPORT_SAMPLE to a WineVDM golden .RPT to run the real preview-rendering check.");
        }

        const LegacyReportReader reader;
        const LegacyReportReader::Result result = reader.readFile(samplePath);
        QVERIFY2(result.ok(), qPrintable(result.errorMessage));
        QVERIFY(!result.reports.isEmpty());

        ReportPreviewData data;
        data.fieldValues.insert(QStringLiteral("Product"), QStringLiteral("CardStack"));
        data.fieldValues.insert(QStringLiteral("Version"), QStringLiteral("1.0"));
        data.fieldValues.insert(QStringLiteral("Company"), QStringLiteral("Open Source"));
        data.fieldValues.insert(QStringLiteral("Notes"), QStringLiteral("Golden report preview smoke test"));
        data.systemValues.insert(QStringLiteral("page"), QStringLiteral("1"));

        for (const ReportDefinition& report : result.reports) {
            QImage image(640, 820, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            data.systemValues.insert(QStringLiteral("reportname"), report.name);

            QPainter painter(&image);
            ReportPreviewRenderer::render(&painter, report, QRectF(20, 20, 600, 780), data);
            painter.end();

            QVERIFY2(hasInk(image), qPrintable(QStringLiteral("Legacy report rendered blank: %1").arg(report.name)));
        }
    }

    void paginatesCardAndLabelGrids()
    {
        const QVector<ReportPrintPage> pages = ReportPrintEngine::paginate(makeCardGridReport(), 5);
        QCOMPARE(pages.size(), 2);
        QCOMPARE(pages.at(0).cells.size(), 4);
        QCOMPARE(pages.at(1).cells.size(), 1);
        QCOMPARE(pages.at(0).cells.at(0).target, QRectF(0.0, 0.0, 0.5, 0.5));
        QCOMPARE(pages.at(0).cells.at(3).target, QRectF(0.5, 0.5, 0.5, 0.5));
    }

    void paginatesLegacyAcrossThenDownLabelGrids()
    {
        ReportDefinition report = makeReport();
        report.formType = ReportFormType::Label;
        report.rows = 2;
        report.columns = 10;

        const QVector<ReportPrintPage> pages = ReportPrintEngine::paginate(report, 20);
        QCOMPARE(pages.size(), 1);
        QCOMPARE(pages.first().cells.size(), 20);
        QCOMPARE(pages.first().cells.at(0).target, QRectF(0.0, 0.0, 0.5, 0.1));
        QCOMPARE(pages.first().cells.at(1).target, QRectF(0.5, 0.0, 0.5, 0.1));
        QCOMPARE(pages.first().cells.at(2).target, QRectF(0.0, 0.1, 0.5, 0.1));
        QCOMPARE(pages.first().cells.at(19).target, QRectF(0.5, 0.9, 0.5, 0.1));
    }

    void paginatesReportBodyRowsBetweenRepeatedBands()
    {
        const QVector<ReportPrintPage> pages = ReportPrintEngine::paginate(makeBandedReport(), 8);
        QCOMPARE(pages.size(), 2);
        QCOMPARE(pages.first().cells.size(), 5);
        QCOMPARE(pages.last().cells.size(), 3);
        QVERIFY(pages.first().usesReportBands);
        QCOMPARE(pages.first().cells.at(0).logicalYOffset, 0);
        QCOMPARE(pages.first().cells.at(1).logicalYOffset, 140);
    }

    void continuesPrintEntireContentsAcrossBodySlots()
    {
        ReportDefinition report = makeBandedReport();
        report.dataFont.faceName = QStringLiteral("Arial");
        report.dataFont.legacyHeight = -20;
        report.frames[1].bounds.setHeight(24);
        report.frames[1].bounds.setWidth(120);
        report.frames[1].printEntireContentsFlag = 1;
        report.frames[2].bounds.moveTop(170);

        ReportPreviewData data;
        data.fieldValues.insert(
            QStringLiteral("Project"),
            QStringLiteral("one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen"));
        const QVector<ReportPrintPage> pages = ReportPrintEngine::paginate(report, QVector<ReportPreviewData>{data});
        QVERIFY(pages.size() > 1);
        QVERIFY(!pages.first().cells.first().fieldValueOverrides.value(QStringLiteral("Project")).isEmpty());
        QVERIFY(!pages.last().cells.last().fieldValueOverrides.value(QStringLiteral("Project")).isEmpty());
        QVERIFY(pages.first().cells.first().fieldValueOverrides.value(QStringLiteral("Project"))
            != pages.last().cells.last().fieldValueOverrides.value(QStringLiteral("Project")));
    }

    void renderPageClipsToTargetAndDrawsCells()
    {
        QImage image(360, 260, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(255, 0, 255));

        QVector<ReportPreviewData> records;
        for (int index = 0; index < 4; ++index) {
            ReportPreviewData data;
            data.fieldValues.insert(QStringLiteral("Project"), QStringLiteral("Project %1").arg(index + 1));
            records.append(data);
        }

        const ReportDefinition report = makeCardGridReport();
        const QVector<ReportPrintPage> pages = ReportPrintEngine::paginate(report, records.size());
        QPainter painter(&image);
        ReportPrintEngine::renderPage(&painter, report, records, pages.first(), QRectF(40, 30, 280, 200));
        painter.end();

        QCOMPARE(image.pixelColor(5, 5), QColor(255, 0, 255));
        QVERIFY(image.pixelColor(50, 40) != QColor(255, 0, 255));
        QVERIFY(hasInk(image.copy(40, 30, 280, 200)));
    }

    void rendersPrintPageToPdfFile()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString filePath = directory.filePath(QStringLiteral("report-output.pdf"));
        QPdfWriter writer(filePath);
        writer.setPageSize(QPageSize(QPageSize::Letter));
        writer.setResolution(96);

        QVector<ReportPreviewData> records;
        ReportPreviewData data;
        data.fieldValues.insert(QStringLiteral("Project"), QStringLiteral("PDF Smoke Test"));
        data.systemValues.insert(QStringLiteral("page"), QStringLiteral("1"));
        records.append(data);

        const ReportDefinition report = makeReport();
        const QVector<ReportPrintPage> pages = ReportPrintEngine::paginate(report, records.size());
        QCOMPARE(pages.size(), 1);

        QPainter painter(&writer);
        ReportPrintEngine::renderPage(&painter, report, records, pages.first(), QRectF(0, 0, 816, 1056));
        painter.end();

        const QFileInfo output(filePath);
        QVERIFY(output.exists());
        QVERIFY(output.size() > 0);
    }

    void rendersEveryLineAndFillStyle()
    {
        const ReportDefinition report = makeStyleMatrixReport();

        QImage image(960, 720, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);

        QPainter painter(&image);
        ReportPreviewRenderer::render(&painter, report, QRectF(0, 0, image.width(), image.height()), {});
        painter.end();

        QVERIFY(hasInk(image));
    }

    void writesManualInspectionPreviewImagesWhenConfigured()
    {
        const QString outputPath = qEnvironmentVariable("CARDSTACK_REPORT_PREVIEW_IMAGE_DIR");
        if (outputPath.isEmpty()) {
            QSKIP("Set CARDSTACK_REPORT_PREVIEW_IMAGE_DIR to write report-preview PNGs for manual inspection.");
        }

        QDir outputDirectory(outputPath);
        QVERIFY2(outputDirectory.exists() || outputDirectory.mkpath(QStringLiteral(".")), qPrintable(outputPath));

        int written = 0;
        ReportPreviewData styleData;
        styleData.systemValues.insert(QStringLiteral("page"), QStringLiteral("1"));
        QVERIFY(writePreviewImage(
            makeStyleMatrixReport(),
            styleData,
            outputDirectory.filePath(QStringLiteral("%1_style_matrix.png").arg(written, 2, 10, QLatin1Char('0')))));
        ++written;

        for (const DeckTemplate& deckTemplate : builtInDeckTemplates()) {
            ReportPreviewData data = previewDataForTemplate(deckTemplate, written + 1);
            for (const ReportDefinition& report : deckTemplate.reports) {
                data.systemValues.insert(QStringLiteral("reportname"), report.name);
                const QString fileName = QStringLiteral("%1_%2_%3.png")
                                             .arg(written, 2, 10, QLatin1Char('0'))
                                             .arg(safeFileStem(deckTemplate.name), safeFileStem(report.name));
                QVERIFY2(
                    writePreviewImage(report, data, outputDirectory.filePath(fileName)),
                    qPrintable(fileName));
                if ((deckTemplate.legacyResourceId == 101 && (report.legacyOffset == 0 || report.legacyOffset == 2253))
                    || (deckTemplate.legacyResourceId == 105 && report.legacyOffset == 0)) {
                    UiBuilder::DialogContext context;
                    context.reportNames = {report.name};
                    auto preview = ReportPreviewDialog::create(nullptr, context, report, data);
                    QVERIFY(preview != nullptr);
                    preview->show();
                    QVERIFY(QTest::qWaitForWindowExposed(preview.get()));
                    QCoreApplication::processEvents();
                    const QString previewFile = QStringLiteral("preview_%1_%2.png")
                        .arg(safeFileStem(deckTemplate.name), safeFileStem(report.name));
                    QVERIFY(preview->grab().save(outputDirectory.filePath(previewFile)));
                    preview->close();
                }
                ++written;
            }
        }

        const Deck genericDeck = createDeckFromTemplate(builtInDeckTemplates().first());
        const ReportPreviewData genericData = previewDataForTemplate(builtInDeckTemplates().first(), 1);
        for (const auto& report : standardReportDefinitionsForDeck(genericDeck, QStringLiteral("Default Page Report"), QStringLiteral("Default Row Report"))) {
            const QString fileName = QStringLiteral("%1_%2.png").arg(written, 2, 10, QLatin1Char('0')).arg(safeFileStem(report.name));
            auto data = genericData;
            data.systemValues.insert(QStringLiteral("reportname"), report.name);
            QVERIFY2(writePreviewImage(report, data, outputDirectory.filePath(fileName)), qPrintable(fileName));
            ++written;
        }
        QCOMPARE(written, 48);
        qInfo("Wrote %d manual report preview images to %s", written, qPrintable(outputDirectory.absolutePath()));
    }

    void rendersMultiPageGridReportToPdfFile()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString filePath = directory.filePath(QStringLiteral("multi-page-report-output.pdf"));
        QPdfWriter writer(filePath);
        writer.setPageSize(QPageSize(QPageSize::Letter));
        writer.setResolution(96);

        QVector<ReportPreviewData> records;
        for (int index = 0; index < 5; ++index) {
            ReportPreviewData data;
            data.fieldValues.insert(QStringLiteral("Project"), QStringLiteral("PDF Grid %1").arg(index + 1));
            records.append(data);
        }

        const ReportDefinition report = makeCardGridReport();
        const QVector<ReportPrintPage> pages = ReportPrintEngine::paginate(report, records.size());
        QCOMPARE(pages.size(), 2);

        QPainter painter(&writer);
        for (int pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
            if (pageIndex > 0) {
                QVERIFY(writer.newPage());
            }
            ReportPrintEngine::renderPage(&painter, report, records, pages.at(pageIndex), QRectF(0, 0, 816, 1056));
        }
        painter.end();

        const QFileInfo output(filePath);
        QVERIFY(output.exists());
        QVERIFY(output.size() > 0);
    }

    void createsUiPreviewDialogWithCanvas()
    {
        const ReportDefinition report = makeReport();
        UiBuilder::DialogContext context;
        context.reportNames = {report.name};

        std::unique_ptr<QDialog> dialog = ReportPreviewDialog::create(nullptr, context, report);
        QVERIFY(dialog != nullptr);
        QVERIFY(dialog->findChild<QWidget*>(QStringLiteral("reportPreviewCanvas")) != nullptr);

        const auto* title = qobject_cast<QLabel*>(
            UiBuilder::controlById(dialog.get(), UiIds::Control::PreviewTitle));
        const auto* pageStatus = qobject_cast<QLabel*>(
            UiBuilder::controlById(dialog.get(), UiIds::Control::PreviewPageStatus));
        const auto* canvas = dialog->findChild<QWidget*>(QStringLiteral("reportPreviewCanvas"));
        const auto* firstPage = qobject_cast<QAbstractButton*>(
            UiBuilder::controlById(dialog.get(), UiIds::Control::PreviewFirstPage));
        const auto* nextPage = qobject_cast<QAbstractButton*>(
            UiBuilder::controlById(dialog.get(), UiIds::Control::PreviewNextPage));
        QVERIFY(title != nullptr);
        QVERIFY(pageStatus != nullptr);
        QVERIFY(canvas != nullptr);
        QVERIFY(firstPage != nullptr);
        QVERIFY(nextPage != nullptr);
        QCOMPARE(title->text(), report.name);
        QVERIFY(!pageStatus->text().trimmed().isEmpty());
        QVERIFY(title->geometry().bottom() < canvas->geometry().top());
        QVERIFY(pageStatus->geometry().bottom() < canvas->geometry().top());
        QVERIFY(firstPage->geometry().left() > canvas->geometry().right());
        QVERIFY(nextPage->geometry().left() > canvas->geometry().right());
        dialog->show();
        QCoreApplication::processEvents();
        const QSize before = canvas->size();
        dialog->resize(dialog->width() + 120, dialog->height() + 80);
        QCoreApplication::processEvents();
        QVERIFY(canvas->width() > before.width());
        QVERIFY(canvas->height() > before.height());
        QVERIFY(title->geometry().bottom() < canvas->geometry().top());
        QVERIFY(!canvas->geometry().intersects(nextPage->geometry()));
    }

    void createsUiPreviewDialogWithPageNavigation()
    {
        const ReportDefinition report = makeReport();
        UiBuilder::DialogContext context;
        context.reportNames = {report.name};

        QVector<ReportPreviewData> records;
        records.resize(2);
        const QVector<ReportPrintPage> pages = ReportPrintEngine::paginate(report, records.size());
        std::unique_ptr<QDialog> dialog = ReportPreviewDialog::create(nullptr, context, report, records, pages);
        QVERIFY(dialog != nullptr);

        auto* nextPage = qobject_cast<QAbstractButton*>(
            UiBuilder::controlById(dialog.get(), UiIds::Control::PreviewNextPage));
        QVERIFY(nextPage != nullptr);
        QVERIFY(nextPage->isEnabled());

        auto* firstPage = qobject_cast<QAbstractButton*>(
            UiBuilder::controlById(dialog.get(), UiIds::Control::PreviewFirstPage));
        QVERIFY(firstPage != nullptr);
        QVERIFY(!firstPage->isEnabled());

        nextPage->click();
        QVERIFY(firstPage->isEnabled());
        QVERIFY(!nextPage->isEnabled());
    }

    void previewRespondsToPageKeysAndMouseWheel()
    {
        const ReportDefinition report = makeReport();
        UiBuilder::DialogContext context;
        context.reportNames = {report.name};
        QVector<ReportPreviewData> records;
        records.resize(3);
        const QVector<ReportPrintPage> pages = ReportPrintEngine::paginate(report, records.size());
        QVERIFY(pages.size() >= 2);

        std::unique_ptr<QDialog> dialog = ReportPreviewDialog::create(nullptr, context, report, records, pages);
        QVERIFY(dialog != nullptr);
        dialog->show();
        QCoreApplication::processEvents();
        QWidget* canvas = dialog->findChild<QWidget*>(QStringLiteral("reportPreviewCanvas"));
        auto* status = qobject_cast<QLabel*>(UiBuilder::controlById(dialog.get(), UiIds::Control::PreviewPageStatus));
        QVERIFY(canvas != nullptr);
        QVERIFY(status != nullptr);
        canvas->setFocus();

        QTest::keyClick(canvas, Qt::Key_PageDown);
        QCOMPARE(status->text(), QStringLiteral("Page 2 of %1").arg(pages.size()));

        const QPoint localPosition(10, 10);
        QWheelEvent wheelUp(
            QPointF(localPosition),
            QPointF(canvas->mapToGlobal(localPosition)),
            QPoint(),
            QPoint(0, 120),
            Qt::NoButton,
            Qt::NoModifier,
            Qt::NoScrollPhase,
            false);
        QCoreApplication::sendEvent(canvas, &wheelUp);
        QCOMPARE(status->text(), QStringLiteral("Page 1 of %1").arg(pages.size()));
        QTest::keyClick(canvas, Qt::Key_PageUp);
        QCOMPARE(status->text(), QStringLiteral("Page 1 of %1").arg(pages.size()));
    }

    void previewPrintIsAffirmativeAndCancelIsNot()
    {
        const ReportDefinition report = makeReport();
        UiBuilder::DialogContext context;
        context.reportNames = {report.name};

        std::unique_ptr<QDialog> printDialog = ReportPreviewDialog::create(nullptr, context, report);
        QVERIFY(printDialog != nullptr);
        auto* printButton = qobject_cast<QAbstractButton*>(
            UiBuilder::controlById(printDialog.get(), UiIds::Control::Ok));
        QVERIFY(printButton != nullptr);
        printButton->click();
        QCOMPARE(printDialog->result(), static_cast<int>(QDialog::Accepted));

        std::unique_ptr<QDialog> cancelDialog = ReportPreviewDialog::create(nullptr, context, report);
        QVERIFY(cancelDialog != nullptr);
        auto* cancelButton = qobject_cast<QAbstractButton*>(
            UiBuilder::controlById(cancelDialog.get(), UiIds::Control::Cancel));
        QVERIFY(cancelButton != nullptr);
        cancelButton->click();
        QCOMPARE(cancelDialog->result(), static_cast<int>(QDialog::Rejected));
    }
};

#ifdef CARDSTACK_TEST_SUITE
int runReportPreviewRendererTests(int argc, char** argv)
{
    ReportPreviewRendererTests test;
    return QTest::qExec(&test, argc, argv);
}
#else
QTEST_MAIN(ReportPreviewRendererTests)
#endif

#include "ReportPreviewRendererTests.moc"

