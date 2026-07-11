#pragma once

#include <QRect>
#include <QString>
#include <QVector>

namespace CardStack {

enum class ReportFormType {
    Unknown = 0,
    Card = 1,
    Label = 2,
    Report = 4
};

enum class ReportFrameKind {
    Unknown,
    Text,
    Data,
    SystemText,
    LineOrBox
};

struct ReportFontDefinition {
    QString faceName;
    int legacyHeight = 0;
};

struct ReportFrameDefinition {
    int legacyOffset = 0;
    quint16 signature = 0;
    quint16 sourceId = 0;
    quint16 order = 0;
    quint16 band = 0;
    QRect bounds;
    QString text;
    QVector<QString> fieldPlaceholders;
    QVector<QString> systemTokens;
    ReportFrameKind kind = ReportFrameKind::Unknown;
    quint8 printEntireContentsFlag = 0;
    quint8 validationFlags = 0;
    quint8 styleFlags = 0;
    int lineBoxShape = 0;
    int lineStyle = 0;
    int fillPattern = 0;
    int cornerRadius = 0;
};

inline constexpr quint8 ReportStyleFlagBold = 0x01;
inline constexpr quint8 ReportStyleFlagItalic = 0x02;
inline constexpr quint8 ReportStyleFlagUnderline = 0x04;
inline constexpr quint8 ReportStyleFlagAlignCenter = 0x10;
inline constexpr quint8 ReportStyleFlagAlignRight = 0x20;

struct ReportDefinition {
    QString name;
    QString formatMagic;
    int legacyOffset = 0;
    int entrySize = 0;
    int headerSize = 0;
    int declaredFrameCount = 0;
    ReportFormType formType = ReportFormType::Unknown;
    int formWidth = 0;
    int formHeight = 0;
    int rows = 0;
    int columns = 0;
    int marginLeft = 0;
    int marginTop = 0;
    int marginRight = 0;
    int marginBottom = 0;
    int horizontalGutter = 0;
    int verticalGutter = 0;
    ReportFontDefinition dataFont;
    ReportFontDefinition textFont;
    QVector<ReportFrameDefinition> frames;
};

} // namespace CardStack
