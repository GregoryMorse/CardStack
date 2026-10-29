#include "DelimitedText.h"

#include <QFile>
#include <QSaveFile>
#include <QBuffer>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>

namespace CardStack::DelimitedText {
namespace {

void setError(QString* target, const QString& message)
{
    if (target != nullptr) {
        *target = message;
    }
}

QString escapedCell(QString value, QChar delimiter)
{
    const bool mustQuote = value.contains(delimiter)
        || value.contains(QLatin1Char('"'))
        || value.contains(QLatin1Char('\n'))
        || value.contains(QLatin1Char('\r'));
    value.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return mustQuote ? QStringLiteral("\"%1\"").arg(value) : value;
}

QVector<QStringList> parseRows(const QString& text, QChar delimiter, ImportBudget& budget)
{
    QVector<QStringList> rows;
    QStringList cells;
    QString cell;
    bool quoted = false;
    const auto appendCell = [&]() {
        if (!budget.fields(quint64(cells.size()) + 1) || !budget.value(quint64(cell.size()) * sizeof(QChar))) return false;
        cells.append(cell);
        cell.clear();
        return true;
    };
    const auto appendRow = [&]() {
        if (!appendCell() || !budget.row()) return false;
        rows.append(cells);
        cells.clear();
        return true;
    };
    for (qsizetype index = 0; index < text.size(); ++index) {
        if (!budget.checkpoint(1) || !budget.cell(quint64(cell.size()) * sizeof(QChar))) return {};
        const QChar ch = text.at(index);
        if (quoted) {
            if (ch == QLatin1Char('"')) {
                if (index + 1 < text.size() && text.at(index + 1) == QLatin1Char('"')) {
                    cell.append(ch);
                    ++index;
                } else quoted = false;
            } else cell.append(ch);
        } else if (ch == QLatin1Char('"')) quoted = true;
        else if (ch == delimiter) { if (!appendCell()) return {}; }
        else if (ch == QLatin1Char('\n')) { if (!appendRow()) return {}; }
        else if (ch != QLatin1Char('\r')) cell.append(ch);
    }
    if ((!cell.isEmpty() || !cells.isEmpty()) && !appendRow()) return {};
    return rows;
}

QVector<int> mappedColumns(const ImportExportProfile& profile, int count)
{
    if (!profile.fieldMappings.isEmpty()) {
        for (int index : profile.fieldMappings) if (index < 0 || index >= count) return {};
        return profile.fieldMappings;
    }
    QVector<int> columns;
    columns.reserve(count);
    for (int index = 0; index < count; ++index) columns.append(index);
    return columns;
}

FieldType fieldTypeForName(const QString& name)
{
    return name.compare(QStringLiteral("Notes"), Qt::CaseInsensitive) == 0 ? FieldType::Notes : FieldType::Text;
}

int maxLengthForColumn(const QVector<QStringList>& rows, int column)
{
    int maxLength = 1;
    for (const QStringList& row : rows) {
        maxLength = std::max(maxLength, static_cast<int>(row.value(column).size()));
    }
    return std::clamp(maxLength, 1, 8192);
}

ImportExportProfile profileForFormat(ImportExportProfileType type, DelimitedTextFormat format)
{
    ImportExportProfile profile;
    profile.type = type;
    profile.format = format;
    profile.delimiter = format == DelimitedTextFormat::Tsv ? QLatin1Char('\t') : QLatin1Char(',');
    profile.hasHeader = true;
    profile.name = format == DelimitedTextFormat::Tsv
        ? QStringLiteral("Tab-delimited text")
        : QStringLiteral("Comma-delimited text");
    return profile;
}

} // namespace

ImportExportProfile csvProfile(ImportExportProfileType type)
{
    return profileForFormat(type, DelimitedTextFormat::Csv);
}

ImportExportProfile tsvProfile(ImportExportProfileType type)
{
    return profileForFormat(type, DelimitedTextFormat::Tsv);
}

QString writeDeck(const Deck& deck, const ImportExportProfile& profile)
{
    const auto columns = mappedColumns(profile, deck.fieldCount());
    if (columns.isEmpty() && !profile.fieldMappings.isEmpty()) return {};
    QString output = QStringLiteral("");
    QTextStream stream(&output);

    auto writeRow = [&](const QStringList& cells) {
        for (int index = 0; index < cells.size(); ++index) {
            if (index > 0) {
                stream << profile.delimiter;
            }
            stream << escapedCell(cells.at(index), profile.delimiter);
        }
        stream << '\n';
    };

    if (profile.hasHeader) {
        QStringList headers;
        for (int fieldIndex : columns) {
            headers.append(deck.fieldAt(fieldIndex).name());
        }
        writeRow(headers);
    }

    for (const CardRecord& card : deck.cards()) {
        QStringList cells;
        for (int fieldIndex : columns) {
            cells.append(card.valueAt(fieldIndex));
        }
        writeRow(cells);
    }

    return output;
}

bool readDeck(const QString& text, const ImportExportProfile& profile, Deck* deck, QString* errorMessage, ImportLimits limits, ImportBudget* sharedBudget)
{
    if (deck == nullptr) {
        setError(errorMessage, QStringLiteral("Deck output is not available."));
        return false;
    }

    ImportBudget local(limits);
    ImportBudget& budget = sharedBudget ? *sharedBudget : local;
    const auto abort = [&] { setError(errorMessage, budget.error()); return false; };
    if (!budget.input(quint64(text.size()) * sizeof(QChar))) return abort();
    QVector<QStringList> rows = parseRows(text, profile.delimiter, budget);
    if (budget.failed()) return abort();
    while (!rows.isEmpty() && rows.last().isEmpty()) {
        rows.removeLast();
    }
    if (rows.isEmpty()) {
        setError(errorMessage, QStringLiteral("Delimited text file contains no rows."));
        return false;
    }

    QStringList headers;
    if (profile.hasHeader) {
        headers = rows.takeFirst();
    } else {
        int columnCount = 0;
        for (const QStringList& row : rows) {
            columnCount = std::max(columnCount, static_cast<int>(row.size()));
        }
        for (int column = 0; column < columnCount; ++column) {
            headers.append(QStringLiteral("Field %1").arg(column + 1));
        }
    }

    if (headers.isEmpty()) {
        setError(errorMessage, QStringLiteral("Delimited text file has no fields."));
        return false;
    }

    const auto columns = mappedColumns(profile, headers.size());
    if (columns.isEmpty()) {
        setError(errorMessage, QCoreApplication::translate("DelimitedText", "The field mapping contains an invalid source column."));
        return false;
    }
    if (!budget.fields(quint64(columns.size()))) return abort();
    Deck imported;
    imported.setName(QStringLiteral("Imported Text Deck"));
    imported.setDescription(QStringLiteral("Imported from a delimited text file."));
    imported.setImportExportProfiles({profile});

    for (int column : columns) {
        const QString fallbackName = QStringLiteral("Field %1").arg(column + 1);
        const QString name = headers.at(column).trimmed().isEmpty() ? fallbackName : headers.at(column).trimmed();
        if (!budget.checkpoint(quint64(rows.size()))) return abort();
        imported.addField(FieldDefinition(name, fieldTypeForName(name), maxLengthForColumn(rows, column)));
    }

    for (const QStringList& row : rows) {
        if (!budget.row()) return abort();
        CardRecord card;
        for (int column = 0; column < imported.fieldCount(); ++column) {
            const int sourceColumn = columns.at(column);
            if (!budget.value(quint64(row.value(sourceColumn).size()) * sizeof(QChar)) || !budget.checkpoint(1)) return abort();
            QString value = row.value(sourceColumn);
            const int maxLength = imported.fieldAt(column).maxLength();
            if (maxLength > 0 && value.size() > maxLength) {
                value.truncate(maxLength);
            }
            card.appendValue(value);
        }
        imported.addCard(card);
    }

    *deck = std::move(imported);
    return true;
}

bool writeDeckFile(const Deck& deck, const ImportExportProfile& profile, const QString& filePath, QString* errorMessage)
{
    const QString output = writeDeck(deck, profile);
    if (output.isNull()) {
        setError(errorMessage, QCoreApplication::translate("DelimitedText", "The field mapping contains an invalid source column."));
        return false;
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setError(errorMessage, file.errorString());
        return false;
    }

    QTextStream stream(&file);
    stream << output;
    stream.flush();
    if (stream.status() != QTextStream::Ok || !file.commit()) {
        setError(errorMessage, file.errorString()); return false;
    }
    return true;
}

bool readDeckFile(const QString& filePath, const ImportExportProfile& profile, Deck* deck, QString* errorMessage, ImportLimits limits)
{
    ImportBudget budget(limits);
    QByteArray bytes;
    if (!readBoundedFile(filePath, &bytes, budget, errorMessage)) return false;
    // Retain QTextStream's BOM and Unicode decoding without an unbounded file read.
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);
    QTextStream stream(&buffer);
    const QString text = stream.readAll();
    if (!readDeck(text, profile, deck, errorMessage, limits, &budget)) return false;
    if (deck) deck->setName(QFileInfo(filePath).completeBaseName());
    return true;
}

} // namespace CardStack::DelimitedText
