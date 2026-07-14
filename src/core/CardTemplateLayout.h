#pragma once

#include <QByteArray>
#include <QRect>
#include <QString>
#include <QVector>
#include <QMetaType>

namespace CardStack {

enum class CardTemplateFrameKind {
    Text,
    DataBox,
    NotesBox,
    LineOrBox
};

enum class CardTemplateLineBoxShape {
    Box,
    HorizontalLine,
    VerticalLine
};

inline constexpr quint8 CardTemplateStyleFlagUnderline = 0x01;
inline constexpr quint8 CardTemplateStyleFlagBold = 0x02;
inline constexpr quint8 CardTemplateStyleFlagItalic = 0x04;
inline constexpr quint8 CardTemplateStyleFlagAlignCenter = 0x10;
inline constexpr quint8 CardTemplateStyleFlagAlignRight = 0x20;

struct CardTemplateFrame {
    CardTemplateFrameKind kind = CardTemplateFrameKind::DataBox;
    QRect bounds;
    QString text;
    int fieldIndex = -1;
    quint8 styleFlags = 0;
    CardTemplateLineBoxShape lineBoxShape = CardTemplateLineBoxShape::Box;
    int lineStyle = 0;
    int fillPattern = 0;
    int cornerRadius = 0;
    QByteArray legacyDescriptor;

    bool operator==(const CardTemplateFrame&) const = default;
};

struct CardTemplateLayout {
    int canvasWidth = 6400;
    int canvasHeight = 4800;
    QVector<CardTemplateFrame> frames;

    bool operator==(const CardTemplateLayout&) const = default;
};

} // namespace CardStack

Q_DECLARE_METATYPE(CardStack::CardTemplateLayout)
