#pragma once

#include "ReportDefinition.h"
#include <QBrush>
#include <QImage>
#include <QPainter>
#include <QTransform>
#include <algorithm>
#include <array>

namespace CardStack::FrameDrawingStyle {
inline constexpr qreal LogicalPenWidth = 18.0;
inline constexpr qreal LogicalUnitsPerPatternPixel = 10.0;

inline QBrush fillBrush(int pattern, const QColor& color = Qt::black, qreal scale = 1.0)
{
    if (pattern == ReportFillPatternClear) return QBrush(Qt::NoBrush);
    if (pattern == ReportFillPatternSolid) return QBrush(color);
    constexpr int Side = 8;
    QImage image(Side, Side, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    if (pattern >= ReportFillPattern5Percent && pattern <= ReportFillPattern90Percent) {
        constexpr std::array<int, 12> Percent{5, 10, 20, 25, 30, 40, 50, 60, 70, 75, 80, 90};
        constexpr int Bayer[Side][Side] = {
            {0,48,12,60,3,51,15,63}, {32,16,44,28,35,19,47,31},
            {8,56,4,52,11,59,7,55}, {40,24,36,20,43,27,39,23},
            {2,50,14,62,1,49,13,61}, {34,18,46,30,33,17,45,29},
            {10,58,6,54,9,57,5,53}, {42,26,38,22,41,25,37,21}};
        const int count = qRound(Percent.at(pattern - ReportFillPattern5Percent) * Side * Side / 100.0);
        for (int y = 0; y < Side; ++y)
            for (int x = 0; x < Side; ++x)
                if (Bayer[y][x] < count) image.setPixelColor(x, y, color);
    } else if (pattern >= ReportFillPatternDarkHorizontal && pattern <= ReportFillPatternLightTrellis) {
        const bool dark = pattern <= ReportFillPatternDarkTrellis;
        const int kind = pattern - (dark ? ReportFillPatternDarkHorizontal : ReportFillPatternLightHorizontal);
        const int thickness = dark ? 2 : 1;
        for (int y = 0; y < Side; ++y) {
            for (int x = 0; x < Side; ++x) {
                const bool horizontal = y < thickness;
                const bool vertical = x < thickness;
                const bool down = (x - y + Side) % Side < thickness;
                const bool up = (x + y) % Side < thickness;
                const bool filled = kind == 0 ? horizontal : kind == 1 ? vertical
                    : kind == 2 ? down : kind == 3 ? up
                    : kind == 4 ? horizontal || vertical : down || up;
                if (filled) image.setPixelColor(x, y, color);
            }
        }
    } else {
        return QBrush(Qt::NoBrush);
    }
    QBrush brush(image);
    brush.setTransform(QTransform::fromScale(scale * LogicalUnitsPerPatternPixel,
                                             scale * LogicalUnitsPerPatternPixel));
    return brush;
}

inline QPen linePen(int style, qreal scale = 1.0, const QColor& color = Qt::black)
{
    QPen pen(color);
    Qt::PenStyle qtStyle = Qt::SolidLine;
    switch (style) {
    case ReportLineStyleDash: case ReportLineStyleThickDash: qtStyle = Qt::DashLine; break;
    case ReportLineStyleDot: case ReportLineStyleThickDot: qtStyle = Qt::DotLine; break;
    case ReportLineStyleDashDot: qtStyle = Qt::DashDotLine; break;
    case ReportLineStyleDashDotDot: qtStyle = Qt::DashDotDotLine; break;
    case ReportLineStyleNoOutline: qtStyle = Qt::NoPen; break;
    default: break;
    }
    pen.setStyle(qtStyle);
    const bool thick = style >= ReportLineStyleThickSolid && style <= ReportLineStyleThickDot;
    pen.setWidthF(style == ReportLineStyleHairline ? 0.0
        : LogicalPenWidth * scale * (thick ? 1.8 : 1.0));
    return pen;
}

inline void draw(QPainter& painter, const QRectF& rect, const ReportFrameDefinition& frame,
                 qreal scale = 1.0, const QColor& color = Qt::black)
{
    painter.save();
    const QPen pen = linePen(frame.lineStyle, scale, color);
    painter.setPen(pen);
    painter.setBrush(fillBrush(frame.fillPattern, color, scale));
    const bool automatic = frame.lineBoxShape != ReportLineShapeBox;
    const bool horizontal = frame.lineBoxShape == ReportLineShapeHorizontal
        || (frame.lineBoxShape == ReportLineShapeLegacyAuto && rect.width() >= rect.height())
        || (automatic && rect.height() <= 30.0 * scale && rect.width() >= rect.height());
    const bool vertical = frame.lineBoxShape == ReportLineShapeVertical
        || (frame.lineBoxShape == ReportLineShapeLegacyAuto && rect.height() > rect.width())
        || (automatic && rect.width() <= 30.0 * scale && rect.height() > rect.width());
    if (horizontal) painter.drawLine(QPointF(rect.left(), rect.center().y()), QPointF(rect.right(), rect.center().y()));
    else if (vertical) painter.drawLine(QPointF(rect.center().x(), rect.top()), QPointF(rect.center().x(), rect.bottom()));
    else {
        const qreal inset = pen.style() == Qt::NoPen ? 0.0 : pen.widthF() / 2.0;
        painter.drawRoundedRect(rect.adjusted(inset, inset, -inset, -inset),
                                frame.cornerRadius * scale, frame.cornerRadius * scale);
    }
    painter.restore();
}
} // namespace CardStack::FrameDrawingStyle
