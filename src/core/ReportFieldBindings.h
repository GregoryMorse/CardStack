#pragma once

#include <QString>
#include <QStringList>

namespace CardStack {
inline QString matchingReportFieldName(const QString& reference, const QStringList& names)
{
    for (const QString& name : names)
        if (name.compare(reference, Qt::CaseInsensitive) == 0) return name;

    // Imported report references may retain only the first 15 name characters.
    constexpr int AbbreviatedFieldNameLength = 15;
    if (reference.size() != AbbreviatedFieldNameLength) return {};
    QString match;
    for (const QString& name : names) {
        if (!name.startsWith(reference, Qt::CaseInsensitive)) continue;
        if (!match.isEmpty() && name.compare(match, Qt::CaseInsensitive) != 0) return {};
        match = name;
    }
    return match;
}
} // namespace CardStack
