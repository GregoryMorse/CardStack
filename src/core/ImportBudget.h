#pragma once

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <algorithm>
#include <atomic>
#include <limits>

namespace CardStack {

inline constexpr quint64 ImportMebibyte = 1024 * 1024;
inline constexpr quint64 DefaultMaximumImportBytes = 64 * ImportMebibyte;
inline constexpr quint64 DefaultMaximumDecodedBytes = 128 * ImportMebibyte;
inline constexpr quint64 DefaultMaximumImportWork = 512 * ImportMebibyte;
inline constexpr quint64 MaximumNativeRecordBytes = ImportMebibyte;
inline constexpr quint64 ImportRowOverheadBytes = 128;
inline constexpr quint64 ImportValueOverheadBytes = 64;
inline constexpr quint64 ImportFrameOverheadBytes = 512;
inline constexpr qint64 ImportReadChunkBytes = 64 * 1024;

struct ImportLimits {
    quint64 maximumInputBytes = DefaultMaximumImportBytes;
    quint64 maximumDecodedBytes = DefaultMaximumDecodedBytes;
    quint64 maximumRows = 1000000;
    quint64 maximumValues = 1000000;
    quint64 maximumFields = 1024;
    quint64 maximumFrames = 65536;
    quint64 maximumCellBytes = 8 * ImportMebibyte;
    quint64 maximumWorkUnits = DefaultMaximumImportWork;
    qint64 maximumMilliseconds = 10000;
    const std::atomic_bool* cancellation = nullptr;
};

inline bool containsByteRange(qsizetype size, quint64 offset, quint64 length)
{
    return size >= 0 && offset <= quint64(size) && length <= quint64(size) - offset;
}

class ImportBudget {
public:
    enum class Failure { None, Input, Decoded, Rows, Values, Fields, Frames, Cell, Work, Deadline, Cancelled };
    explicit ImportBudget(ImportLimits limits = {}) : m_limits(limits) { m_clock.start(); }
    const ImportLimits& limits() const { return m_limits; }
    Failure failure() const { return m_failure; }
    bool failed() const { return m_failure != Failure::None; }
    bool reject(Failure failure)
    {
        if (!failed()) m_failure = failure;
        return false;
    }
    bool checkpoint(quint64 units = 0)
    {
        if (failed()) return false;
        if (m_limits.cancellation && m_limits.cancellation->load(std::memory_order_relaxed))
            return reject(Failure::Cancelled);
        if (m_limits.maximumMilliseconds < 0 || m_clock.elapsed() >= m_limits.maximumMilliseconds)
            return reject(Failure::Deadline);
        return take(m_work, units, m_limits.maximumWorkUnits, Failure::Work);
    }
    bool input(quint64 bytes)
    {
        return checkpoint() && ((bytes <= m_limits.maximumInputBytes && bytes <= quint64((std::numeric_limits<int>::max)())) || reject(Failure::Input));
    }
    bool decoded(quint64 bytes)
    {
        return checkpoint() && take(m_decoded, bytes, m_limits.maximumDecodedBytes, Failure::Decoded);
    }
    bool fields(quint64 count)
    {
        return checkpoint() && (count <= m_limits.maximumFields || reject(Failure::Fields));
    }
    bool cell(quint64 bytes)
    {
        return checkpoint() && (bytes <= m_limits.maximumCellBytes || reject(Failure::Cell));
    }
    bool value(quint64 bytes)
    {
        return cell(bytes) && take(m_values, 1, m_limits.maximumValues, Failure::Values)
            && decoded(ImportValueOverheadBytes) && decoded(bytes);
    }
    bool row()
    {
        return checkpoint(1) && take(m_rows, 1, m_limits.maximumRows, Failure::Rows)
            && decoded(ImportRowOverheadBytes);
    }
    bool rowsFit(quint64 count)
    {
        return checkpoint() && (count <= m_limits.maximumRows || reject(Failure::Rows));
    }
    // Preflight the dense matrix represented by potentially sparse SQL rows.
    bool matrixFits(quint64 rows, quint64 columns)
    {
        if (!rowsFit(rows) || !fields(columns)) return false;
        const quint64 available = m_limits.maximumValues - std::min(m_values, m_limits.maximumValues);
        if (columns != 0 && rows > available / columns) return reject(Failure::Values);
        const quint64 cells = rows * columns;
        const quint64 bytes = m_limits.maximumDecodedBytes - std::min(m_decoded, m_limits.maximumDecodedBytes);
        if (cells > bytes / ImportValueOverheadBytes) return reject(Failure::Decoded);
        const quint64 remaining = bytes - cells * ImportValueOverheadBytes;
        return rows <= remaining / ImportRowOverheadBytes || reject(Failure::Decoded);
    }
    bool frames(quint64 count)
    {
        if (!checkpoint() || !take(m_frames, count, m_limits.maximumFrames, Failure::Frames)) return false;
        if (count > m_limits.maximumDecodedBytes / ImportFrameOverheadBytes) return reject(Failure::Decoded);
        return decoded(count * ImportFrameOverheadBytes);
    }
    QString error() const
    {
        switch (m_failure) {
        case Failure::Input: return QCoreApplication::translate("ImportBudget", "The file exceeds the input-size processing limit.");
        case Failure::Decoded: return QCoreApplication::translate("ImportBudget", "The document exceeds the decoded-data processing limit.");
        case Failure::Rows: return QCoreApplication::translate("ImportBudget", "The document exceeds the record-count processing limit.");
        case Failure::Values: return QCoreApplication::translate("ImportBudget", "The document exceeds the value-count processing limit.");
        case Failure::Fields: return QCoreApplication::translate("ImportBudget", "The document exceeds the field-count processing limit.");
        case Failure::Frames: return QCoreApplication::translate("ImportBudget", "The document exceeds the frame-count processing limit.");
        case Failure::Cell: return QCoreApplication::translate("ImportBudget", "A value exceeds the per-value processing limit.");
        case Failure::Work: return QCoreApplication::translate("ImportBudget", "The document exceeds the processing-work limit.");
        case Failure::Deadline: return QCoreApplication::translate("ImportBudget", "Document processing exceeded its time limit.");
        case Failure::Cancelled: return QCoreApplication::translate("ImportBudget", "Document processing was canceled.");
        case Failure::None: return {};
        }
        return {};
    }
private:
    bool take(quint64& used, quint64 amount, quint64 maximum, Failure failure)
    {
        if (used > maximum || amount > maximum - used) return reject(failure);
        used += amount;
        return true;
    }
    ImportLimits m_limits;
    QElapsedTimer m_clock;
    Failure m_failure = Failure::None;
    quint64 m_decoded = 0, m_rows = 0, m_values = 0, m_frames = 0, m_work = 0;
};

inline bool readBoundedFile(QFile& file, QByteArray* output, ImportBudget& budget, QString* error)
{
    const auto fail = [&](const QString& message) { if (error) *error = message; return false; };
    const qint64 expected = file.size();
    if (!output || file.isSequential() || expected < 0 || !budget.input(quint64(expected)))
        return fail(budget.failed() ? budget.error() : QStringLiteral("The input is not a seekable file."));
    if (expected > (std::numeric_limits<int>::max)())
        return fail(QStringLiteral("The input exceeds the supported address range."));
    QByteArray bytes;
    bytes.reserve(int(expected));
    while (!file.atEnd()) {
        if (!budget.checkpoint()) return fail(budget.error());
        const quint64 remaining = budget.limits().maximumInputBytes - quint64(bytes.size());
        const qint64 count = qint64(std::min<quint64>(ImportReadChunkBytes, remaining));
        if (count == 0) { budget.reject(ImportBudget::Failure::Input); return fail(budget.error()); }
        const QByteArray block = file.read(count);
        if (block.isEmpty()) return fail(file.errorString());
        if (!budget.checkpoint(quint64(block.size()))) return fail(budget.error());
        bytes.append(block);
    }
    if (file.error() != QFileDevice::NoError) return fail(file.errorString());
    if (bytes.size() != expected || file.size() != expected)
        return fail(QStringLiteral("The source file changed while it was being read."));
    *output = std::move(bytes);
    return true;
}

inline bool readBoundedFile(const QString& path, QByteArray* output, ImportBudget& budget, QString* error)
{
    const QFileInfo info(path);
    if (!info.isFile() || info.size() < 0) {
        if (error) *error = QStringLiteral("The input is not a regular file: %1").arg(path);
        return false;
    }
    if (!budget.input(quint64(info.size()))) { if (error) *error = budget.error(); return false; }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { if (error) *error = file.errorString(); return false; }
    return readBoundedFile(file, output, budget, error);
}
} // namespace CardStack
