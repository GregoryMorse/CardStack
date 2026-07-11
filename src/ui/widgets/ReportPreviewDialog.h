#pragma once

#include "UiBuilder.h"
#include "ReportDefinition.h"
#include "ReportPrintEngine.h"
#include "ReportPreviewRenderer.h"

#include <QDialog>

#include <memory>

namespace CardStack {

class ReportPreviewDialog {
public:
    static std::unique_ptr<QDialog> create(
        QWidget* parent,
        const UiBuilder::DialogContext& context,
        const ReportDefinition& report,
        const QVector<ReportPreviewData>& records,
        const QVector<ReportPrintPage>& pages);

    static int exec(
        QWidget* parent,
        const UiBuilder::DialogContext& context,
        const ReportDefinition& report,
        const QVector<ReportPreviewData>& records,
        const QVector<ReportPrintPage>& pages);

    static std::unique_ptr<QDialog> create(
        QWidget* parent,
        const UiBuilder::DialogContext& context,
        const ReportDefinition& report,
        const ReportPreviewData& data = {});

    static int exec(
        QWidget* parent,
        const UiBuilder::DialogContext& context,
        const ReportDefinition& report,
        const ReportPreviewData& data = {});
};

} // namespace CardStack
