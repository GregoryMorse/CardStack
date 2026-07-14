#pragma once

#include "CardTemplateLayout.h"
#include "Deck.h"
#include "LegacyOemCodec.h"
#include "FieldDefinition.h"

#include <QByteArray>
#include <QVector>
#include <algorithm>
#include <optional>
#include <utility>

namespace CardStack::LegacyTemplateLayoutFormat {

inline constexpr int MaximumFieldCount = 40;
inline constexpr int MaximumTextFrameCount = 40;
inline constexpr int CoordinateScale = 10;
inline constexpr int DefaultCanvasWidth = 6400;
inline constexpr int DefaultCanvasHeight = 4800;
inline constexpr int SortKeyCount = 3;
inline constexpr int SortKeyTableOffset = 0x49;
inline constexpr int SortKeyDescriptorSize = 4;
inline constexpr int SortKeyFlagsOffset = 2;
inline constexpr quint16 SortDescendingFlag = 0x0040;

inline constexpr int TextFrameCountOffset = 0x55;
inline constexpr int FieldCountOffset = 0x57;
inline constexpr int TextFrameTableOffset = 0x59;
inline constexpr int TextFrameDescriptorSize = 0x49;
inline constexpr int TextFrameTextSize = 0x40;
inline constexpr int TextFrameLeftOffset = 0x40;
inline constexpr int TextFrameTopOffset = 0x42;
inline constexpr int TextFrameRightOffset = 0x44;
inline constexpr int TextFrameBottomOffset = 0x46;
inline constexpr int TextFrameStyleOffset = 0x48;

inline constexpr int FieldTableOffset = 0x0d3d;
inline constexpr int FieldDescriptorSize = 0x25;
inline constexpr int FieldNameSize = 0x10;
inline constexpr int FieldFlagsOffset = 0x10;
inline constexpr int FieldControlRoleOffset = 0x12;
inline constexpr int FieldDialableMarkerOffset = 0x14;
inline constexpr int FieldRecordOffsetOffset = 0x16;
inline constexpr int FieldDataLengthOffset = 0x18;
inline constexpr int FieldLeftOffset = 0x1a;
inline constexpr int FieldTopOffset = 0x1c;
inline constexpr int FieldRightOffset = 0x1e;
inline constexpr int FieldBottomOffset = 0x20;
inline constexpr int FieldAlignmentOffset = 0x22;
inline constexpr int FieldDisplayWidthOffset = 0x23;

inline constexpr quint16 VariableTextFlag = 0x0001;
inline constexpr quint16 MultilineEditorFlag = 0x0002;
inline constexpr quint16 NotesFlags = VariableTextFlag | MultilineEditorFlag;
inline constexpr quint16 StandaloneControlRole = 1;
inline constexpr char AlignCenter = 'c';
inline constexpr char AlignRight = 'r';

struct ParsedLayout {
    QVector<FieldDefinition> fields;
    QVector<DeckSortKey> sortKeys;
    CardTemplateLayout layout;
};

inline quint16 readUnsigned16(const QByteArray& bytes, int offset)
{
    if (offset < 0 || offset + 1 >= bytes.size()) {
        return 0;
    }
    return static_cast<quint16>(static_cast<quint8>(bytes.at(offset)))
        | static_cast<quint16>(static_cast<quint8>(bytes.at(offset + 1)) << 8);
}

inline QString readLegacyText(const QByteArray& bytes, int offset, int maximumLength)
{
    if (offset < 0 || maximumLength <= 0 || offset >= bytes.size()) {
        return {};
    }
    const int available = std::min(maximumLength, static_cast<int>(bytes.size()) - offset);
    const int terminator = bytes.indexOf('\0', offset);
    const int length = terminator >= offset && terminator < offset + available
        ? terminator - offset
        : available;
    return LegacyOemCodec::decode(QByteArrayView(bytes).sliced(offset, length)).trimmed();
}

inline QRect scaledBounds(const QByteArray& bytes, int descriptorOffset, int leftOffset, int topOffset, int rightOffset, int bottomOffset)
{
    const int left = readUnsigned16(bytes, descriptorOffset + leftOffset);
    const int top = readUnsigned16(bytes, descriptorOffset + topOffset);
    const int right = readUnsigned16(bytes, descriptorOffset + rightOffset);
    const int bottom = readUnsigned16(bytes, descriptorOffset + bottomOffset);
    return QRect(
        left * CoordinateScale,
        top * CoordinateScale,
        std::max(0, right - left) * CoordinateScale,
        std::max(0, bottom - top) * CoordinateScale);
}

inline std::optional<ParsedLayout> parse(const QByteArray& record, int expectedFieldCount)
{
    const int fieldCount = readUnsigned16(record, FieldCountOffset);
    const int textFrameCount = readUnsigned16(record, TextFrameCountOffset);
    if (fieldCount <= 0 || fieldCount > MaximumFieldCount || fieldCount != expectedFieldCount
        || textFrameCount > MaximumTextFrameCount) {
        return std::nullopt;
    }
    const int requiredFieldBytes = FieldTableOffset + fieldCount * FieldDescriptorSize;
    const int requiredTextBytes = TextFrameTableOffset + textFrameCount * TextFrameDescriptorSize;
    if (record.size() < requiredFieldBytes || record.size() < requiredTextBytes) {
        return std::nullopt;
    }

    ParsedLayout parsed;
    parsed.fields.reserve(fieldCount);
    parsed.layout.canvasWidth = DefaultCanvasWidth;
    parsed.layout.canvasHeight = DefaultCanvasHeight;
    parsed.layout.frames.reserve(fieldCount + textFrameCount);
    parsed.sortKeys.reserve(SortKeyCount);

    for (int level = 0; level < SortKeyCount; ++level) {
        const int offset = SortKeyTableOffset + level * SortKeyDescriptorSize;
        const int oneBasedFieldIndex = readUnsigned16(record, offset);
        if (oneBasedFieldIndex <= 0 || oneBasedFieldIndex > fieldCount) {
            continue;
        }
        const quint16 flags = readUnsigned16(record, offset + SortKeyFlagsOffset);
        parsed.sortKeys.append({oneBasedFieldIndex - 1, (flags & SortDescendingFlag) != 0});
    }

    for (int index = 0; index < textFrameCount; ++index) {
        const int offset = TextFrameTableOffset + index * TextFrameDescriptorSize;
        CardTemplateFrame frame;
        frame.kind = CardTemplateFrameKind::Text;
        frame.text = readLegacyText(record, offset, TextFrameTextSize);
        frame.bounds = scaledBounds(record, offset, TextFrameLeftOffset, TextFrameTopOffset, TextFrameRightOffset, TextFrameBottomOffset);
        frame.styleFlags = static_cast<quint8>(record.at(offset + TextFrameStyleOffset));
        frame.legacyDescriptor = record.mid(offset, TextFrameDescriptorSize);
        parsed.layout.frames.append(std::move(frame));
    }

    for (int index = 0; index < fieldCount; ++index) {
        const int offset = FieldTableOffset + index * FieldDescriptorSize;
        const QString name = readLegacyText(record, offset, FieldNameSize);
        const quint16 flags = readUnsigned16(record, offset + FieldFlagsOffset);
        const quint16 controlRole = readUnsigned16(record, offset + FieldControlRoleOffset);
        const quint16 dialableMarker = readUnsigned16(record, offset + FieldDialableMarkerOffset);
        const int dataLength = readUnsigned16(record, offset + FieldDataLengthOffset);
        if (name.isEmpty() || dataLength <= 0) {
            return std::nullopt;
        }

        const FieldType fieldType = (flags & NotesFlags) == NotesFlags ? FieldType::Notes : FieldType::Text;
        parsed.fields.append(FieldDefinition(
            name,
            fieldType,
            dataLength,
            controlRole == StandaloneControlRole,
            dialableMarker != 0,
            record.mid(offset, FieldDescriptorSize),
            readUnsigned16(record, offset + FieldDisplayWidthOffset)));

        CardTemplateFrame frame;
        frame.kind = fieldType == FieldType::Notes ? CardTemplateFrameKind::NotesBox : CardTemplateFrameKind::DataBox;
        frame.fieldIndex = index;
        frame.text = name;
        frame.bounds = scaledBounds(record, offset, FieldLeftOffset, FieldTopOffset, FieldRightOffset, FieldBottomOffset);
        const char alignment = record.at(offset + FieldAlignmentOffset);
        frame.legacyDescriptor = record.mid(offset, FieldDescriptorSize);
        if (alignment == AlignCenter) {
            frame.styleFlags |= CardTemplateStyleFlagAlignCenter;
        } else if (alignment == AlignRight) {
            frame.styleFlags |= CardTemplateStyleFlagAlignRight;
        }
        parsed.layout.frames.append(std::move(frame));
    }
    return parsed;
}

inline std::optional<ParsedLayout> find(const QVector<QByteArray>& records, int expectedFieldCount)
{
    for (const QByteArray& record : records) {
        if (const auto parsed = parse(record, expectedFieldCount); parsed.has_value()) {
            return parsed;
        }
    }
    return std::nullopt;
}

} // namespace CardStack::LegacyTemplateLayoutFormat
