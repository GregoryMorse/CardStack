#pragma once

#include "ReportDefinition.h"

#include <QMap>
#include <QRectF>
#include <QString>

class QPainter;

namespace CardStack {

struct ReportPreviewData {
    QMap<QString, QString> fieldValues;
    QMap<QString, QString> systemValues;
    bool drawFrameOutlines = false;
};

class ReportPreviewRenderer {
public:
    static QString resolveFrameText(const ReportFrameDefinition& frame, const ReportPreviewData& data);
    static void render(QPainter* painter, const ReportDefinition& report, const QRectF& target, const ReportPreviewData& data = {});
    static void renderBand(
        QPainter* painter,
        const ReportDefinition& report,
        const QRectF& target,
        const ReportPreviewData& data,
        quint16 band,
        int logicalYOffset = 0);
};

} // namespace CardStack
