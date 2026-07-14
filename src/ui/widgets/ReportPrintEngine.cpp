#include "ReportPrintEngine.h"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>
#include <QTextLayout>

#include <algorithm>

namespace CardStack {
namespace {

constexpr quint16 ReportBodyBand = 0;
constexpr quint16 ReportHeaderBand = 1;
constexpr quint16 ReportFooterBand = 2;

int safeRows(const ReportDefinition& report)
{
    if (report.formType == ReportFormType::Card || report.formType == ReportFormType::Label) {
        // Legacy card/label reports store the across count first and the down count second.
        return std::max(1, report.columns);
    }
    return 1;
}

int safeColumns(const ReportDefinition& report)
{
    if (report.formType == ReportFormType::Card || report.formType == ReportFormType::Label) {
        return std::max(1, report.rows);
    }
    return 1;
}

QRectF scaleNormalizedCell(const QRectF& normalizedCell, const QRectF& target)
{
    return QRectF(
        target.left() + normalizedCell.left() * target.width(),
        target.top() + normalizedCell.top() * target.height(),
        normalizedCell.width() * target.width(),
        normalizedCell.height() * target.height());
}

ReportPreviewData dataForPage(ReportPreviewData data, int pageNumber)
{
    data.systemValues.insert(QStringLiteral("page"), QString::number(pageNumber));
    return data;
}

bool hasBand(const ReportDefinition& report, quint16 band)
{
    return std::any_of(report.frames.cbegin(), report.frames.cend(), [band](const ReportFrameDefinition& frame) {
        return frame.band == band;
    });
}

QRect bandBounds(const ReportDefinition& report, quint16 band)
{
    QRect bounds;
    for (const ReportFrameDefinition& frame : report.frames) {
        if (frame.band == band) {
            bounds = bounds.isNull() ? frame.bounds : bounds.united(frame.bounds);
        }
    }
    return bounds;
}

QVector<QString> continuationChunks(
    const QString& text,
    const ReportFontDefinition& fontDefinition,
    quint8 styleFlags,
    const QRect& frameBounds)
{
    if (text.isEmpty()) {
        return {QString()};
    }

    QFont font(fontDefinition.faceName);
    font.setPixelSize(std::max(1, std::abs(fontDefinition.legacyHeight)));
    font.setBold((styleFlags & ReportStyleFlagBold) != 0);
    font.setItalic((styleFlags & ReportStyleFlagItalic) != 0);
    font.setUnderline((styleFlags & ReportStyleFlagUnderline) != 0);
    QTextLayout layout(text, font);
    QVector<QPair<int, int>> lines;
    layout.beginLayout();
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) {
            break;
        }
        line.setLineWidth(std::max(1, frameBounds.width()));
        lines.append({line.textStart(), line.textLength()});
    }
    layout.endLayout();

    const QFontMetricsF metrics(font);
    const int linesPerSlot = std::max(1, static_cast<int>(frameBounds.height() / std::max<qreal>(1.0, metrics.lineSpacing())));
    QVector<QString> chunks;
    for (int firstLine = 0; firstLine < lines.size(); firstLine += linesPerSlot) {
        const int lastLine = std::min(firstLine + linesPerSlot, static_cast<int>(lines.size())) - 1;
        const int begin = lines.at(firstLine).first;
        const int end = lines.at(lastLine).first + lines.at(lastLine).second;
        chunks.append(text.mid(begin, end - begin));
    }
    return chunks.isEmpty() ? QVector<QString>{text} : chunks;
}

QVector<QMap<QString, QString>> recordContinuationSlots(
    const ReportDefinition& report,
    const ReportPreviewData& data)
{
    QVector<QMap<QString, QString>> continuationResults(1);
    for (const ReportFrameDefinition& frame : report.frames) {
        if (frame.band != ReportBodyBand || frame.kind != ReportFrameKind::Data
            || frame.printEntireContentsFlag == 0 || frame.fieldPlaceholders.size() != 1) {
            continue;
        }
        const QString fieldName = frame.fieldPlaceholders.first();
        const QVector<QString> chunks = continuationChunks(
            data.fieldValues.value(fieldName), report.dataFont, frame.styleFlags, frame.bounds);
        continuationResults.resize(std::max(continuationResults.size(), chunks.size()));
        for (int index = 0; index < continuationResults.size(); ++index) {
            continuationResults[index].insert(fieldName, index < chunks.size() ? chunks.at(index) : QString());
        }
    }
    return continuationResults;
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

} // namespace

