#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace CardStack {

class BtrieveFileSaverReader {
public:
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

        bool ok() const;
    };

    Result readAllRecords(const QString& filePath, qsizetype maxRecordBytes = 1024 * 1024) const;
};

} // namespace CardStack
