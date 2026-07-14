#pragma once

#include "BtrieveFileSaverReader.h"
#include "Deck.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace CardStack {

class LegacyDeckReader {
public:
    struct Result {
        Deck deck;
        BtrieveFileSaverReader::Metadata btrieveMetadata;
        QVector<QByteArray> rawRecords;
        QString errorMessage;
        QStringList warningMessages;
        bool legacyPasswordProtected = false;
        bool legacyDataEncrypted = false;
        bool legacyPasswordVerified = false;
        bool legacyPasswordVerificationUnavailable = false;
        bool passwordRequired = false;
        bool passwordRejected = false;

        bool ok() const;
    };

    Result readDeck(const QString& filePath, const QString& password = {}) const;
    Result readRecords(
        const QVector<QByteArray>& records,
        const QString& fallbackDeckName = {},
        const QString& password = {}) const;
};

} // namespace CardStack
