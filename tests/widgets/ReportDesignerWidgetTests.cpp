#include "ReportDesignerWidget.h"

#include "../support/ModalDialogDriver.h"
#include "BuiltInReportTemplates.h"
#include "Deck.h"
#include "UiIds.h"
#include "../support/DesignerResizeProbe.h"
#include "../support/DesignerGridProbe.h"
#include "../support/DesignerViewportProbe.h"
#include "../support/DesignerSelectionProbe.h"
#include "DesignerClipboard.h"
#include "SQLitePackageStore.h"
#include <QTemporaryDir>
#include "ReportDesignerWidget.h"
#include "TemplateDesignerWidget.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
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

bool labelWithTextVisible(const QWidget& widget, const QString& text)
{
    for (const QLabel* label : widget.findChildren<QLabel*>()) {
        if (label->text() == text) {
            return !label->isHidden();
        }
    }
    return false;
}

} // namespace

class ReportDesignerWidgetTests : public QObject {
    Q_OBJECT

private slots:
    void sourceGridMatchesOriginalIntegerAlgorithm() { CardStack::Tests::DesignerGridProbe::sourceAlgorithm(); }
    void fontFallbackAndSignedHeightPreserveLayoutMetrics() { CardStack::Tests::DesignerGridProbe::reportFonts(); }
    void keyboardGridDoesNotAccumulateRoundingError() { CardStack::Tests::DesignerGridProbe::reportKeyboard(); }
    void pointerGridIsIndependentOfViewportZoom() { CardStack::Tests::DesignerGridProbe::reportPointer(); }
    void clipboardUsesOriginalNormalizedOriginWithoutStagger() { CardStack::Tests::DesignerGridProbe::reportClipboard(); }
    void reportViewportPreservesOriginalNavigationWithoutEditing()
    {
        CardStack::Tests::DesignerViewportProbe::navigation();
    }

    void reportViewportResizesMarginedScrolledFrames_data()
    {
        CardStack::Tests::DesignerResizeProbe::addData();
    }

    void reportViewportResizesMarginedScrolledFrames()
    {
        CardStack::Tests::DesignerViewportProbe::scrolledResize();
    }

    void reportRenderMatchesDesignerBounds_data()
    {
        QTest::addColumn<int>("formType");
        QTest::newRow("report") << int(ReportFormType::Report);
        QTest::newRow("card") << int(ReportFormType::Card);
        QTest::newRow("label") << int(ReportFormType::Label);
    }

    void reportRenderMatchesDesignerBounds()
    {
        CardStack::Tests::DesignerViewportProbe::renderedFramesMatchDesignBounds();
    }

    void reportRulerLabelsDoNotOverlap()
    {
        CardStack::Tests::DesignerViewportProbe::rulers();
    }

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

    void undoRestoresReportFramesAndToolbarEdits()
    {
        ReportDesignerWidget designer(makeReport(), {QStringLiteral("Product")});
        QVERIFY(!designer.canUndo());

        designer.addTextFrameWithText(QStringLiteral("Original heading"), ReportStyleFlagBold);
        QVERIFY(designer.canUndo());
        QCOMPARE(designer.report().frames.size(), 1);

        designer.updateSelectedFrameFromToolbar(
            QStringLiteral("Changed heading"),
            ReportStyleFlagItalic,
            false,
            ReportLineShapeBox,
            ReportLineStyleSolid,
            ReportFillPatternClear,
            0);
        QCOMPARE(designer.report().frames.first().text, QStringLiteral("Changed heading"));

        designer.undo();
        QCOMPARE(designer.report().frames.size(), 1);
        QCOMPARE(designer.report().frames.first().text, QStringLiteral("Original heading"));
        QCOMPARE(designer.report().frames.first().styleFlags, quint8(ReportStyleFlagBold));

        designer.undo();
        QVERIFY(designer.report().frames.isEmpty());
        QCOMPARE(designer.selectedFrameIndex(), -1);
        QVERIFY(!designer.canUndo());
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
            500,
            0,
            0,
            72,
            84);

