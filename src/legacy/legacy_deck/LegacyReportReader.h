#pragma once

#include "ReportDefinition.h"
#include "ImportBudget.h"

#include <QByteArray>
#include <QString>
#include <QVector>

namespace CardStack {

class LegacyReportReader {
public:
    explicit LegacyReportReader(ImportLimits limits = {}) : m_limits(limits) {}
    struct Result {
        QVector<ReportDefinition> reports;
        QString errorMessage;

        bool ok() const;
    };

    Result readFile(const QString& filePath) const;
    Result readBytes(const QByteArray& bytes, ImportBudget* sharedBudget = nullptr) const;
private:
    ImportLimits m_limits;
};

} // namespace CardStack
