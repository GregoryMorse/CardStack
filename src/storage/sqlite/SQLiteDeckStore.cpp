#include "SQLiteDeckStore.h"

#include "SQLiteDeckRepository.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
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

int reportFormTypeToInt(ReportFormType type)
{
    return static_cast<int>(type);
}

ReportFormType reportFormTypeFromInt(int value)
{
    switch (value) {
    case static_cast<int>(ReportFormType::Card):
        return ReportFormType::Card;
    case static_cast<int>(ReportFormType::Label):
        return ReportFormType::Label;
    case static_cast<int>(ReportFormType::Report):
        return ReportFormType::Report;
    default:
        return ReportFormType::Unknown;
    }
}

int reportFrameKindToInt(ReportFrameKind kind)
{
    return static_cast<int>(kind);
}

ReportFrameKind reportFrameKindFromInt(int value)
{
    switch (value) {
    case static_cast<int>(ReportFrameKind::Text):
        return ReportFrameKind::Text;
    case static_cast<int>(ReportFrameKind::Data):
        return ReportFrameKind::Data;
    case static_cast<int>(ReportFrameKind::SystemText):
        return ReportFrameKind::SystemText;
    case static_cast<int>(ReportFrameKind::LineOrBox):
        return ReportFrameKind::LineOrBox;
    default:
        return ReportFrameKind::Unknown;
    }
}

int cardTemplateFrameKindToInt(CardTemplateFrameKind kind)
{
    return static_cast<int>(kind);
}

CardTemplateFrameKind cardTemplateFrameKindFromInt(int value)
{
    switch (value) {
    case static_cast<int>(CardTemplateFrameKind::Text):
        return CardTemplateFrameKind::Text;
    case static_cast<int>(CardTemplateFrameKind::DataBox):
        return CardTemplateFrameKind::DataBox;
    case static_cast<int>(CardTemplateFrameKind::NotesBox):
        return CardTemplateFrameKind::NotesBox;
    case static_cast<int>(CardTemplateFrameKind::LineOrBox):
        return CardTemplateFrameKind::LineOrBox;
    default:
        return CardTemplateFrameKind::DataBox;
    }
}

int cardTemplateLineBoxShapeToInt(CardTemplateLineBoxShape shape)
{
    return static_cast<int>(shape);
}

CardTemplateLineBoxShape cardTemplateLineBoxShapeFromInt(int value)
{
    switch (value) {
    case static_cast<int>(CardTemplateLineBoxShape::HorizontalLine):
        return CardTemplateLineBoxShape::HorizontalLine;
    case static_cast<int>(CardTemplateLineBoxShape::VerticalLine):
        return CardTemplateLineBoxShape::VerticalLine;
    case static_cast<int>(CardTemplateLineBoxShape::Box):
    default:
        return CardTemplateLineBoxShape::Box;
    }
}

