#include "SQLiteDeckRepository.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <utility>

namespace CardStack {
namespace {

void setError(QString* target, const QSqlError& error)
{
    if (target != nullptr) {
        *target = error.text();
    }
}

void setError(QString* target, const QString& message)
{
    if (target != nullptr) {
        *target = message;
    }
}

QString fieldTypeToString(FieldType type)
{
    switch (type) {
    case FieldType::Notes:
        return QStringLiteral("notes");
    case FieldType::Text:
        return QStringLiteral("text");
    }

    return QStringLiteral("text");
}

FieldType fieldTypeFromString(const QString& value)
{
    if (value == QStringLiteral("notes")) {
        return FieldType::Notes;
    }

    return FieldType::Text;
}

QString profileTypeToString(ImportExportProfileType type)
{
    return type == ImportExportProfileType::Import ? QStringLiteral("import") : QStringLiteral("export");
}

ImportExportProfileType profileTypeFromString(const QString& value)
{
    return value == QStringLiteral("import") ? ImportExportProfileType::Import : ImportExportProfileType::Export;
}

QString delimitedFormatToString(DelimitedTextFormat format)
{
    return format == DelimitedTextFormat::Tsv ? QStringLiteral("tsv") : QStringLiteral("csv");
}

DelimitedTextFormat delimitedFormatFromString(const QString& value)
{
    return value == QStringLiteral("tsv") ? DelimitedTextFormat::Tsv : DelimitedTextFormat::Csv;
}

QString mappingToJson(const QVector<int>& fieldMappings)
{
    QJsonArray array;
    for (int mapping : fieldMappings) {
        array.append(mapping);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QVector<int> mappingFromJson(const QString& value)
{
    QVector<int> mappings;
    const QJsonDocument document = QJsonDocument::fromJson(value.toUtf8());
    if (!document.isArray()) {
        return mappings;
    }

    const QJsonArray array = document.array();
    mappings.reserve(array.size());
    for (const QJsonValue& item : array) {
        mappings.append(item.toInt(-1));
    }
    return mappings;
}

QString nonNull(QString value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

} // namespace

SQLiteDeckRepository::SQLiteDeckRepository(QSqlDatabase database)
    : m_database(std::move(database))
{
}

bool SQLiteDeckRepository::clearDeckGraph(QString* errorMessage)
{
    return exec(QStringLiteral("DELETE FROM report_frames"), errorMessage)
        && exec(QStringLiteral("DELETE FROM reports"), errorMessage)
        && exec(QStringLiteral("DELETE FROM card_values"), errorMessage)
        && exec(QStringLiteral("DELETE FROM cards"), errorMessage)
        && exec(QStringLiteral("DELETE FROM phone_profiles"), errorMessage)
        && exec(QStringLiteral("DELETE FROM import_export_profiles"), errorMessage)
        && exec(QStringLiteral("DELETE FROM deck_formatting"), errorMessage)
        && exec(QStringLiteral("DELETE FROM deck_sort_keys"), errorMessage)
        && exec(QStringLiteral("DELETE FROM template_frames"), errorMessage)
        && exec(QStringLiteral("DELETE FROM template_fields"), errorMessage)
        && exec(QStringLiteral("DELETE FROM templates"), errorMessage)
        && exec(QStringLiteral("DELETE FROM decks"), errorMessage)
        && execIfTableExists(QStringLiteral("fields"), QStringLiteral("DELETE FROM fields"), errorMessage)
        && execIfTableExists(QStringLiteral("deck"), QStringLiteral("DELETE FROM deck"), errorMessage);
}

bool SQLiteDeckRepository::saveDeckShell(const Deck& deck, QString* errorMessage)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO decks(id, uuid, name, description, security_password, security_encrypted, source_format, legacy_metadata_json, legacy_control_record)"
        " VALUES(1, lower(hex(randomblob(16))), ?, ?, ?, ?, 'cardstack-sqlite', '{}', ?)"));
    query.addBindValue(nonNull(deck.name()));
    query.addBindValue(QStringLiteral(""));
    query.addBindValue(nonNull(deck.securityPassword()));
    query.addBindValue(deck.hasEncryptedSecurity() ? 1 : 0);
    query.addBindValue(deck.legacyControlRecord().isNull() ? QByteArray("") : deck.legacyControlRecord());
    if (!query.exec()) {
        setError(errorMessage, query.lastError());
        return false;
    }

