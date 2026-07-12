#pragma once

#include "CardRecord.h"
#include "CardTemplateLayout.h"
#include "FieldDefinition.h"
#include "ImportExportProfile.h"
#include "ReportDefinition.h"

#include <QByteArray>

#include <QString>
#include <QVector>

namespace CardStack {

struct DeckSortKey {
    int fieldIndex = -1;
    bool descending = false;

    bool operator==(const DeckSortKey&) const = default;
};

enum class DeckColorRole {
    IndexForeground = 0,
    DataForeground,
    NameForeground,
    TextForeground,
    DataBackground,
    IndexBackground,
    CardBackground,
    Count
};

struct DeckAppearance {
    QString dataFont;
    QString nameFont;
    QString textFont;
    QString indexFont;
    QVector<QString> customColors = QVector<QString>(static_cast<int>(DeckColorRole::Count));
    bool useSystemColors = true;

    bool operator==(const DeckAppearance&) const = default;
};

class Deck {
public:
    explicit Deck(QString name = {});

    const QString& name() const;
    void setName(QString name);
    const QString& description() const;
    void setDescription(QString description);

    int fieldCount() const;
    int cardCount() const;
    int reportCount() const;
    int sortKeyCount() const;
    int importExportProfileCount() const;

    const QVector<FieldDefinition>& fields() const;
    const QVector<CardRecord>& cards() const;
    const QVector<ReportDefinition>& reports() const;
    const QVector<DeckSortKey>& sortKeys() const;
    const QVector<ImportExportProfile>& importExportProfiles() const;
    const CardTemplateLayout& cardTemplateLayout() const;
    const QByteArray& legacyControlRecord() const;
    const DeckAppearance& appearance() const;
    bool hasSecurity() const;
    bool hasEncryptedSecurity() const;
    const QString& securityPassword() const;

    const FieldDefinition& fieldAt(int index) const;
    const CardRecord& cardAt(int index) const;
    CardRecord& cardAt(int index);
    const ReportDefinition& reportAt(int index) const;

    void addField(FieldDefinition field);
    void setFields(QVector<FieldDefinition> fields);
    void addCard(CardRecord card);
    void setCards(QVector<CardRecord> cards);
    void addReport(ReportDefinition report);
    void insertReport(int index, ReportDefinition report);
    void setSortKeys(QVector<DeckSortKey> sortKeys);
    void clearSortKeys();
    void setImportExportProfiles(QVector<ImportExportProfile> profiles);
    void addImportExportProfile(ImportExportProfile profile);
    void clearImportExportProfiles();
    void setCardTemplateLayout(CardTemplateLayout layout);
    void setLegacyControlRecord(QByteArray record);
    void setAppearance(DeckAppearance appearance);
    void setSecurity(QString password, bool encrypted);
    void clearSecurity();
    void setReport(int index, ReportDefinition report);
    void removeReport(int index);
    void clear();

private:
    QString m_name;
    QString m_description;
    QVector<FieldDefinition> m_fields;
    QVector<CardRecord> m_cards;
    QVector<DeckSortKey> m_sortKeys;
    QVector<ImportExportProfile> m_importExportProfiles;
    QVector<ReportDefinition> m_reports;
    CardTemplateLayout m_cardTemplateLayout;
    QByteArray m_legacyControlRecord;
    DeckAppearance m_appearance;
    QString m_securityPassword;
    bool m_securityEncrypted = false;
};

Deck createSampleProjectDeck();

} // namespace CardStack
