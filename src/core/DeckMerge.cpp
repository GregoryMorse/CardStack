#include "DeckMerge.h"

#include <algorithm>
#include <utility>

namespace CardStack {
namespace {

QString normalizedDestinationValue(const Deck& destination, int fieldIndex, QString value)
{
    const int maxLength = destination.fieldAt(fieldIndex).maxLength();
    if (maxLength > 0 && value.size() > maxLength) {
        value.truncate(maxLength);
    }
    return value;
}

QVector<DeckMergeFieldMapping> validMappings(
    const Deck& destination,
    const Deck& source,
    const QVector<DeckMergeFieldMapping>& mappings)
{
    QVector<DeckMergeFieldMapping> valid;
    for (const DeckMergeFieldMapping& mapping : mappings) {
        if (mapping.sourceFieldIndex < 0 || mapping.sourceFieldIndex >= source.fieldCount()) {
            continue;
        }
        if (mapping.destinationFieldIndex < 0 || mapping.destinationFieldIndex >= destination.fieldCount()) {
            continue;
        }
        valid.append(mapping);
    }
    return valid;
}

QVector<int> sourceCardIndexes(const Deck& source, const DeckMergeOptions& options)
{
    QVector<int> indexes;
    if (options.scope == DeckMergeOptions::Scope::AllCards) {
        indexes.reserve(source.cardCount());
        for (int index = 0; index < source.cardCount(); ++index) {
            indexes.append(index);
        }
        return indexes;
    }

    for (int index : options.selectedSourceCardIndexes) {
        if (index >= 0 && index < source.cardCount() && !indexes.contains(index)) {
            indexes.append(index);
        }
    }
    std::sort(indexes.begin(), indexes.end());
    return indexes;
}

CardRecord blankRecordForDeck(const Deck& deck)
{
    CardRecord record;
    for (int fieldIndex = 0; fieldIndex < deck.fieldCount(); ++fieldIndex) {
        record.appendValue({});
    }
    return record;
}

} // namespace

bool DeckMergeResult::ok() const
{
    return errorMessage.isEmpty();
}

DeckMergeResult mergeDecks(Deck* destination, const Deck& source, const DeckMergeOptions& options)
{
    DeckMergeResult result;
    if (destination == nullptr) {
        result.errorMessage = QStringLiteral("Destination deck is not available.");
        return result;
    }

    const QVector<DeckMergeFieldMapping> mappings = validMappings(*destination, source, options.fieldMappings);
    if (mappings.isEmpty()) {
        result.errorMessage = QStringLiteral("Merge requires at least one valid field mapping.");
        return result;
    }

    const QVector<int> cardIndexes = sourceCardIndexes(source, options);
    if (cardIndexes.isEmpty()) {
        result.errorMessage = QStringLiteral("Merge source contains no matching cards.");
        return result;
    }

    for (int sourceCardIndex : cardIndexes) {
        CardRecord merged = blankRecordForDeck(*destination);
        const CardRecord& sourceRecord = source.cardAt(sourceCardIndex);
        for (const DeckMergeFieldMapping& mapping : mappings) {
            merged.setValueAt(
                mapping.destinationFieldIndex,
                normalizedDestinationValue(
                    *destination,
                    mapping.destinationFieldIndex,
                    sourceRecord.valueAt(mapping.sourceFieldIndex)));
            ++result.valuesMapped;
        }
        destination->addCard(std::move(merged));
        ++result.cardsMerged;
    }

    return result;
}

} // namespace CardStack
