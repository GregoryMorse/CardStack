#pragma once

#include "Deck.h"
#include "ReportDefinition.h"

#include <QVector>
#include <QString>

namespace CardStack {

enum class SQLitePackageType {
    Unknown,
    Deck,
    Template,
    Report,
};

QString sqlitePackageTypeName(SQLitePackageType type);
SQLitePackageType sqlitePackageTypeFromName(const QString& name);

class SQLitePackageStore {
public:
    static bool saveDeckPackage(const Deck& deck, const QString& filePath, QString* errorMessage = nullptr);
    static bool saveTemplatePackage(const Deck& sourceDeck, const QString& filePath, QString* errorMessage = nullptr);
    static bool loadTemplatePackage(const QString& filePath, Deck* templateDeck, QString* errorMessage = nullptr);
    static bool saveReportPackage(const QVector<ReportDefinition>& reports,
                                  const QString& packageName,
                                  const QString& filePath,
                                  QString* errorMessage = nullptr);
    static bool loadReportPackage(const QString& filePath,
                                  QVector<ReportDefinition>* reports,
                                  QString* packageName = nullptr,
                                  QString* errorMessage = nullptr);
    static bool packageType(const QString& filePath, SQLitePackageType* type, QString* errorMessage = nullptr);
};

} // namespace CardStack
