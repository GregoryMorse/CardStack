#pragma once

#include "Deck.h"
#include "ImportBudget.h"

#include <QString>

namespace CardStack {

class LegacyInterchangeReader {
public:
    explicit LegacyInterchangeReader(ImportLimits limits = {}) : m_limits(limits) {}
    enum class Format {
        DBase,
        MicrosoftCardfile,
        TakeNote,
        WordPerfectMerge,
    };

    struct Result {
        Deck deck;
        Format format = Format::DBase;
        QString errorMessage;
        bool legacyPasswordProtected = false;
        bool legacyDataEncrypted = false;
        bool legacyPasswordVerified = false;
        bool legacyPasswordVerificationUnavailable = false;
        bool passwordRequired = false;
        bool passwordRejected = false;
        bool processingAborted = false;

        bool ok() const;
    };

    Result readFile(const QString& filePath, const QString& password = {}) const;
    Result readDBaseFile(const QString& filePath) const;
    Result readMicrosoftCardfile(const QString& filePath) const;
    Result readTakeNoteFile(const QString& filePath, const QString& password = {}) const;
    Result readWordPerfectMergeFile(const QString& filePath) const;
private:
    ImportLimits m_limits;
};

bool isLegacyInterchangePath(const QString& filePath);

} // namespace CardStack
