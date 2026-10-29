#pragma once

#include "Deck.h"
#include "ImportBudget.h"

#include <QSqlDatabase>
#include <QString>

namespace CardStack {

class SQLiteDeckRepository {
public:
    explicit SQLiteDeckRepository(QSqlDatabase database, ImportBudget* budget = nullptr);

    bool clearDeckGraph(QString* errorMessage = nullptr);

    bool saveDeckShell(const Deck& deck, QString* errorMessage = nullptr);
    bool loadDeckShell(Deck* deck, QString* errorMessage = nullptr);

    bool saveDefaultTemplate(const Deck& deck, QString* errorMessage = nullptr);
    bool loadDefaultTemplateId(qint64* templateId, QString* errorMessage = nullptr);

    bool saveFields(const Deck& deck, qint64 templateId, QString* errorMessage = nullptr);
    bool loadFields(qint64 templateId, Deck* deck, QString* errorMessage = nullptr);

    bool saveSortKeys(const Deck& deck, QString* errorMessage = nullptr);
    bool loadSortKeys(Deck* deck, QString* errorMessage = nullptr);

    bool saveAppearance(const Deck& deck, QString* errorMessage = nullptr);
    bool loadAppearance(Deck* deck, QString* errorMessage = nullptr);

    bool saveImportExportProfiles(const Deck& deck, QString* errorMessage = nullptr);
    bool loadImportExportProfiles(Deck* deck, QString* errorMessage = nullptr);

    bool saveCards(const Deck& deck, QString* errorMessage = nullptr);
    bool loadCards(qint64 templateId, Deck* deck, QString* errorMessage = nullptr);

private:
    bool tableExists(const QString& tableName, bool* exists, QString* errorMessage);
    bool execIfTableExists(const QString& tableName, const QString& sql, QString* errorMessage);
    bool exec(const QString& sql, QString* errorMessage);

    QSqlDatabase m_database;
    ImportBudget* m_budget = nullptr;
};

} // namespace CardStack
