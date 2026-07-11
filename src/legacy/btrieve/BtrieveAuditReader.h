#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace CardStack {

class BtrieveAuditReader {
public:
    enum class PageType {
        FileControlRecord,
        Data,
        Other
    };

    struct Page {
        int index = 0;
        qsizetype offset = 0;
        quint32 pageNumber = 0;
        quint16 usageCount = 0;
        PageType type = PageType::Other;
    };

    struct KeySpec {
        int index = 0;
        int fcrOffset = 0;
        quint32 rawEntryPointer = 0;
        quint32 rootPage = 0;
        quint32 uniqueValueCount = 0;
        quint16 flags = 0;
        quint16 totalSegmentLength = 0;
        quint16 keyLength = 0;
        quint16 maxItemsPerPage = 0;
        quint16 minItemsPerPage = 0;
        quint16 duplicatePointerOffset = 0;
        quint16 recordOffset = 0;
        quint16 segmentLength = 0;
    };

    struct Audit {
        int version = 0;
        int pageSize = 0;
        int pageCount = 0;
        int fixedRecordLength = 0;
        int physicalRecordLength = 0;
        int declaredRecordCount = 0;
        int keyCount = 0;
        int occupiedFixedRecordSlots = 0;
        bool oldFormat = false;
        bool owned = false;
        bool ownerDataEncrypted = false;
        bool variableRecordsAllowed = false;
        quint8 ownerFlags = 0;
        QByteArray ownerVerificationBytes;
        quint16 consistentFlag = 0;
        quint16 userFlags = 0;
        QString acsName;
        QVector<Page> pages;
        QVector<KeySpec> keySpecs;
    };

    struct Result {
        Audit audit;
        QString errorMessage;

        bool ok() const;
    };

    Result readFile(const QString& filePath) const;
};

} // namespace CardStack
