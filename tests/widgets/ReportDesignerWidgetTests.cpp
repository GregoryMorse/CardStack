#include "ReportDesignerWidget.h"

#include "../support/ModalDialogDriver.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QPixmap>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTest>

using namespace CardStack;

namespace {

ReportDefinition makeReport()
{
    ReportDefinition report;
    report.name = QStringLiteral("Editable Report");
    report.formWidth = 5000;
    report.formHeight = 3000;
    report.formType = ReportFormType::Report;
    return report;
}

bool saveWidgetImage(QWidget& widget, const QDir& outputDirectory, const QString& fileName)
{
    widget.resize(1080, 760);
    widget.show();
    if (!QTest::qWaitForWindowExposed(&widget)) {
        return false;
    }
    QCoreApplication::processEvents();
    QTest::qWait(30);
    const QPixmap pixmap = widget.grab();
    return !pixmap.isNull() && pixmap.save(outputDirectory.filePath(fileName));
}

} // namespace

class ReportDesignerWidgetTests : public QObject {
    Q_OBJECT

private slots:
    void preservesExactImportedFrameBounds()
    {
        ReportDefinition importedReport = makeReport();
        importedReport.frames = {
            [] {
                ReportFrameDefinition frame;
                frame.kind = ReportFrameKind::Text;
                frame.bounds = QRect(101, 207, 933, 177);
                frame.text = QStringLiteral("Heading");
                return frame;
            }(),
            [] {
                ReportFrameDefinition frame;
                frame.kind = ReportFrameKind::Data;
                frame.bounds = QRect(44, 622, 1803, 333);
                frame.fieldPlaceholders = {QStringLiteral("Product")};
                return frame;
            }(),
            [] {
                ReportFrameDefinition frame;
                frame.kind = ReportFrameKind::LineOrBox;
                frame.bounds = QRect(300, 2400, 4100, 55);
                frame.lineBoxShape = ReportLineShapeHorizontal;
                return frame;
            }(),
        };

        ReportDesignerWidget designer(importedReport, {QStringLiteral("Product")});

        QCOMPARE(designer.report().formWidth, importedReport.formWidth);
        QCOMPARE(designer.report().formHeight, importedReport.formHeight);
        QCOMPARE(designer.report().frames.size(), importedReport.frames.size());
        for (int index = 0; index < importedReport.frames.size(); ++index) {
            QCOMPARE(designer.report().frames.at(index).bounds, importedReport.frames.at(index).bounds);
            QCOMPARE(designer.report().frames.at(index).kind, importedReport.frames.at(index).kind);
        }
    }

    void addsSelectsAndSavesFrames()
    {
        ReportDesignerWidget designer(makeReport(), {QStringLiteral("Product")});
        QSignalSpy saveSpy(&designer, &ReportDesignerWidget::saveRequested);

        designer.addDataFrame();
        QVERIFY(designer.isDirty());
        QCOMPARE(designer.report().frames.size(), 1);
        QCOMPARE(designer.selectedFrameIndex(), 0);
        QCOMPARE(designer.report().frames.first().kind, ReportFrameKind::Data);
        QCOMPARE(designer.report().frames.first().fieldPlaceholders, QVector<QString>({QStringLiteral("Product")}));

        designer.addLineBoxFrame();
        QCOMPARE(designer.report().frames.size(), 2);
        QCOMPARE(designer.selectedFrameIndex(), 1);

        designer.deleteSelectedFrame();
        QCOMPARE(designer.report().frames.size(), 1);
        QCOMPARE(designer.selectedFrameIndex(), 0);

        designer.save();
        QCOMPARE(saveSpy.size(), 1);
        QVERIFY(!designer.isDirty());
    }

    void appliesReportMargins()
    {
        ReportDesignerWidget designer(makeReport(), {QStringLiteral("Product")});

        designer.applyForm(
            ReportFormType::Report,
            8500,
            11000,
            1,
            1,
            500,
            500,
            500,
            500);

        const ReportDefinition report = designer.report();
        QCOMPARE(report.formType, ReportFormType::Report);
        QCOMPARE(report.marginLeft, 500);
        QCOMPARE(report.marginTop, 500);
        QCOMPARE(report.marginRight, 500);
        QCOMPARE(report.marginBottom, 500);
        QVERIFY(designer.isDirty());
    }

    void exposesAndStoresEveryReportLineAndFillStyle()
    {
        ReportDesignerWidget designer(makeReport(), {QStringLiteral("Product")});

        designer.addLineBoxFrameShape(
            ReportLineBoxShape::Box,
            ReportLineStyleNoOutline + 10,
            ReportFillPatternLightTrellis + 10,
            3);
        QCOMPARE(designer.report().frames.last().lineStyle, ReportLineStyleNoOutline);
        QCOMPARE(designer.report().frames.last().fillPattern, ReportFillPatternLightTrellis);
        QCOMPARE(designer.report().frames.last().lineBoxShape, ReportLineShapeBox);

        designer.addLineBoxFrameShape(ReportLineBoxShape::HorizontalLine, ReportLineStyleDash, ReportFillPatternClear, 0);
        QCOMPARE(designer.report().frames.last().lineBoxShape, ReportLineShapeHorizontal);

        designer.addLineBoxFrameShape(ReportLineBoxShape::VerticalLine, ReportLineStyleDot, ReportFillPatternSolid, 0);
        QCOMPARE(designer.report().frames.last().lineBoxShape, ReportLineShapeVertical);

        for (int lineStyle = 0; lineStyle < ReportLineStyleCount; ++lineStyle) {
            designer.addLineBoxFrameShape(ReportLineBoxShape::Box, lineStyle, ReportFillPatternClear, 0);
            QCOMPARE(designer.report().frames.last().lineStyle, lineStyle);
        }

        for (int fillPattern = 0; fillPattern < ReportFillPatternCount; ++fillPattern) {
            designer.addLineBoxFrameShape(ReportLineBoxShape::Box, ReportLineStyleSolid, fillPattern, 0);
            QCOMPARE(designer.report().frames.last().fillPattern, fillPattern);
        }
    }