        const ReportDefinition report = designer.report();
        QCOMPARE(report.formType, ReportFormType::Report);
        QCOMPARE(report.marginLeft, 500);
        QCOMPARE(report.marginTop, 500);
        QCOMPARE(report.marginRight, 500);
        QCOMPARE(report.marginBottom, 500);
        QCOMPARE(report.headerHeight, 72);
        QCOMPARE(report.footerHeight, 84);
        QVERIFY(designer.isDirty());
    }

    void reportFontsParticipateInUndo()
    {
        ReportDesignerWidget designer(makeReport(), {QStringLiteral("Product")});
        const ReportFontDefinition original = designer.report().dataFont;
        designer.setReportFont(true, {QStringLiteral("Arial"), -14});
        QCOMPARE(designer.report().dataFont.faceName, QStringLiteral("Arial"));
        QCOMPARE(designer.report().dataFont.legacyHeight, -14);
        QVERIFY(designer.canUndo());

        designer.undo();
        QCOMPARE(designer.report().dataFont.faceName, original.faceName);
        QCOMPARE(designer.report().dataFont.legacyHeight, original.legacyHeight);
    }

    void generatedDefaultReportsInheritTheirIndependentTemplateFonts()
    {
        Deck deck(QStringLiteral("Font defaults"));
        ReportDefinition pageSource;
        pageSource.dataFont = {QStringLiteral("Arial"), 31};
        pageSource.textFont = {QStringLiteral("Courier New"), 29};
        deck.addReport(pageSource);
        ReportDefinition rowSource;
        rowSource.dataFont = {QStringLiteral("Tahoma"), 27};
        rowSource.textFont = {QStringLiteral("Times New Roman"), 25};
        deck.addReport(rowSource);

        const QVector<ReportDefinition> defaults = standardReportDefinitionsForDeck(
            deck,
            QStringLiteral("Default Page Report"),
            QStringLiteral("Default Row Report"));
        QCOMPARE(defaults.size(), 2);
        QCOMPARE(defaults.at(0).dataFont.faceName, QStringLiteral("Arial"));
        QCOMPARE(defaults.at(0).dataFont.legacyHeight, 31);
        QCOMPARE(defaults.at(0).textFont.faceName, QStringLiteral("Courier New"));
        QCOMPARE(defaults.at(1).dataFont.faceName, QStringLiteral("Tahoma"));
        QCOMPARE(defaults.at(1).dataFont.legacyHeight, 27);
        QCOMPARE(defaults.at(1).textFont.faceName, QStringLiteral("Times New Roman"));
    }

    void canvasExposesPageScaledHorizontalAndVerticalRulers()
    {
        ReportDesignerWidget designer(makeReport(), {});
        QWidget* canvas = designer.findChild<QWidget*>(QStringLiteral("reportDesignCanvas"));
        QVERIFY(canvas != nullptr);
        QVERIFY(canvas->property("hasHorizontalRuler").toBool());
        QVERIFY(canvas->property("hasVerticalRuler").toBool());
        QCOMPARE(canvas->property("guideColor").toString(), QStringLiteral("#0057b8"));
        QCOMPARE(canvas->property("selectionColor").toString(), QStringLiteral("#c51f1f"));
        QCOMPARE(canvas->property("guideLineStyle").toString(), QStringLiteral("dotted"));
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

    void propertyRowsFollowSelectedReportFrameKind()
    {
        ReportDesignerWidget designer(makeReport(), {QStringLiteral("Product")});

        designer.addTextFrameWithText(QStringLiteral("Heading"), 0);
        QVERIFY(labelWithTextVisible(designer, QStringLiteral("Text")));
        QVERIFY(labelWithTextVisible(designer, QStringLiteral("Alignment")));
        QVERIFY(!labelWithTextVisible(designer, QStringLiteral("Line shape")));
        QVERIFY(!labelWithTextVisible(designer, QStringLiteral("Line style")));
        QVERIFY(!labelWithTextVisible(designer, QStringLiteral("Fill pattern")));
        QVERIFY(!labelWithTextVisible(designer, QStringLiteral("Corner radius")));

        designer.addLineBoxFrameShape(ReportLineBoxShape::Box, ReportLineStyleSolid, ReportFillPatternClear, 0);
        QVERIFY(!labelWithTextVisible(designer, QStringLiteral("Text")));
        QVERIFY(!labelWithTextVisible(designer, QStringLiteral("Alignment")));
        QVERIFY(labelWithTextVisible(designer, QStringLiteral("Line shape")));
        QVERIFY(labelWithTextVisible(designer, QStringLiteral("Line style")));
        QVERIFY(labelWithTextVisible(designer, QStringLiteral("Fill pattern")));
        QVERIFY(labelWithTextVisible(designer, QStringLiteral("Corner radius")));
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

    void blankLineFrameSaveAndReloadKeepsItsDrawingProperties()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        ReportDefinition report = makeReport();
        report.formatMagic = QStringLiteral("RPT@#$B");
        ReportFrameDefinition line;
        line.kind = ReportFrameKind::LineOrBox;
        line.bounds = QRect(500, 500, 1800, 600);
        line.lineBoxShape = ReportLineShapeBox;
        line.lineStyle = ReportLineStyleDash;
        line.fillPattern = ReportFillPattern25Percent;
        line.cornerRadius = 7;
        QVERIFY(line.text.isNull());
        report.frames = {line};
        const QString path = directory.filePath(QStringLiteral("empty-text.csreport"));
        QString error;
        QVERIFY2(SQLitePackageStore::saveReportPackage({report}, report.name, path, &error), qPrintable(error));
        QVector<ReportDefinition> loaded;
        QString name;
        QVERIFY2(SQLitePackageStore::loadReportPackage(path, &loaded, &name, &error), qPrintable(error));
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded.first().frames.first(), line);
    }

    void reportGroupClipboardWorksAcrossWindowsAndUndo()
    {
        if (QGuiApplication::platformName() != QStringLiteral("offscreen"))
            QSKIP("Clipboard probes require the isolated offscreen clipboard.");
        ReportDefinition report = makeReport();
        for (int index = 0; index < 3; ++index) {
            ReportFrameDefinition frame;
            frame.kind = index == 1 ? ReportFrameKind::LineOrBox : ReportFrameKind::Text;
            frame.text = index == 1 ? QString() : QStringLiteral("Copied frame %1").arg(index);
            frame.bounds = QRect(700 + index * 1000, 600 + index * 500, 700, 320);
            frame.styleFlags = ReportStyleFlagBold | ReportStyleFlagAlignRight;
            frame.lineBoxShape = ReportLineShapeBox;
            frame.lineStyle = ReportLineStyleDash;
            frame.fillPattern = ReportFillPattern25Percent;
            frame.cornerRadius = 12;
            report.frames.append(frame);
        }
        ReportDesignerWidget source(report, {});
        source.show();
        QCoreApplication::processEvents();
        Tests::DesignerSelectionProbe::selectRows(source, {0, 2});
        auto* canvas = source.findChild<QWidget*>(QStringLiteral("reportDesignCanvas"));
        QVERIFY(canvas);
        QApplication::setActiveWindow(&source);
        canvas->setFocus();
        source.copySelectedFrame();
        const auto copied = DesignerClipboard::readReport();
        QCOMPARE(copied.size(), 2);
        const QPoint copyShift = copied.at(0).bounds.topLeft() - source.report().frames.at(0).bounds.topLeft();
        for (int index = 0; index < copied.size(); ++index) {
            auto expected = source.report().frames.at(index == 0 ? 0 : 2);
            expected.bounds.translate(copyShift);
            QCOMPARE(copied.at(index), expected);
        }

        source.cutSelectedFrame();
        QCOMPARE(source.report().frames.size(), 1);
        source.undo();
        QCOMPARE(source.report().frames, report.frames);
        Tests::DesignerSelectionProbe::expectSelection(source, {0, 2});

        ReportDesignerWidget target(makeReport(), {});
        QVERIFY(target.canPasteFrame());
        target.show();
        QApplication::setActiveWindow(&target);
        target.findChild<QWidget*>(QStringLiteral("reportDesignCanvas"))->setFocus();
        target.pasteFrame();
        QCOMPARE(target.report().frames.size(), 2);
        const QPoint shift = target.report().frames.at(0).bounds.topLeft() - copied.at(0).bounds.topLeft();
        for (int index = 0; index < copied.size(); ++index) {
            auto expected = copied.at(index);
            expected.bounds.translate(shift);
            expected.band = reportBandForBounds(target.report(), expected.bounds);
            QCOMPARE(target.report().frames.at(index), expected);
        }
        Tests::DesignerSelectionProbe::expectSelection(target, {0, 1});
        target.undo();
        QVERIFY(target.report().frames.isEmpty());
        QVERIFY(target.selectedFrameIndices().isEmpty());
        TemplateDesignerWidget otherFormat({}, {});
        QVERIFY(!otherFormat.canPasteFrame());
    }

    void reportClipboardCodecPreservesEveryFrameMember()
    {
        if (QGuiApplication::platformName() != QStringLiteral("offscreen"))
            QSKIP("Clipboard probes require the isolated offscreen clipboard.");
        ReportFrameDefinition frame;
        frame.legacyOffset = 71;
        frame.signature = 0xabcd;
        frame.sourceId = 13;
        frame.order = 9;
        frame.band = ReportHeaderBand;
        frame.bounds = QRect(123, 456, 2100, 410);
        frame.text = QStringLiteral("[Name] / {page}");
        frame.fieldPlaceholders = {QStringLiteral("Name")};
        frame.systemTokens = {QStringLiteral("page")};
        frame.kind = ReportFrameKind::Data;
        frame.printEntireContentsFlag = 1;
        frame.validationFlags = 0x5a;
        frame.styleFlags = ReportStyleFlagBold | ReportStyleFlagUnderline | ReportStyleFlagAlignCenter;
        frame.lineBoxShape = ReportLineShapeVertical;
        frame.lineStyle = ReportLineStyleThickDot;
        frame.fillPattern = ReportFillPatternLightGrid;
        frame.cornerRadius = 17;
        frame.legacyDescriptor = QByteArray("descriptor\0tail", 15);
        QVERIFY(DesignerClipboard::writeReport({frame}));
        QCOMPARE(DesignerClipboard::readReport(), QVector<ReportFrameDefinition>{frame});
    }

    void groupFormattingPreservesExpressionsAndUnrelatedStyles()
    {
        ReportDefinition report = makeReport();
        ReportFrameDefinition text;
        text.kind = ReportFrameKind::Text;
        text.text = QStringLiteral("Heading");
        text.bounds = QRect(500, 500, 1200, 320);
        text.styleFlags = ReportStyleFlagBold;
        ReportFrameDefinition data = text;
        data.kind = ReportFrameKind::Data;
        data.text = QStringLiteral("[Name] - [Status]");
        data.fieldPlaceholders = {QStringLiteral("Name"), QStringLiteral("Status")};
        data.styleFlags = ReportStyleFlagItalic | ReportStyleFlagAlignCenter;
        ReportFrameDefinition line = text;
        line.kind = ReportFrameKind::LineOrBox;
        line.text.clear();
        line.styleFlags = 0;
        report.frames = {text, data, line};
        ReportDesignerWidget designer(report, {QStringLiteral("Name"), QStringLiteral("Status")});
        Tests::DesignerSelectionProbe::selectRows(designer, {0, 1, 2});
        const auto before = designer.report().frames;
        QVERIFY(designer.selectionHasStyledFrames());
        QVERIFY(!designer.selectionHasStyleFlag(ReportStyleFlagBold));
        QCOMPARE(designer.selectionAlignment(), -1);
        designer.setSelectionStyleFlag(ReportStyleFlagBold, true);
        QVERIFY(designer.selectionHasStyleFlag(ReportStyleFlagBold));
        QCOMPARE(designer.report().frames.at(1).text, data.text);
        QCOMPARE(designer.report().frames.at(1).fieldPlaceholders, data.fieldPlaceholders);
        QCOMPARE(designer.report().frames.at(2), before.at(2));
        designer.undo();
        QCOMPARE(designer.report().frames, before);
        Tests::DesignerSelectionProbe::expectSelection(designer, {0, 1, 2});
        designer.setSelectionAlignment(ReportStyleFlagAlignRight);
        QCOMPARE(designer.selectionAlignment(), int(ReportStyleFlagAlignRight));
        QVERIFY((designer.report().frames.at(1).styleFlags & ReportStyleFlagItalic) != 0);
        QCOMPARE(designer.report().frames.at(2), before.at(2));
        designer.undo();
        QCOMPARE(designer.report().frames, before);
    }

    void focusedTextClipboardDoesNotCopyOrDeleteReportObjects()
    {
        if (QGuiApplication::platformName() != QStringLiteral("offscreen"))
            QSKIP("Clipboard probes require the isolated offscreen clipboard.");
        ReportDefinition report = makeReport();
        ReportFrameDefinition frame;
        frame.kind = ReportFrameKind::Text;
        frame.text = QStringLiteral("Text editing");
        frame.bounds = QRect(500, 500, 2000, 320);
        report.frames = {frame};
        ReportDesignerWidget designer(report, {});
        designer.show();
        QApplication::setActiveWindow(&designer);
        auto* edit = designer.findChild<QPlainTextEdit*>();
        QVERIFY(edit);
        edit->setFocus();
        edit->selectAll();
        QCoreApplication::processEvents();
        designer.copySelectedFrame();
        QCOMPARE(QApplication::clipboard()->text(), frame.text);
        QVERIFY(!QApplication::clipboard()->mimeData()->hasFormat(QString::fromLatin1(DesignerClipboard::ReportMime)));
        designer.cutSelectedFrame();
        QCOMPARE(designer.report().frames.size(), 1);
        QVERIFY(edit->toPlainText().isEmpty());
        QApplication::clipboard()->setText(QStringLiteral("Replacement text"));
        designer.pasteFrame();
        QCOMPARE(edit->toPlainText(), QStringLiteral("Replacement text"));
        edit->selectAll();
        designer.deleteSelectedFrame();
        QVERIFY(edit->toPlainText().isEmpty());
        QCOMPARE(designer.report().frames.size(), 1);
    }

    void selectionMessagesPreserveOriginalGroupBehavior()
    {
        ReportDefinition report = makeReport();
        for (const QRect& bounds : {QRect(900, 700, 1200, 400), QRect(3000, 2100, 1200, 400), QRect(2200, 1300, 800, 400)}) {
            ReportFrameDefinition frame;
            frame.kind = ReportFrameKind::Text;
            frame.text = QStringLiteral("Object %1").arg(report.frames.size() + 1);
            frame.bounds = bounds;
            report.frames.append(frame);
        }
        ReportDesignerWidget designer(report, {});
        Tests::DesignerSelectionProbe::exercise(designer, "reportDesignCanvas",
            QSize(report.formWidth, report.formHeight), 28, 31,
            QStringLiteral("report"), [&designer]() { return designer.report().frames; });
    }

    void resizesAllEdgesAndCorners_data()
    {
        Tests::DesignerResizeProbe::addData();
    }

    void resizesAllEdgesAndCorners()
    {
        ReportDefinition report = makeReport();
        ReportFrameDefinition frame;
        frame.kind = ReportFrameKind::Text;
        frame.text = QStringLiteral("Resize from any side");
        frame.bounds = QRect(900, 700, 1400, 500);
        report.frames = {frame};
        ReportDesignerWidget designer(report, {});
        Tests::DesignerResizeProbe::exercise(designer, "reportDesignCanvas",
            QSize(report.formWidth, report.formHeight), frame.bounds, 28, QStringLiteral("report"),
            [&designer]() { return designer.report().frames.first().bounds; });
    }

    void canvasMouseMessagesSelectMoveResizeAndActivateFrames()
    {
        ReportDefinition report = makeReport();
        ReportFrameDefinition frame;
        frame.kind = ReportFrameKind::Text;
        frame.text = QStringLiteral("Move me");
        frame.bounds = QRect(900, 700, 1400, 500);
        report.frames = {frame};

        ReportDesignerWidget designer(report, {});
        designer.resize(1000, 700);
        designer.show();
        QCoreApplication::processEvents();
        QWidget* canvas = designer.findChild<QWidget*>(QStringLiteral("reportDesignCanvas"));
        QVERIFY(canvas != nullptr);

        const QRectF page = canvas->property("designerPageRect").toRectF();
        const auto visualBounds = [&page, &report](const QRect& bounds) {
            return QRectF(page.left() + bounds.left() * page.width() / report.formWidth,
                          page.top() + bounds.top() * page.height() / report.formHeight,
                          bounds.width() * page.width() / report.formWidth,
                          bounds.height() * page.height() / report.formHeight);
        };

        const QPoint initialCenter = visualBounds(frame.bounds).center().toPoint();
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->property("designerContentRect").toRect().topLeft() + QPoint(2, 2));
        QCOMPARE(designer.selectedFrameIndex(), -1);
        QTest::mouseClick(canvas, Qt::RightButton, Qt::NoModifier, initialCenter);
        QCOMPARE(designer.selectedFrameIndex(), -1);
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, initialCenter);
        QCOMPARE(designer.selectedFrameIndex(), 0);
        QVERIFY(canvas->hasFocus());

        const QRect beforeMove = designer.report().frames.first().bounds;
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, initialCenter);
        QTest::mouseMove(canvas, initialCenter + QPoint(24, 18));
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, initialCenter + QPoint(24, 18));
        const QRect afterMove = designer.report().frames.first().bounds;
        QVERIFY(afterMove.topLeft() != beforeMove.topLeft());

        const QPoint resizePoint = visualBounds(afterMove).bottomRight().toPoint() - QPoint(2, 2);
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, resizePoint);
        QTest::mouseMove(canvas, resizePoint + QPoint(22, 16));
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, resizePoint + QPoint(22, 16));
        const QRect afterResize = designer.report().frames.first().bounds;
        QVERIFY(afterResize.width() > afterMove.width());
        QVERIFY(afterResize.height() > afterMove.height());
        QVERIFY(designer.canUndo());
        designer.undo();
        QCOMPARE(designer.report().frames.first().bounds, afterMove);
        designer.undo();
        QCOMPARE(designer.report().frames.first().bounds, beforeMove);

        QSignalSpy commandSpy(&designer, &ReportDesignerWidget::commandRequested);
        QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, visualBounds(beforeMove).center().toPoint());
        QCOMPARE(commandSpy.size(), 1);
        QCOMPARE(commandSpy.takeFirst().first().toInt(), UiIds::Command::ToolFrameAttributes);
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