QVector<ReportPrintPage> ReportPrintEngine::paginate(const ReportDefinition& report, int recordCount)
{
    QVector<ReportPrintPage> pages;
    if (recordCount <= 0) {
        return pages;
    }

    if (report.formType == ReportFormType::Report
        && (hasBand(report, ReportHeaderBand) || hasBand(report, ReportFooterBand))) {
        const QRect bodyBounds = bandBounds(report, ReportBodyBand);
        const QRect headerBounds = bandBounds(report, ReportHeaderBand);
        const QRect footerBounds = bandBounds(report, ReportFooterBand);
        const int bodyTop = bodyBounds.isNull() ? headerBounds.bottom() + 1 : bodyBounds.top();
        const int bodyHeight = std::max(1, bodyBounds.height());
        const int footerTop = footerBounds.isNull() ? report.formHeight : footerBounds.top();
        const int firstRowTop = std::max(bodyTop, headerBounds.isNull() ? bodyTop : headerBounds.bottom() + 1);
        const int rowsPerPage = std::max(1, (footerTop - firstRowTop) / bodyHeight);

        int recordIndex = 0;
        while (recordIndex < recordCount) {
            ReportPrintPage page;
            page.pageNumber = pages.size() + 1;
            page.usesReportBands = true;
            for (int row = 0; row < rowsPerPage && recordIndex < recordCount; ++row, ++recordIndex) {
                const int rowTop = firstRowTop + row * bodyHeight;
                page.cells.append({
                    recordIndex,
                    QRectF(0.0, static_cast<qreal>(rowTop) / std::max(1, report.formHeight), 1.0,
                        static_cast<qreal>(bodyHeight) / std::max(1, report.formHeight)),
                    rowTop - bodyTop,
                });
            }
            pages.append(std::move(page));
        }
        return pages;
    }

    const int rows = safeRows(report);
    const int columns = safeColumns(report);
    const int cellsPerPage = std::max(1, rows * columns);
    const qreal logicalWidth = std::max<qreal>(
        1.0,
        report.formWidth - report.marginLeft - report.marginRight);
    const qreal logicalHeight = std::max<qreal>(
        1.0,
        report.formHeight - report.marginTop - report.marginBottom);
    const qreal horizontalGutter = std::clamp(
        report.horizontalGutter / logicalWidth,
        0.0,
        columns <= 1 ? 0.0 : 1.0 / (columns - 1));
    const qreal verticalGutter = std::clamp(
        report.verticalGutter / logicalHeight,
        0.0,
        rows <= 1 ? 0.0 : 1.0 / (rows - 1));
    const qreal cellWidth = std::max<qreal>(0.01, (1.0 - horizontalGutter * (columns - 1)) / columns);
    const qreal cellHeight = std::max<qreal>(0.01, (1.0 - verticalGutter * (rows - 1)) / rows);

    int recordIndex = 0;
    while (recordIndex < recordCount) {
        ReportPrintPage page;
        page.pageNumber = pages.size() + 1;

        for (int cellIndex = 0; cellIndex < cellsPerPage && recordIndex < recordCount; ++cellIndex, ++recordIndex) {
            const int row = cellIndex / columns;
            const int column = cellIndex % columns;
            page.cells.append({
                recordIndex,
                QRectF(
                    column * (cellWidth + horizontalGutter),
                    row * (cellHeight + verticalGutter),
                    cellWidth,
                    cellHeight),
            });
        }

        pages.append(std::move(page));
    }

    return pages;
}

