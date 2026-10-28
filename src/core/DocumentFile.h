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
    if (result.isEmpty()) result = QDir::cleanPath(info.absoluteFilePath());
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
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