QString stringVectorToJson(const QVector<QString>& values)
{
    QJsonArray array;
    for (const QString& value : values) {
        array.append(value);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QVector<QString> stringVectorFromJson(const QString& value)
{
    QVector<QString> values;
    const QJsonDocument document = QJsonDocument::fromJson(value.toUtf8());
    if (!document.isArray()) {
        return values;
    }

    const QJsonArray array = document.array();
    values.reserve(array.size());
    for (const QJsonValue& item : array) {
        values.append(item.toString());
    }
    return values;
}

} // namespace

SQLiteDeckStore::SQLiteDeckStore()
    : m_connectionName(QStringLiteral("cardstack_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

SQLiteDeckStore::~SQLiteDeckStore()
{
    close();
}

bool SQLiteDeckStore::open(const QString& filePath, QString* errorMessage)
{
    close();

    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(filePath);

    if (!m_database.open()) {
        setError(errorMessage, m_database.lastError());
        return false;
    }

    return ensureSchema(errorMessage);
}

bool SQLiteDeckStore::saveDeck(const Deck& deck, QString* errorMessage)
{
    if (!m_database.isOpen()) {
        setError(errorMessage, QStringLiteral("SQLite database is not open."));
        return false;
    }

    if (!m_database.transaction()) {
        setError(errorMessage, m_database.lastError());
        return false;
    }

    const auto rollback = [this] {
        m_database.rollback();
    };

    SQLiteDeckRepository repository(m_database);
    if (!repository.clearDeckGraph(errorMessage)
        || !repository.saveDeckShell(deck, errorMessage)
        || !repository.saveDefaultTemplate(deck, errorMessage)
        || !repository.saveFields(deck, 1, errorMessage)
        || !saveCardTemplateLayout(deck, 1, errorMessage)
        || !repository.saveSortKeys(deck, errorMessage)
        || !repository.saveAppearance(deck, errorMessage)
        || !repository.saveImportExportProfiles(deck, errorMessage)
        || !repository.saveCards(deck, errorMessage)) {
        rollback();
        return false;
    }

    if (!saveReports(deck, errorMessage)) {
        rollback();
        return false;
    }

    if (!m_database.commit()) {
        setError(errorMessage, m_database.lastError());
        return false;
    }

    return true;
}

bool SQLiteDeckStore::loadDeck(Deck* deck, QString* errorMessage)
{
    if (deck == nullptr) {
        setError(errorMessage, QStringLiteral("Deck output is null."));
        return false;
    }

    if (!m_database.isOpen()) {
        setError(errorMessage, QStringLiteral("SQLite database is not open."));
        return false;
    }

    Deck loaded;
    SQLiteDeckRepository repository(m_database);
    if (!repository.loadDeckShell(&loaded, errorMessage)) {
        return false;
    }

    int templateId = 0;
    if (!repository.loadDefaultTemplateId(&templateId, errorMessage)) {
        return false;
    }

    if (!repository.loadFields(templateId, &loaded, errorMessage)
        || !loadCardTemplateLayout(&loaded, templateId, errorMessage)
        || !repository.loadSortKeys(&loaded, errorMessage)
        || !repository.loadAppearance(&loaded, errorMessage)
        || !repository.loadImportExportProfiles(&loaded, errorMessage)
        || !repository.loadCards(templateId, &loaded, errorMessage)) {
        return false;
    }

    if (!loadReports(&loaded, errorMessage)) {
        return false;
    }

    *deck = std::move(loaded);
    return true;
}

bool SQLiteDeckStore::setAppSetting(const QString& key, const QString& value, QString* errorMessage)
{
    if (!m_database.isOpen()) {
        setError(errorMessage, QStringLiteral("SQLite database is not open."));
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO app_settings(key, value, updated_at)"
        " VALUES(?, ?, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))"));
    query.addBindValue(key);
    query.addBindValue(value);
    if (!query.exec()) {
        setError(errorMessage, query.lastError());
        return false;
    }

    return true;
}

bool SQLiteDeckStore::appSetting(const QString& key, QString* value, QString* errorMessage) const
{
    if (value == nullptr) {
        setError(errorMessage, QStringLiteral("App setting output is null."));
        return false;
    }

    if (!m_database.isOpen()) {
        setError(errorMessage, QStringLiteral("SQLite database is not open."));
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT value FROM app_settings WHERE key = ?"));
    query.addBindValue(key);
    if (!query.exec()) {
        setError(errorMessage, query.lastError());
        return false;
    }

    if (!query.next()) {
        value->clear();
        return true;
    }

    *value = query.value(0).toString();
    return true;
}

void SQLiteDeckStore::close()
{
    if (m_database.isValid()) {
        m_database.close();
    }

    m_database = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool SQLiteDeckStore::ensureSchema(QString* errorMessage)
{
    if (!exec(QStringLiteral("PRAGMA foreign_keys = ON"), errorMessage)
        || !exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS schema_migrations("
            "version INTEGER PRIMARY KEY,"
            "name TEXT NOT NULL,"
            "applied_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))"
            ")"), errorMessage)
        || !exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS app_settings("
            "key TEXT PRIMARY KEY,"
            "value TEXT NOT NULL,"
            "updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))"
            ")"), errorMessage)
        || !exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS decks("
            "id INTEGER PRIMARY KEY CHECK(id = 1),"
            "uuid TEXT NOT NULL UNIQUE,"
            "name TEXT NOT NULL,"
            "description TEXT NOT NULL DEFAULT '',"
            "security_password TEXT NOT NULL DEFAULT '',"
            "security_encrypted INTEGER NOT NULL DEFAULT 0,"
            "source_format TEXT NOT NULL DEFAULT 'cardstack-sqlite',"
            "legacy_metadata_json TEXT NOT NULL DEFAULT '{}',"
            "legacy_control_record BLOB NOT NULL DEFAULT X'',"
            "created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),"
            "updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))"
            ")"), errorMessage)
        || !ensureColumn(
            QStringLiteral("decks"),
            QStringLiteral("security_password"),
            QStringLiteral("ALTER TABLE decks ADD COLUMN security_password TEXT NOT NULL DEFAULT ''"),
            errorMessage)
        || !ensureColumn(
            QStringLiteral("decks"),
            QStringLiteral("security_encrypted"),
            QStringLiteral("ALTER TABLE decks ADD COLUMN security_encrypted INTEGER NOT NULL DEFAULT 0"),
            errorMessage)
        || !exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS templates("
            "id INTEGER PRIMARY KEY,"
            "deck_id INTEGER NOT NULL REFERENCES decks(id) ON DELETE CASCADE,"
            "ordinal INTEGER NOT NULL,"
            "name TEXT NOT NULL,"
            "description TEXT NOT NULL DEFAULT '',"
            "legacy_metadata_json TEXT NOT NULL DEFAULT '{}',"
            "UNIQUE(deck_id, ordinal)"
            ")"), errorMessage)
        || !exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS template_fields("
            "id INTEGER PRIMARY KEY,"
            "template_id INTEGER NOT NULL REFERENCES templates(id) ON DELETE CASCADE,"
            "ordinal INTEGER NOT NULL,"
            "name TEXT NOT NULL,"
            "type TEXT NOT NULL,"
            "max_length INTEGER NOT NULL,"
            "show_name INTEGER NOT NULL DEFAULT 1,"
            "is_phone INTEGER NOT NULL DEFAULT 0,"
            "legacy_descriptor BLOB NOT NULL DEFAULT X'',"
            "display_width INTEGER NOT NULL DEFAULT 0,"
            "alignment TEXT NOT NULL DEFAULT 'left',"
            "is_required INTEGER NOT NULL DEFAULT 0,"
            "default_value TEXT NOT NULL DEFAULT '',"
            "legacy_offset INTEGER NOT NULL DEFAULT 0,"
            "legacy_length INTEGER NOT NULL DEFAULT 0,"
            "metadata_json TEXT NOT NULL DEFAULT '{}',"
            "UNIQUE(template_id, ordinal)"
            ")"), errorMessage)
        || !ensureColumn(
            QStringLiteral("template_fields"),
            QStringLiteral("show_name"),
            QStringLiteral("ALTER TABLE template_fields ADD COLUMN show_name INTEGER NOT NULL DEFAULT 1"),
            errorMessage)
        || !ensureColumn(
            QStringLiteral("template_fields"),
            QStringLiteral("is_phone"),
            QStringLiteral("ALTER TABLE template_fields ADD COLUMN is_phone INTEGER NOT NULL DEFAULT 0"),
            errorMessage)) {
        return false;
    }

    if (!ensureColumn(
            QStringLiteral("decks"),
            QStringLiteral("legacy_control_record"),
            QStringLiteral("ALTER TABLE decks ADD COLUMN legacy_control_record BLOB NOT NULL DEFAULT X''"),
            errorMessage)
        || !ensureColumn(
            QStringLiteral("template_fields"),
            QStringLiteral("legacy_descriptor"),
            QStringLiteral("ALTER TABLE template_fields ADD COLUMN legacy_descriptor BLOB NOT NULL DEFAULT X''"),
            errorMessage)) {
        return false;
    }

    if (!exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS template_frames("
        "template_id INTEGER NOT NULL REFERENCES templates(id) ON DELETE CASCADE,"
        "ordinal INTEGER NOT NULL,"
        "kind INTEGER NOT NULL,"
        "field_index INTEGER NOT NULL DEFAULT -1,"
        "left_pos INTEGER NOT NULL,"
        "top_pos INTEGER NOT NULL,"
        "width INTEGER NOT NULL,"
        "height INTEGER NOT NULL,"
        "text TEXT NOT NULL DEFAULT '',"
        "style_flags INTEGER NOT NULL DEFAULT 0,"
        "line_box_shape INTEGER NOT NULL DEFAULT 0,"
        "line_style INTEGER NOT NULL DEFAULT 0,"
        "fill_pattern INTEGER NOT NULL DEFAULT 0,"
        "corner_radius INTEGER NOT NULL DEFAULT 0,"
        "legacy_descriptor BLOB NOT NULL DEFAULT X'',"
        "PRIMARY KEY(template_id, ordinal)"
        ")"), errorMessage)) {
        return false;
    }
    if (!ensureColumn(
            QStringLiteral("template_frames"),
            QStringLiteral("legacy_descriptor"),
            QStringLiteral("ALTER TABLE template_frames ADD COLUMN legacy_descriptor BLOB NOT NULL DEFAULT X''"),
            errorMessage)) {
        return false;
    }

    bool hasEarlyDeckTable = false;
    if (!tableExists(QStringLiteral("deck"), &hasEarlyDeckTable, errorMessage)) {
        return false;
    }
    if (hasEarlyDeckTable && !ensureLegacyDeckDescriptionColumn(errorMessage)) {
        return false;
    }

    if (!exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS cards("
        "id INTEGER PRIMARY KEY,"
        "deck_id INTEGER NOT NULL REFERENCES decks(id) ON DELETE CASCADE DEFAULT 1,"
        "ordinal INTEGER NOT NULL"
        ")"), errorMessage)
        || !ensureColumn(
            QStringLiteral("cards"),
            QStringLiteral("deck_id"),
            QStringLiteral("ALTER TABLE cards ADD COLUMN deck_id INTEGER NOT NULL DEFAULT 1"),
            errorMessage)
        || !exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS card_values("
        "card_id INTEGER NOT NULL REFERENCES cards(id) ON DELETE CASCADE,"
        "field_id INTEGER NOT NULL REFERENCES template_fields(id) ON DELETE CASCADE,"
        "value TEXT NOT NULL,"
        "PRIMARY KEY(card_id, field_id)"
        ")"), errorMessage)
        || !exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS deck_sort_keys("
        "deck_id INTEGER NOT NULL REFERENCES decks(id) ON DELETE CASCADE,"
        "ordinal INTEGER NOT NULL,"
        "field_id INTEGER NOT NULL REFERENCES template_fields(id) ON DELETE CASCADE,"
        "direction TEXT NOT NULL DEFAULT 'ascending',"
        "case_sensitive INTEGER NOT NULL DEFAULT 0,"
        "PRIMARY KEY(deck_id, ordinal)"
        ")"), errorMessage)
        || !exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS deck_formatting("
        "deck_id INTEGER NOT NULL REFERENCES decks(id) ON DELETE CASCADE,"
        "scope TEXT NOT NULL,"
        "target_id INTEGER NOT NULL DEFAULT 0,"
        "font_family TEXT NOT NULL DEFAULT '',"
        "font_size REAL NOT NULL DEFAULT 0,"
        "foreground_color TEXT NOT NULL DEFAULT '',"
        "background_color TEXT NOT NULL DEFAULT '',"
        "alignment TEXT NOT NULL DEFAULT '',"
        "metadata_json TEXT NOT NULL DEFAULT '{}',"
        "PRIMARY KEY(deck_id, scope, target_id)"
        ")"), errorMessage)
        || !exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS reports("
        "id INTEGER PRIMARY KEY,"
        "deck_id INTEGER NOT NULL REFERENCES decks(id) ON DELETE CASCADE DEFAULT 1,"
        "ordinal INTEGER NOT NULL,"
        "name TEXT NOT NULL,"
        "format_magic TEXT NOT NULL,"
        "legacy_offset INTEGER NOT NULL,"
        "entry_size INTEGER NOT NULL,"
        "header_size INTEGER NOT NULL,"
        "declared_frame_count INTEGER NOT NULL,"
        "form_type INTEGER NOT NULL,"
        "form_width INTEGER NOT NULL,"
        "form_height INTEGER NOT NULL,"
        "rows INTEGER NOT NULL,"
        "columns INTEGER NOT NULL,"
        "margin_left INTEGER NOT NULL DEFAULT 0,"
        "margin_top INTEGER NOT NULL DEFAULT 0,"
        "margin_right INTEGER NOT NULL DEFAULT 0,"
        "margin_bottom INTEGER NOT NULL DEFAULT 0,"
        "horizontal_gutter INTEGER NOT NULL DEFAULT 0,"
        "vertical_gutter INTEGER NOT NULL DEFAULT 0,"
        "paper_style_id INTEGER NOT NULL DEFAULT 0,"
        "page_width INTEGER NOT NULL DEFAULT 0,"
        "page_height INTEGER NOT NULL DEFAULT 0,"
        "orientation INTEGER NOT NULL DEFAULT 0,"
        "data_font_face TEXT NOT NULL,"
        "data_font_height INTEGER NOT NULL,"
        "text_font_face TEXT NOT NULL,"
        "text_font_height INTEGER NOT NULL,"
        "legacy_header BLOB NOT NULL DEFAULT X''"
        ")"), errorMessage)
        && ensureColumn(
            QStringLiteral("reports"),
            QStringLiteral("deck_id"),
            QStringLiteral("ALTER TABLE reports ADD COLUMN deck_id INTEGER NOT NULL DEFAULT 1"),
            errorMessage)
        && ensureColumn(
            QStringLiteral("reports"),
            QStringLiteral("margin_left"),
            QStringLiteral("ALTER TABLE reports ADD COLUMN margin_left INTEGER NOT NULL DEFAULT 0"),
            errorMessage)
        && ensureColumn(
            QStringLiteral("reports"),
            QStringLiteral("margin_top"),
            QStringLiteral("ALTER TABLE reports ADD COLUMN margin_top INTEGER NOT NULL DEFAULT 0"),
            errorMessage)
        && ensureColumn(
            QStringLiteral("reports"),
            QStringLiteral("margin_right"),
            QStringLiteral("ALTER TABLE reports ADD COLUMN margin_right INTEGER NOT NULL DEFAULT 0"),
            errorMessage)
        && ensureColumn(
            QStringLiteral("reports"),
            QStringLiteral("margin_bottom"),
            QStringLiteral("ALTER TABLE reports ADD COLUMN margin_bottom INTEGER NOT NULL DEFAULT 0"),
            errorMessage)
        && ensureColumn(
            QStringLiteral("reports"),
            QStringLiteral("horizontal_gutter"),
            QStringLiteral("ALTER TABLE reports ADD COLUMN horizontal_gutter INTEGER NOT NULL DEFAULT 0"),
            errorMessage)
        && ensureColumn(
            QStringLiteral("reports"),
            QStringLiteral("vertical_gutter"),
            QStringLiteral("ALTER TABLE reports ADD COLUMN vertical_gutter INTEGER NOT NULL DEFAULT 0"),
            errorMessage)
        && ensureColumn(
            QStringLiteral("reports"),
            QStringLiteral("legacy_header"),
            QStringLiteral("ALTER TABLE reports ADD COLUMN legacy_header BLOB NOT NULL DEFAULT X''"),
            errorMessage)
        && ensureColumn(
            QStringLiteral("reports"),
            QStringLiteral("paper_style_id"),
            QStringLiteral("ALTER TABLE reports ADD COLUMN paper_style_id INTEGER NOT NULL DEFAULT 0"),
            errorMessage)
        && ensureColumn(
            QStringLiteral("reports"),
            QStringLiteral("page_width"),
            QStringLiteral("ALTER TABLE reports ADD COLUMN page_width INTEGER NOT NULL DEFAULT 0"),
            errorMessage)
        && ensureColumn(
            QStringLiteral("reports"),
            QStringLiteral("page_height"),
            QStringLiteral("ALTER TABLE reports ADD COLUMN page_height INTEGER NOT NULL DEFAULT 0"),
            errorMessage)
        && ensureColumn(
            QStringLiteral("reports"),
            QStringLiteral("orientation"),
            QStringLiteral("ALTER TABLE reports ADD COLUMN orientation INTEGER NOT NULL DEFAULT 0"),
            errorMessage)
        || !exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS report_frames("
        "report_id INTEGER NOT NULL REFERENCES reports(id) ON DELETE CASCADE,"
        "ordinal INTEGER NOT NULL,"
        "legacy_offset INTEGER NOT NULL,"
        "signature INTEGER NOT NULL,"
        "source_id INTEGER NOT NULL,"
        "frame_order INTEGER NOT NULL,"
        "band INTEGER NOT NULL,"
        "left_pos INTEGER NOT NULL,"
        "top_pos INTEGER NOT NULL,"
        "width INTEGER NOT NULL,"
        "height INTEGER NOT NULL,"
        "text TEXT NOT NULL,"
        "kind INTEGER NOT NULL,"
        "print_entire_contents_flag INTEGER NOT NULL,"
        "validation_flags INTEGER NOT NULL,"
        "style_flags INTEGER NOT NULL,"
        "line_box_shape INTEGER NOT NULL DEFAULT 0,"
        "line_style INTEGER NOT NULL DEFAULT 0,"
        "fill_pattern INTEGER NOT NULL DEFAULT 0,"
        "corner_radius INTEGER NOT NULL DEFAULT 0,"
        "field_placeholders_json TEXT NOT NULL,"
        "system_tokens_json TEXT NOT NULL,"
        "legacy_descriptor BLOB NOT NULL DEFAULT X'',"
        "PRIMARY KEY(report_id, ordinal)"
        ")"), errorMessage)
        || !ensureColumn(
            QStringLiteral("report_frames"),
            QStringLiteral("line_box_shape"),
            QStringLiteral("ALTER TABLE report_frames ADD COLUMN line_box_shape INTEGER NOT NULL DEFAULT 0"),
            errorMessage)
        || !ensureColumn(
            QStringLiteral("report_frames"),
            QStringLiteral("line_style"),
            QStringLiteral("ALTER TABLE report_frames ADD COLUMN line_style INTEGER NOT NULL DEFAULT 0"),
            errorMessage)
        || !ensureColumn(
            QStringLiteral("report_frames"),
            QStringLiteral("fill_pattern"),
            QStringLiteral("ALTER TABLE report_frames ADD COLUMN fill_pattern INTEGER NOT NULL DEFAULT 0"),
            errorMessage)
        || !ensureColumn(
            QStringLiteral("report_frames"),
            QStringLiteral("corner_radius"),
            QStringLiteral("ALTER TABLE report_frames ADD COLUMN corner_radius INTEGER NOT NULL DEFAULT 0"),
            errorMessage)
        || !ensureColumn(
            QStringLiteral("report_frames"),
            QStringLiteral("legacy_descriptor"),
            QStringLiteral("ALTER TABLE report_frames ADD COLUMN legacy_descriptor BLOB NOT NULL DEFAULT X''"),
            errorMessage)
        || !exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS import_export_profiles("
        "id INTEGER PRIMARY KEY,"
        "deck_id INTEGER NOT NULL REFERENCES decks(id) ON DELETE CASCADE,"
        "name TEXT NOT NULL,"
        "profile_type TEXT NOT NULL,"
        "format TEXT NOT NULL,"
        "delimiter TEXT NOT NULL DEFAULT ',',"
        "has_header INTEGER NOT NULL DEFAULT 1,"
        "mapping_json TEXT NOT NULL DEFAULT '[]',"
        "options_json TEXT NOT NULL DEFAULT '{}'"
        ")"), errorMessage)
        || !exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS phone_profiles("
        "id INTEGER PRIMARY KEY,"
        "deck_id INTEGER NOT NULL REFERENCES decks(id) ON DELETE CASCADE,"
        "name TEXT NOT NULL,"
        "field_id INTEGER REFERENCES template_fields(id) ON DELETE SET NULL,"
        "dial_prefix TEXT NOT NULL DEFAULT '',"
        "dial_suffix TEXT NOT NULL DEFAULT '',"
        "options_json TEXT NOT NULL DEFAULT '{}'"
        ")"), errorMessage)
        || !exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_templates_deck ON templates(deck_id, ordinal)"), errorMessage)
        || !exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_template_fields_template ON template_fields(template_id, ordinal)"), errorMessage)
        || !exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_template_frames_template ON template_frames(template_id, ordinal)"), errorMessage)
        || !exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_cards_deck ON cards(deck_id, ordinal)"), errorMessage)
        || !exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_card_values_field ON card_values(field_id)"), errorMessage)
        || !exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_reports_deck ON reports(deck_id, ordinal)"), errorMessage)
        || !migrateEarlySchema(errorMessage)
        || !exec(QStringLiteral(
            "INSERT OR IGNORE INTO schema_migrations(version, name) VALUES(1, 'initial_cardstack_sqlite_schema')"),
            errorMessage)
        || !exec(QStringLiteral(
            "INSERT OR REPLACE INTO app_settings(key, value, updated_at)"
            " VALUES('schema_version', '1', strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))"),
            errorMessage)
        || !exec(QStringLiteral("PRAGMA user_version = 1"), errorMessage)) {
        return false;
    }

    return true;
}

