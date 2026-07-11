#include "BtrieveFileSaverReader.h"

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
