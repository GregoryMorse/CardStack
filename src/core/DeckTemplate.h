#pragma once

#include "Deck.h"

#include <optional>
#include <QRect>
#include <QStringList>

namespace CardStack {

enum class LegacyTemplateControlRole {
    Grouped = 0,
    Standalone = 1
};

enum class LegacyTemplateTextAlignment {
    Left = 'l',
    Center = 'c',
    Right = 'r'
};

struct LegacyTemplateFieldDescriptor {
    int fieldIndex = -1;
    QString name;
    FieldType fieldType = FieldType::Text;
    quint16 legacyFlags = 0;
    LegacyTemplateControlRole controlRole = LegacyTemplateControlRole::Grouped;
    quint16 dialableMarker = 0;
    int recordOffset = 0;
    int dataLength = 0;
    QRect legacyBounds640;
    LegacyTemplateTextAlignment textAlignment = LegacyTemplateTextAlignment::Left;
    quint8 legacyDisplayWidthLowByte = 0;
    quint8 legacyDisplayWidthHighByte = 0;

    int displayWidth() const
    {
        return static_cast<int>(legacyDisplayWidthLowByte)
            | (static_cast<int>(legacyDisplayWidthHighByte) << 8);
    }

    bool operator==(const LegacyTemplateFieldDescriptor&) const = default;
};

struct LegacyTemplateTextFrameDescriptor {
    QString text;
    QRect legacyBounds640;
    quint8 legacyTailByte = 0;

    bool operator==(const LegacyTemplateTextFrameDescriptor&) const = default;
};

struct DeckTemplate {
    int legacyResourceId = 0;
    QString name;
    QString description;
    QVector<FieldDefinition> fields;
    QVector<DeckSortKey> sortKeys;
    QVector<ReportDefinition> reports;
    QVector<LegacyTemplateFieldDescriptor> legacyFields;
    QVector<LegacyTemplateTextFrameDescriptor> legacyTextFrames;
    CardTemplateLayout layout;
    DeckAppearance appearance;
    bool schemaFromLegacyResource = false;
    bool layoutFromLegacyResource = false;
    bool layoutGeneratedFromSchema = false;
};

const QVector<DeckTemplate>& builtInDeckTemplates();
QStringList builtInDeckTemplateNames();
std::optional<DeckTemplate> findBuiltInDeckTemplate(const QString& name);

Deck createDeckFromTemplate(const DeckTemplate& deckTemplate, QString deckName = {});
Deck createDeckFromTemplateName(const QString& templateName, QString deckName = {});
Deck createDeckFromScratch(QString deckName = {});
Deck createDeckPatternedAfterDeck(const Deck& sourceDeck, QString deckName = {});

} // namespace CardStack
