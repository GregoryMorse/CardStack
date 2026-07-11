#pragma once

#include "Deck.h"
#include "ImportExportProfile.h"

#include <QString>

namespace CardStack::DelimitedText {

ImportExportProfile csvProfile(ImportExportProfileType type);
ImportExportProfile tsvProfile(ImportExportProfileType type);

QString writeDeck(const Deck& deck, const ImportExportProfile& profile);
bool readDeck(const QString& text, const ImportExportProfile& profile, Deck* deck, QString* errorMessage = nullptr);

bool writeDeckFile(const Deck& deck, const ImportExportProfile& profile, const QString& filePath, QString* errorMessage = nullptr);
bool readDeckFile(const QString& filePath, const ImportExportProfile& profile, Deck* deck, QString* errorMessage = nullptr);

} // namespace CardStack::DelimitedText
