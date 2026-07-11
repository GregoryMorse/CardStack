#include "DelimitedText.h"

#include <QFile>
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

QStringList parseLine(const QString& line, QChar delimiter)
{
    QStringList cells;
    QString cell;
    bool quoted = false;

    for (int index = 0; index < line.size(); ++index) {
        const QChar ch = line.at(index);
        if (quoted) {
            if (ch == QLatin1Char('"')) {
                if (index + 1 < line.size() && line.at(index + 1) == QLatin1Char('"')) {
                    cell.append(QLatin1Char('"'));
                    ++index;
                } else {
                    quoted = false;
                }
            } else {
                cell.append(ch);
            }
            continue;
        }

        if (ch == QLatin1Char('"')) {
            quoted = true;
        } else if (ch == delimiter) {
            cells.append(cell);
            cell.clear();
        } else {
            cell.append(ch);
        }
    }

    cells.append(cell);
    return cells;
}

QVector<QStringList> parseRows(const QString& text, QChar delimiter)
{
    QVector<QStringList> rows;
    QStringList cells;
    QString cell;
    bool quoted = false;

    for (int index = 0; index < text.size(); ++index) {
        const QChar ch = text.at(index);
        if (quoted) {
            if (ch == QLatin1Char('"')) {
                if (index + 1 < text.size() && text.at(index + 1) == QLatin1Char('"')) {
                    cell.append(QLatin1Char('"'));
                    ++index;
                } else {
                    quoted = false;
                }
            } else {
                cell.append(ch);
            }
            continue;
        }

        if (ch == QLatin1Char('"')) {
            quoted = true;
        } else if (ch == delimiter) {
            cells.append(cell);
            cell.clear();
        } else if (ch == QLatin1Char('\n')) {
            cells.append(cell);
            rows.append(cells);
            cells.clear();
            cell.clear();
        } else if (ch != QLatin1Char('\r')) {
            cell.append(ch);
        }
    }

    if (!cell.isEmpty() || !cells.isEmpty()) {
        cells.append(cell);
        rows.append(cells);
    }
    return rows;
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
    QString output;
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
        for (int fieldIndex = 0; fieldIndex < deck.fieldCount(); ++fieldIndex) {
            headers.append(deck.fieldAt(fieldIndex).name());
        }
        writeRow(headers);
    }

    for (const CardRecord& card : deck.cards()) {
        QStringList cells;
        for (int fieldIndex = 0; fieldIndex < deck.fieldCount(); ++fieldIndex) {
            cells.append(card.valueAt(fieldIndex));
        }
        writeRow(cells);
    }

    return output;
}

bool readDeck(const QString& text, const ImportExportProfile& profile, Deck* deck, QString* errorMessage)
{
    if (deck == nullptr) {
        setError(errorMessage, QStringLiteral("Deck output is not available."));
        return false;
    }

    QVector<QStringList> rows = parseRows(text, profile.delimiter);
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

    Deck imported;
    imported.setName(QStringLiteral("Imported Text Deck"));
    imported.setDescription(QStringLiteral("Imported from a delimited text file."));
    imported.setImportExportProfiles({profile});

    for (int column = 0; column < headers.size(); ++column) {
        const QString fallbackName = QStringLiteral("Field %1").arg(column + 1);
        const QString name = headers.at(column).trimmed().isEmpty() ? fallbackName : headers.at(column).trimmed();
        imported.addField(FieldDefinition(name, fieldTypeForName(name), maxLengthForColumn(rows, column)));
    }

    for (const QStringList& row : rows) {
        CardRecord card;
        for (int column = 0; column < imported.fieldCount(); ++column) {
            QString value = row.value(column);
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
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setError(errorMessage, file.errorString());
        return false;
    }

    QTextStream stream(&file);
    stream << writeDeck(deck, profile);
    return true;
}

bool readDeckFile(const QString& filePath, const ImportExportProfile& profile, Deck* deck, QString* errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(errorMessage, file.errorString());
        return false;
    }

    QTextStream stream(&file);
    if (!readDeck(stream.readAll(), profile, deck, errorMessage)) {
        return false;
    }

    if (deck != nullptr) {
        deck->setName(QFileInfo(filePath).completeBaseName());
    }
    return true;
}

} // namespace CardStack::DelimitedText
