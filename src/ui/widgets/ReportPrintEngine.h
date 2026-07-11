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
};

struct ReportPrintPage {
    int pageNumber = 1;
    QVector<ReportPrintCell> cells;
};

class ReportPrintEngine {
public:
    static QVector<ReportPrintPage> paginate(const ReportDefinition& report, int recordCount);
    static void renderPage(
        QPainter* painter,
        const ReportDefinition& report,
        const QVector<ReportPreviewData>& records,
        const ReportPrintPage& page,
        const QRectF& target);
};

} // namespace CardStack
