#include "ReportPrintEngine.h"
#include "ReportTextLayout.h"
#include <QPageLayout>
#include <QPageSize>

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>
#include <QTextLayout>

#include <algorithm>

namespace CardStack {
namespace {

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

struct ContinuationSlot {
    QMap<QString, QString> fields;
    QMap<int, QString> frames;
};

QVector<ContinuationSlot> recordContinuationSlots(const ReportDefinition& report, const ReportPreviewData& data)
{
    struct Field { int frameIndex; QString name; QVector<QString> chunks; };
    QVector<Field> fields;
    qsizetype count = 1;
    for (int index = 0; index < report.frames.size(); ++index) {
        const auto& frame = report.frames.at(index);
        if (frame.band != ReportBodyBand || frame.kind != ReportFrameKind::Data
            || frame.printEntireContentsFlag == 0) continue;
        const auto chunks = ReportTextLayout::chunks(ReportPreviewRenderer::resolveFrameText(frame, data),
            report.dataFont, frame.styleFlags, frame.bounds);
        fields.append({index, frame.fieldPlaceholders.size() == 1 ? frame.fieldPlaceholders.first() : QString(), chunks});
        count = std::max(count, chunks.size());
    }
    QVector<ContinuationSlot> result(count);
    for (const auto& field : fields) {
        for (qsizetype index = 0; index < count; ++index) {
            const QString chunk = index < field.chunks.size() ? field.chunks.at(index) : QString();
            result[index].frames.insert(field.frameIndex, chunk);
            if (!field.name.isEmpty()) result[index].fields.insert(field.name, chunk);
        }
    }
    return result;
}

QRectF contentTarget(const ReportDefinition& report, const QRectF& target)
{
    const qreal width = std::max(1.0, ReportPrintEngine::pageSizeMils(report).width());
    const qreal height = std::max(1.0, ReportPrintEngine::pageSizeMils(report).height());
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

QSizeF ReportPrintEngine::pageSizeMils(const ReportDefinition& report)
{
    QSizeF size(report.pageWidth > 0 ? report.pageWidth : std::max(1, report.formWidth),
                report.pageHeight > 0 ? report.pageHeight : std::max(1, report.formHeight));
    if (report.orientation != 0 && size.width() < size.height()) size.transpose();
    return size;
}

QPageLayout ReportPrintEngine::pageLayout(const ReportDefinition& report)
{
    const QSizeF dimensions = pageSizeMils(report);
    const bool landscape = report.orientation != 0 || dimensions.width() > dimensions.height();
    const QSizeF portrait(std::min(dimensions.width(), dimensions.height()) / 1000.0,
                          std::max(dimensions.width(), dimensions.height()) / 1000.0);
    QPageLayout layout(QPageSize(portrait, QPageSize::Inch, QString(), QPageSize::ExactMatch),
                       landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
                       QMarginsF(), QPageLayout::Inch);
    layout.setMode(QPageLayout::FullPageMode);
    return layout;
}

QVector<ReportPrintPage> ReportPrintEngine::paginate(const ReportDefinition& report, int recordCount)
{
    QVector<ReportPrintPage> pages;
    if (recordCount <= 0) {
        return pages;
    }

    if (report.formType == ReportFormType::Report) {
        const QRect bodyBounds = bandBounds(report, ReportBodyBand);
        const QRect headerBounds = bandBounds(report, ReportHeaderBand);
        const QRect footerBounds = bandBounds(report, ReportFooterBand);
        const int bodyTop = bodyBounds.isNull() ? headerBounds.bottom() + 1 : bodyBounds.top();
        const int bodyHeight = std::max(1, report.headerHeight > 0
            ? bodyBounds.bottom() + 1 - report.headerHeight : bodyBounds.height());
        const int footerTop = report.footerHeight > 0 ? report.formHeight - report.footerHeight
            : footerBounds.isNull() ? report.formHeight : footerBounds.top();
        const int firstRowTop = report.headerHeight > 0 ? report.headerHeight
            : std::max(bodyTop, headerBounds.isNull() ? bodyTop : headerBounds.bottom() + 1);
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
                    report.headerHeight > 0 ? row * bodyHeight : rowTop - bodyTop,
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
        pageSizeMils(report).width() - report.marginLeft - report.marginRight);
    const qreal logicalHeight = std::max<qreal>(
        1.0,
        pageSizeMils(report).height() - report.marginTop - report.marginBottom);
    const qreal horizontalGutter = std::clamp(
        report.horizontalGutter / logicalWidth,
        0.0,
        columns <= 1 ? 0.0 : 1.0 / (columns - 1));
    const qreal verticalGutter = std::clamp(
        report.verticalGutter / logicalHeight,
        0.0,
        rows <= 1 ? 0.0 : 1.0 / (rows - 1));
    const bool physicalGrid = (report.formType == ReportFormType::Card || report.formType == ReportFormType::Label)
        && report.pageWidth > 0 && report.pageHeight > 0;
    const qreal cellWidth = physicalGrid ? report.formWidth / logicalWidth
        : std::max<qreal>(0.01, (1.0 - horizontalGutter * (columns - 1)) / columns);
    const qreal cellHeight = physicalGrid ? report.formHeight / logicalHeight
        : std::max<qreal>(0.01, (1.0 - verticalGutter * (rows - 1)) / rows);

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
        ContinuationSlot overrides;
    };
    QVector<Slot> flattenedSlots;
    for (int recordIndex = 0; recordIndex < records.size(); ++recordIndex) {
        const auto recordSlots = recordContinuationSlots(report, records.at(recordIndex));
        for (const auto& overrides : recordSlots) {
            flattenedSlots.append({recordIndex, overrides});
        }
    }

    QVector<ReportPrintPage> pages = paginate(report, flattenedSlots.size());
    for (ReportPrintPage& page : pages) {
        for (ReportPrintCell& cell : page.cells) {
            const int slotIndex = cell.recordIndex;
            if (slotIndex >= 0 && slotIndex < flattenedSlots.size()) {
                cell.recordIndex = flattenedSlots.at(slotIndex).recordIndex;
                cell.fieldValueOverrides = flattenedSlots.at(slotIndex).overrides.fields;
                cell.frameTextOverrides = flattenedSlots.at(slotIndex).overrides.frames;
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
                cellData.frameTextOverrides = cell.frameTextOverrides;
                if (cell.frameTextOverrides.isEmpty())
                    for (auto iterator = cell.fieldValueOverrides.cbegin(); iterator != cell.fieldValueOverrides.cend(); ++iterator)
                        cellData.fieldValues.insert(iterator.key(), iterator.value());
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
        cellData.frameTextOverrides = cell.frameTextOverrides;
        if (cell.frameTextOverrides.isEmpty())
            for (auto iterator = cell.fieldValueOverrides.cbegin(); iterator != cell.fieldValueOverrides.cend(); ++iterator)
                cellData.fieldValues.insert(iterator.key(), iterator.value());
        ReportPreviewRenderer::render(
            painter,
            cellReport,
            cellTarget,
            cellData);

        painter->restore();
    }

    painter->restore();
}

} // namespace CardStack
