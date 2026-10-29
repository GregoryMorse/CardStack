#pragma once

#include "ReportDesignerViewport.h"
#include "DesignerInteractionGrid.h"
#include "ReportDesignerWidget.h"
#include "ReportPrintEngine.h"
#include "DesignerSelectionProbe.h"

#include <QScrollBar>
#include <QTableWidget>
#include <QWheelEvent>
#include <QTest>

namespace CardStack::Tests::DesignerViewportProbe {
using DesignerSelectionProbe::pointer;
using DesignerSelectionProbe::click;

inline ReportDefinition largeReport(QRect bounds = QRect(300, 400, 1800, 400))
{
    ReportDefinition report;
    report.name = QStringLiteral("Scrollable Report");
    report.formType = ReportFormType::Report;
    report.formWidth = 10000;
    report.formHeight = 16000;
    ReportFrameDefinition frame;
    frame.kind = ReportFrameKind::Text;
    frame.text = QStringLiteral("Editable heading");
    frame.bounds = bounds;
    report.frames.append(frame);
    return report;
}

inline void capture(QWidget& widget, const QString& name)
{
    const QString output = qEnvironmentVariable("CARDSTACK_VIEWPORT_GALLERY_DIR");
    if (output.isEmpty()) return;
    QDir directory(output);
    QVERIFY(directory.exists() || directory.mkpath(QStringLiteral(".")));
    QCoreApplication::processEvents();
    QVERIFY(widget.grab().save(directory.filePath(name + QStringLiteral(".png"))));
}

inline QRectF visualBounds(QWidget* canvas, const ReportDefinition& report, const QRect& bounds)
{
    const QRectF page = canvas->property("designerPageRect").toRectF();
    const bool cell = report.formType == ReportFormType::Card || report.formType == ReportFormType::Label;
    const QSizeF paper = ReportPrintEngine::pageSizeMils(report);
    const QRectF content = cell ? page : page.adjusted(report.marginLeft * page.width() / paper.width(),
                                        report.marginTop * page.height() / paper.height(),
                                        -report.marginRight * page.width() / paper.width(),
                                        -report.marginBottom * page.height() / paper.height());
    return {content.left() + bounds.left() * content.width() / report.formWidth,
            content.top() + bounds.top() * content.height() / report.formHeight,
            bounds.width() * content.width() / report.formWidth,
            bounds.height() * content.height() / report.formHeight};
}

inline void navigation()
{
    ReportDesignerWidget designer(largeReport(), {});
    designer.setAttribute(Qt::WA_DontShowOnScreen);
    designer.resize(1080, 760);
    designer.show();
    QCoreApplication::processEvents();
    auto* canvas = designer.findChild<QWidget*>(QStringLiteral("reportDesignCanvas"));
    auto* horizontal = designer.findChild<QScrollBar*>(QStringLiteral("reportDesignerHorizontalScroll"));
    auto* vertical = designer.findChild<QScrollBar*>(QStringLiteral("reportDesignerVerticalScroll"));
    QVERIFY(canvas && horizontal && vertical);
    QVERIFY(horizontal->maximum() > 0 && vertical->maximum() > 0);
    horizontal->setValue(0);
    vertical->setValue(0);
    QApplication::setActiveWindow(&designer);
    canvas->setFocus();
    QCOMPARE(QApplication::focusWidget(), canvas);
    const auto original = designer.report();
    const QRectF initialPage = canvas->property("designerPageRect").toRectF();
    QCOMPARE(initialPage.width(), original.formWidth * ReportDesignerViewport::LogicalPixelsPerInch / ReportDesignerViewport::MilsPerInch);
    capture(designer, QStringLiteral("report_viewport_initial"));
    QTest::keyClick(canvas, Qt::Key_PageDown);
    QCOMPARE(vertical->value(), std::min(vertical->pageStep(), vertical->maximum()));
    QCOMPARE(canvas->property("designerPageRect").toRectF(), initialPage.translated(0, -vertical->value()));
    capture(designer, QStringLiteral("report_viewport_page_down"));
    QTest::keyClick(canvas, Qt::Key_PageUp);
    QCOMPARE(vertical->value(), 0);
    QTest::keyClick(canvas, Qt::Key_End);
    QCOMPARE(horizontal->value(), std::min(horizontal->pageStep(), horizontal->maximum()));
    QTest::keyClick(canvas, Qt::Key_Home);
    QCOMPARE(horizontal->value(), 0);
    horizontal->setValue(horizontal->maximum());
    vertical->setValue(vertical->maximum());
    QTest::keyClick(canvas, Qt::Key_End);
    QTest::keyClick(canvas, Qt::Key_PageDown);
    QCOMPARE(horizontal->value(), horizontal->maximum());
    QCOMPARE(vertical->value(), vertical->maximum());
    capture(designer, QStringLiteral("report_viewport_page_end"));
    horizontal->setValue(0);
    vertical->setValue(0);
    if (canvas->property("scrollLockActive").toBool()) QTest::keyClick(canvas, Qt::Key_ScrollLock);
    QTest::keyClick(canvas, Qt::Key_ScrollLock);
    QVERIFY(canvas->property("scrollLockActive").toBool());
    QTest::keyClick(canvas, Qt::Key_Right);
    QTest::keyClick(canvas, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(horizontal->value(), horizontal->singleStep());
    QCOMPARE(vertical->value(), vertical->singleStep());
    QTest::keyClick(canvas, Qt::Key_Left);
    QTest::keyClick(canvas, Qt::Key_Up);
    QCOMPARE(horizontal->value(), 0);
    QCOMPARE(vertical->value(), 0);
    QTest::keyClick(canvas, Qt::Key_ScrollLock);
    QVERIFY(!canvas->property("scrollLockActive").toBool());
    QCOMPARE(designer.report().frames, original.frames);
    QCOMPARE(designer.report().formWidth, original.formWidth);
    QCOMPARE(designer.report().formHeight, original.formHeight);
    QVERIFY(!designer.isDirty() && !designer.canUndo());

    const QPointF wheelPosition(canvas->rect().center());
    QWheelEvent wheel(wheelPosition, canvas->mapToGlobal(wheelPosition.toPoint()), QPoint(), QPoint(0, -120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(canvas, &wheel);
    QVERIFY(vertical->value() > 0);
    QCOMPARE(designer.report().frames, original.frames);
    QCOMPARE(designer.report().formWidth, original.formWidth);
    QCOMPARE(designer.report().formHeight, original.formHeight);
    QVERIFY(!designer.canUndo());

    QTest::keyClick(canvas, Qt::Key_Tab);
    QVERIFY(QApplication::focusWidget() != canvas);
    QVERIFY(!qobject_cast<QScrollBar*>(QApplication::focusWidget()));
    horizontal->setValue(0);
    vertical->setValue(0);
    canvas->setFocus();
    QTest::keyClick(canvas, Qt::Key_Right);
    QCOMPARE(designer.report().frames.first().bounds, original.frames.first().bounds.translated(31, 0));
    designer.undo();
    QCOMPARE(designer.report().frames, original.frames);
    QCOMPARE(designer.report().formWidth, original.formWidth);
    QCOMPARE(designer.report().formHeight, original.formHeight);
    QVERIFY(!designer.canUndo());

    // Selecting a table entry outside the viewport reveals it without modifying the report.
    ReportDesignerWidget distant(largeReport(QRect(6500, 13000, 1800, 400)), {});
    distant.setAttribute(Qt::WA_DontShowOnScreen);
    distant.resize(1080, 760);
    distant.show();
    QCoreApplication::processEvents();
    auto* distantCanvas = distant.findChild<QWidget*>(QStringLiteral("reportDesignCanvas"));
    const QRectF visible(distantCanvas->property("designerContentRect").toRect());
    QVERIFY(visible.contains(visualBounds(distantCanvas, distant.report(), distant.report().frames.first().bounds)));
    QVERIFY(!distant.isDirty() && !distant.canUndo());
    capture(distant, QStringLiteral("report_viewport_distant_selection"));
    designer.save();
    distant.save();
}

inline void scrolledResize()
{
    QFETCH(int, horizontalEdge);
    QFETCH(int, verticalEdge);
    auto report = largeReport(QRect(2200, 3400, 1800, 400));
    report.marginLeft = 250;
    report.marginTop = 150;
    report.marginRight = 350;
    report.marginBottom = 250;
    ReportDesignerWidget designer(report, {});
    designer.setAttribute(Qt::WA_DontShowOnScreen);
    designer.resize(1080, 760);
    designer.show();
    QCoreApplication::processEvents();
    auto* canvas = designer.findChild<QWidget*>(QStringLiteral("reportDesignCanvas"));
    designer.findChild<QScrollBar*>(QStringLiteral("reportDesignerHorizontalScroll"))->setValue(120);
    designer.findChild<QScrollBar*>(QStringLiteral("reportDesignerVerticalScroll"))->setValue(200);
    const QRect original = report.frames.first().bounds;
    QRectF visual = visualBounds(canvas, designer.report(), original);
    QVERIFY(canvas->property("designerContentRect").toRect().contains(visual.toAlignedRect()));
    click(canvas, visual.center().toPoint());
    QVERIFY(!designer.canUndo());
    const QPoint start(qRound(horizontalEdge < 0 ? visual.left() : horizontalEdge > 0 ? visual.right() : visual.center().x()),
                       qRound(verticalEdge < 0 ? visual.top() : verticalEdge > 0 ? visual.bottom() : visual.center().y()));
    const QPoint end = start + QPoint(horizontalEdge * 18, verticalEdge * 14);
    pointer(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    pointer(canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    pointer(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    const QRect resized = designer.report().frames.first().bounds;
    if (horizontalEdge) QVERIFY(resized.width() > original.width());
    else QVERIFY(qAbs(resized.width() - original.width()) <= 2);
    if (verticalEdge) QVERIFY(resized.height() > original.height());
    else QVERIFY(qAbs(resized.height() - original.height()) <= 2);
    if (horizontalEdge <= 0) QVERIFY(qAbs(resized.right() - original.right()) <= 2);
    if (horizontalEdge >= 0) QVERIFY(qAbs(resized.left() - original.left()) <= 2);
    if (verticalEdge <= 0) QVERIFY(qAbs(resized.bottom() - original.bottom()) <= 2);
    if (verticalEdge >= 0) QVERIFY(qAbs(resized.top() - original.top()) <= 2);
    capture(designer, QStringLiteral("report_viewport_margined_resize_") + QString::fromLatin1(QTest::currentDataTag()));
    designer.undo();
    QCOMPARE(designer.report().frames.first().bounds, original);
    QVERIFY(!designer.canUndo());

    visual = visualBounds(canvas, designer.report(), original);
    const QPoint center = visual.center().toPoint();
    const QPoint move(24, 18);
    pointer(canvas, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    pointer(canvas, QEvent::MouseMove, center + move, Qt::NoButton, Qt::LeftButton);
    pointer(canvas, QEvent::MouseButtonRelease, center + move, Qt::LeftButton, Qt::NoButton);
    const auto modelPoint = [&](QPoint point) {
        return QPointF(original.left() + (point.x() - visual.left()) * original.width() / visual.width(),
                       original.top() + (point.y() - visual.top()) * original.height() / visual.height());
    };
    const QPoint sourceDelta = DesignerInteraction::ReportGrid.pointerDelta(modelPoint(center), modelPoint(center + move));
    const QPoint expected = DesignerInteraction::ReportGrid.modelDelta(original.topLeft(), sourceDelta);
    const QRect moved = designer.report().frames.first().bounds;
    QVERIFY((moved.topLeft() - original.topLeft() - expected).manhattanLength() <= 2);
    QCOMPARE(moved.size(), original.size());
    designer.undo();
    QCOMPARE(designer.report().frames.first().bounds, original);
    QVERIFY(!designer.canUndo());
    designer.save();
}


inline void renderedFramesMatchDesignBounds()
{
    QFETCH(int, formType);
    auto report = largeReport(QRect(1000, 600, 1500, 700));
    report.formType = static_cast<ReportFormType>(formType);
    report.formWidth = 5000;
    report.formHeight = 3000;
    report.pageWidth = 8500;
    report.pageHeight = 11000;
    report.marginLeft = 1500;
    report.marginTop = 1200;
    report.marginRight = 500;
    report.marginBottom = 3000;
    auto& frame = report.frames.first();
    frame.kind = ReportFrameKind::LineOrBox;
    frame.lineBoxShape = ReportLineShapeBox;
    frame.lineStyle = ReportLineStyleNoOutline;
    frame.fillPattern = ReportFillPatternSolid;
    ReportDesignerWidget designer(report, {});
    designer.setAttribute(Qt::WA_DontShowOnScreen);
    designer.resize(1080, 760);
    designer.show();
    QCoreApplication::processEvents();
    auto* canvas = designer.findChild<QWidget*>(QStringLiteral("reportDesignCanvas"));
    click(canvas, canvas->property("designerContentRect").toRect().topLeft() + QPoint(2, 2));
    QCOMPARE(designer.selectedFrameIndex(), -1);
    const QImage image = canvas->grab().toImage();
    const QRectF visual = visualBounds(canvas, report, frame.bounds);
    QVERIFY(canvas->property("designerContentRect").toRect().contains(visual.toAlignedRect()));
    const auto colorAt = [&](QPointF point) {
        return image.pixelColor((point * image.devicePixelRatio()).toPoint());
    };
    // The body must fill the hit-test rectangle, not a second un-margined rectangle.
    for (const QPointF& point : {visual.center(), visual.topLeft() + QPointF(6, 6), visual.bottomRight() - QPointF(6, 6)})
        QVERIFY2(colorAt(point).value() < 40, "Rendered object does not match its designer bounds");
    for (const QPointF& point : {QPointF(visual.left() - 8, visual.center().y()), QPointF(visual.right() + 8, visual.center().y())})
        QVERIFY2(colorAt(point).lightness() > 220, "Drawing extends beyond the hit-test rectangle");
    click(canvas, visual.center().toPoint());
    QCOMPARE(designer.selectedFrameIndex(), 0);
    QVERIFY(!designer.canUndo() && !designer.isDirty());
    capture(designer, QStringLiteral("report_render_alignment_") + QString::fromLatin1(QTest::currentDataTag()));
    designer.save();
}

inline void rulers()
{
    const QFontMetricsF metrics(QApplication::font());
    for (qreal width : {530.0, 816.0}) {
        for (int scroll : {0, 100, 400}) {
            const auto labels = ReportDesignerRulers::labels(QRectF(44 - scroll, 44 - scroll, width, width * 14 / 8.5),
                                                             QSize(8500, 14000), QRect(0, 0, 1000, 760), metrics);
            QVERIFY(!labels.isEmpty());
            for (int first = 0; first < labels.size(); ++first) {
                QVERIFY(labels.at(first).bounds.left() >= 0 && labels.at(first).bounds.top() >= 0);
                for (int second = first + 1; second < labels.size(); ++second)
                    QVERIFY2(!labels.at(first).bounds.intersects(labels.at(second).bounds), "Ruler labels overlap");
            }
            if (scroll == 0) QVERIFY(std::any_of(labels.begin(), labels.end(), [](const auto& label) { return label.text == QStringLiteral("8.5"); }));
        }
    }
}
} // namespace CardStack::Tests::DesignerViewportProbe