    return true;
}

bool SQLiteDeckRepository::loadDeckShell(Deck* deck, QString* errorMessage)
{
    if (deck == nullptr) {
        setError(errorMessage, QStringLiteral("Deck output is null."));
        return false;
    }

    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT d.name, t.description, d.security_password, d.security_encrypted, d.legacy_control_record"
            " FROM decks d JOIN templates t ON t.deck_id = d.id"
            " WHERE d.id = 1 ORDER BY t.ordinal LIMIT 1"))) {
        setError(errorMessage, query.lastError());
        return false;
    }

    if (query.next()) {
        deck->setName(query.value(0).toString());
        deck->setDescription(query.value(1).toString());
        const QString securityPassword = query.value(2).toString();
        if (securityPassword.isEmpty()) {
            deck->clearSecurity();
        } else {
            deck->setSecurity(securityPassword, query.value(3).toInt() != 0);
        }
        deck->setLegacyControlRecord(query.value(4).toByteArray());
    }

    return true;
}

bool SQLiteDeckRepository::saveDefaultTemplate(const Deck& deck, QString* errorMessage)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO templates(id, deck_id, ordinal, name, description)"
        " VALUES(1, 1, 0, ?, ?)"));
    query.addBindValue(deck.name().isEmpty() ? QStringLiteral("Default") : deck.name());
    query.addBindValue(nonNull(deck.description()));
    if (!query.exec()) {
        setError(errorMessage, query.lastError());
        return false;
    }

    return true;
}

bool SQLiteDeckRepository::loadDefaultTemplateId(int* templateId, QString* errorMessage)
{
    if (templateId == nullptr) {
        setError(errorMessage, QStringLiteral("Template id output is null."));
        return false;
    }

    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT id FROM templates WHERE deck_id = 1 ORDER BY ordinal LIMIT 1"))) {
        setError(errorMessage, query.lastError());
        return false;
    }

    *templateId = query.next() ? query.value(0).toInt() : 0;
    return true;
}

