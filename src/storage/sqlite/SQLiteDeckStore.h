#pragma once

#include "Deck.h"

#include <QSqlDatabase>
#include <QString>

namespace CardStack {

class SQLiteDeckStore {
public:
    SQLiteDeckStore();
    ~SQLiteDeckStore();

    bool open(const QString& filePath, QString* errorMessage = nullptr);
    bool saveDeck(const Deck& deck, QString* errorMessage = nullptr);
    bool loadDeck(Deck* deck, QString* errorMessage = nullptr);
    bool setAppSetting(const QString& key, const QString& value, QString* errorMessage = nullptr);
    bool appSetting(const QString& key, QString* value, QString* errorMessage = nullptr) const;
    void close();

private:
    bool ensureSchema(QString* errorMessage);
    bool ensureLegacyDeckDescriptionColumn(QString* errorMessage);
    bool ensureColumn(const QString& tableName, const QString& columnName, const QString& alterSql, QString* errorMessage);
    bool tableExists(const QString& tableName, bool* exists, QString* errorMessage);
    bool columnExists(const QString& tableName, const QString& columnName, bool* exists, QString* errorMessage);
    bool hasRows(const QString& tableName, bool* hasRows, QString* errorMessage);
    bool execIfTableExists(const QString& tableName, const QString& sql, QString* errorMessage);
    bool migrateEarlySchema(QString* errorMessage);
    bool saveCardTemplateLayout(const Deck& deck, int templateId, QString* errorMessage);
    bool loadCardTemplateLayout(Deck* deck, int templateId, QString* errorMessage);
    bool saveReports(const Deck& deck, QString* errorMessage);
    bool loadReports(Deck* deck, QString* errorMessage);
    bool savePhoneCallLog(const Deck& deck, QString* errorMessage);
    bool loadPhoneCallLog(Deck* deck, QString* errorMessage);
    bool exec(const QString& sql, QString* errorMessage);

    QString m_connectionName;
    QSqlDatabase m_database;
};

} // namespace CardStack
