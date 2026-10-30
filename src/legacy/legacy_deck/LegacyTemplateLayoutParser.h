#pragma once

#include "CardTemplateLayout.h"
#include "Deck.h"
#include "LegacyTextCodec.h"
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

inline QString readLegacyText(const QByteArray& bytes, int offset, int maximumLength, LegacyTextEncoding encoding = LegacyTextEncoding::Windows1252, bool trim = true)
{
    if (offset < 0 || maximumLength <= 0 || offset >= bytes.size()) {
        return {};
    }
    const int available = std::min(maximumLength, static_cast<int>(bytes.size()) - offset);
    const int terminator = bytes.indexOf('\0', offset);
    const int length = terminator >= offset && terminator < offset + available
        ? terminator - offset
        : available;
    const QString text = LegacyTextCodec::decode(QByteArrayView(bytes).sliced(offset, length), encoding);
    return trim ? text.trimmed() : text;
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


namespace WinIndex {
inline constexpr int CanvasWidthOffset = 0x49;
inline constexpr int CanvasHeightOffset = 0x4b;
inline constexpr int SortTableOffset = 0x4d;
inline constexpr int FieldCountOffset = 0x59;
inline constexpr int TextCountOffset = 0x5c;
inline constexpr int TextTableOffset = 0x5e;
inline constexpr int TextDescriptorSize = 0x26;
inline constexpr int TextCapacity = 0x1e;
inline constexpr int FieldTableOffset = 0x3cc;
inline constexpr int FieldDescriptorSize = 0x39;
inline constexpr int FieldCapacity = 0x1e;
inline constexpr int FieldRoleOffset = 0x1e;
inline constexpr int DialableOffset = 0x20;
inline constexpr int DataLengthOffset = 0x24;
inline constexpr int FlagsOffset = 0x27;
inline constexpr int FieldLeftOffset = 0x31;
inline constexpr int FieldTopOffset = 0x33;
inline constexpr int FieldWidthOffset = 0x35;
inline constexpr int FieldHeightOffset = 0x37;
inline constexpr int DataFontHeightOffset = 0x2ce;
inline constexpr int MaximumFields = 20;
inline constexpr int MaximumTexts = 15;
inline constexpr int GridPixels = 3;
inline constexpr int HeaderPaddingPixels = 6;
inline constexpr int DefaultFontCellPixels = 16;

struct Edges { int left, top, right, bottom; };
inline int snap(int& coordinate)
{
    const int previous = coordinate;
    coordinate = ((coordinate + (coordinate < 0 ? -1 : 1)) / GridPixels) * GridPixels;
    return coordinate - previous;
}
inline void snapLayout(QVector<Edges>& fields, QVector<Edges>& texts, int& width, int& height)
{
    for (int i = 0; i < fields.size(); ++i) {
        auto& field = fields[i];
        snap(field.left); snap(field.right);
        const auto propagate = [&](int previous, int delta) {
            if (delta == 0) return;
            for (int next = i + 1; next < fields.size(); ++next) {
                if (fields[next].top >= previous) fields[next].top += delta;
                if (fields[next].bottom >= previous) fields[next].bottom += delta;
            }
            for (auto& text : texts) {
                if (text.top >= previous) text.top += delta;
                if (text.bottom >= previous) text.bottom += delta;
            }
            height += delta;
        };
        const int previousTop = field.top;
        const int topDelta = snap(field.top);
        field.bottom += topDelta;
        propagate(previousTop, topDelta);
        const int previousBottom = field.bottom;
        const int bottomDelta = snap(field.bottom);
        propagate(previousBottom, bottomDelta);
    }
    for (auto& text : texts) {
        snap(text.left); snap(text.right);
        text.bottom += snap(text.top);
        snap(text.bottom);
    }
    snap(width); snap(height);
}
inline QRect bounds(const Edges& edges)
{
    return QRect(edges.left * CoordinateScale, edges.top * CoordinateScale,
        std::max(0, edges.right - edges.left) * CoordinateScale,
        std::max(0, edges.bottom - edges.top) * CoordinateScale);
}
} // namespace WinIndex

inline std::optional<ParsedLayout> parseWinIndex(const QByteArray& record, int expectedFieldCount, LegacyTextEncoding encoding)
{
    using namespace WinIndex;
    if (record.size() < WinIndex::FieldTableOffset) return std::nullopt;
    const int count = quint8(record.at(WinIndex::FieldCountOffset));
    const int textCount = readUnsigned16(record, TextCountOffset);
    int width = qint16(readUnsigned16(record, CanvasWidthOffset));
    int height = qint16(readUnsigned16(record, CanvasHeightOffset));
    if (count <= 0 || count > MaximumFields || count != expectedFieldCount
        || textCount > MaximumTexts || width <= 0 || height <= 0
        || record.size() < WinIndex::FieldTableOffset + count * WinIndex::FieldDescriptorSize) return std::nullopt;
    int fontCell = qint16(readUnsigned16(record, DataFontHeightOffset));
    if (fontCell < 0) fontCell = fontCell * 12 / -10;
    if (fontCell == 0) fontCell = DefaultFontCellPixels;
    const int headerInset = fontCell + HeaderPaddingPixels;
    QVector<WinIndex::Edges> fields, texts;
    for (int i = 0; i < textCount; ++i) {
        const int at = TextTableOffset + i * TextDescriptorSize;
        const int left = qint16(readUnsigned16(record, at + TextCapacity));
        const int top = qint16(readUnsigned16(record, at + TextCapacity + 2)) + headerInset;
        const int right = left + qint16(readUnsigned16(record, at + TextCapacity + 4));
        const int bottom = top + qint16(readUnsigned16(record, at + TextCapacity + 6));
        if (right < left || bottom < top) return std::nullopt;
        texts.append({left, top, right, bottom});
    }
    for (int i = 0; i < count; ++i) {
        const int at = WinIndex::FieldTableOffset + i * WinIndex::FieldDescriptorSize;
        const int left = qint16(readUnsigned16(record, at + WinIndex::FieldLeftOffset));
        const int top = qint16(readUnsigned16(record, at + WinIndex::FieldTopOffset)) + headerInset;
        const int right = left + qint16(readUnsigned16(record, at + FieldWidthOffset));
        const int bottom = top + qint16(readUnsigned16(record, at + FieldHeightOffset));
        if (right < left || bottom < top) return std::nullopt;
        fields.append({left, top, right, bottom});
    }
    snapLayout(fields, texts, width, height);
    ParsedLayout parsed;
    parsed.layout.canvasWidth = width * CoordinateScale;
    parsed.layout.canvasHeight = height * CoordinateScale;
    for (int level = 0; level < SortKeyCount; ++level) {
        const int at = SortTableOffset + level * SortKeyDescriptorSize;
        const int field = readUnsigned16(record, at);
        if (field > 0 && field <= count)
            parsed.sortKeys.append({field - 1, (readUnsigned16(record, at + SortKeyFlagsOffset) & SortDescendingFlag) != 0});
    }
    for (int i = 0; i < textCount; ++i) {
        CardTemplateFrame frame;
        frame.kind = CardTemplateFrameKind::Text;
        frame.text = readLegacyText(record, TextTableOffset + i * TextDescriptorSize, TextCapacity, encoding, false);
        frame.bounds = WinIndex::bounds(texts.at(i));
        frame.legacyDescriptor = record.mid(TextTableOffset + i * TextDescriptorSize, TextDescriptorSize);
        parsed.layout.frames.append(std::move(frame));
    }
    for (int i = 0; i < count; ++i) {
        const int at = WinIndex::FieldTableOffset + i * WinIndex::FieldDescriptorSize;
        const QString name = readLegacyText(record, at, FieldCapacity, encoding);
        const quint16 flags = readUnsigned16(record, at + WinIndex::FlagsOffset);
        const int length = readUnsigned16(record, at + DataLengthOffset);
        if (name.isEmpty() || length <= 0) return std::nullopt;
        const bool notes = (flags & VariableTextFlag) != 0;
        const QByteArray descriptor = record.mid(at, WinIndex::FieldDescriptorSize);
        parsed.fields.append(FieldDefinition(name, notes ? FieldType::Notes : FieldType::Text,
            notes ? DefaultNotesMaximumLength : length,
            readUnsigned16(record, at + FieldRoleOffset) == StandaloneControlRole,
            readUnsigned16(record, at + DialableOffset) != 0, descriptor,
            readUnsigned16(record, at + FieldWidthOffset)));
        CardTemplateFrame frame;
        frame.kind = notes ? CardTemplateFrameKind::NotesBox : CardTemplateFrameKind::DataBox;
        frame.fieldIndex = i; frame.text = name;
        frame.bounds = WinIndex::bounds(fields.at(i));
        frame.legacyDescriptor = descriptor;
        parsed.layout.frames.append(std::move(frame));
    }
    return parsed;
}

inline std::optional<ParsedLayout> parse(const QByteArray& record, int expectedFieldCount, LegacyTextEncoding encoding = LegacyTextEncoding::Windows1252)
{
    if (record.mid(5, 6) == QByteArrayLiteral("WINDEX")) return parseWinIndex(record, expectedFieldCount, encoding);
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
        frame.text = readLegacyText(record, offset, TextFrameTextSize, encoding, false);
        frame.bounds = scaledBounds(record, offset, TextFrameLeftOffset, TextFrameTopOffset, TextFrameRightOffset, TextFrameBottomOffset);
        frame.styleFlags = static_cast<quint8>(record.at(offset + TextFrameStyleOffset));
        frame.legacyDescriptor = record.mid(offset, TextFrameDescriptorSize);
        parsed.layout.frames.append(std::move(frame));
    }

    for (int index = 0; index < fieldCount; ++index) {
        const int offset = FieldTableOffset + index * FieldDescriptorSize;
        const QString name = readLegacyText(record, offset, FieldNameSize, encoding);
        const quint16 flags = readUnsigned16(record, offset + FieldFlagsOffset);
        const quint16 controlRole = readUnsigned16(record, offset + FieldControlRoleOffset);
        const quint16 dialableMarker = readUnsigned16(record, offset + FieldDialableMarkerOffset);
        const int dataLength = readUnsigned16(record, offset + FieldDataLengthOffset);
        if (name.isEmpty() || dataLength <= 0) {
            return std::nullopt;
        }

        const FieldType fieldType = (flags & VariableTextFlag) != 0 ? FieldType::Notes : FieldType::Text;
        parsed.fields.append(FieldDefinition(
            name,
            fieldType,
            fieldType == FieldType::Notes ? DefaultNotesMaximumLength : dataLength,
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

inline std::optional<ParsedLayout> find(const QVector<QByteArray>& records, int expectedFieldCount, LegacyTextEncoding encoding = LegacyTextEncoding::Windows1252)
{
    for (const QByteArray& record : records) {
        if (const auto parsed = parse(record, expectedFieldCount, encoding); parsed.has_value()) {
            return parsed;
        }
    }
    return std::nullopt;
}

} // namespace CardStack::LegacyTemplateLayoutFormat