bool SQLiteDeckRepository::saveFields(const Deck& deck, int templateId, QString* errorMessage)
{
    QSqlQuery fieldQuery(m_database);
    fieldQuery.prepare(QStringLiteral(
        "INSERT INTO template_fields(id, template_id, ordinal, name, type, max_length, show_name, is_phone, display_width, legacy_descriptor)"
        " VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));

    bool hasEarlyFieldsTable = false;
    if (!tableExists(QStringLiteral("fields"), &hasEarlyFieldsTable, errorMessage)) {
        return false;
    }

    QSqlQuery earlyFieldQuery(m_database);
    if (hasEarlyFieldsTable) {
        earlyFieldQuery.prepare(QStringLiteral(
            "INSERT INTO fields(id, ordinal, name, type, max_length) VALUES(?, ?, ?, ?, ?)"));
    }

    for (int i = 0; i < deck.fieldCount(); ++i) {
        const FieldDefinition& field = deck.fieldAt(i);
        fieldQuery.bindValue(0, i + 1);
        fieldQuery.bindValue(1, templateId);
        fieldQuery.bindValue(2, i);
        fieldQuery.bindValue(3, nonNull(field.name()));
        fieldQuery.bindValue(4, fieldTypeToString(field.type()));
        fieldQuery.bindValue(5, field.maxLength());
        fieldQuery.bindValue(6, field.showName() ? 1 : 0);
        fieldQuery.bindValue(7, field.isPhone() ? 1 : 0);
        fieldQuery.bindValue(8, field.displayWidth());
        fieldQuery.bindValue(9, field.legacyDescriptor().isNull() ? QByteArray("") : field.legacyDescriptor());
        if (!fieldQuery.exec()) {
            setError(errorMessage, fieldQuery.lastError());
            return false;
        }

        if (hasEarlyFieldsTable) {
            earlyFieldQuery.bindValue(0, i + 1);
            earlyFieldQuery.bindValue(1, i);
            earlyFieldQuery.bindValue(2, nonNull(field.name()));
            earlyFieldQuery.bindValue(3, fieldTypeToString(field.type()));
            earlyFieldQuery.bindValue(4, field.maxLength());
            if (!earlyFieldQuery.exec()) {
                setError(errorMessage, earlyFieldQuery.lastError());
                return false;
            }
        }
    }

    return true;
}

bool SQLiteDeckRepository::loadFields(int templateId, Deck* deck, QString* errorMessage)
{
    if (deck == nullptr) {
        setError(errorMessage, QStringLiteral("Deck output is null."));
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT name, type, max_length, show_name, is_phone, legacy_descriptor, display_width FROM template_fields WHERE template_id = ? ORDER BY ordinal"));
    query.addBindValue(templateId);
    if (!query.exec()) {
        setError(errorMessage, query.lastError());
        return false;
    }

    while (query.next()) {
        deck->addField(FieldDefinition(
            query.value(0).toString(),
            fieldTypeFromString(query.value(1).toString()),
            query.value(2).toInt(),
            query.value(3).toInt() != 0,
            query.value(4).toInt() != 0,
            query.value(5).toByteArray(),
            query.value(6).toInt()));
    }

    return true;
}

bool SQLiteDeckRepository::saveSortKeys(const Deck& deck, QString* errorMessage)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO deck_sort_keys(deck_id, ordinal, field_id, direction, case_sensitive)"
        " VALUES(1, ?, ?, ?, 0)"));

    int ordinal = 0;
    for (const DeckSortKey& key : deck.sortKeys()) {
        if (key.fieldIndex < 0 || key.fieldIndex >= deck.fieldCount()) {
            continue;
        }

        query.bindValue(0, ordinal++);
        query.bindValue(1, key.fieldIndex + 1);
        query.bindValue(2, key.descending ? QStringLiteral("descending") : QStringLiteral("ascending"));
        if (!query.exec()) {
            setError(errorMessage, query.lastError());
            return false;
        }
    }

    return true;
}

bool SQLiteDeckRepository::loadSortKeys(Deck* deck, QString* errorMessage)
{
    if (deck == nullptr) {
        setError(errorMessage, QStringLiteral("Deck output is null."));
        return false;
    }

    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT field_id, direction FROM deck_sort_keys WHERE deck_id = 1 ORDER BY ordinal"))) {
        setError(errorMessage, query.lastError());
        return false;
    }

    QVector<DeckSortKey> sortKeys;
    while (query.next()) {
        DeckSortKey key;
        key.fieldIndex = query.value(0).toInt() - 1;
        key.descending = query.value(1).toString() == QStringLiteral("descending");
        if (key.fieldIndex >= 0 && key.fieldIndex < deck->fieldCount()) {
            sortKeys.append(key);
        }
    }

    deck->setSortKeys(std::move(sortKeys));
    return true;
}

