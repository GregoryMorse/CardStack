#include "PhoneCallLog.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace CardStack::PhoneCallLog {
namespace {

QString decodeWindows1252(const QByteArray& bytes)
{
    static constexpr ushort controls[32] = {
        0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
        0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
        0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
        0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178,
    };

    QString result;
    result.reserve(bytes.size());
    for (const char ch : bytes) {
        const quint8 byte = static_cast<quint8>(ch);
        result.append(byte >= 0x80 && byte <= 0x9f
                ? QChar(controls[byte - 0x80])
                : QChar(static_cast<ushort>(byte)));
    }
    return result;
}

QByteArray encodeWindows1252(const QString& text)
{
    static constexpr ushort controls[32] = {
        0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
        0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
        0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
        0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178,
    };

    QByteArray result;
    result.reserve(text.size());
    for (const QChar ch : text) {
        const ushort value = ch.unicode();
        if (value <= 0xff && !(value >= 0x80 && value <= 0x9f)) {
            result.append(static_cast<char>(value));
            continue;
        }
        int encoded = -1;
        for (int index = 0; index < 32; ++index) {
            if (controls[index] == value) {
                encoded = 0x80 + index;
                break;
            }
        }
        result.append(encoded >= 0 ? static_cast<char>(encoded) : '?');
    }
    return result;
}

QStringList parseCsvLine(const QString& line, ImportBudget& budget)
{
    QStringList values;
    QString value;
    bool quoted = false;
    for (int index = 0; index < line.size(); ++index) {
        if (!budget.checkpoint(1) || !budget.cell(quint64(value.size()) * sizeof(QChar))) return {};
        const QChar ch = line.at(index);
        if (ch == QLatin1Char('"')) {
            if (quoted && index + 1 < line.size() && line.at(index + 1) == QLatin1Char('"')) {
                value.append(ch);
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (ch == QLatin1Char(',') && !quoted) {
            if (!budget.fields(quint64(values.size()) + 1) || !budget.value(quint64(value.size()) * sizeof(QChar))) return {};
            values.append(value);
            value.clear();
        } else {
            value.append(ch);
        }
    }
    if (!budget.fields(quint64(values.size()) + 1) || !budget.value(quint64(value.size()) * sizeof(QChar))) return {};
    values.append(value);
    return values;
}

QString csvValue(QString value)
{
    if (value.contains(QLatin1Char('"'))) {
        value.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    }
    if (value.contains(QLatin1Char(',')) || value.contains(QLatin1Char('"'))
        || value.contains(QLatin1Char('\r')) || value.contains(QLatin1Char('\n'))) {
        return QStringLiteral("\"%1\"").arg(value);
    }
    return value;
}

QString legacySidecarPath(const QString& deckFilePath, ImportBudget& budget)
{
    const QFileInfo deckInfo(deckFilePath);
    const QDir directory = deckInfo.dir();
    const QString expected = directory.filePath(deckInfo.completeBaseName() + QStringLiteral(".LOG"));
    if (QFileInfo::exists(expected)) {
        return expected;
    }

    QDirIterator files(directory.absolutePath(), QDir::Files | QDir::Readable);
    while (files.hasNext()) {
        if (!budget.checkpoint(1)) return {};
        files.next();
        const QFileInfo file = files.fileInfo();
        if (file.completeBaseName().compare(deckInfo.completeBaseName(), Qt::CaseInsensitive) == 0
            && file.suffix().compare(QStringLiteral("log"), Qt::CaseInsensitive) == 0) {
            return file.absoluteFilePath();
        }
    }
    return {};
}

} // namespace

int importLegacyFile(const QString& logPath, Deck* deck, QString* warningMessage, ImportLimits limits, ImportBudget* sharedBudget)
{
    if (deck == nullptr) {
        if (warningMessage != nullptr) {
            *warningMessage = QStringLiteral("The imported deck is not available for call-log migration.");
        }
        return 0;
    }

    ImportBudget local(limits);
    ImportBudget& budget = sharedBudget ? *sharedBudget : local;
    QByteArray source;
    if (!readBoundedFile(logPath, &source, budget, warningMessage)) return 0;
    const auto abort = [&] { if (warningMessage) *warningMessage = budget.error(); return 0; };
    QVector<PhoneCallLogEntry> staged;
    qsizetype begin = 0;
    while (begin < source.size()) {
        qsizetype end = source.indexOf('\n', begin);
        if (end < 0) end = source.size();
        if (!budget.row() || !budget.cell(quint64(end - begin) * sizeof(QChar))
            || !budget.decoded(quint64(end - begin) * 3)) return abort();
        QByteArray bytes = source.mid(begin, end - begin);
        begin = end == source.size() ? end : end + 1;
        if (bytes.endsWith('\r')) {
            bytes.chop(1);
        }
        if (bytes.isEmpty()) {
            continue;
        }

        PhoneCallLogEntry entry;
        entry.rawLegacyBytes = bytes;
        const QStringList values = parseCsvLine(decodeWindows1252(bytes), budget);
        if (budget.failed()) return abort();
        if (values.size() >= 2) {
            QDateTime calledAt = QDateTime::fromString(
                values.at(0) + QLatin1Char(' ') + values.at(1),
                QStringLiteral("MM/dd/yy HH:mm"));
            if (calledAt.isValid()) {
                calledAt.setTimeSpec(Qt::LocalTime);
                entry.calledAtUtc = calledAt.toUTC().toString(Qt::ISODateWithMs);
            }
        }
        if (values.size() >= 3) {
            entry.phoneNumber = values.at(2);
        }
        for (int index = 3; index < values.size() && entry.cardSummaryValues.size() < 3; ++index) {
            if (entry.cardSummaryValues.size() == 2 && values.size() > 6) {
                entry.cardSummaryValues.append(values.mid(index).join(QLatin1Char(',')));
                break;
            }
            entry.cardSummaryValues.append(values.at(index));
        }
        staged.append(std::move(entry));
    }
    for (auto& entry : staged) deck->addPhoneCallLogEntry(std::move(entry));
    return int(staged.size());
}

int importLegacySidecar(const QString& deckFilePath, Deck* deck, QString* warningMessage, ImportLimits limits, ImportBudget* sharedBudget)
{
    ImportBudget local(limits);
    ImportBudget& budget = sharedBudget ? *sharedBudget : local;
    const QString logPath = legacySidecarPath(deckFilePath, budget);
    if (budget.failed()) { if (warningMessage) *warningMessage = budget.error(); return 0; }
    if (logPath.isEmpty()) {
        return 0;
    }
    return importLegacyFile(logPath, deck, warningMessage, limits, &budget);
}

bool writeLegacyFile(const Deck& deck, const QString& filePath, QString* errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage != nullptr) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    for (const PhoneCallLogEntry& entry : deck.phoneCallLogEntries()) {
        if (!entry.rawLegacyBytes.isEmpty()) {
            file.write(entry.rawLegacyBytes);
            file.write("\r\n");
            continue;
        }

        const QDateTime calledAt = QDateTime::fromString(entry.calledAtUtc, Qt::ISODateWithMs).toLocalTime();
        QStringList values{
            calledAt.isValid() ? calledAt.date().toString(QStringLiteral("MM/dd/yy")) : QString(),
            calledAt.isValid() ? calledAt.time().toString(QStringLiteral("HH:mm")) : QString(),
            entry.phoneNumber,
        };
        for (int index = 0; index < 3; ++index) {
            values.append(index < entry.cardSummaryValues.size() ? entry.cardSummaryValues.at(index) : QString());
        }
        for (QString& value : values) {
            value = csvValue(value);
        }
        file.write(encodeWindows1252(values.join(QLatin1Char(','))));
        file.write("\r\n");
    }
    return true;
}

} // namespace CardStack::PhoneCallLog
