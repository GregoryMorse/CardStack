#pragma once

#include "../../core/ImportBudget.h"

#include <QByteArray>
#include <QString>
#include <QVector>

namespace CardStack {

class BtrieveFileSaverReader {
public:
    explicit BtrieveFileSaverReader(ImportLimits limits = {}) : m_limits(limits) {}
    struct Metadata {
        int version = 0;
        int pageSize = 0;
        int fixedRecordLength = 0;
        int internalFixedRecordLength = 0;
        int declaredRecordCount = 0;
        int dataPageCount = 0;
        bool variableRecordsAllowed = false;
    };

    struct Result {
        QVector<QByteArray> records;
        Metadata metadata;
        QString errorMessage;
        unsigned short status = 0;
        bool processingAborted = false;

        bool ok() const;
    };

    Result readAllRecords(const QString& filePath, qsizetype maxRecordBytes = qsizetype(MaximumNativeRecordBytes),
                          ImportBudget* sharedBudget = nullptr) const;
    Result readMemory(const QByteArray& bytes, qsizetype maxRecordBytes = qsizetype(MaximumNativeRecordBytes),
                      ImportBudget* sharedBudget = nullptr) const;
private:
    ImportLimits m_limits;
};

} // namespace CardStack
