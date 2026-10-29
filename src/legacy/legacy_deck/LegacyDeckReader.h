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
    explicit LegacyDeckReader(ImportLimits limits = {}) : m_limits(limits) {}
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
        bool processingAborted = false;

        bool ok() const;
    };

    Result readDeck(const QString& filePath, const QString& password = {}, ImportBudget* sharedBudget = nullptr) const;
    Result readRecords(
        const QVector<QByteArray>& records,
        const QString& fallbackDeckName = {},
        const QString& password = {}, ImportBudget* sharedBudget = nullptr) const;
private:
    ImportLimits m_limits;
};

} // namespace CardStack
