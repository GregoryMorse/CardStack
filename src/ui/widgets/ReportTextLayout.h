#pragma once

#include "ReportDefinition.h"
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>
#include <QTextLayout>
#include <QTextOption>
#include <algorithm>
#include <cmath>

namespace CardStack::ReportTextLayout {
inline constexpr qreal UnitsPerInch = 1000.0;
inline constexpr qreal StoredFontUnitsPerInch = 720.0;
inline constexpr qreal DefaultFontPoints = 9.0;
inline constexpr qreal Padding = 0.0;

inline QString normalizedText(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text;
}
inline Qt::Alignment alignment(quint8 flags)
{
    return Qt::AlignTop | ((flags & ReportStyleFlagAlignRight) != 0 ? Qt::AlignRight
        : (flags & ReportStyleFlagAlignCenter) != 0 ? Qt::AlignHCenter : Qt::AlignLeft);
}
inline QFont font(const ReportFontDefinition& definition, quint8 flags)
{
    QFont result = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    if (!definition.faceName.isEmpty()) result.setFamily(definition.faceName);
    const qreal height = definition.legacyHeight == 0
        ? DefaultFontPoints * UnitsPerInch / 72.0
        : std::abs(definition.legacyHeight) * UnitsPerInch / StoredFontUnitsPerInch;
    result.setPixelSize(std::max(1, qRound(height)));
    result.setWeight((flags & ReportStyleFlagBold) != 0 ? QFont::Bold
        : static_cast<QFont::Weight>(definition.weight == 0 ? int(QFont::Normal) : std::clamp(definition.weight, 100, 900)));
    result.setItalic(definition.italic || (flags & ReportStyleFlagItalic) != 0);
    result.setUnderline(definition.underline || (flags & ReportStyleFlagUnderline) != 0);
    result.setStrikeOut(definition.strikeOut);
    result.setFixedPitch((definition.pitchAndFamily & 0x03) == 0x01);
    if (definition.legacyHeight > 0) {
        // Positive LOGFONT heights describe the complete cell, not the em square.
        const qreal cellHeight = QFontMetricsF(result).height();
        int pixels = std::max(1, qFloor(height * result.pixelSize() / std::max<qreal>(1.0, cellHeight)));
        result.setPixelSize(pixels);
        if (pixels > 1 && QFontMetricsF(result).height() > height) result.setPixelSize(--pixels);
    }
    if (definition.width != 0) {
        const qreal desired = std::abs(definition.width) * UnitsPerInch / StoredFontUnitsPerInch;
        result.setStretch(std::clamp(qRound(100.0 * desired / std::max<qreal>(1.0, QFontMetricsF(result).averageCharWidth())), 1, 4000));
    }
    if (definition.quality == 3) result.setStyleStrategy(QFont::NoAntialias);
    return result;
}
inline QRectF textRect(const QRectF& bounds)
{
    return bounds.adjusted(Padding, Padding, -Padding, -Padding);
}
inline void configure(QTextLayout& layout, quint8 flags, bool wrap)
{
    QTextOption option;
    option.setAlignment(alignment(flags));
    option.setWrapMode(wrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
    layout.setTextOption(option);
}

inline QVector<QString> chunks(const QString& original, const ReportFontDefinition& definition,
                               quint8 flags, const QRect& bounds)
{
    const QString text = normalizedText(original);
    if (text.isEmpty()) return {QString()};
    QString layoutText = text;
    layoutText.replace(QLatin1Char('\n'), QChar::LineSeparator);
    QTextLayout layout(layoutText, font(definition, flags));
    configure(layout, flags, true);
    const QRectF area = textRect(bounds);
    QVector<QString> result;
    int chunkStart = 0;
    qreal height = 0.0;
    layout.beginLayout();
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(std::max<qreal>(1.0, area.width()));
        if (height > 0.0 && height + line.height() > area.height()) {
            result.append(text.mid(chunkStart, line.textStart() - chunkStart));
            chunkStart = line.textStart();
            height = 0.0;
        }
        height += line.height();
    }
    layout.endLayout();
    result.append(text.mid(chunkStart));
    return result;
}

inline void draw(QPainter& painter, const QString& original, const ReportFontDefinition& definition,
                 quint8 flags, bool wrap, const QRectF& bounds)
{
    const QRectF area = textRect(bounds);
    if (area.width() <= 0 || area.height() <= 0) return;
    QString text = normalizedText(original);
    text.replace(QLatin1Char('\n'), QChar::LineSeparator);
    QTextLayout layout(text, font(definition, flags));
    configure(layout, flags, wrap);
    qreal y = 0.0;
    layout.beginLayout();
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(area.width());
        line.setPosition(QPointF(0, y));
        y += line.height();
    }
    layout.endLayout();
    painter.save();
    painter.setClipRect(bounds, Qt::IntersectClip);
    painter.translate(area.topLeft());
    const int rotation = definition.escapementTenths != 0 ? definition.escapementTenths : definition.orientationTenths;
    if (rotation != 0) painter.rotate(-rotation / 10.0);
    layout.draw(&painter, QPointF());
    painter.restore();
}
} // namespace CardStack::ReportTextLayout