    void closePromptCancelKeepsDesignerOpen()
    {
        ReportDesignerWidget designer(makeReport(), {QStringLiteral("Product")});
        designer.show();
        QVERIFY(QTest::qWaitForWindowExposed(&designer));

        designer.addTextFrameWithText(QStringLiteral("Unsaved"), 0);
        QVERIFY(designer.isDirty());

        Tests::chooseNextMessageBoxButton(QMessageBox::Cancel);
        QVERIFY(!designer.close());
        QVERIFY(designer.isVisible());
        QVERIFY(designer.isDirty());
    }

    void closePromptDiscardClosesWithoutSaving()
    {
        ReportDesignerWidget designer(makeReport(), {QStringLiteral("Product")});
        designer.show();
        QVERIFY(QTest::qWaitForWindowExposed(&designer));
        QSignalSpy saveSpy(&designer, &ReportDesignerWidget::saveRequested);

        designer.addTextFrameWithText(QStringLiteral("Discarded"), 0);
        QVERIFY(designer.isDirty());

        Tests::chooseNextMessageBoxButton(QMessageBox::Discard);
        QVERIFY(designer.close());
        QVERIFY(!designer.isVisible());
        QCOMPARE(saveSpy.size(), 0);
    }

    void closePromptSaveEmitsSaveAndCloses()
    {
        ReportDesignerWidget designer(makeReport(), {QStringLiteral("Product")});
        designer.show();
        QVERIFY(QTest::qWaitForWindowExposed(&designer));
        QSignalSpy saveSpy(&designer, &ReportDesignerWidget::saveRequested);

        designer.addTextFrameWithText(QStringLiteral("Saved"), 0);
        QVERIFY(designer.isDirty());

        Tests::chooseNextMessageBoxButton(QMessageBox::Save);
        QVERIFY(designer.close());
        QVERIFY(!designer.isVisible());
        QCOMPARE(saveSpy.size(), 1);
        QVERIFY(!designer.isDirty());
    }

    void writesManualDesignerInspectionImagesWhenConfigured()
    {
        const QString outputPath = qEnvironmentVariable("CARDSTACK_DESIGNER_GALLERY_DIR");
        if (outputPath.isEmpty()) {
            QSKIP("Set CARDSTACK_DESIGNER_GALLERY_DIR to write report designer PNGs for manual inspection.");
        }

        QDir outputDirectory(outputPath);
        QVERIFY2(outputDirectory.exists() || outputDirectory.mkpath(QStringLiteral(".")), qPrintable(outputPath));

        ReportDefinition report = makeReport();
        report.formWidth = 8500;
        report.formHeight = 11000;
        report.frames = {
            [] {
                ReportFrameDefinition frame;
                frame.kind = ReportFrameKind::Text;
                frame.bounds = QRect(500, 500, 2600, 320);
                frame.text = QStringLiteral("Report heading");
                frame.styleFlags = ReportStyleFlagBold;
                return frame;
            }(),
            [] {
                ReportFrameDefinition frame;
                frame.kind = ReportFrameKind::Data;
                frame.bounds = QRect(500, 1100, 3200, 360);
                frame.fieldPlaceholders = {QStringLiteral("Product")};
                return frame;
            }(),
            [] {
                ReportFrameDefinition frame;
                frame.kind = ReportFrameKind::SystemText;
                frame.bounds = QRect(500, 1700, 2400, 320);
                frame.text = QStringLiteral("Page Number");
                return frame;
            }(),
            [] {
                ReportFrameDefinition frame;
                frame.kind = ReportFrameKind::LineOrBox;
                frame.bounds = QRect(500, 2300, 3800, 900);
                frame.lineBoxShape = ReportLineShapeBox;
                frame.lineStyle = ReportLineStyleDash;
                frame.fillPattern = ReportFillPatternLightTrellis;
                frame.cornerRadius = 4;
                return frame;
            }(),
        };

        ReportDesignerWidget designer(report, {QStringLiteral("Product"), QStringLiteral("Version"), QStringLiteral("Notes")});
        QVERIFY(saveWidgetImage(designer, outputDirectory, QStringLiteral("report_designer_all_elements.png")));

        QTableWidget* frameTable = designer.findChild<QTableWidget*>();
        QVERIFY(frameTable != nullptr);
        const QVector<QString> stateNames = {
            QStringLiteral("text_selected"),
            QStringLiteral("data_selected"),
            QStringLiteral("system_selected"),
            QStringLiteral("line_box_selected"),
        };
        for (int row = 0; row < stateNames.size(); ++row) {
            frameTable->setCurrentCell(row, 0);
            frameTable->selectRow(row);
            QCoreApplication::processEvents();
            QTest::qWait(30);
            QVERIFY(saveWidgetImage(
                designer,
                outputDirectory,
                QStringLiteral("report_designer_%1.png").arg(stateNames.at(row))));
        }
    }
};

#ifdef CARDSTACK_TEST_SUITE
int runReportDesignerWidgetTests(int argc, char** argv)
{
    ReportDesignerWidgetTests test;
    return QTest::qExec(&test, argc, argv);
}
#else
QTEST_MAIN(ReportDesignerWidgetTests)
#endif

#include "ReportDesignerWidgetTests.moc"

