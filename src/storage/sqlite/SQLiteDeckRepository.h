#pragma once

#include "Deck.h"

#include <QSqlDatabase>
#include <QString>

namespace CardStack {

class SQLiteDeckRepository {
public:
    explicit SQLiteDeckRepository(QSqlDatabase database);

    bool clearDeckGraph(QString* errorMessage = nullptr);

    bool saveDeckShell(const Deck& deck, QString* errorMessage = nullptr);
    bool loadDeckShell(Deck* deck, QString* errorMessage = nullptr);

    bool saveDefaultTemplate(const Deck& deck, QString* errorMessage = nullptr);
    bool loadDefaultTemplateId(int* templateId, QString* errorMessage = nullptr);

    bool saveFields(const Deck& deck, int templateId, QString* errorMessage = nullptr);
    bool loadFields(int templateId, Deck* deck, QString* errorMessage = nullptr);

    bool saveSortKeys(const Deck& deck, QString* errorMessage = nullptr);
    bool loadSortKeys(Deck* deck, QString* errorMessage = nullptr);

    bool saveImportExportProfiles(const Deck& deck, QString* errorMessage = nullptr);
    bool loadImportExportProfiles(Deck* deck, QString* errorMessage = nullptr);

    bool saveCards(const Deck& deck, QString* errorMessage = nullptr);
    bool loadCards(int templateId, Deck* deck, QString* errorMessage = nullptr);

private:
    bool tableExists(const QString& tableName, bool* exists, QString* errorMessage);
    bool execIfTableExists(const QString& tableName, const QString& sql, QString* errorMessage);
    bool exec(const QString& sql, QString* errorMessage);

    QSqlDatabase m_database;
};

} // namespace CardStack
