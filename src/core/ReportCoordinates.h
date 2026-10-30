#pragma once

#include "ReportDefinition.h"
#include <optional>

namespace CardStack::ReportCoordinates {
inline constexpr qreal MilsPerInch = 1000.0;
inline constexpr qreal SourcePixelsPerInch = 96.0;
inline constexpr int DesignerInsetPixels = 18;
inline constexpr int DescriptorSize = 0x9b;
inline constexpr quint16 DescriptorSignature = 0xabcd;
inline constexpr int DescriptorBoundsOffset = 0x08;
inline constexpr int DescriptorKindOffset = 0x8d;

inline QRect convert(const QRect& source, int inset)
{
    const auto edge = [inset](int value) { return qRound((value - inset) * MilsPerInch / SourcePixelsPerInch); };
    const int left = edge(source.left()), top = edge(source.top());
    return QRect(left, top, edge(source.left() + source.width()) - left,
        edge(source.top() + source.height()) - top);
}
inline QRect fromDesigner(const QRect& source) { return convert(source, DesignerInsetPixels); }
inline QRect previousUnadjusted(const QRect& source) { return convert(source, 0); }
inline QRect sourceFromPage(const QRect& page)
{
    const auto edge = [](int value) { return qRound(value * SourcePixelsPerInch / MilsPerInch) + DesignerInsetPixels; };
    const int left = edge(page.left()), top = edge(page.top());
    return QRect(left, top, edge(page.left() + page.width()) - left,
        edge(page.top() + page.height()) - top);
}
inline std::optional<QRect> descriptorBounds(const ReportFrameDefinition& frame)
{
    const auto& bytes = frame.legacyDescriptor;
    if (bytes.size() != DescriptorSize || frame.signature != DescriptorSignature) return std::nullopt;
    const auto word = [&bytes](int at) { return quint16(quint8(bytes.at(at))) | (quint16(quint8(bytes.at(at + 1))) << 8); };
    if (word(0) != DescriptorSignature) return std::nullopt;
    const char kind = bytes.at(DescriptorKindOffset);
    if ((kind == 'D' && frame.kind != ReportFrameKind::Data)
        || (kind == 'L' && frame.kind != ReportFrameKind::LineOrBox)
        || (kind == 'T' && frame.kind != ReportFrameKind::Text && frame.kind != ReportFrameKind::SystemText)
        || (kind != 'D' && kind != 'T' && kind != 'L')) return std::nullopt;
    const int left = qint16(word(DescriptorBoundsOffset));
    const int top = qint16(word(DescriptorBoundsOffset + 2));
    const int right = qint16(word(DescriptorBoundsOffset + 4));
    const int bottom = qint16(word(DescriptorBoundsOffset + 6));
    if (right < left || bottom < top) return std::nullopt;
    return QRect(left, top, right - left, bottom - top);
}
// Repair only a complete, unchanged old coordinate set. Edited designs remain untouched.
inline bool repairPreviousCoordinates(ReportDefinition* report, const QVector<ReportDefinition>& canonical)
{
    if (!report || report->frames.isEmpty()
        || (report->formatMagic != QLatin1String("RPT@#$A") && report->formatMagic != QLatin1String("RPT@#$B"))) return false;
    QVector<QRect> replacement;
    bool anyDescriptor = false;
    for (const auto& frame : report->frames) anyDescriptor |= !frame.legacyDescriptor.isEmpty();
    if (anyDescriptor) {
        for (const auto& frame : report->frames) {
            const auto raw = descriptorBounds(frame);
            if (!raw || frame.bounds != previousUnadjusted(*raw)) return false;
            replacement.append(fromDesigner(*raw));
        }
    } else {
        for (const auto& reference : canonical) {
            if (reference.name != report->name || reference.formatMagic != report->formatMagic
                || reference.entrySize != report->entrySize || reference.headerSize != report->headerSize
                || reference.legacyOffset != report->legacyOffset || reference.frames.size() != report->frames.size()) continue;
            bool matches = true;
            for (int i = 0; i < reference.frames.size(); ++i) {
                const auto& actual = report->frames.at(i);
                const auto& expected = reference.frames.at(i);
                matches &= actual.signature == DescriptorSignature && actual.legacyOffset == expected.legacyOffset
                    && actual.sourceId == expected.sourceId && actual.kind == expected.kind
                    && actual.bounds == previousUnadjusted(sourceFromPage(expected.bounds));
            }
            if (!matches) continue;
            for (const auto& frame : reference.frames) replacement.append(frame.bounds);
            break;
        }
    }
    if (replacement.size() != report->frames.size()) return false;
    for (int i = 0; i < replacement.size(); ++i) report->frames[i].bounds = replacement.at(i);
    classifyReportBands(report);
    return true;
}
} // namespace CardStack::ReportCoordinates
