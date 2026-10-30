#pragma once

#include "Deck.h"

namespace CardStack {

inline constexpr int EmptyIndexBucket = 0;
inline constexpr int AlphabeticIndexBucketBase = 1;
inline constexpr int NumericIndexBucketBase = AlphabeticIndexBucketBase + 26;

inline int titleBucketRank(const QString& title)
{
    const QString trimmed = title.trimmed();
    if (trimmed.isEmpty() || !trimmed.at(0).isLetterOrNumber()) {
        return EmptyIndexBucket;
    }

    const QChar first = trimmed.at(0).toUpper();
    if (first >= QLatin1Char('A') && first <= QLatin1Char('Z')) {
        return AlphabeticIndexBucketBase + first.unicode() - QLatin1Char('A').unicode();
    }
    if (first >= QLatin1Char('0') && first <= QLatin1Char('9')) {
        return NumericIndexBucketBase + first.unicode() - QLatin1Char('0').unicode();
    }
    return EmptyIndexBucket;
}

inline bool indexSortsBefore(const CardRecord& left, const CardRecord& right, const QVector<DeckSortKey>& sortKeys)
{
    for (const DeckSortKey& key : sortKeys) {
        const QString leftTitle = left.valueAt(key.fieldIndex).trimmed();
        const QString rightTitle = right.valueAt(key.fieldIndex).trimmed();
        int comparison = titleBucketRank(leftTitle) - titleBucketRank(rightTitle);
        if (comparison == 0) {
            comparison = QString::localeAwareCompare(leftTitle, rightTitle);
        }
        if (comparison != 0) {
            return key.descending ? comparison > 0 : comparison < 0;
        }
    }
    return false;
}


} // namespace CardStack