bool SQLiteDeckStore::ensureLegacyDeckDescriptionColumn(QString* errorMessage)
{
    QSqlQuery tableInfo(m_database);
    if (!tableInfo.exec(QStringLiteral("PRAGMA table_info(deck)"))) {
        setError(errorMessage, tableInfo.lastError());
        return false;
    }

    bool hasDescription = false;
    while (tableInfo.next()) {
        if (tableInfo.value(1).toString() == QStringLiteral("description")) {
            hasDescription = true;
            break;
        }
    }

    if (hasDescription) {
        return true;
    }

    return exec(QStringLiteral("ALTER TABLE deck ADD COLUMN description TEXT NOT NULL DEFAULT ''"), errorMessage);
}

bool SQLiteDeckStore::ensureColumn(
    const QString& tableName,
    const QString& columnName,
    const QString& alterSql,
    QString* errorMessage)
{
    bool hasColumn = false;
    if (!columnExists(tableName, columnName, &hasColumn, errorMessage)) {
        return false;
    }

    return hasColumn || exec(alterSql, errorMessage);
}

bool SQLiteDeckStore::tableExists(const QString& tableName, bool* exists, QString* errorMessage)
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

bool SQLiteDeckStore::columnExists(
    const QString& tableName,
    const QString& columnName,
    bool* exists,
    QString* errorMessage)
{
    QSqlQuery tableInfo(m_database);
    if (!tableInfo.exec(QStringLiteral("PRAGMA table_info(%1)").arg(tableName))) {
        setError(errorMessage, tableInfo.lastError());
        return false;
    }

    bool found = false;
    while (tableInfo.next()) {
        if (tableInfo.value(1).toString() == columnName) {
            found = true;
            break;
        }
    }

    if (exists != nullptr) {
        *exists = found;
    }
    return true;
}

