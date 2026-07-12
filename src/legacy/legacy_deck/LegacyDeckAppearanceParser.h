#pragma once

#include "Deck.h"

#include <QColor>
#include <QFont>

#include <algorithm>

namespace CardStack::LegacyDeckAppearanceFormat {

inline constexpr int Win16LogFontSize = 0x32;
inline constexpr int FontHeightOffset = 0x00;
inline constexpr int FontWeightOffset = 0x08;
inline constexpr int FontItalicOffset = 0x0a;
inline constexpr int FontUnderlineOffset = 0x0b;
inline constexpr int FontStrikeOutOffset = 0x0c;
inline constexpr int FontPitchAndFamilyOffset = 0x11;
inline constexpr int FontFaceNameOffset = 0x12;
inline constexpr int FontFaceNameLength = 32;

inline constexpr int IndexFontOffset = 0x0bcb;
inline constexpr int NameFontOffset = 0x0bfd;
inline constexpr int DataFontOffset = 0x0c2f;
inline constexpr int TextFontOffset = 0x0c61;

inline constexpr int DataForegroundColorOffset = 0x0c93;
inline constexpr int IndexBackgroundColorOffset = 0x0c97;
inline constexpr int TextForegroundColorOffset = 0x0c9b;
inline constexpr int CardBackgroundColorOffset = 0x0c9f;
inline constexpr int IndexForegroundColorOffset = 0x0ca3;
inline constexpr int DataBackgroundColorOffset = 0x0ca7;
inline constexpr int NameForegroundColorOffset = 0x0cab;
inline constexpr int UseSystemColorsOffset = 0x0caf;
inline constexpr int MinimumAppearanceRecordSize = UseSystemColorsOffset + 2;

inline quint16 readU16(const QByteArray& bytes, int offset)
{
    if (offset < 0 || offset + 1 >= bytes.size()) {
        return 0;
    }
    return static_cast<quint16>(static_cast<quint8>(bytes.at(offset))
        | (static_cast<quint16>(static_cast<quint8>(bytes.at(offset + 1))) << 8));
}

inline quint32 readColorRef(const QByteArray& bytes, int offset)
{
    return static_cast<quint32>(readU16(bytes, offset))
        | (static_cast<quint32>(readU16(bytes, offset + 2)) << 16);
}

inline QString colorName(const QByteArray& bytes, int offset)
{
    const quint32 colorRef = readColorRef(bytes, offset);
    return QColor(
        static_cast<int>(colorRef & 0xff),
        static_cast<int>((colorRef >> 8) & 0xff),
        static_cast<int>((colorRef >> 16) & 0xff)).name(QColor::HexRgb);
}

inline QString fontString(const QByteArray& bytes, int offset)
{
    if (offset < 0 || offset + Win16LogFontSize > bytes.size()) {
        return {};
    }
    int faceLength = 0;
    while (faceLength < FontFaceNameLength
           && bytes.at(offset + FontFaceNameOffset + faceLength) != '\0') {
        ++faceLength;
    }
    const QString family = QString::fromLocal8Bit(
        bytes.constData() + offset + FontFaceNameOffset,
        faceLength).trimmed();
    if (family.isEmpty()) {
        return {};
    }

    QFont font(family);
    const qint16 legacyHeight = static_cast<qint16>(readU16(bytes, offset + FontHeightOffset));
    if (legacyHeight != 0) {
        font.setPixelSize(std::max(1, std::abs(static_cast<int>(legacyHeight))));
    }
    const int legacyWeight = readU16(bytes, offset + FontWeightOffset);
    if (legacyWeight > 0) {
        font.setWeight(static_cast<QFont::Weight>(std::clamp(legacyWeight, 100, 900)));
    }
    font.setItalic(bytes.at(offset + FontItalicOffset) != 0);
    font.setUnderline(bytes.at(offset + FontUnderlineOffset) != 0);
    font.setStrikeOut(bytes.at(offset + FontStrikeOutOffset) != 0);
    font.setFixedPitch((static_cast<quint8>(bytes.at(offset + FontPitchAndFamilyOffset)) & 0x03) == 1);
    return font.toString();
}

inline DeckAppearance parse(const QByteArray& record)
{
    DeckAppearance appearance;
    if (record.size() < MinimumAppearanceRecordSize) {
        return appearance;
    }

    appearance.indexFont = fontString(record, IndexFontOffset);
    appearance.nameFont = fontString(record, NameFontOffset);
    appearance.dataFont = fontString(record, DataFontOffset);
    appearance.textFont = fontString(record, TextFontOffset);
    appearance.customColors[static_cast<int>(DeckColorRole::IndexForeground)] = colorName(record, IndexForegroundColorOffset);
    appearance.customColors[static_cast<int>(DeckColorRole::DataForeground)] = colorName(record, DataForegroundColorOffset);
    appearance.customColors[static_cast<int>(DeckColorRole::NameForeground)] = colorName(record, NameForegroundColorOffset);
    appearance.customColors[static_cast<int>(DeckColorRole::TextForeground)] = colorName(record, TextForegroundColorOffset);
    appearance.customColors[static_cast<int>(DeckColorRole::DataBackground)] = colorName(record, DataBackgroundColorOffset);
    appearance.customColors[static_cast<int>(DeckColorRole::IndexBackground)] = colorName(record, IndexBackgroundColorOffset);
    appearance.customColors[static_cast<int>(DeckColorRole::CardBackground)] = colorName(record, CardBackgroundColorOffset);
    appearance.useSystemColors = readU16(record, UseSystemColorsOffset) != 0;
    return appearance;
}

} // namespace CardStack::LegacyDeckAppearanceFormat
