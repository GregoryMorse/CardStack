#include "BtrieveFileSaverReader.h"

#include "BtrieveAuditReader.h"

#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QVector>

#include <algorithm>
#include <cstring>
#include <vector>

extern "C" {
#include "BtrieveFileSaverLib.h"
}

#ifdef bool
#undef bool
#endif
#ifdef true
#undef true
#endif
#ifdef false
#undef false
#endif

namespace CardStack {

namespace {

constexpr int OldFormatDataPageHeaderSize = 6;
constexpr int OldFormatEmptyRecordScanOffset = 8;

QString statusToMessage(unsigned short status)
{
    switch (status) {
    case NO_ERROR:
        return {};
    case IO_ERROR:
        return QStringLiteral("I/O error while reading Btrieve file.");
    case MEM_ERROR:
        return QStringLiteral("Out of memory while reading Btrieve file.");
    case CLIENT_CONNECTION_ERROR:
        return QStringLiteral("Btrieve reader was called without a valid client state.");
    case END_OF_FILE:
        return {};
    case DATA_BUFFER_TO_SHORT:
        return QStringLiteral("Record is larger than the configured migration buffer.");
    case NO_BTR_FILE:
        return QStringLiteral("File is not a supported Btrieve file.");
    default:
        return QStringLiteral("BtrieveFileSaver returned status %1.").arg(status);
    }
}

bool isOccupiedOldFormatRecordSlot(const QByteArray& bytes, int offset, int physicalLength)
{
    const int scanStart = offset + OldFormatEmptyRecordScanOffset;
    const int scanEnd = std::min(offset + physicalLength, static_cast<int>(bytes.size()));
    for (int index = scanStart; index < scanEnd; ++index) {
        if (bytes.at(index) != '\0') {
            return true;
        }
    }
    return false;
}

BtrieveFileSaverReader::Metadata metadataFromAudit(const BtrieveAuditReader::Audit& audit)
{
    BtrieveFileSaverReader::Metadata metadata;
    metadata.version = audit.version;
    metadata.pageSize = audit.pageSize;
    metadata.fixedRecordLength = audit.fixedRecordLength;
    metadata.internalFixedRecordLength = audit.physicalRecordLength;
    metadata.declaredRecordCount = audit.declaredRecordCount;
    metadata.dataPageCount = 0;
    metadata.variableRecordsAllowed = audit.variableRecordsAllowed;
    for (const BtrieveAuditReader::Page& page : audit.pages) {
        if (page.type == BtrieveAuditReader::PageType::Data) {
            ++metadata.dataPageCount;
        }
    }
    return metadata;
}

QVector<QByteArray> extractOldFormatFixedRecords(
    const QString& filePath,
    const BtrieveAuditReader::Audit& audit,
    qsizetype maxRecordBytes)
{
    QVector<QByteArray> records;
    if (audit.pageSize <= 0 || audit.physicalRecordLength <= 0 || audit.fixedRecordLength <= 0) {
        return records;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return records;
    }

    const QByteArray bytes = file.readAll();
    if (bytes.size() < audit.pageSize || bytes.size() % audit.pageSize != 0) {
        return records;
    }

    const int recordsPerPage = (audit.pageSize - 2) / audit.physicalRecordLength;
    const int recordLength = std::min<int>(
        static_cast<int>(std::max<qsizetype>(maxRecordBytes, 1)),
        std::min(audit.fixedRecordLength, audit.physicalRecordLength));
    for (const BtrieveAuditReader::Page& page : audit.pages) {
        if (page.type != BtrieveAuditReader::PageType::Data) {
            continue;
        }

        const int pageOffset = static_cast<int>(page.offset);
        for (int slot = 0; slot < recordsPerPage; ++slot) {
            const int recordOffset = pageOffset + OldFormatDataPageHeaderSize + slot * audit.physicalRecordLength;
            if (recordOffset < 0 || recordOffset + audit.physicalRecordLength > bytes.size()) {
                break;
            }
            if (!isOccupiedOldFormatRecordSlot(bytes, recordOffset, audit.physicalRecordLength)) {
                continue;
            }
            records.append(bytes.mid(recordOffset, recordLength));
        }
    }
    return records;
}

} // namespace

bool BtrieveFileSaverReader::Result::ok() const
{
    return status == NO_ERROR || status == END_OF_FILE;
}

BtrieveFileSaverReader::Result BtrieveFileSaverReader::readAllRecords(
    const QString& filePath,
    qsizetype maxRecordBytes) const
{
    Result result;

    const BtrieveAuditReader auditReader;
    const BtrieveAuditReader::Result audit = auditReader.readFile(filePath);
    if (audit.ok() && audit.audit.oldFormat) {
        result.metadata = metadataFromAudit(audit.audit);
        result.records = extractOldFormatFixedRecords(filePath, audit.audit, maxRecordBytes);
        if (!result.records.isEmpty()) {
            result.status = NO_ERROR;
            return result;
        }
    }

    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        result.status = IO_ERROR;
        result.errorMessage = QStringLiteral("File does not exist: %1").arg(filePath);
        return result;
    }

    const QByteArray nativePath = QFile::encodeName(fileInfo.absoluteFilePath());
    std::vector<char> mutablePath(nativePath.begin(), nativePath.end());
    mutablePath.push_back('\0');

    CLIENT_STRUCT client;
    std::memset(&client, 0, sizeof(client));

    result.status = BF_OPEN(&client, mutablePath.data());
    if (result.status != NO_ERROR) {
        result.errorMessage = statusToMessage(result.status);
        return result;
    }

    result.metadata.version = client.fVersion;
    result.metadata.pageSize = client.fPageSize;
    result.metadata.fixedRecordLength = client.FixRecLen;
    result.metadata.internalFixedRecordLength = client.IFixedRecLen;
    result.metadata.declaredRecordCount = static_cast<int>(client.numRecs);
    result.metadata.dataPageCount = static_cast<int>(client.numDATPages);
    result.metadata.variableRecordsAllowed = client.VarRecsAllowed != 0;

    const auto closeClient = qScopeGuard([&client] {
        BF_CLOSE(&client);
    });

    maxRecordBytes = std::max<qsizetype>(maxRecordBytes, 1);
    std::vector<char> buffer(static_cast<size_t>(maxRecordBytes));

    while (true) {
        unsigned long recordLength = static_cast<unsigned long>(buffer.size());
        result.status = BF_GET_REC(&client, buffer.data(), &recordLength);
        if (result.status == END_OF_FILE) {
            result.errorMessage.clear();
            break;
        }

        if (result.status != NO_ERROR) {
            result.errorMessage = statusToMessage(result.status);
            break;
        }

        result.records.append(QByteArray(buffer.data(), static_cast<qsizetype>(recordLength)));
    }

    return result;
}

} // namespace CardStack