bool SQLiteDeckStore::hasRows(const QString& tableName, bool* hasRowsValue, QString* errorMessage)
{
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT 1 FROM %1 LIMIT 1").arg(tableName))) {
        setError(errorMessage, query.lastError());
        return false;
    }

    if (hasRowsValue != nullptr) {
        *hasRowsValue = query.next();
    }
    return true;
}

bool SQLiteDeckStore::execIfTableExists(const QString& tableName, const QString& sql, QString* errorMessage)
{
    bool exists = false;
    if (!tableExists(tableName, &exists, errorMessage)) {
        return false;
    }

    return !exists || exec(sql, errorMessage);
}

bool SQLiteDeckStore::migrateEarlySchema(QString* errorMessage)
{
    bool decksHaveRows = false;
    if (!hasRows(QStringLiteral("decks"), &decksHaveRows, errorMessage)) {
        return false;
    }
    if (decksHaveRows) {
        return true;
    }

    bool hasEarlyDeckTable = false;
    if (!tableExists(QStringLiteral("deck"), &hasEarlyDeckTable, errorMessage)) {
        return false;
    }
    if (!hasEarlyDeckTable) {
        return true;
    }

    if (!exec(QStringLiteral(
        "INSERT OR IGNORE INTO decks(id, uuid, name, description, source_format, legacy_metadata_json)"
        " SELECT id, lower(hex(randomblob(16))), name, description, 'cardstack-early-sqlite', '{}'"
        " FROM deck WHERE id = 1"), errorMessage)
        || !exec(QStringLiteral(
        "INSERT OR IGNORE INTO templates(id, deck_id, ordinal, name, description)"
        " SELECT 1, 1, 0, name, '' FROM deck WHERE id = 1"), errorMessage)) {
        return false;
    }

    bool hasEarlyFieldsTable = false;
    if (!tableExists(QStringLiteral("fields"), &hasEarlyFieldsTable, errorMessage)) {
        return false;
    }
    if (hasEarlyFieldsTable
        && !exec(QStringLiteral(
            "INSERT OR IGNORE INTO template_fields(id, template_id, ordinal, name, type, max_length)"
            " SELECT id, 1, ordinal, name, type, max_length FROM fields ORDER BY ordinal"),
            errorMessage)) {
        return false;
    }

    return true;
}