QVector<ReportPrintPage> ReportPrintEngine::paginate(
    const ReportDefinition& report,
    const QVector<ReportPreviewData>& records)
{
    if (report.formType != ReportFormType::Report) {
        return paginate(report, records.size());
    }

    struct Slot {
        int recordIndex = 0;
        QMap<QString, QString> overrides;
    };
    QVector<Slot> flattenedSlots;
    for (int recordIndex = 0; recordIndex < records.size(); ++recordIndex) {
        const QVector<QMap<QString, QString>> recordSlots = recordContinuationSlots(report, records.at(recordIndex));
        for (const QMap<QString, QString>& overrides : recordSlots) {
            flattenedSlots.append({recordIndex, overrides});
        }
    }

    QVector<ReportPrintPage> pages = paginate(report, flattenedSlots.size());
    for (ReportPrintPage& page : pages) {
        for (ReportPrintCell& cell : page.cells) {
            const int slotIndex = cell.recordIndex;
            if (slotIndex >= 0 && slotIndex < flattenedSlots.size()) {
                cell.recordIndex = flattenedSlots.at(slotIndex).recordIndex;
                cell.fieldValueOverrides = flattenedSlots.at(slotIndex).overrides;
            }
        }
    }
    return pages;
}

void ReportPrintEngine::renderPage(
    QPainter* painter,
    const ReportDefinition& report,
    const QVector<ReportPreviewData>& records,
    const ReportPrintPage& page,
    const QRectF& target)
{
    if (painter == nullptr || target.isEmpty()) {
        return;
    }

    painter->save();
    painter->fillRect(target, Qt::white);
    const QRectF content = contentTarget(report, target);
    ReportDefinition cellReport = report;
    cellReport.marginLeft = 0;
    cellReport.marginTop = 0;
    cellReport.marginRight = 0;
    cellReport.marginBottom = 0;
    cellReport.horizontalGutter = 0;
    cellReport.verticalGutter = 0;

    if (page.usesReportBands) {
        const int firstRecordIndex = page.cells.isEmpty() ? -1 : page.cells.first().recordIndex;
        ReportPreviewData pageData;
        if (firstRecordIndex >= 0 && firstRecordIndex < records.size()) {
            pageData = dataForPage(records.at(firstRecordIndex), page.pageNumber);
        } else {
            pageData = dataForPage({}, page.pageNumber);
        }
        ReportPreviewRenderer::renderBand(
            painter, cellReport, content, pageData, ReportHeaderBand);
        ReportPreviewRenderer::renderBand(
            painter, cellReport, content, pageData, ReportFooterBand);
        for (const ReportPrintCell& cell : page.cells) {
            if (cell.recordIndex >= 0 && cell.recordIndex < records.size()) {
                ReportPreviewData cellData = dataForPage(records.at(cell.recordIndex), page.pageNumber);
                for (auto iterator = cell.fieldValueOverrides.cbegin(); iterator != cell.fieldValueOverrides.cend(); ++iterator) {
                    cellData.fieldValues.insert(iterator.key(), iterator.value());
                }
                ReportPreviewRenderer::renderBand(
                    painter,
                    cellReport,
                    content,
                    cellData,
                    ReportBodyBand,
                    cell.logicalYOffset);
            }
        }
        painter->restore();
        return;
    }

    for (const ReportPrintCell& cell : page.cells) {
        if (cell.recordIndex < 0 || cell.recordIndex >= records.size()) {
            continue;
        }

        const QRectF cellTarget = scaleNormalizedCell(cell.target, content);
        painter->save();
        painter->setClipRect(cellTarget);
        ReportPreviewData cellData = dataForPage(records.at(cell.recordIndex), page.pageNumber);
        for (auto iterator = cell.fieldValueOverrides.cbegin(); iterator != cell.fieldValueOverrides.cend(); ++iterator) {
            cellData.fieldValues.insert(iterator.key(), iterator.value());
        }
        ReportPreviewRenderer::render(
            painter,
            cellReport,
            cellTarget.adjusted(2.0, 2.0, -2.0, -2.0),
            cellData);
        if (page.cells.size() > 1) {
            QPen cellPen(QColor(210, 210, 210));
            cellPen.setStyle(Qt::DotLine);
            painter->setPen(cellPen);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(cellTarget.adjusted(0.0, 0.0, -1.0, -1.0));
        }
        painter->restore();
    }

    painter->restore();
}

} // namespace CardStack
