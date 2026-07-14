#pragma once

#include "Deck.h"

#include <QString>

namespace CardStack::PhoneCallLog {

int importLegacyFile(const QString& logFilePath, Deck* deck, QString* warningMessage = nullptr);
int importLegacySidecar(const QString& deckFilePath, Deck* deck, QString* warningMessage = nullptr);
bool writeLegacyFile(const Deck& deck, const QString& filePath, QString* errorMessage = nullptr);

} // namespace CardStack::PhoneCallLog
