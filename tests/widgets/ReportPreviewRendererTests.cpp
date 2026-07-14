#include "ReportPreviewRenderer.h"

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
#include "LegacyReportReader.h"
#include "UiIds.h"
#include "UiBuilder.h"
#include "ReportPrintEngine.h"
#include "ReportPreviewDialog.h"

using namespace CardStack;

namespace {

constexpr int ManualPreviewImageLimit = 40;
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
        data.fieldValues.insert(
            field.name(),
            QStringLiteral("%1 sample %2").arg(field.name()).arg(sampleIndex));
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
            iterator.value() = QStringLiteral("%1 %2").arg(iterator.value()).arg(index + 1);
        }
        records.append(record);
    }
    const QVector<ReportPrintPage> pages = ReportPrintEngine::paginate(report, records.size());

    QPainter painter(&image);
    if (!pages.isEmpty()) {
        ReportPrintEngine::renderPage(
            &painter,
            report,
            records,
            pages.first(),
            QRectF(20, 20, image.width() - 40, image.height() - 40));
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
                if (written >= ManualPreviewImageLimit) {
                    qInfo("Wrote %d manual report preview images to %s", written, qPrintable(outputDirectory.absolutePath()));
                    return;
                }

                data.systemValues.insert(QStringLiteral("reportname"), report.name);
                const QString fileName = QStringLiteral("%1_%2_%3.png")
                                             .arg(written, 2, 10, QLatin1Char('0'))
                                             .arg(safeFileStem(deckTemplate.name), safeFileStem(report.name));
                QVERIFY2(
                    writePreviewImage(report, data, outputDirectory.filePath(fileName)),
                    qPrintable(fileName));
                ++written;
            }
        }

        QVERIFY(written > 1);
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

