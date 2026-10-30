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

constexpr qreal BodyCollisionToleranceMils = 10.0;
constexpr quint8 AlignmentFlags = ReportStyleFlagAlignCenter | ReportStyleFlagAlignRight;

struct FlowGroup {
    QVector<int> frames;
    QRectF bounds;
    quint16 band = ReportBodyBand;
    int previous = -1;
};

bool hasVisibleText(const QString& text)
{
    return std::any_of(text.cbegin(), text.cend(), [](QChar ch) { return ch != QLatin1Char(' '); });
}

QVector<ReportFrameLayout> computeLayout(const ReportDefinition& report, const ReportPreviewData& data,
                                       std::optional<quint16> selectedBand, ReportFrameLayoutMode mode)
{
    QVector<int> indices;
    for (int i = 0; i < report.frames.size(); ++i)
        if (!selectedBand || report.frames.at(i).band == *selectedBand) indices.append(i);
    std::stable_sort(indices.begin(), indices.end(), [&](int left, int right) {
        const auto& a = report.frames.at(left); const auto& b = report.frames.at(right);
        const bool aLine = a.kind == ReportFrameKind::LineOrBox, bLine = b.kind == ReportFrameKind::LineOrBox;
        if (aLine != bLine) return aLine;
        if (a.bounds.top() != b.bounds.top()) return a.bounds.top() < b.bounds.top();
        return a.bounds.left() < b.bounds.left();
    });
    QVector<ReportFrameLayout> result;
    if (mode == ReportFrameLayoutMode::DesignerFrames) {
        for (int index : indices) {
            const auto& frame = report.frames.at(index);
            result.append({index, QRectF(frame.bounds),
                data.frameTextOverrides.value(index, ReportPreviewRenderer::resolveFrameText(frame, data)), frame.styleFlags});
        }
        return result;
    }
    QVector<FlowGroup> groups;
    for (int index : indices) {
        const auto& frame = report.frames.at(index);
        const QRectF bounds(frame.bounds);
        if (frame.kind == ReportFrameKind::LineOrBox) {
            result.append({index, bounds, {}, frame.styleFlags});
            continue;
        }
        bool joined = false;
        if (!groups.isEmpty()) {
            auto& group = groups.last();
            if (group.band == frame.band && group.bounds.top() == bounds.top()
                && group.bounds.bottom() == bounds.bottom() && group.bounds.right() == bounds.left()) {
                group.frames.append(index); group.bounds = group.bounds.united(bounds); joined = true;
            }
        }
        if (!joined) groups.append({{index}, bounds, frame.band, -1});
    }
    for (int i = 0; i < groups.size(); ++i) {
        auto& group = groups[i];
        for (int previous = 0; previous < i; ++previous) {
            const auto& candidate = groups.at(previous);
            if (candidate.band == group.band && candidate.bounds.bottom() == group.bounds.top()
                && candidate.bounds.left() < group.bounds.right() && group.bounds.left() < candidate.bounds.right()) {
                group.previous = previous; break;
            }
        }
    }
    QMap<quint16, QVector<QRectF>> occupied;
    for (const auto& group : groups) {
        QRectF bounds = group.bounds;
        const qreal tolerance = group.band == ReportBodyBand ? BodyCollisionToleranceMils : 0.0;
        for (int previous = group.previous; previous >= 0; previous = groups.at(previous).previous) {
            QRectF candidate = bounds; candidate.moveTop(groups.at(previous).bounds.top());
            bool collision = false;
            for (const auto& used : occupied.value(group.band)) {
                collision |= candidate.right() - tolerance > used.left() && used.right() > candidate.left() + tolerance
                    && candidate.bottom() - tolerance > used.top() && used.bottom() > candidate.top() + tolerance;
            }
            if (collision) break;
            bounds = candidate;
        }
        const auto textFor = [&](int index) {
            return data.frameTextOverrides.value(index, ReportPreviewRenderer::resolveFrameText(report.frames.at(index), data));
        };
        const auto fontFor = [&](int index) -> const ReportFontDefinition& {
            return report.frames.at(index).kind == ReportFrameKind::Data ? report.dataFont : report.textFont;
        };
        const quint8 alignment = report.frames.at(group.frames.first()).styleFlags & AlignmentFlags;
        const bool right = (alignment & ReportStyleFlagAlignRight) != 0;
        qreal left = bounds.left(), end = bounds.right();
        if (!right && (alignment & ReportStyleFlagAlignCenter) != 0) {
            qreal used = 0.0;
            for (int index : group.frames) {
                const QRectF remaining(left + used, bounds.top(), std::max<qreal>(0.0, bounds.width() - used), bounds.height());
                used += ReportTextLayout::extent(textFor(index), fontFor(index), report.frames.at(index).styleFlags, remaining).width();
            }
            left += std::max<qreal>(0.0, (bounds.width() - used) / 2.0);
        }
        bool populated = false;
        for (int position = 0; position < group.frames.size(); ++position) {
            const int member = right ? group.frames.size() - position - 1 : position;
            const int index = group.frames.at(member);
            const auto& frame = report.frames.at(index);
            const QString text = textFor(index);
            populated |= hasVisibleText(text);
            const QRectF remaining(left, bounds.top(), std::max<qreal>(0.0, end - left), bounds.height());
            const quint8 flags = (frame.styleFlags & ~AlignmentFlags) | (right ? ReportStyleFlagAlignRight : 0);
            const qreal advance = ReportTextLayout::extent(text, fontFor(index), flags, remaining).width();
            result.append({index, remaining, text, flags});
            if (right) end -= advance; else left += advance;
        }
        if (populated) occupied[group.band].append(bounds);
    }
    return result;
}

