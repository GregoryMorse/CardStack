#pragma once

#include "ImportBudget.h"
#include "ReportDefinition.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>

namespace CardStack {

// Snapshot budgets cover stored cells; this budget covers the hydrated model.
inline bool budgetModelRow(const QSqlQuery& query, ImportBudget* budget, QString* error)
{
    if (!budget) return true;
    bool ok = budget->checkpoint(quint64(query.record().count())) && budget->decoded(ImportRowOverheadBytes);
    for (int column = 0; ok && column < query.record().count(); ++column) {
        const QVariant value = query.value(column);
        const quint64 bytes = value.metaType().id() == QMetaType::QByteArray
            ? quint64(value.toByteArray().size()) : quint64(value.toString().size()) * sizeof(QChar);
        ok = budget->cell(bytes) && budget->decoded(ImportValueOverheadBytes) && budget->decoded(bytes);
    }
    if (!ok && error) *error = budget->error();
    return ok;
}

inline bool readModelBounds(const QSqlQuery& query, int first, QRect* bounds, QString* error)
{
    qint64 values[4];
    for (int i = 0; i < 4; ++i) {
        values[i] = query.value(first + i).toLongLong();
        if (values[i] < (i < 2 ? -MaximumReportGeometryUnits : 0)
            || values[i] > MaximumReportGeometryUnits) {
            if (error) *error = QCoreApplication::translate("SQLiteDeckStore", "A frame has unsupported coordinates or dimensions.");
            return false;
        }
    }
    *bounds = QRect(int(values[0]), int(values[1]), int(values[2]), int(values[3]));
    return true;
}
} // namespace CardStack
