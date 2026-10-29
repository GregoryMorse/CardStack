#pragma once

#include "Deck.h"
#include "ImportBudget.h"

#include <QString>

namespace CardStack::PhoneCallLog {

int importLegacyFile(const QString& logFilePath, Deck* deck, QString* warningMessage = nullptr, ImportLimits limits = {}, ImportBudget* sharedBudget = nullptr);
int importLegacySidecar(const QString& deckFilePath, Deck* deck, QString* warningMessage = nullptr, ImportLimits limits = {}, ImportBudget* sharedBudget = nullptr);
bool writeLegacyFile(const Deck& deck, const QString& filePath, QString* errorMessage = nullptr);

} // namespace CardStack::PhoneCallLog