void renderFrames(QPainter* painter, const ReportDefinition& report, const QRectF& target,
                  const ReportPreviewData& data, const std::optional<quint16>& selectedBand,
                  int logicalYOffset, bool clearBackground, ReportFrameLayoutMode mode = ReportFrameLayoutMode::PrintFlow)
{
    if (!painter || target.isEmpty() || !validateReportDefinition(report)) return;
    const auto layout = ReportPreviewRenderer::layoutFrames(report, data, selectedBand, mode);
    painter->save();
    if (clearBackground) painter->fillRect(target, Qt::white);
    painter->setClipRect(target);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);
    painter->translate(target.topLeft());
    painter->scale(target.width() / std::max(1, report.formWidth), target.height() / std::max(1, report.formHeight));
    for (const auto& item : layout) {
        const auto& frame = report.frames.at(item.frameIndex);
        const QRectF bounds = item.bounds.translated(0, logicalYOffset);
        if (bounds.isEmpty()) continue;
        if (frame.kind == ReportFrameKind::LineOrBox) { FrameDrawingStyle::draw(*painter, bounds, frame); continue; }
        if (data.drawFrameOutlines) {
            painter->save(); painter->setPen(QPen(Qt::lightGray, 1.0, Qt::DotLine)); painter->drawRect(bounds); painter->restore();
        }
        painter->setPen(Qt::black);
        const auto& font = frame.kind == ReportFrameKind::Data ? report.dataFont : report.textFont;
        const bool wrap = mode == ReportFrameLayoutMode::PrintFlow || frame.printEntireContentsFlag != 0;
        ReportTextLayout::draw(*painter, item.text, font, item.styleFlags, wrap, bounds);
    }
    painter->restore();
}

} // namespace

QVector<ReportFrameLayout> ReportPreviewRenderer::layoutFrames(const ReportDefinition& report,
    const ReportPreviewData& data, std::optional<quint16> band, ReportFrameLayoutMode mode)
{
    if (!validateReportDefinition(report)) return {};
    return computeLayout(report, data, band, mode);
}

QString ReportPreviewRenderer::resolveFrameText(const ReportFrameDefinition& frame, const ReportPreviewData& data)
{
    QString text = frame.text;
    if (frame.kind == ReportFrameKind::Data) {
        const int begin = text.indexOf(QLatin1Char('['));
        const int end = begin < 0 ? -1 : text.indexOf(QLatin1Char(']'), begin + 1);
        if (end > begin) {
            const QString name = matchingReportFieldName(text.mid(begin + 1, end - begin - 1).trimmed(), data.fieldValues.keys());
            const QString value = data.fieldValues.value(name);
            // A blank primary field suppresses its entire caption, including any prefix.
            if (std::all_of(value.cbegin(), value.cend(), [](QChar ch) { return ch == QLatin1Char(' '); })) return {};
        }
    }
    if (frame.kind == ReportFrameKind::Data)
        text = replaceDelimitedToken(text, '[', ']', data.fieldValues);
    text = replaceDelimitedToken(text, '{', '}', data.systemValues);
    return text;
}

void ReportPreviewRenderer::render(QPainter* painter, const ReportDefinition& report, const QRectF& target, const ReportPreviewData& data)
{
    renderFrames(painter, report, target, data, std::nullopt, 0, true);
}

void ReportPreviewRenderer::renderDesigner(QPainter* painter, const ReportDefinition& report, const QRectF& target, const ReportPreviewData& data)
{
    renderFrames(painter, report, target, data, std::nullopt, 0, true, ReportFrameLayoutMode::DesignerFrames);
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
