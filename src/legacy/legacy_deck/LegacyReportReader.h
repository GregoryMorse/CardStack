#pragma once

#include "ReportDefinition.h"
#include "ImportBudget.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
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

    Result readFile(const QString& filePath, const QStringList& fieldNames = {}) const;
    Result readBytes(const QByteArray& bytes, ImportBudget* sharedBudget = nullptr, const QStringList& fieldNames = {}) const;
private:
    ImportLimits m_limits;
};

} // namespace CardStack
