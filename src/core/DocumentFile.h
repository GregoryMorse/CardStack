#pragma once

#include <QCryptographicHash>
#include "ImportBudget.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace CardStack {
inline QString documentFileIdentity(const QString& path)
{
    const QFileInfo info(path);
    QString result = info.canonicalFilePath();
    if (result.isEmpty()) {
        QString parent = info.dir().canonicalPath();
        if (parent.isEmpty()) parent = info.absolutePath();
        result = QDir::cleanPath(QDir(parent).filePath(info.fileName()));
    }
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}

inline QString documentLockPath(const QString& path)
{
    return documentFileIdentity(path) + QStringLiteral(".lock");
}

inline QByteArray documentFileFingerprint(const QString& path, bool* ok = nullptr, ImportLimits limits = {})
{
    if (ok) *ok = false;
    const QFileInfo info(path);
    if (!info.exists()) { if (ok) *ok = true; return {}; }
    ImportBudget budget(limits);
    if (!info.isFile() || info.size() < 0 || !budget.input(quint64(info.size()))) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.isSequential()) return {};
    const qint64 expected = file.size();
    if (expected < 0 || !budget.input(quint64(expected))) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    quint64 total = 0;
    while (!file.atEnd()) {
        if (!budget.checkpoint()) return {};
        const quint64 remaining = budget.limits().maximumInputBytes - total;
        const qint64 count = qint64(std::min<quint64>(quint64(ImportReadChunkBytes), remaining));
        if (count == 0) return {};
        const QByteArray block = file.read(count);
        if (block.isEmpty() || !budget.checkpoint(quint64(block.size()))) return {};
        total += quint64(block.size());
        hash.addData(block);
    }
    if (file.error() != QFileDevice::NoError || total != quint64(expected) || file.size() != expected) return {};
    if (ok) *ok = true;
    return hash.result();
}
} // namespace CardStack
