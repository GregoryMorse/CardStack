#include "SQLiteSnapshot.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>
#include <QUuid>
#include <cmath>

namespace CardStack::SQLiteSnapshot {
namespace {
constexpr int MaximumSnapshotColumns = 128;
constexpr int MaximumIdentifierCharacters = 128;
constexpr quint64 MaximumSchemaBytes = 64 * 1024;
constexpr int MaximumJsonDepth = 128;
const QStringList Tables{
    QStringLiteral("schema_migrations"), QStringLiteral("app_settings"),
    QStringLiteral("decks"), QStringLiteral("templates"),
    QStringLiteral("template_fields"), QStringLiteral("template_frames"),
    QStringLiteral("cards"), QStringLiteral("card_values"),
    QStringLiteral("deck_sort_keys"), QStringLiteral("deck_formatting"),
    QStringLiteral("reports"), QStringLiteral("report_frames"),
    QStringLiteral("import_export_profiles"), QStringLiteral("phone_profiles"),
    QStringLiteral("phone_call_log"), QStringLiteral("deck"), QStringLiteral("fields")};

bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}
QString quoted(QString name)
{
    name.replace(QLatin1Char('"'), QString(2, QLatin1Char('"')));
    return QLatin1Char('"') + name + QLatin1Char('"');
}
bool exec(QSqlDatabase db, const QString& sql, QString* error)
{
    QSqlQuery query(db);
    return query.exec(sql) || fail(error, query.lastError().text());
}
QJsonValue encode(const QVariant& value)
{
    if (value.isNull()) return QJsonValue(QJsonValue::Null);
    switch (value.metaType().id()) {
    case QMetaType::QByteArray:
        return QJsonObject{{QStringLiteral("blob"), QString::fromLatin1(value.toByteArray().toBase64())}};
    case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong: case QMetaType::ULongLong:
        return QJsonObject{{QStringLiteral("integer"), value.toString()}};
    case QMetaType::Double:
        return QJsonObject{{QStringLiteral("real"), value.toDouble()}};
    default:
        return QJsonObject{{QStringLiteral("text"), value.toString()}};
    }
}
QVariant decode(const QJsonValue& value)
{
    if (value.isNull()) return {};
    const QJsonObject object = value.toObject();
    if (object.contains(QStringLiteral("blob"))) {
        const QByteArray bytes = QByteArray::fromBase64(object.value(QStringLiteral("blob")).toString().toLatin1());
        return bytes.isNull() ? QByteArray("") : bytes;
    }
    if (object.contains(QStringLiteral("integer"))) return object.value(QStringLiteral("integer")).toString().toLongLong();
    if (object.contains(QStringLiteral("real"))) return object.value(QStringLiteral("real")).toDouble();
    const QString text = object.value(QStringLiteral("text")).toString();
    return text.isNull() ? QStringLiteral("") : text;
}
bool physicalTables(QSqlDatabase db, QSet<QString>* existing, ImportBudget& budget, QString* error)
{
    for (const auto& table : Tables) {
        if (!budget.checkpoint(1)) return fail(error, budget.error());
        QSqlQuery query(db);
        query.setForwardOnly(true);
        query.prepare(QStringLiteral("SELECT type, length(CAST(sql AS BLOB)), CASE WHEN length(CAST(sql AS BLOB)) <= ? THEN sql END FROM sqlite_master WHERE name = ? LIMIT 2"));
        query.addBindValue(qulonglong(MaximumSchemaBytes));
        query.addBindValue(table);
        if (!query.exec()) return fail(error, query.lastError().text());
        if (!query.next()) continue;
        const QString definition = query.value(2).toString();
        if (query.value(0).toString() != QLatin1String("table")
            || query.value(1).toULongLong() > MaximumSchemaBytes
            || definition.trimmed().startsWith(QLatin1String("CREATE VIRTUAL"), Qt::CaseInsensitive))
            return fail(error, QStringLiteral("Document data must use ordinary SQLite tables."));
        QSqlQuery kind(db);
        if (!kind.exec(QStringLiteral("PRAGMA main.table_list(%1)").arg(quoted(table)))
            || !kind.next() || kind.value(2).toString() != QLatin1String("table"))
            return fail(error, QStringLiteral("Document data must not use virtual or shadow tables."));
        existing->insert(table);
        if (query.next()) return fail(error, QStringLiteral("The document schema is ambiguous."));
        if (query.lastError().isValid()) return fail(error, query.lastError().text());
    }
    return true;
}
bool validCell(const QJsonValue& value, ImportBudget& budget, QString* error)
{
    if (value.isNull()) return budget.value(0) || fail(error, budget.error());
    if (!value.isObject() || value.toObject().size() != 1)
        return fail(error, QStringLiteral("The document contains an invalid typed value."));
    const auto object = value.toObject();
    const QString key = object.begin().key();
    const auto data = object.begin().value();
    if (key == QLatin1String("real")) {
        if (!data.isDouble() || !std::isfinite(data.toDouble()))
            return fail(error, QStringLiteral("The document contains an invalid numeric value."));
        return budget.value(sizeof(double)) || fail(error, budget.error());
    }
    if (!data.isString() || (key != QLatin1String("text") && key != QLatin1String("integer") && key != QLatin1String("blob")))
        return fail(error, QStringLiteral("The document contains an unsupported typed value."));
    const QString text = data.toString();
    if (!budget.value(quint64(text.size()) * sizeof(QChar))) return fail(error, budget.error());
    if (key == QLatin1String("integer")) {
        bool valid = false;
        text.toLongLong(&valid);
        qsizetype start = text.startsWith(QLatin1Char('-')) ? 1 : 0;
        if (!valid || start == text.size() || text.size() > 20)
            return fail(error, QStringLiteral("The document contains an invalid integer."));
        for (qsizetype index = start; index < text.size(); ++index)
            if (text[index] < QLatin1Char('0') || text[index] > QLatin1Char('9'))
                return fail(error, QStringLiteral("The document contains an invalid integer."));
    } else if (key == QLatin1String("blob")) {
        const auto decoded = QByteArray::fromBase64Encoding(text.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded || decoded.decoded.toBase64() != text.toLatin1())
            return fail(error, QStringLiteral("The document contains invalid binary encoding."));
    }
    return true;
}
bool validate(const QJsonObject& snapshot, ImportBudget& budget, QString* error)
{
    if (snapshot.size() != 2 || snapshot.value(QStringLiteral("snapshot_version")) != QJsonValue(1)
        || !snapshot.value(QStringLiteral("tables")).isObject())
        return fail(error, QStringLiteral("The document snapshot format is not supported."));
    const auto tables = snapshot.value(QStringLiteral("tables")).toObject();
    for (auto it = tables.begin(); it != tables.end(); ++it) {
        if (!Tables.contains(it.key()) || !it.value().isObject())
            return fail(error, QStringLiteral("The document contains an unsupported table."));
        const auto data = it.value().toObject();
        if (data.size() != 2 || !data.value(QStringLiteral("columns")).isArray() || !data.value(QStringLiteral("rows")).isArray())
            return fail(error, QStringLiteral("The document contains an invalid table definition."));
        const auto columns = data.value(QStringLiteral("columns")).toArray();
        if (columns.isEmpty() || columns.size() > MaximumSnapshotColumns)
            return fail(error, QStringLiteral("The document contains an invalid column count."));
        QSet<QString> names;
        for (const auto& column : columns) {
            const auto name = column.toString();
            if (!column.isString() || name.isEmpty() || name.size() > MaximumIdentifierCharacters || names.contains(name))
                return fail(error, QStringLiteral("The document contains an invalid column."));
            names.insert(name);
        }
        const auto rows = data.value(QStringLiteral("rows")).toArray();
        if (!budget.rowsFit(quint64(rows.size()))) return fail(error, budget.error());
        if (it.key() == QLatin1String("template_fields") && !budget.fields(quint64(rows.size()))) return fail(error, budget.error());
        if ((it.key() == QLatin1String("report_frames") || it.key() == QLatin1String("template_frames"))
            && !budget.frames(quint64(rows.size()))) return fail(error, budget.error());
        int keyColumn = -1, valueColumn = -1;
        for (int index = 0; index < columns.size(); ++index) {
            if (columns[index].toString() == QLatin1String("key")) keyColumn = index;
            if (columns[index].toString() == QLatin1String("value")) valueColumn = index;
        }
        for (const auto& value : rows) {
            if (!value.isArray() || value.toArray().size() != columns.size())
                return fail(error, QStringLiteral("The document contains an incomplete row."));
            if (!budget.row()) return fail(error, budget.error());
            const auto row = value.toArray();
            for (const auto& cell : row)
                if (!validCell(cell, budget, error)) return false;
            if (it.key() == QLatin1String("app_settings") && keyColumn >= 0 && valueColumn >= 0) {
                const QString key = decode(row[keyColumn]).toString();
                if (key == QLatin1String("schema_version") || key == QLatin1String("package_version")) {
                    bool valid = false;
                    const int version = decode(row[valueColumn]).toString().toInt(&valid);
                    if (!valid || version < 1 || version > (key == QLatin1String("schema_version") ? CurrentSchemaVersion : 1))
                        return fail(error, QStringLiteral("This document has an unsupported storage version."));
                }
            }
        }
    }
    return true;
}
bool captureBounded(QSqlDatabase db, QJsonObject* snapshot, ImportBudget& budget, QString* error)
{
    if (!snapshot) return fail(error, QStringLiteral("Snapshot output is missing."));
    QSet<QString> existing;
    if (!physicalTables(db, &existing, budget, error)) return false;
    QJsonObject tables;
    for (const auto& table : Tables) {
        if (!existing.contains(table)) continue;
        QSqlQuery info(db);
        info.setForwardOnly(true);
        if (!info.exec(QStringLiteral("PRAGMA table_xinfo(%1)").arg(quoted(table))))
            return fail(error, info.lastError().text());
        QStringList names, projections;
        QJsonArray columns;
        while (info.next()) {
            if (!budget.checkpoint(1)) return fail(error, budget.error());
            const QString name = info.value(1).toString();
            if (names.size() >= MaximumSnapshotColumns || name.isEmpty() || name.size() > MaximumIdentifierCharacters || info.value(6).toInt() != 0)
                return fail(error, QStringLiteral("The document contains an unsupported column definition."));
            names.append(name);
            columns.append(name);
            const QString column = quoted(name);
            // Oversized cells are rejected by SQL before Qt allocates their values.
            projections.append(QStringLiteral("length(CAST(%1 AS BLOB))").arg(column));
            projections.append(QStringLiteral("CASE WHEN length(CAST(%1 AS BLOB)) <= %2 THEN %1 END")
                .arg(column).arg(budget.limits().maximumCellBytes));
        }
        if (info.lastError().isValid()) return fail(error, info.lastError().text());
        if (names.isEmpty()) return fail(error, QStringLiteral("The document has an empty table definition."));
        QSqlQuery query(db);
        query.setForwardOnly(true);
        const quint64 rowLimit = std::min<quint64>(budget.limits().maximumRows, 1000000) + 1;
        if (!query.exec(QStringLiteral("SELECT %1 FROM %2 LIMIT %3").arg(projections.join(QLatin1Char(',')), quoted(table)).arg(rowLimit)))
            return fail(error, query.lastError().text());
        QJsonArray rows;
        while (query.next()) {
            if (!budget.row()) return fail(error, budget.error());
            if (table == QLatin1String("template_fields") && !budget.fields(quint64(rows.size()) + 1)) return fail(error, budget.error());
            if ((table == QLatin1String("template_frames") || table == QLatin1String("report_frames")) && !budget.frames(1))
                return fail(error, budget.error());
            QJsonArray row;
            for (int index = 0; index < names.size(); ++index) {
                const quint64 size = query.value(index * 2).toULongLong();
                if (!budget.cell(size) || !budget.checkpoint(size) || !budget.value(size * 4))
                    return fail(error, budget.error());
                row.append(encode(query.value(index * 2 + 1)));
            }
            rows.append(row);
        }
        if (query.lastError().isValid()) return fail(error, query.lastError().text());
        tables.insert(table, QJsonObject{{QStringLiteral("columns"), columns}, {QStringLiteral("rows"), rows}});
    }
    QJsonObject result{{QStringLiteral("snapshot_version"), 1}, {QStringLiteral("tables"), tables}};
    // Separate validation counters: capture has already charged allocations.
    ImportBudget validation(budget.limits());
    if (!validate(result, validation, error) || !budget.checkpoint()) return false;
    *snapshot = std::move(result);
    return true;
}
} // namespace

