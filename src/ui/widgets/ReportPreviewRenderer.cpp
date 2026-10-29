#include "ReportPreviewRenderer.h"
#include "FrameDrawingStyle.h"
#include "ReportTextLayout.h"
#include "ReportFieldBindings.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <optional>

namespace CardStack {
namespace {

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
        QString valueKey;
        if (open == QLatin1Char('[')) {
            valueKey = matchingReportFieldName(key, values.keys());
        } else {
            for (auto it = values.cbegin(); it != values.cend(); ++it)
                if (it.key().compare(key, Qt::CaseInsensitive) == 0) { valueKey = it.key(); break; }
        }
        const QString replacement = valueKey.isEmpty() ? QString() : values.value(valueKey);
        text.replace(begin, end - begin + 1, replacement);
        searchFrom = begin + replacement.size();
    }
    return text;
}

void renderFrames(
    QPainter* painter,
    const ReportDefinition& report,
    const QRectF& target,
    const ReportPreviewData& data,
    const std::optional<quint16>& selectedBand,
    int logicalYOffset,
    bool clearBackground)
{
    if (painter == nullptr || target.isEmpty() || !validateReportDefinition(report)) {
        return;
    }

    painter->save();
    if (clearBackground) {
        painter->fillRect(target, Qt::white);
    }
    painter->setClipRect(target);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    const qreal width = std::max(1, report.formWidth);
    const qreal height = std::max(1, report.formHeight);
    painter->translate(target.topLeft());
    painter->scale(target.width() / width, target.height() / height);
    QVector<const ReportFrameDefinition*> ordered;
    for (const auto& frame : report.frames) ordered.append(&frame);
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
        const bool leftBackground = left->kind == ReportFrameKind::LineOrBox;
        const bool rightBackground = right->kind == ReportFrameKind::LineOrBox;
        return leftBackground && !rightBackground;
    });
    for (const auto* definition : ordered) {
        const ReportFrameDefinition& frame = *definition;
        if (selectedBand.has_value() && frame.band != selectedBand.value()) continue;
        const QRectF frameRect = QRectF(frame.bounds).translated(0, logicalYOffset);
        if (frameRect.isEmpty()) continue;
        if (frame.kind == ReportFrameKind::LineOrBox) {
            FrameDrawingStyle::draw(*painter, frameRect, frame);
            continue;
        }
        if (data.drawFrameOutlines) {
            painter->save();
            painter->setPen(QPen(Qt::lightGray, 1.0, Qt::DotLine));
            painter->drawRect(frameRect);
            painter->restore();
        }
        painter->setPen(Qt::black);
        const ReportFontDefinition& font = frame.kind == ReportFrameKind::Data ? report.dataFont : report.textFont;
        const int frameIndex = static_cast<int>(definition - report.frames.constData());
        const QString text = data.frameTextOverrides.value(frameIndex, ReportPreviewRenderer::resolveFrameText(frame, data));
        ReportTextLayout::draw(*painter, text, font, frame.styleFlags, frame.printEntireContentsFlag != 0, frameRect);
    }
    painter->restore();
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
    renderFrames(painter, report, target, data, std::nullopt, 0, true);
}

void ReportPreviewRenderer::renderBand(
    QPainter* painter,
    const ReportDefinition& report,
    const QRectF& target,
    const ReportPreviewData& data,
    quint16 band,
    int logicalYOffset)
{
    renderFrames(painter, report, target, data, band, logicalYOffset, false);
}

} // namespace CardStack
