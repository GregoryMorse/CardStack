#include "SQLiteSnapshot.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>
#include <QUuid>

namespace CardStack::SQLiteSnapshot {
namespace {
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
    if (error != nullptr) *error = message;
    return false;
}
QString quoted(QString name)
{
    name.replace(QLatin1Char('"'), QStringLiteral("\"\""));
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
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
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
    if (object.contains(QStringLiteral("integer")))
        return object.value(QStringLiteral("integer")).toString().toLongLong();
    if (object.contains(QStringLiteral("real")))
        return object.value(QStringLiteral("real")).toDouble();
    const QString text = object.value(QStringLiteral("text")).toString();
    return text.isNull() ? QStringLiteral("") : text;
}
} // namespace

bool readFile(const QString& path, QJsonObject* snapshot, QString* error)
{
    const QString connectionName = QStringLiteral("cardstack_snapshot_%1").arg(QUuid::createUuid().toString());
    bool result = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        db.setDatabaseName(path);
        const auto read = [&]() {
            if (!db.open()) return fail(error, db.lastError().text());
            const QStringList tables = db.tables();
            if ((!tables.contains(QStringLiteral("decks")) || !tables.contains(QStringLiteral("templates")))
                && (!tables.contains(QStringLiteral("deck")) || !tables.contains(QStringLiteral("fields")))) {
                return fail(error, QStringLiteral("This SQLite file is not a CardStack document."));
            }
            QSqlQuery query(db);
            if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next())
                return fail(error, query.lastError().text());
            if (query.value(0).toInt() > CurrentSchemaVersion)
                return fail(error, QStringLiteral("This document requires a newer CardStack version."));
            if (!query.exec(QStringLiteral("PRAGMA application_id")) || !query.next())
                return fail(error, query.lastError().text());
            const int appId = query.value(0).toInt();
            if (appId != 0 && appId != ApplicationId)
                return fail(error, QStringLiteral("This database belongs to another application."));
            if (tables.contains(QStringLiteral("app_settings"))) {
                if (!query.exec(QStringLiteral("SELECT key, value FROM app_settings WHERE key IN ('schema_version', 'package_version')")))
                    return fail(error, query.lastError().text());
                while (query.next()) {
                    const int maximum = query.value(0).toString() == QLatin1String("schema_version")
                        ? CurrentSchemaVersion : 1;
                    bool valid = false;
                    const int version = query.value(1).toString().toInt(&valid);
                    if (!valid || version < 1 || version > maximum)
                        return fail(error, QStringLiteral("This document has an unsupported storage version."));
                }
            }
            return capture(db, snapshot, error);
        };
        result = read();
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return result;
}

bool capture(QSqlDatabase database, QJsonObject* snapshot, QString* error)
{
    if (snapshot == nullptr) return fail(error, QStringLiteral("Snapshot output is missing."));
    QJsonObject result;
    const QStringList existing = database.tables();
    for (const QString& table : Tables) {
        if (!existing.contains(table)) continue;
        QSqlQuery query(database);
        if (!query.exec(QStringLiteral("SELECT * FROM %1").arg(quoted(table))))
            return fail(error, query.lastError().text());
        const QSqlRecord record = query.record();
        QJsonArray columns;
        for (int index = 0; index < record.count(); ++index) columns.append(record.fieldName(index));
        QJsonArray rows;
        while (query.next()) {
            QJsonArray row;
            for (int index = 0; index < record.count(); ++index) row.append(encode(query.value(index)));
            rows.append(row);
        }
        result.insert(table, QJsonObject{{QStringLiteral("columns"), columns}, {QStringLiteral("rows"), rows}});
    }
    *snapshot = {{QStringLiteral("snapshot_version"), 1}, {QStringLiteral("tables"), result}};
    return true;
}

