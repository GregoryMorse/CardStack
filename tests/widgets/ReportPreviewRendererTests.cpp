#include "ReportPreviewRenderer.h"

#include <QAbstractButton>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPdfWriter>
#include <QTest>
#include <QTemporaryDir>

#include "LegacyReportReader.h"
#include "UiIds.h"
#include "UiBuilder.h"
#include "ReportPrintEngine.h"
#include "ReportPreviewDialog.h"

using namespace CardStack;

namespace {

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

ReportDefinition makeCardGridReport()
{
    ReportDefinition report = makeReport();
    report.formType = ReportFormType::Card;
    report.rows = 2;
    report.columns = 2;
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
        QVERIFY(title != nullptr);
        QCOMPARE(title->text(), report.name);
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