bool SQLiteDeckStore::saveCardTemplateLayout(const Deck& deck, int templateId, QString* errorMessage)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO template_frames("
        "template_id, ordinal, kind, field_index, left_pos, top_pos, width, height,"
        "text, style_flags, line_box_shape, line_style, fill_pattern, corner_radius, legacy_descriptor"
        ") VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));

    const CardTemplateLayout& layout = deck.cardTemplateLayout();
    if (!setAppSetting(QStringLiteral("card_template_canvas_width"), QString::number(layout.canvasWidth), errorMessage)
        || !setAppSetting(QStringLiteral("card_template_canvas_height"), QString::number(layout.canvasHeight), errorMessage)) {
        return false;
    }

    for (int frameIndex = 0; frameIndex < layout.frames.size(); ++frameIndex) {
        const CardTemplateFrame& frame = layout.frames.at(frameIndex);
        query.bindValue(0, templateId);
        query.bindValue(1, frameIndex);
        query.bindValue(2, cardTemplateFrameKindToInt(frame.kind));
        query.bindValue(3, frame.fieldIndex);
        query.bindValue(4, frame.bounds.left());
        query.bindValue(5, frame.bounds.top());
        query.bindValue(6, frame.bounds.width());
        query.bindValue(7, frame.bounds.height());
        query.bindValue(8, frame.text.isEmpty() ? QStringLiteral("") : frame.text);
        query.bindValue(9, frame.styleFlags);
        query.bindValue(10, cardTemplateLineBoxShapeToInt(frame.lineBoxShape));
        query.bindValue(11, frame.lineStyle);
        query.bindValue(12, frame.fillPattern);
        query.bindValue(13, frame.cornerRadius);
        query.bindValue(14, frame.legacyDescriptor.isNull() ? QByteArray("") : frame.legacyDescriptor);
        if (!query.exec()) {
            setError(errorMessage, query.lastError());
            return false;
        }
    }

    return true;
}

