#include "BtrieveFileSaverReader.h"

#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QMutex>
#include <QMutexLocker>
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
    return !processingAborted && (status == NO_ERROR || status == END_OF_FILE);
}

namespace {
int nativeProgress(void* context, uint64_t units)
{
    return static_cast<ImportBudget*>(context)->checkpoint(units) ? 1 : 0;
}
}

BtrieveFileSaverReader::Result BtrieveFileSaverReader::readAllRecords(
    const QString& filePath, qsizetype maxRecordBytes, ImportBudget* sharedBudget) const
{
    ImportBudget local(m_limits);
    ImportBudget& budget = sharedBudget ? *sharedBudget : local;
    QByteArray bytes;
    QString error;
    if (!readBoundedFile(filePath, &bytes, budget, &error)) {
        Result result;
        result.status = budget.failed() ? PROCESSING_LIMIT : IO_ERROR;
        result.processingAborted = budget.failed();
        result.errorMessage = error;
        return result;
    }
    return readMemory(bytes, maxRecordBytes, &budget);
}

BtrieveFileSaverReader::Result BtrieveFileSaverReader::readMemory(
    const QByteArray& bytes, qsizetype maxRecordBytes, ImportBudget* sharedBudget) const
{
    ImportBudget local(m_limits);
    ImportBudget& budget = sharedBudget ? *sharedBudget : local;
    Result result;
    const auto aborted = [&]() {
        result.status = PROCESSING_LIMIT;
        result.processingAborted = true;
        result.errorMessage = budget.failed() ? budget.error() : QStringLiteral("Btrieve processing exceeded a configured limit.");
        result.records.clear();
        return result;
    };
    if (!budget.input(quint64(bytes.size()))) return aborted();

    static QMutex nativeReaderMutex;
    while (!nativeReaderMutex.tryLock(50))
        if (!budget.checkpoint()) return aborted();
    const auto unlock = qScopeGuard([&] { nativeReaderMutex.unlock(); });
    CLIENT_STRUCT client{};
    const BF_READ_OPTIONS options{budget.limits().maximumInputBytes, nativeProgress, &budget};
    result.status = BF_OPEN_MEMORY(&client, bytes.constData(), uint64_t(bytes.size()), &options);
    if (result.status != NO_ERROR) {
        if (budget.failed() || result.status == PROCESSING_LIMIT) return aborted();
        result.errorMessage = statusToMessage(result.status);
        return result;
    }
    const auto close = qScopeGuard([&] { BF_CLOSE(&client); });
    if (!budget.rowsFit(client.numRecs)) return aborted();

    result.metadata.version = client.fVersion;
    result.metadata.pageSize = client.fPageSize;
    result.metadata.fixedRecordLength = client.FixRecLen;
    result.metadata.internalFixedRecordLength = client.IFixedRecLen;
    result.metadata.declaredRecordCount = int(client.numRecs);
    result.metadata.dataPageCount = int(client.numDATPages);
    result.metadata.variableRecordsAllowed = client.VarRecsAllowed != 0;

    maxRecordBytes = std::clamp<qsizetype>(maxRecordBytes, 1, qsizetype(MaximumNativeRecordBytes));
    std::vector<char> buffer(size_t(maxRecordBytes), '\0');
    const qsizetype physicalLength = std::max<qsizetype>(1, client.IFixedRecLen);
    const qsizetype traversalLimit = client.numRecs > 0
        ? qsizetype(client.numRecs) : std::max<qsizetype>(1, bytes.size() / physicalLength + 1);
    while (result.records.size() < traversalLimit) {
        if (!budget.checkpoint()) return aborted();
        unsigned long length = static_cast<unsigned long>(buffer.size());
        result.status = BF_GET_REC(&client, buffer.data(), &length);
        if (result.status == END_OF_FILE) break;
        if (budget.failed() || result.status == PROCESSING_LIMIT) return aborted();
        if (result.status != NO_ERROR || length > buffer.size()) {
            if (length > buffer.size()) result.status = DATA_BUFFER_TO_SHORT;
            result.errorMessage = statusToMessage(result.status);
            return result;
        }
        if (!budget.row() || !budget.decoded(length) || !budget.checkpoint(length)) return aborted();
        result.records.append(QByteArray(buffer.data(), qsizetype(length)));
    }
    if (result.status == NO_ERROR) {
        if (client.numRecs > 0) result.status = END_OF_FILE;
        else {
            result.status = IO_ERROR;
            result.errorMessage = QStringLiteral("Btrieve traversal did not terminate within the physical file bound.");
        }
    }
    return result;
}

} // namespace CardStack