bool validateJsonBytes(const QByteArray& bytes, QString* error, ImportLimits limits)
{
    ImportBudget budget(limits);
    if (!budget.input(quint64(bytes.size())) || !budget.decoded(quint64(bytes.size()) * 2)) return fail(error, budget.error());
    int depth = 0;
    bool quotedString = false, escaped = false;
    quint64 stringBytes = 0;
    for (char ch : bytes) {
        if (!budget.checkpoint(1)) return fail(error, budget.error());
        if (quotedString) {
            if (++stringBytes > limits.maximumCellBytes) { budget.reject(ImportBudget::Failure::Cell); return fail(error, budget.error()); }
            if (escaped) escaped = false;
            else if (ch == char(0x5c)) escaped = true;
            else if (ch == '"') quotedString = false;
        } else if (ch == '"') { quotedString = true; stringBytes = 0; }
        else if (ch == '{' || ch == '[') {
            if (++depth > MaximumJsonDepth) return fail(error, QStringLiteral("The document JSON is nested too deeply."));
            if (!budget.decoded(64)) return fail(error, budget.error());
        } else if (ch == '}' || ch == ']') { if (--depth < 0) return fail(error, QStringLiteral("The document JSON is invalid.")); }
        else if (ch == ',' && !budget.decoded(64)) return fail(error, budget.error());
    }
    return (!quotedString && depth == 0) || fail(error, QStringLiteral("The document JSON is incomplete."));
}