bool SQLiteDeckStore::loadCardTemplateLayout(Deck* deck, int templateId, QString* errorMessage)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT kind, field_index, left_pos, top_pos, width, height, text, style_flags,"
        "line_box_shape, line_style, fill_pattern, corner_radius, legacy_descriptor "
        "FROM template_frames WHERE template_id = ? ORDER BY ordinal"));
    query.bindValue(0, templateId);
    if (!query.exec()) {
        setError(errorMessage, query.lastError());
        return false;
    }

    CardTemplateLayout layout;
    QString canvasWidth;
    QString canvasHeight;
    if (!appSetting(QStringLiteral("card_template_canvas_width"), &canvasWidth, errorMessage)
        || !appSetting(QStringLiteral("card_template_canvas_height"), &canvasHeight, errorMessage)) {
        return false;
    }
    if (!canvasWidth.isEmpty()) {
        layout.canvasWidth = canvasWidth.toInt();
    }
    if (!canvasHeight.isEmpty()) {
        layout.canvasHeight = canvasHeight.toInt();
    }

    while (query.next()) {
        CardTemplateFrame frame;
        frame.kind = cardTemplateFrameKindFromInt(query.value(0).toInt());
        frame.fieldIndex = query.value(1).toInt();
        frame.bounds = QRect(
            query.value(2).toInt(),
            query.value(3).toInt(),
            query.value(4).toInt(),
            query.value(5).toInt());
        frame.text = query.value(6).toString();
        frame.styleFlags = static_cast<quint8>(query.value(7).toUInt());
        frame.lineBoxShape = cardTemplateLineBoxShapeFromInt(query.value(8).toInt());
        frame.lineStyle = query.value(9).toInt();
        frame.fillPattern = query.value(10).toInt();
        frame.cornerRadius = query.value(11).toInt();
        frame.legacyDescriptor = query.value(12).toByteArray();
        layout.frames.append(frame);
    }

    deck->setCardTemplateLayout(std::move(layout));
    return true;
}

