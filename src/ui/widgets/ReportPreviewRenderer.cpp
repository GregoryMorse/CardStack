#include "ReportPreviewRenderer.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>

#include <algorithm>

namespace CardStack {
namespace {

constexpr qreal LegacyFontScaleMultiplier = 70.0;
constexpr qreal MinimumReportFontPixels = 7.0;
constexpr qreal MaximumReportFontPixels = 96.0;
constexpr qreal LogicalFramePadding = 35.0;
constexpr qreal MinimumFramePadding = 1.5;
constexpr qreal MaximumFramePadding = 6.0;
constexpr qreal LogicalFrameLineWidth = 18.0;
constexpr qreal MinimumFrameLineWidth = 0.75;
constexpr qreal MaximumFrameLineWidth = 2.0;
constexpr qreal MinimumLineFrameThreshold = 2.0;
constexpr qreal LogicalLineFrameThreshold = 30.0;

QString replaceDelimitedToken(QString text, QChar open, QChar close, const QMap<QString, QString>& values)
{
    int searchFrom = 0;
    while (searchFrom < text.size()) {
        const int begin = text.indexOf(open, searchFrom);
        if (begin < 0) {
            break;
        }
        const int end = text.indexOf(close, begin + 1);
        if (end < 0) {
            break;
        }

        const QString key = text.mid(begin + 1, end - begin - 1).trimmed();
        const QString replacement = values.value(key, text.mid(begin, end - begin + 1));
        text.replace(begin, end - begin + 1, replacement);
        searchFrom = begin + replacement.size();
    }
    return text;
}

QRectF scaleBounds(const QRect& bounds, const ReportDefinition& report, const QRectF& target)
{
    const qreal width = std::max(1, report.formWidth);
    const qreal height = std::max(1, report.formHeight);
    const qreal xScale = target.width() / width;
    const qreal yScale = target.height() / height;
    return QRectF(
        target.left() + bounds.left() * xScale,
        target.top() + bounds.top() * yScale,
        bounds.width() * xScale,
        bounds.height() * yScale);
}

QRectF contentTarget(const ReportDefinition& report, const QRectF& target)
{
    const qreal width = std::max(1, report.formWidth);
    const qreal height = std::max(1, report.formHeight);
    const qreal xScale = target.width() / width;
    const qreal yScale = target.height() / height;
    const QRectF content = target.adjusted(
        report.marginLeft * xScale,
        report.marginTop * yScale,
        -report.marginRight * xScale,
        -report.marginBottom * yScale);
    return content.isValid() && !content.isEmpty() ? content : target;
}

qreal logicalScale(const ReportDefinition& report, const QRectF& target)
{
    const qreal width = std::max(1, report.formWidth);
    const qreal height = std::max(1, report.formHeight);
    return std::min(target.width() / width, target.height() / height);
}

QFont frameFont(const QPainter* painter, const ReportDefinition& report, const ReportFrameDefinition& frame, qreal scale)
{
    QFont font = painter->font();
    const ReportFontDefinition& reportFont = frame.kind == ReportFrameKind::Data ? report.dataFont : report.textFont;
    const QString faceName = reportFont.faceName.isEmpty() ? report.textFont.faceName : reportFont.faceName;
    if (!faceName.isEmpty()) {
        font.setFamily(faceName);
    }
    if (reportFont.legacyHeight != 0) {
        const qreal pixelSize = std::clamp(
            std::abs(reportFont.legacyHeight) * std::max<qreal>(1.0, scale * LegacyFontScaleMultiplier),
            MinimumReportFontPixels,
            MaximumReportFontPixels);
        font.setPixelSize(qRound(pixelSize));
    }

    font.setBold((frame.styleFlags & ReportStyleFlagBold) != 0);
    font.setItalic((frame.styleFlags & ReportStyleFlagItalic) != 0);
    font.setUnderline((frame.styleFlags & ReportStyleFlagUnderline) != 0);
    return font;
}

Qt::Alignment textAlignment(const ReportFrameDefinition& frame)
{
    Qt::Alignment alignment = Qt::AlignTop;
    if ((frame.styleFlags & ReportStyleFlagAlignRight) != 0) {
        alignment |= Qt::AlignRight;
    } else if ((frame.styleFlags & ReportStyleFlagAlignCenter) != 0) {
        alignment |= Qt::AlignHCenter;
    } else {
        alignment |= Qt::AlignLeft;
    }
    return alignment;
}

qreal framePadding(qreal scale)
{
    return std::clamp(scale * LogicalFramePadding, MinimumFramePadding, MaximumFramePadding);
}

Qt::PenStyle penStyleForFrame(const ReportFrameDefinition& frame)
{
    switch (std::clamp(frame.lineStyle, 0, 9)) {
    case 1:
    case 6:
        return Qt::DashLine;
    case 2:
    case 7:
        return Qt::DotLine;
    case 3:
        return Qt::DashDotLine;
    case 4:
        return Qt::DashDotDotLine;
    case 9:
        return Qt::NoPen;
    case 0:
    case 5:
    case 8:
    default:
        return Qt::SolidLine;
    }
}

qreal penWidthForFrame(const ReportFrameDefinition& frame, qreal scale)
{
    qreal width = std::clamp(scale * LogicalFrameLineWidth, MinimumFrameLineWidth, MaximumFrameLineWidth);
    switch (std::clamp(frame.lineStyle, 0, 9)) {
    case 5:
    case 6:
    case 7:
        width *= 1.8;
        break;
    case 8:
        width = MinimumFrameLineWidth;
        break;
    default:
        break;
    }
    return width;
}

Qt::BrushStyle brushStyleForFrame(const ReportFrameDefinition& frame)
{
    switch (std::clamp(frame.fillPattern, 0, 25)) {
    case 1:
        return Qt::SolidPattern;
    case 2:
        return Qt::Dense1Pattern;
    case 3:
        return Qt::Dense2Pattern;
    case 4:
        return Qt::Dense3Pattern;
    case 5:
        return Qt::Dense4Pattern;
    case 6:
        return Qt::Dense5Pattern;
    case 7:
        return Qt::Dense6Pattern;
    case 8:
        return Qt::Dense7Pattern;
    case 9:
    case 15:
    case 21:
        return Qt::HorPattern;
    case 10:
    case 16:
    case 22:
        return Qt::VerPattern;
    case 11:
    case 17:
    case 23:
        return Qt::CrossPattern;
    case 12:
    case 18:
    case 24:
        return Qt::BDiagPattern;
    case 13:
    case 19:
    case 25:
        return Qt::FDiagPattern;
    case 14:
    case 20:
        return Qt::DiagCrossPattern;
    case 0:
    default:
        return Qt::NoBrush;
    }
}

void drawLineOrBox(QPainter* painter, const QRectF& frameRect, const ReportFrameDefinition& frame, qreal scale)
{
    QPen pen(Qt::black);
    pen.setStyle(penStyleForFrame(frame));
    pen.setWidthF(penWidthForFrame(frame, scale));
    painter->setPen(pen);

    const bool horizontalLine = frame.lineBoxShape == 'h'
        || (frame.lineBoxShape != 'b'
            && frameRect.height() <= std::max<qreal>(MinimumLineFrameThreshold, scale * LogicalLineFrameThreshold)
            && frameRect.width() >= frameRect.height());
    const bool verticalLine = frame.lineBoxShape == 'v'
        || (frame.lineBoxShape != 'b'
            && frameRect.width() <= std::max<qreal>(MinimumLineFrameThreshold, scale * LogicalLineFrameThreshold)
            && frameRect.height() > frameRect.width());

    if (horizontalLine) {
        const qreal y = frameRect.center().y();
        painter->drawLine(QPointF(frameRect.left(), y), QPointF(frameRect.right(), y));
        return;
    }
    if (verticalLine) {
        const qreal x = frameRect.center().x();
        painter->drawLine(QPointF(x, frameRect.top()), QPointF(x, frameRect.bottom()));
        return;
    }

    const Qt::BrushStyle brushStyle = brushStyleForFrame(frame);
    painter->setBrush(brushStyle == Qt::NoBrush ? QBrush(Qt::NoBrush) : QBrush(QColor(220, 220, 220), brushStyle));
    const QRectF adjusted = frameRect.adjusted(0.0, 0.0, -pen.widthF(), -pen.widthF());
    const qreal radius = std::max<qreal>(0.0, frame.cornerRadius * scale);
    if (radius > 0.0) {
        painter->drawRoundedRect(adjusted, radius, radius);
    } else {
        painter->drawRect(adjusted);
    }
}

} // namespace

QString ReportPreviewRenderer::resolveFrameText(const ReportFrameDefinition& frame, const ReportPreviewData& data)
{
    QString text = frame.text;
    text = replaceDelimitedToken(text, '[', ']', data.fieldValues);
    text = replaceDelimitedToken(text, '{', '}', data.systemValues);
    return text;
}

void ReportPreviewRenderer::render(QPainter* painter, const ReportDefinition& report, const QRectF& target, const ReportPreviewData& data)
{
    if (painter == nullptr || target.isEmpty()) {
        return;
    }

    painter->save();
    painter->fillRect(target, Qt::white);
    painter->setClipRect(target);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    const qreal scale = logicalScale(report, target);
    const QRectF content = contentTarget(report, target);

    for (const ReportFrameDefinition& frame : report.frames) {
        const QRectF frameRect = scaleBounds(frame.bounds, report, content);
        if (frameRect.isEmpty()) {
            continue;
        }

        if (frame.kind == ReportFrameKind::LineOrBox) {
            drawLineOrBox(painter, frameRect, frame, scale);
            continue;
        }

        if (data.drawFrameOutlines) {
            painter->save();
            QPen outline(Qt::lightGray);
            outline.setStyle(Qt::DotLine);
            painter->setPen(outline);
            painter->drawRect(frameRect);
            painter->restore();
        }

        painter->save();
        painter->setClipRect(frameRect);
        painter->setFont(frameFont(painter, report, frame, scale));
        painter->setPen(Qt::black);
        const qreal padding = framePadding(scale);
        const QRectF textRect = frameRect.adjusted(padding, padding, -padding, -padding);
        const int flags = static_cast<int>(textAlignment(frame))
            | (frame.printEntireContentsFlag != 0 ? Qt::TextWordWrap : Qt::TextSingleLine);
        painter->drawText(
            textRect,
            flags,
            resolveFrameText(frame, data));
        painter->restore();
    }

    painter->restore();
}

} // namespace CardStack
