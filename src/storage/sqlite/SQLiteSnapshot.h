#pragma once

#include <QJsonObject>
#include "ImportBudget.h"
#include <QSqlDatabase>

namespace CardStack::SQLiteSnapshot {
inline constexpr int CurrentSchemaVersion = 3;
inline constexpr int ApplicationId = 0x4353544b;

bool readFile(const QString& path, QJsonObject* snapshot, QString* error, ImportLimits limits = {});
bool capture(QSqlDatabase database, QJsonObject* snapshot, QString* error, ImportLimits limits = {});
bool restore(QSqlDatabase database, const QJsonObject& snapshot, QString* error, ImportLimits limits = {});
bool validateJsonBytes(const QByteArray& bytes, QString* error, ImportLimits limits = {});
bool checkForeignKeys(QSqlDatabase database, QString* error);
} // namespace CardStack::SQLiteSnapshot