bool SQLiteDeckStore::saveReports(const Deck& deck, QString* errorMessage)
{
    QSqlQuery reportQuery(m_database);
    reportQuery.prepare(QStringLiteral(
        "INSERT INTO reports("
        "id, deck_id, ordinal, name, format_magic, legacy_offset, entry_size, header_size, declared_frame_count,"
        "form_type, form_width, form_height, rows, columns,"
        "margin_left, margin_top, margin_right, margin_bottom, horizontal_gutter, vertical_gutter,"
        "paper_style_id, page_width, page_height, orientation,"
        "data_font_face, data_font_height,"
        "text_font_face, text_font_height, legacy_header"
        ") VALUES(?, 1, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));

    QSqlQuery frameQuery(m_database);
    frameQuery.prepare(QStringLiteral(
        "INSERT INTO report_frames("
        "report_id, ordinal, legacy_offset, signature, source_id, frame_order, band,"
        "left_pos, top_pos, width, height, text, kind, print_entire_contents_flag,"
        "validation_flags, style_flags, line_box_shape, line_style, fill_pattern, corner_radius,"
        "field_placeholders_json, system_tokens_json, legacy_descriptor"
        ") VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));

    for (int reportIndex = 0; reportIndex < deck.reportCount(); ++reportIndex) {
        const int reportId = reportIndex + 1;
        const ReportDefinition& report = deck.reportAt(reportIndex);

        reportQuery.bindValue(0, reportId);
        reportQuery.bindValue(1, reportIndex);
        reportQuery.bindValue(2, report.name);
        reportQuery.bindValue(3, report.formatMagic);
        reportQuery.bindValue(4, report.legacyOffset);
        reportQuery.bindValue(5, report.entrySize);
        reportQuery.bindValue(6, report.headerSize);
        reportQuery.bindValue(7, report.declaredFrameCount);
        reportQuery.bindValue(8, reportFormTypeToInt(report.formType));
        reportQuery.bindValue(9, report.formWidth);
        reportQuery.bindValue(10, report.formHeight);
        reportQuery.bindValue(11, report.rows);
        reportQuery.bindValue(12, report.columns);
        reportQuery.bindValue(13, report.marginLeft);
        reportQuery.bindValue(14, report.marginTop);
        reportQuery.bindValue(15, report.marginRight);
        reportQuery.bindValue(16, report.marginBottom);
        reportQuery.bindValue(17, report.horizontalGutter);
        reportQuery.bindValue(18, report.verticalGutter);
        reportQuery.bindValue(19, report.paperStyleId);
        reportQuery.bindValue(20, report.pageWidth);
        reportQuery.bindValue(21, report.pageHeight);
        reportQuery.bindValue(22, report.orientation);
        reportQuery.bindValue(23, report.dataFont.faceName.isNull() ? QStringLiteral("") : report.dataFont.faceName);
        reportQuery.bindValue(24, report.dataFont.legacyHeight);
        reportQuery.bindValue(25, report.textFont.faceName.isNull() ? QStringLiteral("") : report.textFont.faceName);
        reportQuery.bindValue(26, report.textFont.legacyHeight);
        reportQuery.bindValue(27, report.legacyHeader.isNull() ? QByteArray("") : report.legacyHeader);
        if (!reportQuery.exec()) {
            setError(errorMessage, reportQuery.lastError());
            return false;
        }

        for (int frameIndex = 0; frameIndex < report.frames.size(); ++frameIndex) {
            const ReportFrameDefinition& frame = report.frames.at(frameIndex);
            frameQuery.bindValue(0, reportId);
            frameQuery.bindValue(1, frameIndex);
            frameQuery.bindValue(2, frame.legacyOffset);
            frameQuery.bindValue(3, frame.signature);
            frameQuery.bindValue(4, frame.sourceId);
            frameQuery.bindValue(5, frame.order);
            frameQuery.bindValue(6, frame.band);
            frameQuery.bindValue(7, frame.bounds.left());
            frameQuery.bindValue(8, frame.bounds.top());
            frameQuery.bindValue(9, frame.bounds.width());
            frameQuery.bindValue(10, frame.bounds.height());
            frameQuery.bindValue(11, frame.text);
            frameQuery.bindValue(12, reportFrameKindToInt(frame.kind));
            frameQuery.bindValue(13, frame.printEntireContentsFlag);
            frameQuery.bindValue(14, frame.validationFlags);
            frameQuery.bindValue(15, frame.styleFlags);
            frameQuery.bindValue(16, frame.lineBoxShape);
            frameQuery.bindValue(17, frame.lineStyle);
            frameQuery.bindValue(18, frame.fillPattern);
            frameQuery.bindValue(19, frame.cornerRadius);
            frameQuery.bindValue(20, stringVectorToJson(frame.fieldPlaceholders));
            frameQuery.bindValue(21, stringVectorToJson(frame.systemTokens));
            frameQuery.bindValue(22, frame.legacyDescriptor.isNull() ? QByteArray("") : frame.legacyDescriptor);
            if (!frameQuery.exec()) {
                setError(errorMessage, frameQuery.lastError());
                return false;
            }
        }
    }

    return true;
}

bool SQLiteDeckStore::loadReports(Deck* deck, QString* errorMessage)
{
    QSqlQuery reportQuery(m_database);
    if (!reportQuery.exec(QStringLiteral(
            "SELECT id, name, format_magic, legacy_offset, entry_size, header_size, declared_frame_count,"
            "form_type, form_width, form_height, rows, columns,"
            "margin_left, margin_top, margin_right, margin_bottom, horizontal_gutter, vertical_gutter,"
            "paper_style_id, page_width, page_height, orientation,"
            "data_font_face, data_font_height,"
            "text_font_face, text_font_height, legacy_header FROM reports WHERE deck_id = 1 ORDER BY ordinal"))) {
        setError(errorMessage, reportQuery.lastError());
        return false;
    }

    QSqlQuery frameQuery(m_database);
    frameQuery.prepare(QStringLiteral(
        "SELECT legacy_offset, signature, source_id, frame_order, band, left_pos, top_pos, width, height,"
        "text, kind, print_entire_contents_flag, validation_flags, style_flags,"
        "line_box_shape, line_style, fill_pattern, corner_radius,"
        "field_placeholders_json, system_tokens_json, legacy_descriptor "
        "FROM report_frames WHERE report_id = ? ORDER BY ordinal"));

    while (reportQuery.next()) {
        ReportDefinition report;
        const int reportId = reportQuery.value(0).toInt();
        report.name = reportQuery.value(1).toString();
        report.formatMagic = reportQuery.value(2).toString();
        report.legacyOffset = reportQuery.value(3).toInt();
        report.entrySize = reportQuery.value(4).toInt();
        report.headerSize = reportQuery.value(5).toInt();
        report.declaredFrameCount = reportQuery.value(6).toInt();
        report.formType = reportFormTypeFromInt(reportQuery.value(7).toInt());
        report.formWidth = reportQuery.value(8).toInt();
        report.formHeight = reportQuery.value(9).toInt();
        report.rows = reportQuery.value(10).toInt();
        report.columns = reportQuery.value(11).toInt();
        report.marginLeft = reportQuery.value(12).toInt();
        report.marginTop = reportQuery.value(13).toInt();
        report.marginRight = reportQuery.value(14).toInt();
        report.marginBottom = reportQuery.value(15).toInt();
        report.horizontalGutter = reportQuery.value(16).toInt();
        report.verticalGutter = reportQuery.value(17).toInt();
        report.paperStyleId = reportQuery.value(18).toInt();
        report.pageWidth = reportQuery.value(19).toInt();
        report.pageHeight = reportQuery.value(20).toInt();
        report.orientation = reportQuery.value(21).toInt();
        report.dataFont.faceName = reportQuery.value(22).toString();
        report.dataFont.legacyHeight = reportQuery.value(23).toInt();
        report.textFont.faceName = reportQuery.value(24).toString();
        report.textFont.legacyHeight = reportQuery.value(25).toInt();
        report.legacyHeader = reportQuery.value(26).toByteArray();

        frameQuery.bindValue(0, reportId);
        if (!frameQuery.exec()) {
            setError(errorMessage, frameQuery.lastError());
            return false;
        }

        while (frameQuery.next()) {
            ReportFrameDefinition frame;
            frame.legacyOffset = frameQuery.value(0).toInt();
            frame.signature = static_cast<quint16>(frameQuery.value(1).toUInt());
            frame.sourceId = static_cast<quint16>(frameQuery.value(2).toUInt());
            frame.order = static_cast<quint16>(frameQuery.value(3).toUInt());
            frame.band = static_cast<quint16>(frameQuery.value(4).toUInt());
            frame.bounds = QRect(
                frameQuery.value(5).toInt(),
                frameQuery.value(6).toInt(),
                frameQuery.value(7).toInt(),
                frameQuery.value(8).toInt());
            frame.text = frameQuery.value(9).toString();
            frame.kind = reportFrameKindFromInt(frameQuery.value(10).toInt());
            frame.printEntireContentsFlag = static_cast<quint8>(frameQuery.value(11).toUInt());
            frame.validationFlags = static_cast<quint8>(frameQuery.value(12).toUInt());
            frame.styleFlags = static_cast<quint8>(frameQuery.value(13).toUInt());
            frame.lineBoxShape = frameQuery.value(14).toInt();
            frame.lineStyle = frameQuery.value(15).toInt();
            frame.fillPattern = frameQuery.value(16).toInt();
            frame.cornerRadius = frameQuery.value(17).toInt();
            frame.fieldPlaceholders = stringVectorFromJson(frameQuery.value(18).toString());
            frame.systemTokens = stringVectorFromJson(frameQuery.value(19).toString());
            frame.legacyDescriptor = frameQuery.value(20).toByteArray();
            report.frames.append(std::move(frame));
        }

        deck->addReport(std::move(report));
    }

    return true;
}

bool SQLiteDeckStore::exec(const QString& sql, QString* errorMessage)
{
    QSqlQuery query(m_database);
    if (!query.exec(sql)) {
        setError(errorMessage, query.lastError());
        return false;
    }

    return true;
}

} // namespace CardStack
