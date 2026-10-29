#include "ReportDefinition.h"
#include <QCoreApplication>
#include <algorithm>
#include <cstdlib>

namespace CardStack {

bool validateReportFont(const ReportFontDefinition& font)
{
    const auto signedFontUnit = [](int value) {
        return std::abs(qint64(value)) <= MaximumReportFontUnits;
    };
    return signedFontUnit(font.legacyHeight) && signedFontUnit(font.width)
        && signedFontUnit(font.escapementTenths) && signedFontUnit(font.orientationTenths)
        && font.weight >= 0 && font.weight <= 1000;
}

bool validateReportDefinition(const ReportDefinition& report, QString* errorMessage)
{
    const auto reject = [&](const QString& message) {
        if (errorMessage) *errorMessage = message;
        return false;
    };
    for (int value : {report.formWidth, report.formHeight, report.headerHeight, report.footerHeight,
                      report.marginLeft, report.marginTop, report.marginRight, report.marginBottom,
                      report.horizontalGutter, report.verticalGutter, report.pageWidth, report.pageHeight}) {
        if (value < 0 || value > MaximumReportGeometryUnits)
            return reject(QCoreApplication::translate("ReportDefinition", "The report has unsupported dimensions or margins."));
    }
    if (report.rows < 0 || report.columns < 0
        || report.rows > MaximumReportGridCount || report.columns > MaximumReportGridCount)
        return reject(QCoreApplication::translate("ReportDefinition", "The report has an unsupported number of rows or columns."));
    if (!validateReportFont(report.dataFont) || !validateReportFont(report.textFont))
        return reject(QCoreApplication::translate("ReportDefinition", "The report contains unsupported font dimensions or attributes."));
    for (const auto& frame : report.frames) {
        if (std::abs(qint64(frame.bounds.x())) > MaximumReportGeometryUnits
            || std::abs(qint64(frame.bounds.y())) > MaximumReportGeometryUnits
            || frame.bounds.width() < 0 || frame.bounds.height() < 0
            || frame.bounds.width() > MaximumReportGeometryUnits || frame.bounds.height() > MaximumReportGeometryUnits
            || frame.cornerRadius < 0 || frame.cornerRadius > MaximumReportGeometryUnits)
            return reject(QCoreApplication::translate("ReportDefinition", "A report frame has unsupported coordinates or dimensions."));
    }
    return true;
}
} // namespace CardStack
