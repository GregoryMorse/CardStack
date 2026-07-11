#include "ReportPrintEngine.h"

#include <QColor>
#include <QPainter>
#include <QPen>

#include <algorithm>

namespace CardStack {
namespace {

int safeRows(const ReportDefinition& report)
{
    if (report.formType == ReportFormType::Card || report.formType == ReportFormType::Label) {
        return std::max(1, report.rows);
    }
    return 1;
}

int safeColumns(const ReportDefinition& report)
{
    if (report.formType == ReportFormType::Card || report.formType == ReportFormType::Label) {
        return std::max(1, report.columns);
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

    for (const ReportPrintCell& cell : page.cells) {
        if (cell.recordIndex < 0 || cell.recordIndex >= records.size()) {
            continue;
        }

        const QRectF cellTarget = scaleNormalizedCell(cell.target, content);
        painter->save();
        painter->setClipRect(cellTarget);
        ReportPreviewRenderer::render(
            painter,
            cellReport,
            cellTarget.adjusted(2.0, 2.0, -2.0, -2.0),
            dataForPage(records.at(cell.recordIndex), page.pageNumber));
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
