#pragma once

#include <QCryptographicHash>
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

inline QByteArray documentFileFingerprint(const QString& path, bool* ok = nullptr)
{
    if (!QFileInfo::exists(path)) {
        if (ok != nullptr) *ok = true;
        return {};
    }
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const bool succeeded = file.open(QIODevice::ReadOnly) && hash.addData(&file);
    if (ok != nullptr) *ok = succeeded;
    return succeeded ? hash.result() : QByteArray();
}
} // namespace CardStack