bool restore(QSqlDatabase database, const QJsonObject& snapshot, QString* error)
{
    if (snapshot.value(QStringLiteral("snapshot_version")).toInt() != 1
        || !snapshot.value(QStringLiteral("tables")).isObject())
        return fail(error, QStringLiteral("The document snapshot format is not supported."));
    const QJsonObject tables = snapshot.value(QStringLiteral("tables")).toObject();
    const auto settings = tables.value(QStringLiteral("app_settings")).toObject();
    const auto settingColumns = settings.value(QStringLiteral("columns")).toArray();
    int keyColumn = -1, valueColumn = -1;
    for (int index = 0; index < settingColumns.size(); ++index) {
        if (settingColumns[index].toString() == QLatin1String("key")) keyColumn = index;
        if (settingColumns[index].toString() == QLatin1String("value")) valueColumn = index;
    }
    if (keyColumn >= 0 && valueColumn >= 0) {
        for (const auto& entry : settings.value(QStringLiteral("rows")).toArray()) {
            const auto row = entry.toArray();
            if (row.size() <= std::max(keyColumn, valueColumn))
                return fail(error, QStringLiteral("The document settings are incomplete."));
            const QString key = decode(row[keyColumn]).toString();
            if (key != QLatin1String("schema_version") && key != QLatin1String("package_version")) continue;
            bool valid = false;
            const int version = decode(row[valueColumn]).toString().toInt(&valid);
            if (!valid || version < 1 || version > (key == QLatin1String("schema_version") ? CurrentSchemaVersion : 1))
                return fail(error, QStringLiteral("This document requires a newer CardStack version."));
        }
    }
    for (auto it = tables.begin(); it != tables.end(); ++it) {
        if (!Tables.contains(it.key()))
            return fail(error, QStringLiteral("The document contains an unsupported table."));
    }
    if (tables.contains(QStringLiteral("deck"))
        && !exec(database, QStringLiteral("CREATE TABLE IF NOT EXISTS deck(id INTEGER PRIMARY KEY, name TEXT NOT NULL, description TEXT NOT NULL DEFAULT '')"), error))
        return false;
    if (tables.contains(QStringLiteral("fields"))
        && !exec(database, QStringLiteral("CREATE TABLE IF NOT EXISTS fields(id INTEGER PRIMARY KEY, ordinal INTEGER NOT NULL, name TEXT NOT NULL, type TEXT NOT NULL, max_length INTEGER NOT NULL)"), error))
        return false;
    if (!exec(database, QStringLiteral("PRAGMA foreign_keys = OFF"), error) || !database.transaction())
        return fail(error, database.lastError().text());
    const auto rollback = [&]() {
        database.rollback();
        exec(database, QStringLiteral("PRAGMA foreign_keys = ON"), nullptr);
        return false;
    };
    const QStringList existing = database.tables();
    for (auto it = Tables.crbegin(); it != Tables.crend(); ++it) {
        if (existing.contains(*it) && !exec(database, QStringLiteral("DELETE FROM %1").arg(quoted(*it)), error))
            return rollback();
    }
    for (const QString& table : Tables) {
        if (!tables.contains(table)) continue;
        const QJsonObject data = tables.value(table).toObject();
        const QJsonArray columns = data.value(QStringLiteral("columns")).toArray();
        const QSqlRecord target = database.record(table);
        QStringList names, placeholders;
        for (const QJsonValue& column : columns) {
            if (!column.isString() || target.indexOf(column.toString()) < 0) {
                fail(error, QStringLiteral("The document contains an unsupported column."));
                return rollback();
            }
            names.append(quoted(column.toString()));
            placeholders.append(QStringLiteral("?"));
        }
        if (names.isEmpty()) {
            fail(error, QStringLiteral("The document contains an empty table definition."));
            return rollback();
        }
        QSqlQuery insert(database);
        if (!insert.prepare(QStringLiteral("INSERT INTO %1(%2) VALUES(%3)")
                .arg(quoted(table), names.join(QLatin1Char(',')), placeholders.join(QLatin1Char(','))))) {
            fail(error, insert.lastError().text());
            return rollback();
        }
        for (const QJsonValue& value : data.value(QStringLiteral("rows")).toArray()) {
            const QJsonArray row = value.toArray();
            if (row.size() != columns.size()) {
                fail(error, QStringLiteral("The document contains an incomplete row."));
                return rollback();
            }
            for (int index = 0; index < row.size(); ++index) insert.bindValue(index, decode(row[index]));
            if (!insert.exec()) {
                fail(error, insert.lastError().text());
                return rollback();
            }
        }
    }
    if (!database.commit()) {
        fail(error, database.lastError().text());
        return rollback();
    }
    return exec(database, QStringLiteral("PRAGMA foreign_keys = ON"), error);
}

bool checkForeignKeys(QSqlDatabase database, QString* error)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("PRAGMA foreign_key_check"))) return fail(error, query.lastError().text());
    return !query.next() || fail(error, QStringLiteral("The document contains invalid data references."));
}
} // namespace CardStack::SQLiteSnapshot