bool readFile(const QString& path, QJsonObject* snapshot, QString* error, ImportLimits limits)
{
    ImportBudget budget(limits);
    quint64 total = 0;
    for (const auto& suffix : {QString(), QStringLiteral("-wal"), QStringLiteral("-journal")}) {
        const QFileInfo info(path + suffix);
        if (!info.exists() && !suffix.isEmpty()) continue;
        if (!info.isFile() || info.size() < 0) return fail(error, QStringLiteral("The document is not a regular file."));
        const quint64 size = quint64(info.size());
        if (size > limits.maximumInputBytes || total > limits.maximumInputBytes - size || !budget.input(total + size))
            return fail(error, budget.failed() ? budget.error() : QStringLiteral("The document and journal exceed the input-size limit."));
        total += size;
    }
    const QString connectionName = QStringLiteral("cardstack_snapshot_%1").arg(QUuid::createUuid().toString());
    bool result = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=250"));
        db.setDatabaseName(path);
        const auto read = [&]() {
            if (!db.open()) return fail(error, db.lastError().text());
            if (!exec(db, QStringLiteral("PRAGMA query_only=ON"), error) || !exec(db, QStringLiteral("PRAGMA trusted_schema=OFF"), error)
                || !exec(db, QStringLiteral("PRAGMA mmap_size=0"), error) || !exec(db, QStringLiteral("PRAGMA cache_size=-2048"), error)
                || !db.transaction()) return false;
            QSet<QString> tables;
            if (!physicalTables(db, &tables, budget, error)) return false;
            if ((!tables.contains(QStringLiteral("decks")) || !tables.contains(QStringLiteral("templates")))
                && (!tables.contains(QStringLiteral("deck")) || !tables.contains(QStringLiteral("fields"))))
                return fail(error, QStringLiteral("This SQLite file is not a CardStack document."));
            QSqlQuery query(db);
            if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next()) return fail(error, query.lastError().text());
            if (query.value(0).toInt() > CurrentSchemaVersion) return fail(error, QStringLiteral("This document requires a newer CardStack version."));
            if (!query.exec(QStringLiteral("PRAGMA application_id")) || !query.next()) return fail(error, query.lastError().text());
            const int appId = query.value(0).toInt();
            if (appId != 0 && appId != ApplicationId) return fail(error, QStringLiteral("This database belongs to another application."));
            return captureBounded(db, snapshot, budget, error);
        };
        result = read();
        db.rollback();
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return result;
}

