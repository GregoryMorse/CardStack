#pragma once

#include "Deck.h"

#include <QString>
#include <QVector>

namespace CardStack {

struct DeckMergeFieldMapping {
    int sourceFieldIndex = -1;
    int destinationFieldIndex = -1;
};

struct DeckMergeOptions {
    enum class Scope {
        AllCards,
        SelectedCards
    };

    QVector<DeckMergeFieldMapping> fieldMappings;
    Scope scope = Scope::AllCards;
    QVector<int> selectedSourceCardIndexes;
};

struct DeckMergeResult {
    int cardsMerged = 0;
    int valuesMapped = 0;
    QString errorMessage;

    bool ok() const;
};

DeckMergeResult mergeDecks(Deck* destination, const Deck& source, const DeckMergeOptions& options);

} // namespace CardStack