bool SQLiteDeckRepository::saveAppearance(const Deck& deck, QString* errorMessage)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO deck_formatting(deck_id, scope, target_id, font_family, font_size, foreground_color, background_color, alignment, metadata_json)"
        " VALUES(1, ?, ?, ?, 0, ?, '', ?, '{}')"));

    const DeckAppearance& appearance = deck.appearance();
    const QVector<QString> fonts = {appearance.dataFont, appearance.nameFont, appearance.textFont, appearance.indexFont};
    for (int index = 0; index < fonts.size(); ++index) {
        query.bindValue(0, QStringLiteral("template-font"));
        query.bindValue(1, index);
        query.bindValue(2, nonNull(fonts.at(index)));
        query.bindValue(3, QStringLiteral(""));
        query.bindValue(4, QStringLiteral(""));
        if (!query.exec()) {
            setError(errorMessage, query.lastError());
            return false;
        }
    }

    for (int index = 0; index < static_cast<int>(DeckColorRole::Count); ++index) {
        query.bindValue(0, QStringLiteral("template-color"));
        query.bindValue(1, index);
        query.bindValue(2, QStringLiteral(""));
        query.bindValue(3, index < appearance.customColors.size() ? nonNull(appearance.customColors.at(index)) : QString());
        query.bindValue(4, QStringLiteral(""));
        if (!query.exec()) {
            setError(errorMessage, query.lastError());
            return false;
        }
    }

    query.bindValue(0, QStringLiteral("template-color-mode"));
    query.bindValue(1, 0);
    query.bindValue(2, QStringLiteral(""));
    query.bindValue(3, QStringLiteral(""));
    query.bindValue(4, appearance.useSystemColors ? QStringLiteral("system") : QStringLiteral("custom"));
    if (!query.exec()) {
        setError(errorMessage, query.lastError());
        return false;
    }
    return true;
}

bool SQLiteDeckRepository::loadAppearance(Deck* deck, QString* errorMessage)
{
    if (deck == nullptr) {
        setError(errorMessage, QStringLiteral("Deck output is null."));
        return false;
    }

    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT scope, target_id, font_family, foreground_color, alignment"
            " FROM deck_formatting WHERE deck_id = 1 ORDER BY scope, target_id"))) {
        setError(errorMessage, query.lastError());
        return false;
    }

    DeckAppearance appearance;
    while (query.next()) {
        const QString scope = query.value(0).toString();
        const int target = query.value(1).toInt();
        if (scope == QStringLiteral("template-font")) {
            const QString value = query.value(2).toString();
            switch (target) {
            case 0: appearance.dataFont = value; break;
            case 1: appearance.nameFont = value; break;
            case 2: appearance.textFont = value; break;
            case 3: appearance.indexFont = value; break;
            default: break;
            }
        } else if (scope == QStringLiteral("template-color")
                   && target >= 0
                   && target < static_cast<int>(DeckColorRole::Count)) {
            appearance.customColors[target] = query.value(3).toString();
        } else if (scope == QStringLiteral("template-color-mode")) {
            appearance.useSystemColors = query.value(4).toString() != QStringLiteral("custom");
        }
    }
    deck->setAppearance(std::move(appearance));
    return true;
}

bool SQLiteDeckRepository::saveImportExportProfiles(const Deck& deck, QString* errorMessage)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO import_export_profiles("
        "id, deck_id, name, profile_type, format, delimiter, has_header, mapping_json, options_json"
        ") VALUES(?, 1, ?, ?, ?, ?, ?, ?, '{}')"));

    int profileId = 1;
    for (const ImportExportProfile& profile : deck.importExportProfiles()) {
        query.bindValue(0, profileId++);
        query.bindValue(1, nonNull(profile.name));
        query.bindValue(2, profileTypeToString(profile.type));
        query.bindValue(3, delimitedFormatToString(profile.format));
        query.bindValue(4, QString(profile.delimiter));
        query.bindValue(5, profile.hasHeader ? 1 : 0);
        query.bindValue(6, mappingToJson(profile.fieldMappings));
        if (!query.exec()) {
            setError(errorMessage, query.lastError());
            return false;
        }
    }

    return true;
}

bool SQLiteDeckRepository::loadImportExportProfiles(Deck* deck, QString* errorMessage)
{
    if (deck == nullptr) {
        setError(errorMessage, QStringLiteral("Deck output is null."));
        return false;
    }

    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT name, profile_type, format, delimiter, has_header, mapping_json"
            " FROM import_export_profiles WHERE deck_id = 1 ORDER BY id"))) {
        setError(errorMessage, query.lastError());
        return false;
    }

    QVector<ImportExportProfile> profiles;
    while (query.next()) {
        ImportExportProfile profile;
        profile.name = query.value(0).toString();
        profile.type = profileTypeFromString(query.value(1).toString());
        profile.format = delimitedFormatFromString(query.value(2).toString());
        const QString delimiter = query.value(3).toString();
        profile.delimiter = delimiter.isEmpty() ? QLatin1Char(',') : delimiter.at(0);
        profile.hasHeader = query.value(4).toInt() != 0;
        profile.fieldMappings = mappingFromJson(query.value(5).toString());
        profiles.append(std::move(profile));
    }

    deck->setImportExportProfiles(std::move(profiles));
    return true;
}

