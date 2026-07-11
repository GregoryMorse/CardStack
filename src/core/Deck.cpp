#include "Deck.h"

#include "DelimitedText.h"

#include <utility>

namespace CardStack {

namespace {
const FieldDefinition emptyField;
const CardRecord emptyCard;
const ReportDefinition emptyReport;
}

Deck::Deck(QString name)
    : m_name(std::move(name))
{
}

const QString& Deck::name() const
{
    return m_name;
}

void Deck::setName(QString name)
{
    m_name = std::move(name);
}

const QString& Deck::description() const
{
    return m_description;
}

void Deck::setDescription(QString description)
{
    m_description = std::move(description);
}

int Deck::fieldCount() const
{
    return m_fields.size();
}

int Deck::cardCount() const
{
    return m_cards.size();
}

int Deck::reportCount() const
{
    return m_reports.size();
}

int Deck::sortKeyCount() const
{
    return m_sortKeys.size();
}

int Deck::importExportProfileCount() const
{
    return m_importExportProfiles.size();
}

const QVector<FieldDefinition>& Deck::fields() const
{
    return m_fields;
}

const QVector<CardRecord>& Deck::cards() const
{
    return m_cards;
}

const QVector<ReportDefinition>& Deck::reports() const
{
    return m_reports;
}

const QVector<DeckSortKey>& Deck::sortKeys() const
{
    return m_sortKeys;
}

const QVector<ImportExportProfile>& Deck::importExportProfiles() const
{
    return m_importExportProfiles;
}

const CardTemplateLayout& Deck::cardTemplateLayout() const
{
    return m_cardTemplateLayout;
}

bool Deck::hasSecurity() const
{
    return !m_securityPassword.isEmpty();
}

bool Deck::hasEncryptedSecurity() const
{
    return m_securityEncrypted;
}

const QString& Deck::securityPassword() const
{
    return m_securityPassword;
}

const FieldDefinition& Deck::fieldAt(int index) const
{
    if (index < 0 || index >= m_fields.size()) {
        return emptyField;
    }

    return m_fields[index];
}

const CardRecord& Deck::cardAt(int index) const
{
    if (index < 0 || index >= m_cards.size()) {
        return emptyCard;
    }

    return m_cards[index];
}

CardRecord& Deck::cardAt(int index)
{
    return m_cards[index];
}

const ReportDefinition& Deck::reportAt(int index) const
{
    if (index < 0 || index >= m_reports.size()) {
        return emptyReport;
    }

    return m_reports[index];
}

void Deck::addField(FieldDefinition field)
{
    m_fields.append(std::move(field));
}

void Deck::addCard(CardRecord card)
{
    m_cards.append(std::move(card));
}

void Deck::addReport(ReportDefinition report)
{
    m_reports.append(std::move(report));
}

void Deck::setSortKeys(QVector<DeckSortKey> sortKeys)
{
    m_sortKeys = std::move(sortKeys);
}

void Deck::clearSortKeys()
{
    m_sortKeys.clear();
}

void Deck::setImportExportProfiles(QVector<ImportExportProfile> profiles)
{
    m_importExportProfiles = std::move(profiles);
}

void Deck::addImportExportProfile(ImportExportProfile profile)
{
    m_importExportProfiles.append(std::move(profile));
}

void Deck::clearImportExportProfiles()
{
    m_importExportProfiles.clear();
}

void Deck::setCardTemplateLayout(CardTemplateLayout layout)
{
    m_cardTemplateLayout = std::move(layout);
}

void Deck::setSecurity(QString password, bool encrypted)
{
    m_securityPassword = std::move(password);
    m_securityEncrypted = encrypted && !m_securityPassword.isEmpty();
}

void Deck::clearSecurity()
{
    m_securityPassword.clear();
    m_securityEncrypted = false;
}

void Deck::setReport(int index, ReportDefinition report)
{
    if (index < 0 || index >= m_reports.size()) {
        return;
    }

    m_reports[index] = std::move(report);
}

void Deck::removeReport(int index)
{
    if (index < 0 || index >= m_reports.size()) {
        return;
    }

    m_reports.removeAt(index);
}

void Deck::clear()
{
    m_fields.clear();
    m_cards.clear();
    m_sortKeys.clear();
    m_importExportProfiles.clear();
    m_reports.clear();
    m_cardTemplateLayout = {};
    clearSecurity();
}

Deck createSampleProjectDeck()
{
    Deck deck(QStringLiteral("Community Projects"));
    deck.setDescription(QStringLiteral("A modern sample deck for tracking open-source and neighborhood projects."));
    deck.addField(FieldDefinition(QStringLiteral("Project"), FieldType::Text, 255));
    deck.addField(FieldDefinition(QStringLiteral("Status"), FieldType::Text, 80));
    deck.addField(FieldDefinition(QStringLiteral("Lead"), FieldType::Text, 120));
    deck.addField(FieldDefinition(QStringLiteral("Website"), FieldType::Text, 255));
    deck.addField(FieldDefinition(QStringLiteral("Next step"), FieldType::Text, 255));
    deck.addField(FieldDefinition(QStringLiteral("Notes"), FieldType::Notes, 8192));
    deck.setSortKeys({
        {0, false},
        {1, false},
    });
    deck.setImportExportProfiles({
        DelimitedText::csvProfile(ImportExportProfileType::Export),
        DelimitedText::tsvProfile(ImportExportProfileType::Export),
    });

    deck.addCard(CardRecord({
        QStringLiteral("CardStack"),
        QStringLiteral("Planning"),
        QStringLiteral("Core team"),
        QStringLiteral("https://example.org/cardstack"),
        QStringLiteral("Finish SQLite-backed deck editing"),
        QStringLiteral("Sample record for the modern open-source application workflow.")
    }));
    deck.addCard(CardRecord({
        QStringLiteral("Neighborhood Archive"),
        QStringLiteral("In progress"),
        QStringLiteral("Avery"),
        QStringLiteral("https://example.org/archive"),
        QStringLiteral("Import the first CSV contact list"),
        QStringLiteral("Tracks volunteers, collections, and follow-up tasks.")
    }));

    return deck;
}

} // namespace CardStack
