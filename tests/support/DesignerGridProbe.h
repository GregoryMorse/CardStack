#pragma once

#include "DesignerInteractionGrid.h"
#include "DesignerClipboard.h"
#include "DesignerSelectionProbe.h"
#include "ReportDesignerWidget.h"
#include "ReportTextLayout.h"
#include <QFontInfo>
#include "TemplateDesignerWidget.h"
#include <QFont>
#include <QComboBox>
#include <QImage>
#include <QTest>

namespace CardStack::Tests::DesignerGridProbe {
inline int originalSnap(int value)
{
    const int remainder = value < 0 ? (value - 1) % 3 + 1 : (value + 1) % 3 - 1;
    return value - remainder;
}
inline void sourceAlgorithm()
{
    for (int value = -240; value <= 240; ++value)
        QCOMPARE(DesignerInteraction::Grid::snapPixel(value), originalSnap(value));
    QCOMPARE(DesignerInteraction::TemplateGrid.copyDelta(QRect(300, 400, 800, 160), 22), QPoint(-210, -120));
    QCOMPARE(DesignerInteraction::ReportGrid.copyDelta(QRect(1000, 1000, 1800, 400)), QPoint(-906, -937));
}

inline ReportDefinition reportDefinition()
{
    ReportDefinition report;
    report.name = QStringLiteral("Grid precision");
    report.formType = ReportFormType::Report;
    report.formWidth = 5000;
    report.formHeight = 3000;
    ReportFrameDefinition frame;
    frame.kind = ReportFrameKind::Text;
    frame.text = QStringLiteral("Grid sample");
    frame.bounds = QRect(1000, 1000, 1800, 400);
    report.frames.append(frame);
    return report;
}
inline CardTemplateLayout templateDefinition()
{
    CardTemplateLayout layout;
    CardTemplateFrame frame;
    frame.kind = CardTemplateFrameKind::Text;
    frame.text = QStringLiteral("Grid sample");
    frame.bounds = QRect(1000, 1000, 1800, 400);
    layout.frames.append(frame);
    return layout;
}

inline void show(QWidget& widget)
{
    widget.setAttribute(Qt::WA_DontShowOnScreen);
    widget.resize(1080, 760);
    widget.show();
    QCoreApplication::processEvents();
    QApplication::setActiveWindow(&widget);
    const QString expectedDpr = qEnvironmentVariable("CARDSTACK_EXPECTED_DPR");
    if (!expectedDpr.isEmpty())
        QVERIFY2(qAbs(widget.devicePixelRatioF() - expectedDpr.toDouble()) < 0.01,
                 qPrintable(QStringLiteral("DPR requested %1, actual %2").arg(expectedDpr).arg(widget.devicePixelRatioF())));
}
inline void capture(QWidget& widget, const QString& name)
{
    const QString output = qEnvironmentVariable("CARDSTACK_GRID_GALLERY_DIR");
    if (output.isEmpty()) return;
    QDir directory(output);
    QVERIFY(directory.exists() || directory.mkpath(QStringLiteral(".")));
    QCoreApplication::processEvents();
    QVERIFY(widget.grab().save(directory.filePath(name + QStringLiteral(".png"))));
}

template <class Designer, class ReadBounds>
void keyboard(Designer& designer, const char* canvasName, int fourStepUnits, ReadBounds read)
{
    show(designer);
    auto* canvas = designer.template findChild<QWidget*>(QString::fromLatin1(canvasName));
    QVERIFY(canvas);
    canvas->setFocus();
    if (canvas->property("scrollLockActive").toBool()) QTest::keyClick(canvas, Qt::Key_ScrollLock);
    const QRect original = read();
    for (int count = 0; count < 4; ++count) QTest::keyClick(canvas, Qt::Key_Right);
    QCOMPARE(read(), original.translated(fourStepUnits, 0));
    for (int count = 0; count < 4; ++count) QTest::keyClick(canvas, Qt::Key_Left);
    QCOMPARE(read(), original);
    for (int count = 0; count < 4; ++count) QTest::keyClick(canvas, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(read().topLeft(), original.topLeft());
    QCOMPARE(read().height(), original.height() + fourStepUnits);
    for (int count = 0; count < 4; ++count) QTest::keyClick(canvas, Qt::Key_Up, Qt::ShiftModifier);
    QCOMPARE(read(), original);
    for (int count = 0; count < 16; ++count) designer.undo();
    QCOMPARE(read(), original);
    QVERIFY(!designer.canUndo());
    designer.save();
}

template <class Designer, class ReadBounds>
void pointer(Designer& designer, const char* canvasName, QSize size, int rulers,
             const DesignerInteraction::Grid& grid, const QString& prefix, ReadBounds read)
{
    show(designer);
    auto* canvas = designer.template findChild<QWidget*>(QString::fromLatin1(canvasName));
    QVERIFY(canvas);
    const QRect original = read();
    for (QSize window : {QSize(1000, 700), QSize(1380, 900)}) {
        designer.resize(window);
        QCoreApplication::processEvents();
        const QRectF page = DesignerSelectionProbe::page(canvas, size, rulers);
        const auto position = [&](QPointF value) {
            return QPoint(qRound(page.left() + value.x() * page.width() / size.width()),
                          qRound(page.top() + value.y() * page.height() / size.height()));
        };
        const QPoint start = position(original.center());
        const QPoint end = position(QPointF(original.center()) + QPointF(6 * grid.unitsPerPixel, -3 * grid.unitsPerPixel));
        const auto model = [&](QPoint p) {
            return QPointF((p.x() - page.left()) * size.width() / page.width(),
                           (p.y() - page.top()) * size.height() / page.height());
        };
        const auto first = model(start);
        const auto last = model(end);
        const QPoint sourceDelta(originalSnap(qRound(last.x() / grid.unitsPerPixel)) - originalSnap(qRound(first.x() / grid.unitsPerPixel)),
                                  originalSnap(qRound(last.y() / grid.unitsPerPixel)) - originalSnap(qRound(first.y() / grid.unitsPerPixel)));
        QCOMPARE(sourceDelta, QPoint(6, -3));
        DesignerSelectionProbe::click(canvas, start);
        QCOMPARE(read(), original);
        QVERIFY(!designer.canUndo());
        DesignerSelectionProbe::pointer(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
        DesignerSelectionProbe::pointer(canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
        DesignerSelectionProbe::pointer(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(read(), original.translated(grid.modelDelta(original.topLeft(), sourceDelta)));
        capture(designer, prefix + (window.width() == 1000 ? QStringLiteral("_small") : QStringLiteral("_large")));
        designer.undo();
        QCOMPARE(read(), original);
        QVERIFY(!designer.canUndo());
    }
    designer.save();
}

inline void reportKeyboard()
{
    ReportDesignerWidget widget(reportDefinition(), {});
    keyboard(widget, "reportDesignCanvas", 125, [&] { return widget.report().frames.first().bounds; });
}
inline void templateKeyboard()
{
    TemplateDesignerWidget widget(templateDefinition(), {});
    keyboard(widget, "templateDesignCanvas", 120, [&] { return widget.layoutDefinition().frames.first().bounds; });
}
inline void reportPointer()
{
    ReportDesignerWidget widget(reportDefinition(), {});
    pointer(widget, "reportDesignCanvas", QSize(5000, 3000), 28, DesignerInteraction::ReportGrid,
            QStringLiteral("report_grid"), [&] { return widget.report().frames.first().bounds; });
}
inline void templatePointer()
{
    TemplateDesignerWidget widget(templateDefinition(), {});
    pointer(widget, "templateDesignCanvas", QSize(6400, 4800), 0, DesignerInteraction::TemplateGrid,
            QStringLiteral("template_grid"), [&] { return widget.layoutDefinition().frames.first().bounds; });
}

inline void reportClipboard()
{
    if (QGuiApplication::platformName() != QStringLiteral("offscreen")) QSKIP("Keep the user's native clipboard untouched.");
    ReportDesignerWidget source(reportDefinition(), {});
    show(source);
    source.findChild<QWidget*>(QStringLiteral("reportDesignCanvas"))->setFocus();
    source.copySelectedFrame();
    auto empty = reportDefinition();
    empty.frames.clear();
    ReportDesignerWidget target(empty, {});
    show(target);
    target.findChild<QWidget*>(QStringLiteral("reportDesignCanvas"))->setFocus();
    target.pasteFrame();
    QCOMPARE(target.report().frames.size(), 1);
    QCOMPARE(target.report().frames.first().bounds, QRect(94, 63, 1800, 400));
    target.pasteFrame();
    QCOMPARE(target.report().frames.size(), 2);
    QCOMPARE(target.report().frames.at(1).bounds, target.report().frames.first().bounds);
    target.undo();
    QCOMPARE(target.report().frames.size(), 1);
    target.undo();
    QVERIFY(target.report().frames.isEmpty());
    source.save();
    target.save();
}

inline void templateClipboard()
{
    if (QGuiApplication::platformName() != QStringLiteral("offscreen")) QSKIP("Keep the user's native clipboard untouched.");
    auto layout = templateDefinition();
    layout.frames.first().bounds = QRect(300, 400, 800, 160);
    TemplateDesignerWidget source(layout, {});
    show(source);
    source.findChild<QWidget*>(QStringLiteral("templateDesignCanvas"))->setFocus();
    source.copySelectedFrame();
    layout.frames.clear();
    TemplateDesignerWidget target(layout, {});
    show(target);
    target.findChild<QWidget*>(QStringLiteral("templateDesignCanvas"))->setFocus();
    target.pasteFrame();
    QCOMPARE(target.layoutDefinition().frames.size(), 1);
    QCOMPARE(target.layoutDefinition().frames.first().bounds, QRect(90, 280, 800, 160));
    target.pasteFrame();
    QCOMPARE(target.layoutDefinition().frames.size(), 2);
    QCOMPARE(target.layoutDefinition().frames.at(1).bounds, target.layoutDefinition().frames.first().bounds);
    target.undo();
    QCOMPARE(target.layoutDefinition().frames.size(), 1);
    target.undo();
    QVERIFY(target.layoutDefinition().frames.isEmpty());
    source.save();
    target.save();
}

inline void reportFonts()
{
    for (const QString& family : {QStringLiteral("Arial"), QStringLiteral("CardStack Missing Font Test")}) {
        for (int height : {-72, 72}) {
            ReportFontDefinition definition;
            definition.faceName = family;
            definition.legacyHeight = height;
            definition.weight = 0;
            const QFont selected = ReportTextLayout::font(definition, 0);
            QCOMPARE(selected.weight(), QFont::Normal);
            if (height < 0) QCOMPARE(selected.pixelSize(), 100);
            else {
                const qreal cell = QFontMetricsF(selected).height();
                QVERIFY2(cell <= 100.1 && cell > 97.0, "Positive font cell height changed its layout size");
            }
            const QFontInfo resolved(selected);
            QVERIFY(!resolved.family().isEmpty());
            const QFontMetricsF metrics(selected);
            for (const QChar glyph : QStringLiteral("CardStack 0123456789 \u00bd"))
                QVERIFY2(metrics.inFontUcs4(glyph.unicode()), "Fallback font does not contain a required glyph");
            definition.width = 40;
            const QFont stretched = ReportTextLayout::font(definition, 0);
            const auto advance = [](const QFont& font) {
                QTextLayout layout(QStringLiteral("MMMM0000iiii"), font);
                layout.beginLayout();
                QTextLine line = layout.createLine();
                line.setLineWidth(10000);
                const qreal width = line.naturalTextWidth();
                layout.endLayout();
                return width;
            };
            const qreal expectedRatio = (40.0 * 1000.0 / 720.0) / QFontMetricsF(selected).averageCharWidth();
            const qreal actualRatio = advance(stretched) / advance(selected);
            QVERIFY2(qAbs(actualRatio - expectedRatio) < 0.025,
                qPrintable(QStringLiteral("Family %1, height %2: expected advance ratio %3, actual %4")
                    .arg(family).arg(height).arg(expectedRatio).arg(actualRatio)));
            definition.width = 0;
            auto report = reportDefinition();
            report.frames.first().text = QStringLiteral("CardStack 123 \u00bd");
            report.frames.first().bounds = QRect(700, 700, 3000, 400);
            report.textFont = definition;
            ReportDesignerWidget designer(report, {});
            show(designer);
            QCOMPARE(designer.report().textFont, definition);
            QCOMPARE(designer.report().frames, report.frames);
            qInfo().noquote() << "Font request:" << family << "resolved:" << resolved.family() << "stored height:" << height;
            const QString prefix = family == QStringLiteral("Arial") ? QStringLiteral("report_font") : QStringLiteral("report_fallback");
            capture(designer, prefix + (height < 0 ? QStringLiteral("_em") : QStringLiteral("_cell")));
            designer.save();
        }
    }
}

inline void templateTable()
{
    auto layout = templateDefinition();
    CardTemplateFrame line;
    line.kind = CardTemplateFrameKind::LineOrBox;
    line.bounds = QRect(1000, 2400, 1800, 500);
    layout.frames.append(line);
    TemplateDesignerWidget designer(layout, {});
    show(designer);
    auto* table = designer.findChild<QTableWidget*>();
    auto* text = designer.findChild<QLineEdit*>(QStringLiteral("templateTextEdit"));
    auto* shape = designer.findChild<QComboBox*>(QStringLiteral("templateLineShapeCombo"));
    QVERIFY(table && text && shape);
    for (int row : {1, 0}) {
        const QRect item = table->visualRect(table->model()->index(row, 0));
        QVERIFY(!item.isEmpty());
        DesignerSelectionProbe::click(table->viewport(), item.center());
        QCOMPARE(designer.selectedFrameIndices(), QVector<int>{row});
        QCOMPARE(text->isHidden(), row != 0);
        QCOMPARE(shape->isHidden(), row != 1);
        capture(designer, row == 0 ? QStringLiteral("template_table_text") : QStringLiteral("template_table_line"));
    }
    QVERIFY(!designer.isDirty() && !designer.canUndo());
    designer.save();
}

inline void templateFonts(const QString& family = QStringLiteral("Arial"))
{
    auto layout = templateDefinition();
    layout.frames.first().text = QStringLiteral("MMMM");
    layout.frames.first().bounds = QRect(1000, 1000, 2000, 500);
    DeckAppearance appearance;
    QFont font(family);
    font.setPixelSize(14);
    appearance.textFont = font.toString();
    appearance.indexFont = font.toString();
    TemplateDesignerWidget designer(layout, {}, nullptr, appearance);
    show(designer);
    auto* canvas = designer.findChild<QWidget*>(QStringLiteral("templateDesignCanvas"));
    qreal referenceWidth = 0;
    for (QSize window : {QSize(900, 700), QSize(1380, 1000)}) {
        designer.resize(window);
        QCoreApplication::processEvents();
        const QRectF page = DesignerSelectionProbe::page(canvas, QSize(6400, 4800), 0);
        DesignerSelectionProbe::click(canvas, page.topRight().toPoint() + QPoint(-8, 8));
        const QImage image = canvas->grab().toImage();
        const qreal dpr = image.devicePixelRatio();
        const qreal scale = page.width() / 6400;
        const QRectF frame(page.left() + 1000 * scale, page.top() + 1000 * scale, 2000 * scale, 500 * scale);
        const QRect pixels = QRectF(frame.topLeft() * dpr, frame.size() * dpr).toAlignedRect().intersected(image.rect());
        QRect ink;
        for (int y = pixels.top(); y <= pixels.bottom(); ++y)
            for (int x = pixels.left(); x <= pixels.right(); ++x) {
                const auto color = image.pixelColor(x, y);
                if (color.alpha() > 0 && color.red() < 90 && color.green() < 90 && color.blue() < 90)
                    ink = ink.united(QRect(x, y, 1, 1));
            }
        QVERIFY(!ink.isEmpty());
        const qreal sourceWidth = ink.width() / dpr / (scale * DesignerInteraction::TemplateUnitsPerSourcePixel);
        if (referenceWidth == 0) referenceWidth = sourceWidth;
        else QVERIFY2(qAbs(sourceWidth - referenceWidth) < 3, "Template text does not scale with its source-pixel geometry");
        QCOMPARE(designer.layoutDefinition().frames, layout.frames);
        QCOMPARE(designer.appearance().textFont, appearance.textFont);
        const QString prefix = family == QStringLiteral("Arial") ? QStringLiteral("template_font") : QStringLiteral("template_fallback");
        capture(designer, prefix + (window.width() == 900 ? QStringLiteral("_small") : QStringLiteral("_large")));
    }
    designer.save();
}
} // namespace CardStack::Tests::DesignerGridProbe
