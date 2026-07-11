#include "DeckTemplate.h"

#include <algorithm>
#include <QStringList>
#include <utility>

namespace CardStack {
namespace {

constexpr int DefaultTextLength = 255;
constexpr int NotesLength = 40;
constexpr int LegacyTemplateScale = 10;
constexpr int LegacyTemplateCanvasWidth = 640;
constexpr int LegacyTemplateCanvasHeight = 480;
constexpr quint16 LegacyVariableTextField = 0x0001;
constexpr quint16 LegacyMultilineEditor = 0x0002;
constexpr quint16 LegacyFixedTextField = 0;
constexpr quint16 LegacyNotesField = LegacyVariableTextField | LegacyMultilineEditor;
constexpr quint16 LegacyNotDialable = 0;
constexpr quint16 LegacyDialable = 1;

FieldDefinition textField(const QString& name, int maxLength = DefaultTextLength)
{
    return FieldDefinition(name, FieldType::Text, maxLength);
}

FieldDefinition notesField(const QString& name = QStringLiteral("Notes"), int maxLength = NotesLength)
{
    return FieldDefinition(name, FieldType::Notes, maxLength);
}

QRect legacyBounds(int left, int top, int right, int bottom)
{
    return QRect(left, top, std::max(0, right - left), std::max(0, bottom - top));
}

QRect scaledLegacyBounds(const QRect& bounds)
{
    return QRect(
        bounds.left() * LegacyTemplateScale,
        bounds.top() * LegacyTemplateScale,
        bounds.width() * LegacyTemplateScale,
        bounds.height() * LegacyTemplateScale);
}

FieldType fieldTypeFromLegacyFlags(quint16 legacyFlags)
{
    return (legacyFlags & LegacyVariableTextField) != 0 ? FieldType::Notes : FieldType::Text;
}

LegacyTemplateFieldDescriptor legacyField(
    int fieldIndex,
    QString name,
    quint16 legacyFlags,
    LegacyTemplateControlRole controlRole,
    quint16 dialableMarker,
    int recordOffset,
    int dataLength,
    int left,
    int top,
    int right,
    int bottom,
    LegacyTemplateTextAlignment textAlignment,
    quint8 legacyStyleMetricByte,
    quint8 legacyTailByte)
{
    LegacyTemplateFieldDescriptor field;
    field.fieldIndex = fieldIndex;
    field.name = std::move(name);
    field.fieldType = fieldTypeFromLegacyFlags(legacyFlags);
    field.legacyFlags = legacyFlags;
    field.controlRole = controlRole;
    field.dialableMarker = dialableMarker;
    field.recordOffset = recordOffset;
    field.dataLength = dataLength;
    field.legacyBounds640 = legacyBounds(left, top, right, bottom);
    field.textAlignment = textAlignment;
    field.legacyStyleMetricByte = legacyStyleMetricByte;
    field.legacyTailByte = legacyTailByte;
    return field;
}

LegacyTemplateTextFrameDescriptor legacyTextFrame(
    QString text,
    int left,
    int top,
    int right,
    int bottom,
    quint8 legacyTailByte)
{
    LegacyTemplateTextFrameDescriptor frame;
    frame.text = std::move(text);
    frame.legacyBounds640 = legacyBounds(left, top, right, bottom);
    frame.legacyTailByte = legacyTailByte;
    return frame;
}

QVector<FieldDefinition> fieldDefinitionsFromLegacyFields(const QVector<LegacyTemplateFieldDescriptor>& legacyFields)
{
    QVector<FieldDefinition> fields;
    fields.reserve(legacyFields.size());
    for (const LegacyTemplateFieldDescriptor& legacyField : legacyFields) {
        fields.append(FieldDefinition(legacyField.name, legacyField.fieldType, legacyField.dataLength));
    }
    return fields;
}

Deck deckWithFields(QString name, const QVector<FieldDefinition>& fields)
{
    Deck deck(std::move(name));
    deck.setDescription(QStringLiteral("Created from a built-in CardStack template decoded from the legacy template catalog."));
    for (const FieldDefinition& field : fields) {
        deck.addField(field);
    }
    return deck;
}

CardTemplateLayout generatedLayoutForFields(const QVector<FieldDefinition>& fields)
{
    CardTemplateLayout layout;
    constexpr int LabelLeft = 240;
    constexpr int FieldLeft = 1780;
    constexpr int TopStart = 280;
    constexpr int RowHeight = 360;
    constexpr int LabelWidth = 1280;
    constexpr int FieldWidth = 4200;
    constexpr int TextHeight = 230;
    constexpr int NotesHeight = 1050;

    int top = TopStart;
    for (int index = 0; index < fields.size(); ++index) {
        const FieldDefinition& field = fields.at(index);

        CardTemplateFrame label;
        label.kind = CardTemplateFrameKind::Text;
        label.bounds = QRect(LabelLeft, top, LabelWidth, TextHeight);
        label.text = field.name();
        layout.frames.append(label);

        CardTemplateFrame data;
        data.kind = field.isNotes() ? CardTemplateFrameKind::NotesBox : CardTemplateFrameKind::DataBox;
        data.bounds = QRect(FieldLeft, top, FieldWidth, field.isNotes() ? NotesHeight : TextHeight);
        data.text = field.name();
        data.fieldIndex = index;
        layout.frames.append(data);

        top += field.isNotes() ? NotesHeight + 180 : RowHeight;
    }

    layout.canvasHeight = std::max(layout.canvasHeight, top + 240);
    return layout;
}

CardTemplateLayout layoutFromLegacyFields(
    const QVector<LegacyTemplateFieldDescriptor>& legacyFields,
    const QVector<LegacyTemplateTextFrameDescriptor>& legacyTextFrames)
{
    CardTemplateLayout layout;
    layout.canvasWidth = LegacyTemplateCanvasWidth * LegacyTemplateScale;
    layout.canvasHeight = LegacyTemplateCanvasHeight * LegacyTemplateScale;

    layout.frames.reserve(legacyTextFrames.size() + legacyFields.size());
    for (const LegacyTemplateTextFrameDescriptor& legacyTextFrame : legacyTextFrames) {
        CardTemplateFrame frame;
        frame.kind = CardTemplateFrameKind::Text;
        frame.bounds = scaledLegacyBounds(legacyTextFrame.legacyBounds640);
        frame.text = legacyTextFrame.text;
        frame.styleFlags = legacyTextFrame.legacyTailByte;
        layout.frames.append(frame);
    }

    for (const LegacyTemplateFieldDescriptor& legacyField : legacyFields) {
        CardTemplateFrame frame;
        frame.kind = legacyField.fieldType == FieldType::Notes ? CardTemplateFrameKind::NotesBox : CardTemplateFrameKind::DataBox;
        frame.bounds = scaledLegacyBounds(legacyField.legacyBounds640);
        frame.text = legacyField.name;
        frame.fieldIndex = legacyField.fieldIndex;
        frame.styleFlags = legacyField.legacyTailByte;
        layout.frames.append(frame);
    }

    return layout;
}

DeckTemplate deckTemplateFromDecodedLegacyTemplate(
    int legacyResourceId,
    QString name,
    QVector<LegacyTemplateFieldDescriptor> legacyFields,
    QVector<LegacyTemplateTextFrameDescriptor> legacyTextFrames)
{
    DeckTemplate deckTemplate;
    deckTemplate.legacyResourceId = legacyResourceId;
    deckTemplate.name = std::move(name);
    deckTemplate.legacyFields = std::move(legacyFields);
    deckTemplate.legacyTextFrames = std::move(legacyTextFrames);
    deckTemplate.fields = fieldDefinitionsFromLegacyFields(deckTemplate.legacyFields);
    deckTemplate.layout = layoutFromLegacyFields(deckTemplate.legacyFields, deckTemplate.legacyTextFrames);
    deckTemplate.schemaFromLegacyResource = true;
    deckTemplate.layoutFromLegacyResource = true;
    deckTemplate.layoutGeneratedFromSchema = false;
    return deckTemplate;
}

} // namespace

const QVector<DeckTemplate>& builtInDeckTemplates()
{
    static const QVector<DeckTemplate> templates = {
        deckTemplateFromDecodedLegacyTemplate(101, QStringLiteral("Book Library"), {
            legacyField(0, QStringLiteral("Title"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 0, 50, 66, 30, 345, 51, LegacyTemplateTextAlignment::Left, 23, 1),
            legacyField(1, QStringLiteral("Index"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 50, 50, 66, 54, 345, 75, LegacyTemplateTextAlignment::Left, 23, 1),
            legacyField(2, QStringLiteral("Author"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 100, 50, 66, 78, 345, 99, LegacyTemplateTextAlignment::Left, 23, 1),
            legacyField(3, QStringLiteral("Publisher"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 150, 50, 66, 102, 345, 123, LegacyTemplateTextAlignment::Left, 23, 1),
            legacyField(4, QStringLiteral("Catalog number"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 200, 20, 105, 126, 186, 147, LegacyTemplateTextAlignment::Left, 81, 0),
            legacyField(5, QStringLiteral("Year published"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 220, 5, 291, 126, 345, 147, LegacyTemplateTextAlignment::Left, 54, 0),
            legacyField(6, QStringLiteral("Volume"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 225, 10, 105, 150, 150, 171, LegacyTemplateTextAlignment::Left, 45, 0),
            legacyField(7, QStringLiteral("Number"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 235, 5, 207, 150, 252, 171, LegacyTemplateTextAlignment::Left, 45, 0),
            legacyField(8, QStringLiteral("Pages"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 240, 4, 300, 150, 345, 171, LegacyTemplateTextAlignment::Left, 45, 0),
            legacyField(9, QStringLiteral("Genre"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 244, 30, 105, 174, 345, 195, LegacyTemplateTextAlignment::Left, 240, 0),
            legacyField(10, QStringLiteral("Subject"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 274, 50, 105, 198, 345, 219, LegacyTemplateTextAlignment::Left, 240, 0),
            legacyField(11, QStringLiteral("Description"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 324, 200, 105, 222, 345, 243, LegacyTemplateTextAlignment::Left, 240, 0),
            legacyField(12, QStringLiteral("Notes"), LegacyNotesField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 524, 40, 66, 246, 345, 306, LegacyTemplateTextAlignment::Left, 23, 1),
        }, {

        }),
        deckTemplateFromDecodedLegacyTemplate(102, QStringLiteral("Business Cards"), {
            legacyField(0, QStringLiteral("First Name"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 0, 20, 69, 24, 144, 45, LegacyTemplateTextAlignment::Left, 75, 0),
            legacyField(1, QStringLiteral("Middle Initial"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 20, 15, 147, 24, 174, 45, LegacyTemplateTextAlignment::Left, 27, 0),
            legacyField(2, QStringLiteral("Last Name"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 35, 30, 177, 24, 294, 45, LegacyTemplateTextAlignment::Left, 117, 0),
            legacyField(3, QStringLiteral("Title"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 65, 50, 69, 48, 294, 69, LegacyTemplateTextAlignment::Left, 225, 0),
            legacyField(4, QStringLiteral("Company"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 115, 50, 69, 72, 294, 93, LegacyTemplateTextAlignment::Left, 225, 0),
            legacyField(5, QStringLiteral("Address 1"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 165, 50, 69, 96, 294, 117, LegacyTemplateTextAlignment::Left, 225, 0),
            legacyField(6, QStringLiteral("Address 2"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 215, 50, 69, 120, 294, 141, LegacyTemplateTextAlignment::Left, 225, 0),
            legacyField(7, QStringLiteral("City"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 265, 25, 69, 144, 177, 165, LegacyTemplateTextAlignment::Left, 108, 0),
            legacyField(8, QStringLiteral("State"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 290, 5, 180, 144, 207, 165, LegacyTemplateTextAlignment::Left, 27, 0),
            legacyField(9, QStringLiteral("Zip + 4"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 295, 10, 210, 144, 294, 165, LegacyTemplateTextAlignment::Left, 84, 0),
            legacyField(10, QStringLiteral("Country"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 305, 25, 69, 168, 294, 189, LegacyTemplateTextAlignment::Left, 225, 0),
            legacyField(11, QStringLiteral("Phone"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyDialable, 330, 25, 69, 192, 153, 213, LegacyTemplateTextAlignment::Left, 84, 0),
            legacyField(12, QStringLiteral("Fax"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 355, 15, 210, 192, 294, 213, LegacyTemplateTextAlignment::Left, 84, 0),
            legacyField(13, QStringLiteral("Home"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyDialable, 370, 15, 69, 216, 153, 237, LegacyTemplateTextAlignment::Left, 84, 0),
            legacyField(14, QStringLiteral("Cellular"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyDialable, 385, 25, 210, 216, 294, 237, LegacyTemplateTextAlignment::Left, 84, 0),
            legacyField(15, QStringLiteral("Category"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 410, 50, 69, 240, 294, 261, LegacyTemplateTextAlignment::Left, 225, 0),
            legacyField(16, QStringLiteral("Notes"), LegacyNotesField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 460, 40, 69, 264, 294, 321, LegacyTemplateTextAlignment::Left, 225, 0),
        }, {
            legacyTextFrame(QStringLiteral("Name"), 21, 24, 66, 48, 114),
            legacyTextFrame(QStringLiteral("Address"), 9, 96, 66, 117, 114),
        }),
        deckTemplateFromDecodedLegacyTemplate(103, QStringLiteral("Credit Cards"), {
            legacyField(0, QStringLiteral("Type"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 0, 30, 87, 24, 237, 45, LegacyTemplateTextAlignment::Left, 150, 0),
            legacyField(1, QStringLiteral("Institution"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 30, 50, 87, 48, 339, 69, LegacyTemplateTextAlignment::Left, 252, 0),
            legacyField(2, QStringLiteral("Address"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 80, 35, 87, 72, 339, 93, LegacyTemplateTextAlignment::Left, 252, 0),
            legacyField(3, QStringLiteral("City"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 115, 35, 87, 96, 222, 117, LegacyTemplateTextAlignment::Left, 135, 0),
            legacyField(4, QStringLiteral("State"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 150, 5, 225, 96, 255, 117, LegacyTemplateTextAlignment::Left, 30, 0),
            legacyField(5, QStringLiteral("Zip + 4"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 155, 10, 258, 96, 339, 117, LegacyTemplateTextAlignment::Left, 81, 0),
            legacyField(6, QStringLiteral("Card number"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 165, 20, 87, 120, 237, 141, LegacyTemplateTextAlignment::Left, 150, 0),
            legacyField(7, QStringLiteral("Expiration"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 185, 5, 303, 120, 339, 141, LegacyTemplateTextAlignment::Left, 36, 0),
            legacyField(8, QStringLiteral("Cardholder"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 190, 50, 87, 144, 270, 165, LegacyTemplateTextAlignment::Left, 183, 0),
            legacyField(9, QStringLiteral("PIN"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 240, 28, 87, 168, 237, 189, LegacyTemplateTextAlignment::Left, 150, 0),
            legacyField(10, QStringLiteral("Credit limit"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 268, 11, 87, 192, 153, 213, LegacyTemplateTextAlignment::Left, 66, 0),
            legacyField(11, QStringLiteral("Call if lost"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 279, 20, 87, 216, 237, 240, LegacyTemplateTextAlignment::Left, 150, 0),
            legacyField(12, QStringLiteral("Cust. Service"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 299, 20, 87, 243, 237, 264, LegacyTemplateTextAlignment::Left, 150, 0),
            legacyField(13, QStringLiteral("Notes"), LegacyNotesField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 319, 40, 87, 267, 342, 333, LegacyTemplateTextAlignment::Left, 255, 0),
        }, {

        }),
        deckTemplateFromDecodedLegacyTemplate(104, QStringLiteral("Data/Boxes"), {
            legacyField(0, QStringLiteral("Data Box 1"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 0, 75, 9, 27, 345, 51, LegacyTemplateTextAlignment::Left, 80, 1),
            legacyField(1, QStringLiteral("Data Box 2"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 75, 75, 9, 54, 345, 78, LegacyTemplateTextAlignment::Left, 80, 1),
            legacyField(2, QStringLiteral("Data Box 3"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 150, 75, 9, 81, 345, 105, LegacyTemplateTextAlignment::Left, 80, 1),
            legacyField(3, QStringLiteral("Data Box 4"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 225, 75, 9, 108, 345, 132, LegacyTemplateTextAlignment::Left, 80, 1),
            legacyField(4, QStringLiteral("Data Box 5"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 300, 75, 9, 135, 345, 159, LegacyTemplateTextAlignment::Left, 80, 1),
            legacyField(5, QStringLiteral("Notes"), LegacyNotesField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 375, 40, 9, 162, 345, 246, LegacyTemplateTextAlignment::Left, 80, 1),
        }, {

        }),
        deckTemplateFromDecodedLegacyTemplate(105, QStringLiteral("File Folders"), {
            legacyField(0, QStringLiteral("Main Heading"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 0, 50, 105, 24, 333, 48, LegacyTemplateTextAlignment::Left, 228, 0),
            legacyField(1, QStringLiteral("Sub Heading"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 50, 50, 105, 51, 333, 75, LegacyTemplateTextAlignment::Left, 228, 0),
            legacyField(2, QStringLiteral("Cabinet Location"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 100, 35, 105, 78, 195, 102, LegacyTemplateTextAlignment::Left, 90, 0),
            legacyField(3, QStringLiteral("Drawer"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 135, 15, 255, 78, 333, 102, LegacyTemplateTextAlignment::Left, 78, 0),
            legacyField(4, QStringLiteral("Contents"), LegacyNotesField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 150, 40, 105, 105, 333, 165, LegacyTemplateTextAlignment::Left, 228, 0),
        }, {

        }),
        deckTemplateFromDecodedLegacyTemplate(106, QStringLiteral("Home Inventory"), {
            legacyField(0, QStringLiteral("Item"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 0, 50, 108, 27, 336, 51, LegacyTemplateTextAlignment::Left, 228, 0),
            legacyField(1, QStringLiteral("Make/Model"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 50, 50, 108, 54, 336, 78, LegacyTemplateTextAlignment::Left, 228, 0),
            legacyField(2, QStringLiteral("Serial number"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 100, 30, 108, 81, 249, 105, LegacyTemplateTextAlignment::Left, 141, 0),
            legacyField(3, QStringLiteral("Location"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 130, 50, 108, 108, 336, 132, LegacyTemplateTextAlignment::Left, 228, 0),
            legacyField(4, QStringLiteral("Purchase date"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 180, 20, 108, 135, 177, 159, LegacyTemplateTextAlignment::Left, 69, 0),
            legacyField(5, QStringLiteral("Purchase price"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 200, 15, 279, 135, 336, 159, LegacyTemplateTextAlignment::Left, 57, 0),
            legacyField(6, QStringLiteral("Purchased from"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 215, 30, 108, 162, 336, 186, LegacyTemplateTextAlignment::Left, 228, 0),
            legacyField(7, QStringLiteral("Current value"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 245, 15, 108, 189, 177, 213, LegacyTemplateTextAlignment::Left, 69, 0),
            legacyField(8, QStringLiteral("Insured value"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 260, 15, 279, 189, 336, 213, LegacyTemplateTextAlignment::Left, 57, 0),
            legacyField(9, QStringLiteral("Warranty expires"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 275, 20, 108, 216, 177, 240, LegacyTemplateTextAlignment::Left, 69, 0),
            legacyField(10, QStringLiteral("Notes"), LegacyNotesField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 295, 40, 108, 243, 336, 306, LegacyTemplateTextAlignment::Left, 228, 0),
        }, {

        }),
        deckTemplateFromDecodedLegacyTemplate(107, QStringLiteral("Mailing List"), {
            legacyField(0, QStringLiteral("First Name"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 0, 30, 81, 24, 186, 45, LegacyTemplateTextAlignment::Left, 105, 0),
            legacyField(1, QStringLiteral("Last Name"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 30, 30, 189, 24, 306, 45, LegacyTemplateTextAlignment::Left, 117, 0),
            legacyField(2, QStringLiteral("Address 1"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 60, 50, 81, 48, 306, 69, LegacyTemplateTextAlignment::Left, 225, 0),
            legacyField(3, QStringLiteral("Address 2"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 110, 50, 81, 72, 306, 93, LegacyTemplateTextAlignment::Left, 225, 0),
            legacyField(4, QStringLiteral("City"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 160, 25, 81, 96, 189, 117, LegacyTemplateTextAlignment::Left, 108, 0),
            legacyField(5, QStringLiteral("State:"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 185, 2, 192, 96, 219, 117, LegacyTemplateTextAlignment::Left, 27, 0),
            legacyField(6, QStringLiteral("Zip + 4"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 187, 10, 222, 96, 306, 117, LegacyTemplateTextAlignment::Left, 84, 0),
            legacyField(7, QStringLiteral("Phone"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyDialable, 197, 15, 81, 120, 165, 141, LegacyTemplateTextAlignment::Left, 84, 0),
            legacyField(8, QStringLiteral("Phone 2"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyDialable, 212, 15, 222, 120, 306, 141, LegacyTemplateTextAlignment::Left, 84, 0),
            legacyField(9, QStringLiteral("Country"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 227, 25, 81, 144, 150, 165, LegacyTemplateTextAlignment::Left, 69, 0),
        }, {
            legacyTextFrame(QStringLiteral("Name"), 33, 24, 78, 42, 114),
            legacyTextFrame(QStringLiteral("Address"), 9, 48, 78, 69, 114),
        }),
        deckTemplateFromDecodedLegacyTemplate(108, QStringLiteral("Music Library"), {
            legacyField(0, QStringLiteral("Title"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 0, 100, 48, 24, 453, 48, LegacyTemplateTextAlignment::Left, 149, 1),
            legacyField(1, QStringLiteral("Artist"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 100, 50, 84, 51, 234, 75, LegacyTemplateTextAlignment::Left, 150, 0),
            legacyField(2, QStringLiteral("Composer"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 150, 50, 303, 51, 453, 75, LegacyTemplateTextAlignment::Left, 150, 0),
            legacyField(3, QStringLiteral("Conductor"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 200, 35, 84, 78, 234, 102, LegacyTemplateTextAlignment::Left, 150, 0),
            legacyField(4, QStringLiteral("Soloist"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 235, 35, 303, 78, 453, 102, LegacyTemplateTextAlignment::Left, 150, 0),
            legacyField(5, QStringLiteral("Category"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 270, 30, 84, 105, 186, 129, LegacyTemplateTextAlignment::Left, 102, 0),
            legacyField(6, QStringLiteral("Label"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 300, 50, 48, 132, 273, 156, LegacyTemplateTextAlignment::Left, 225, 0),
            legacyField(7, QStringLiteral("Catalog number"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 350, 15, 375, 132, 453, 156, LegacyTemplateTextAlignment::Left, 78, 0),
            legacyField(8, QStringLiteral("Producer"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 365, 35, 84, 159, 195, 183, LegacyTemplateTextAlignment::Left, 111, 0),
            legacyField(9, QStringLiteral("Engineer"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 400, 35, 255, 159, 366, 183, LegacyTemplateTextAlignment::Left, 111, 0),
            legacyField(10, QStringLiteral("Year"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 435, 4, 402, 159, 453, 183, LegacyTemplateTextAlignment::Left, 51, 0),
            legacyField(11, QStringLiteral("Tracks"), LegacyNotesField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 494, 40, 45, 186, 297, 264, LegacyTemplateTextAlignment::Left, 252, 0),
            legacyField(12, QStringLiteral("Date"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 439, 25, 345, 186, 450, 210, LegacyTemplateTextAlignment::Left, 105, 0),
            legacyField(13, QStringLiteral("Medium"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 464, 20, 360, 213, 450, 237, LegacyTemplateTextAlignment::Left, 90, 0),
            legacyField(14, QStringLiteral("Tape number"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 484, 10, 393, 240, 450, 264, LegacyTemplateTextAlignment::Left, 57, 0),
        }, {

        }),
        deckTemplateFromDecodedLegacyTemplate(109, QStringLiteral("Notes"), {
            legacyField(0, QStringLiteral("Notes"), LegacyNotesField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 0, 40, 9, 27, 312, 216, LegacyTemplateTextAlignment::Left, 47, 1),
        }, {

        }),
        deckTemplateFromDecodedLegacyTemplate(110, QStringLiteral("Personnel Records"), {
            legacyField(0, QStringLiteral("First Name"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 0, 20, 102, 24, 189, 45, LegacyTemplateTextAlignment::Left, 87, 0),
            legacyField(1, QStringLiteral("Middle Initial"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 20, 2, 192, 24, 216, 45, LegacyTemplateTextAlignment::Left, 24, 0),
            legacyField(2, QStringLiteral("Last Name"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 22, 30, 219, 24, 339, 45, LegacyTemplateTextAlignment::Left, 120, 0),
            legacyField(3, QStringLiteral("Title"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 52, 50, 102, 48, 339, 69, LegacyTemplateTextAlignment::Left, 237, 0),
            legacyField(4, QStringLiteral("Department"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 102, 30, 102, 72, 339, 93, LegacyTemplateTextAlignment::Left, 237, 0),
            legacyField(5, QStringLiteral("Supervisor"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 132, 30, 102, 96, 339, 117, LegacyTemplateTextAlignment::Left, 237, 0),
            legacyField(6, QStringLiteral("Date Hired"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 162, 20, 102, 120, 186, 141, LegacyTemplateTextAlignment::Left, 84, 0),
            legacyField(7, QStringLiteral("Birthday"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 182, 20, 261, 120, 339, 141, LegacyTemplateTextAlignment::Left, 78, 0),
            legacyField(8, QStringLiteral("Phone"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyDialable, 202, 25, 102, 144, 186, 165, LegacyTemplateTextAlignment::Left, 84, 0),
            legacyField(9, QStringLiteral("Home"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyDialable, 227, 20, 249, 144, 339, 165, LegacyTemplateTextAlignment::Left, 90, 0),
            legacyField(10, QStringLiteral("Social Security"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 247, 11, 102, 168, 219, 192, LegacyTemplateTextAlignment::Left, 117, 0),
            legacyField(11, QStringLiteral("Address"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 258, 50, 102, 195, 339, 216, LegacyTemplateTextAlignment::Left, 237, 0),
            legacyField(12, QStringLiteral("City"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 308, 20, 102, 219, 222, 240, LegacyTemplateTextAlignment::Left, 120, 0),
            legacyField(13, QStringLiteral("State"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 328, 2, 225, 219, 252, 240, LegacyTemplateTextAlignment::Left, 27, 0),
            legacyField(14, QStringLiteral("Zip + 4"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 330, 10, 255, 219, 339, 240, LegacyTemplateTextAlignment::Left, 84, 0),
            legacyField(15, QStringLiteral("Emergency"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 340, 25, 102, 243, 186, 264, LegacyTemplateTextAlignment::Left, 84, 0),
            legacyField(16, QStringLiteral("Contact"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 365, 30, 255, 243, 339, 264, LegacyTemplateTextAlignment::Left, 84, 0),
            legacyField(17, QStringLiteral("Notes"), LegacyNotesField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 395, 40, 102, 267, 339, 327, LegacyTemplateTextAlignment::Left, 237, 0),
        }, {
            legacyTextFrame(QStringLiteral("Name"), 57, 24, 99, 45, 114),
        }),
        deckTemplateFromDecodedLegacyTemplate(111, QStringLiteral("Recipes"), {
            legacyField(0, QStringLiteral("Title"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 0, 50, 99, 24, 351, 45, LegacyTemplateTextAlignment::Left, 252, 0),
            legacyField(1, QStringLiteral("Category"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 50, 50, 99, 48, 351, 69, LegacyTemplateTextAlignment::Left, 252, 0),
            legacyField(2, QStringLiteral("From"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 100, 50, 99, 72, 351, 93, LegacyTemplateTextAlignment::Left, 252, 0),
            legacyField(3, QStringLiteral("Servings"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 150, 20, 99, 96, 210, 117, LegacyTemplateTextAlignment::Left, 111, 0),
            legacyField(4, QStringLiteral("Calories"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 170, 8, 270, 96, 351, 117, LegacyTemplateTextAlignment::Left, 81, 0),
            legacyField(5, QStringLiteral("Time to prepare"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 178, 20, 99, 120, 210, 141, LegacyTemplateTextAlignment::Left, 111, 0),
            legacyField(6, QStringLiteral("Fat"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 198, 8, 270, 120, 351, 141, LegacyTemplateTextAlignment::Left, 81, 0),
            legacyField(7, QStringLiteral("Recipe"), LegacyNotesField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 206, 40, 99, 144, 351, 246, LegacyTemplateTextAlignment::Left, 252, 0),
        }, {

        }),
        deckTemplateFromDecodedLegacyTemplate(112, QStringLiteral("Rolodex Cards"), {
            legacyField(0, QStringLiteral("First Name"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 0, 20, 84, 27, 171, 48, LegacyTemplateTextAlignment::Left, 87, 0),
            legacyField(1, QStringLiteral("Middle Initial"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 20, 2, 174, 27, 198, 48, LegacyTemplateTextAlignment::Left, 24, 0),
            legacyField(2, QStringLiteral("Last Name"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 22, 30, 201, 27, 324, 48, LegacyTemplateTextAlignment::Left, 123, 0),
            legacyField(3, QStringLiteral("Company"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 52, 40, 84, 51, 324, 72, LegacyTemplateTextAlignment::Left, 240, 0),
            legacyField(4, QStringLiteral("Address 1"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 92, 40, 84, 75, 324, 96, LegacyTemplateTextAlignment::Left, 240, 0),
            legacyField(5, QStringLiteral("City"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 132, 20, 84, 99, 216, 120, LegacyTemplateTextAlignment::Left, 132, 0),
            legacyField(6, QStringLiteral("State"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 152, 2, 219, 99, 246, 120, LegacyTemplateTextAlignment::Left, 27, 0),
            legacyField(7, QStringLiteral("Zip + 4"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 154, 10, 249, 99, 324, 120, LegacyTemplateTextAlignment::Left, 75, 0),
            legacyField(8, QStringLiteral("Country"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 164, 30, 84, 123, 324, 144, LegacyTemplateTextAlignment::Left, 240, 0),
            legacyField(9, QStringLiteral("Work"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyDialable, 194, 25, 84, 147, 171, 168, LegacyTemplateTextAlignment::Left, 87, 0),
            legacyField(10, QStringLiteral("Home"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyDialable, 219, 15, 237, 147, 324, 168, LegacyTemplateTextAlignment::Left, 87, 0),
            legacyField(11, QStringLiteral("Birthday"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 234, 20, 84, 171, 213, 192, LegacyTemplateTextAlignment::Left, 129, 0),
            legacyField(12, QStringLiteral("Notes"), LegacyNotesField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 254, 40, 84, 195, 324, 243, LegacyTemplateTextAlignment::Left, 240, 0),
        }, {
            legacyTextFrame(QStringLiteral("Name"), 36, 27, 81, 48, 114),
            legacyTextFrame(QStringLiteral("Address"), 24, 72, 81, 93, 114),
        }),
        deckTemplateFromDecodedLegacyTemplate(113, QStringLiteral("Software Library"), {
            legacyField(0, QStringLiteral("Product"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 0, 30, 108, 27, 243, 48, LegacyTemplateTextAlignment::Left, 135, 0),
            legacyField(1, QStringLiteral("Version"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 30, 10, 300, 27, 336, 48, LegacyTemplateTextAlignment::Left, 36, 0),
            legacyField(2, QStringLiteral("Company"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 40, 50, 108, 51, 336, 72, LegacyTemplateTextAlignment::Left, 228, 0),
            legacyField(3, QStringLiteral("Serial #"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 90, 25, 108, 75, 219, 96, LegacyTemplateTextAlignment::Left, 111, 0),
            legacyField(4, QStringLiteral("Registered to"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 115, 50, 108, 99, 336, 120, LegacyTemplateTextAlignment::Left, 228, 0),
            legacyField(5, QStringLiteral("Purchased"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 165, 20, 108, 123, 177, 144, LegacyTemplateTextAlignment::Left, 69, 0),
            legacyField(6, QStringLiteral("From"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 185, 30, 219, 123, 336, 144, LegacyTemplateTextAlignment::Left, 117, 0),
            legacyField(7, QStringLiteral("Tech support"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 215, 20, 108, 147, 198, 168, LegacyTemplateTextAlignment::Left, 90, 0),
            legacyField(8, QStringLiteral("Support plan"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 235, 20, 108, 171, 198, 192, LegacyTemplateTextAlignment::Left, 90, 0),
            legacyField(9, QStringLiteral("Customer service"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 255, 20, 108, 195, 198, 216, LegacyTemplateTextAlignment::Left, 90, 0),
            legacyField(10, QStringLiteral("Notes"), LegacyNotesField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 275, 40, 108, 219, 333, 294, LegacyTemplateTextAlignment::Left, 225, 0),
        }, {

        }),
        deckTemplateFromDecodedLegacyTemplate(114, QStringLiteral("Video Library"), {
            legacyField(0, QStringLiteral("Title"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 0, 50, 93, 24, 339, 45, LegacyTemplateTextAlignment::Left, 246, 0),
            legacyField(1, QStringLiteral("Category"), LegacyFixedTextField, LegacyTemplateControlRole::Grouped, LegacyNotDialable, 50, 30, 93, 48, 339, 69, LegacyTemplateTextAlignment::Left, 246, 0),
            legacyField(2, QStringLiteral("Year made"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 80, 4, 93, 72, 138, 93, LegacyTemplateTextAlignment::Left, 45, 0),
            legacyField(3, QStringLiteral("Rating"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 84, 5, 192, 72, 237, 93, LegacyTemplateTextAlignment::Left, 45, 0),
            legacyField(4, QStringLiteral("Review"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 89, 10, 297, 72, 339, 93, LegacyTemplateTextAlignment::Left, 42, 0),
            legacyField(5, QStringLiteral("Date taped"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 99, 20, 93, 96, 183, 117, LegacyTemplateTextAlignment::Left, 90, 0),
            legacyField(6, QStringLiteral("Format"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 119, 20, 246, 96, 339, 117, LegacyTemplateTextAlignment::Left, 93, 0),
            legacyField(7, QStringLiteral("Tape number"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 139, 12, 93, 120, 183, 141, LegacyTemplateTextAlignment::Left, 90, 0),
            legacyField(8, QStringLiteral("Start"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 151, 6, 93, 144, 126, 165, LegacyTemplateTextAlignment::Left, 33, 0),
            legacyField(9, QStringLiteral("End"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 157, 6, 159, 144, 192, 165, LegacyTemplateTextAlignment::Left, 33, 0),
            legacyField(10, QStringLiteral("Length"), LegacyFixedTextField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 163, 30, 246, 144, 339, 165, LegacyTemplateTextAlignment::Left, 93, 0),
            legacyField(11, QStringLiteral("Notes"), LegacyNotesField, LegacyTemplateControlRole::Standalone, LegacyNotDialable, 193, 40, 93, 168, 339, 228, LegacyTemplateTextAlignment::Left, 246, 0),
        }, {

        }),    };
    return templates;
}

QStringList builtInDeckTemplateNames()
{
    QStringList names;
    const QVector<DeckTemplate>& templates = builtInDeckTemplates();
    names.reserve(templates.size());
    for (const DeckTemplate& deckTemplate : templates) {
        names.append(deckTemplate.name);
    }
    return names;
}

std::optional<DeckTemplate> findBuiltInDeckTemplate(const QString& name)
{
    for (const DeckTemplate& deckTemplate : builtInDeckTemplates()) {
        if (deckTemplate.name.compare(name, Qt::CaseInsensitive) == 0) {
            return deckTemplate;
        }
    }
    return std::nullopt;
}

Deck createDeckFromTemplate(const DeckTemplate& deckTemplate, QString deckName)
{
    if (deckName.trimmed().isEmpty()) {
        deckName = deckTemplate.name;
    }
    Deck deck = deckWithFields(std::move(deckName), deckTemplate.fields);
    deck.setCardTemplateLayout(deckTemplate.layout);
    return deck;
}

Deck createDeckFromTemplateName(const QString& templateName, QString deckName)
{
    const std::optional<DeckTemplate> deckTemplate = findBuiltInDeckTemplate(templateName);
    if (deckTemplate.has_value()) {
        return createDeckFromTemplate(deckTemplate.value(), std::move(deckName));
    }
    return createDeckFromScratch(std::move(deckName));
}

Deck createDeckFromScratch(QString deckName)
{
    if (deckName.trimmed().isEmpty()) {
        deckName = QStringLiteral("Untitled Deck");
    }

    Deck deck(std::move(deckName));
    deck.setDescription(QStringLiteral("Designed from scratch."));
    deck.addField(textField(QStringLiteral("New Data Box")));
    deck.setCardTemplateLayout(generatedLayoutForFields(deck.fields()));
    return deck;
}

Deck createDeckPatternedAfterDeck(const Deck& sourceDeck, QString deckName)
{
    if (deckName.trimmed().isEmpty()) {
        deckName = sourceDeck.name().isEmpty()
            ? QStringLiteral("Untitled Deck")
            : QStringLiteral("%1 Copy").arg(sourceDeck.name());
    }

    Deck deck(std::move(deckName));
    deck.setDescription(QStringLiteral("Patterned after an existing deck."));
    for (const FieldDefinition& field : sourceDeck.fields()) {
        deck.addField(field);
    }
    deck.setSortKeys(sourceDeck.sortKeys());
    deck.setImportExportProfiles(sourceDeck.importExportProfiles());
    for (const ReportDefinition& report : sourceDeck.reports()) {
        deck.addReport(report);
    }
    deck.setCardTemplateLayout(sourceDeck.cardTemplateLayout());
    return deck;
}

} // namespace CardStack