bool capture(QSqlDatabase database, QJsonObject* snapshot, QString* error, ImportLimits limits)
{
    ImportBudget budget(limits);
    return captureBounded(database, snapshot, budget, error);
}

bool restore(QSqlDatabase database, const QJsonObject& snapshot, QString* error, ImportLimits limits)
{
    ImportBudget budget(limits);
    // Validate every row and typed value before altering or deleting any data.
    if (!validate(snapshot, budget, error)) return false;
    const auto tables = snapshot.value(QStringLiteral("tables")).toObject();
    if (!exec(database, QStringLiteral("PRAGMA foreign_keys=OFF"), error) || !database.transaction())
        return fail(error, database.lastError().text());
    const auto rollback = [&]() { database.rollback(); exec(database, QStringLiteral("PRAGMA foreign_keys=ON"), nullptr); return false; };
    if (tables.contains(QStringLiteral("deck"))
        && !exec(database, QStringLiteral("CREATE TABLE IF NOT EXISTS deck(id INTEGER PRIMARY KEY, name TEXT NOT NULL, description TEXT NOT NULL DEFAULT '')"), error)) return rollback();
    if (tables.contains(QStringLiteral("fields"))
        && !exec(database, QStringLiteral("CREATE TABLE IF NOT EXISTS fields(id INTEGER PRIMARY KEY, ordinal INTEGER NOT NULL, name TEXT NOT NULL, type TEXT NOT NULL, max_length INTEGER NOT NULL)"), error)) return rollback();
    QSet<QString> existing;
    if (!physicalTables(database, &existing, budget, error)) return rollback();
    for (auto it = Tables.crbegin(); it != Tables.crend(); ++it)
        if (existing.contains(*it) && !exec(database, QStringLiteral("DELETE FROM %1").arg(quoted(*it)), error)) return rollback();
    for (const auto& table : Tables) {
        if (!tables.contains(table)) continue;
        const auto data = tables.value(table).toObject();
        const auto columns = data.value(QStringLiteral("columns")).toArray();
        const QSqlRecord target = database.record(table);
        QStringList names, placeholders;
        for (const auto& column : columns) {
            if (target.indexOf(column.toString()) < 0) { fail(error, QStringLiteral("The document contains an unsupported column.")); return rollback(); }
            names.append(quoted(column.toString()));
            placeholders.append(QStringLiteral("?"));
        }
        QSqlQuery insert(database);
        if (!insert.prepare(QStringLiteral("INSERT INTO %1(%2) VALUES(%3)").arg(quoted(table), names.join(QLatin1Char(',')), placeholders.join(QLatin1Char(','))))) {
            fail(error, insert.lastError().text()); return rollback();
        }
        for (const auto& value : data.value(QStringLiteral("rows")).toArray()) {
            if (!budget.checkpoint(1)) { fail(error, budget.error()); return rollback(); }
            const auto row = value.toArray();
            for (int index = 0; index < row.size(); ++index) insert.bindValue(index, decode(row[index]));
            if (!insert.exec()) { fail(error, insert.lastError().text()); return rollback(); }
        }
    }
    if (!database.commit()) { fail(error, database.lastError().text()); return rollback(); }
    return exec(database, QStringLiteral("PRAGMA foreign_keys=ON"), error);
}

bool checkForeignKeys(QSqlDatabase database, QString* error)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("PRAGMA foreign_key_check"))) return fail(error, query.lastError().text());
    return !query.next() || fail(error, QStringLiteral("The document contains invalid data references."));
}
} // namespace CardStack::SQLiteSnapshot
