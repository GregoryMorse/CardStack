#pragma once

#include "ReportDefinition.h"
#include "ReportPreviewRenderer.h"

#include <QRectF>
#include <QVector>

class QPainter;

namespace CardStack {

struct ReportPrintCell {
    int recordIndex = 0;
    QRectF target;
    int logicalYOffset = 0;
    QMap<QString, QString> fieldValueOverrides;
};

struct ReportPrintPage {
    int pageNumber = 1;
    QVector<ReportPrintCell> cells;
    bool usesReportBands = false;
};

class ReportPrintEngine {
public:
    static QVector<ReportPrintPage> paginate(const ReportDefinition& report, int recordCount);
    static QVector<ReportPrintPage> paginate(
        const ReportDefinition& report,
        const QVector<ReportPreviewData>& records);
    static void renderPage(
        QPainter* painter,
        const ReportDefinition& report,
        const QVector<ReportPreviewData>& records,
        const ReportPrintPage& page,
        const QRectF& target);
};

} // namespace CardStack
