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

inline constexpr int ReportLineShapeNone = 0;
inline constexpr int ReportLineShapeBox = 'b';
inline constexpr int ReportLineShapeHorizontal = 'h';
inline constexpr int ReportLineShapeVertical = 'v';
inline constexpr int ReportLineShapeLegacyAuto = 'l';

inline constexpr int ReportLineStyleSolid = 0;
inline constexpr int ReportLineStyleDash = 1;
inline constexpr int ReportLineStyleDot = 2;
inline constexpr int ReportLineStyleDashDot = 3;
inline constexpr int ReportLineStyleDashDotDot = 4;
inline constexpr int ReportLineStyleThickSolid = 5;
inline constexpr int ReportLineStyleThickDash = 6;
inline constexpr int ReportLineStyleThickDot = 7;
inline constexpr int ReportLineStyleHairline = 8;
inline constexpr int ReportLineStyleNoOutline = 9;
inline constexpr int ReportLineStyleCount = 10;

inline constexpr int ReportFillPatternClear = 0;
inline constexpr int ReportFillPatternSolid = 1;
inline constexpr int ReportFillPattern5Percent = 2;
inline constexpr int ReportFillPattern10Percent = 3;
inline constexpr int ReportFillPattern20Percent = 4;
inline constexpr int ReportFillPattern25Percent = 5;
inline constexpr int ReportFillPattern30Percent = 6;
inline constexpr int ReportFillPattern40Percent = 7;
inline constexpr int ReportFillPattern50Percent = 8;
inline constexpr int ReportFillPattern60Percent = 9;
inline constexpr int ReportFillPattern70Percent = 10;
inline constexpr int ReportFillPattern75Percent = 11;
inline constexpr int ReportFillPattern80Percent = 12;
inline constexpr int ReportFillPattern90Percent = 13;
inline constexpr int ReportFillPatternDarkHorizontal = 14;
inline constexpr int ReportFillPatternDarkVertical = 15;
inline constexpr int ReportFillPatternDarkDownDiagonal = 16;
inline constexpr int ReportFillPatternDarkUpDiagonal = 17;
inline constexpr int ReportFillPatternDarkGrid = 18;
inline constexpr int ReportFillPatternDarkTrellis = 19;
inline constexpr int ReportFillPatternLightHorizontal = 20;
inline constexpr int ReportFillPatternLightVertical = 21;
inline constexpr int ReportFillPatternLightDownDiagonal = 22;
inline constexpr int ReportFillPatternLightUpDiagonal = 23;
inline constexpr int ReportFillPatternLightGrid = 24;
inline constexpr int ReportFillPatternLightTrellis = 25;
inline constexpr int ReportFillPatternCount = 26;

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
