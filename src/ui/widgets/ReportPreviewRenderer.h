#pragma once

#include "ReportDefinition.h"

#include <QMap>
#include <QRectF>
#include <QString>
#include <optional>

class QPainter;

namespace CardStack {

struct ReportPreviewData {
    QMap<QString, QString> fieldValues;
    QMap<QString, QString> systemValues;
    bool drawFrameOutlines = false;
    QMap<int, QString> frameTextOverrides;
};

enum class ReportFrameLayoutMode { PrintFlow, DesignerFrames };

struct ReportFrameLayout {
    int frameIndex = -1;
    QRectF bounds;
    QString text;
    quint8 styleFlags = 0;
};

class ReportPreviewRenderer {
public:
    static QVector<ReportFrameLayout> layoutFrames(const ReportDefinition& report,
        const ReportPreviewData& data, std::optional<quint16> band = {},
        ReportFrameLayoutMode mode = ReportFrameLayoutMode::PrintFlow);
    static QString resolveFrameText(const ReportFrameDefinition& frame, const ReportPreviewData& data);
    static void render(QPainter* painter, const ReportDefinition& report, const QRectF& target, const ReportPreviewData& data = {});
    static void renderDesigner(QPainter* painter, const ReportDefinition& report, const QRectF& target, const ReportPreviewData& data = {});
    static void renderBand(
        QPainter* painter,
        const ReportDefinition& report,
        const QRectF& target,
        const ReportPreviewData& data,
        quint16 band,
        int logicalYOffset = 0);
};

} // namespace CardStack