bool SQLiteDeckRepository::saveCards(const Deck& deck, QString* errorMessage)
{
    QSqlQuery cardQuery(m_database);
    cardQuery.prepare(QStringLiteral("INSERT INTO cards(id, deck_id, ordinal) VALUES(?, 1, ?)"));

    QSqlQuery valueQuery(m_database);
    valueQuery.prepare(QStringLiteral(
        "INSERT INTO card_values(card_id, field_id, value) VALUES(?, ?, ?)"));

    for (int cardIndex = 0; cardIndex < deck.cardCount(); ++cardIndex) {
        const int cardId = cardIndex + 1;
        const CardRecord& card = deck.cardAt(cardIndex);

        cardQuery.bindValue(0, cardId);
        cardQuery.bindValue(1, cardIndex);
        if (!cardQuery.exec()) {
            setError(errorMessage, cardQuery.lastError());
            return false;
        }

        for (int fieldIndex = 0; fieldIndex < deck.fieldCount(); ++fieldIndex) {
            const QString value = card.valueAt(fieldIndex);
            valueQuery.bindValue(0, cardId);
            valueQuery.bindValue(1, fieldIndex + 1);
            valueQuery.bindValue(2, nonNull(value));
            if (!valueQuery.exec()) {
                setError(errorMessage, valueQuery.lastError());
                return false;
            }
        }
    }

    return true;
}

bool SQLiteDeckRepository::loadCards(int templateId, Deck* deck, QString* errorMessage)
{
    if (deck == nullptr) {
        setError(errorMessage, QStringLiteral("Deck output is null."));
        return false;
    }

    QSqlQuery cardQuery(m_database);
    if (!cardQuery.exec(QStringLiteral("SELECT id FROM cards WHERE deck_id = 1 ORDER BY ordinal"))) {
        setError(errorMessage, cardQuery.lastError());
        return false;
    }

    QSqlQuery valueQuery(m_database);
    valueQuery.prepare(QStringLiteral(
        "SELECT COALESCE(card_values.value, '') "
        "FROM template_fields "
        "LEFT JOIN card_values ON card_values.field_id = template_fields.id AND card_values.card_id = ? "
        "WHERE template_fields.template_id = ? "
        "ORDER BY template_fields.ordinal"));

    while (cardQuery.next()) {
        const int cardId = cardQuery.value(0).toInt();
        CardRecord card;

        valueQuery.bindValue(0, cardId);
        valueQuery.bindValue(1, templateId);
        if (!valueQuery.exec()) {
            setError(errorMessage, valueQuery.lastError());
            return false;
        }

        while (valueQuery.next()) {
            card.appendValue(valueQuery.value(0).toString());
        }

        deck->addCard(std::move(card));
    }

    return true;
}

bool SQLiteDeckRepository::tableExists(const QString& tableName, bool* exists, QString* errorMessage)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ? LIMIT 1"));
    query.addBindValue(tableName);
    if (!query.exec()) {
        setError(errorMessage, query.lastError());
        return false;
    }

    if (exists != nullptr) {
        *exists = query.next();
    }
    return true;
}

bool SQLiteDeckRepository::execIfTableExists(
    const QString& tableName,
    const QString& sql,
    QString* errorMessage)
{
    bool exists = false;
    if (!tableExists(tableName, &exists, errorMessage)) {
        return false;
    }

    return !exists || exec(sql, errorMessage);
}

bool SQLiteDeckRepository::exec(const QString& sql, QString* errorMessage)
{
    QSqlQuery query(m_database);
    if (!query.exec(sql)) {
        setError(errorMessage, query.lastError());
        return false;
    }

    return true;
}

} // namespace CardStack
