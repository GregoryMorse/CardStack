#pragma once

#include "ReportDefinition.h"

#include <QByteArray>
#include <QString>
#include <QVector>

namespace CardStack {

class LegacyReportReader {
public:
    struct Result {
        QVector<ReportDefinition> reports;
        QString errorMessage;

        bool ok() const;
    };

    Result readFile(const QString& filePath) const;
    Result readBytes(const QByteArray& bytes) const;
};

} // namespace CardStack
