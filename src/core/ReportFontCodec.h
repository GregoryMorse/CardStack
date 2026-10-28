#pragma once

#include "ReportDefinition.h"
#include <QJsonDocument>
#include <QJsonObject>

namespace CardStack {
inline constexpr quint8 StoredReportBold = 0x01;
inline constexpr quint8 StoredReportItalic = 0x02;
inline constexpr quint8 StoredReportUnderline = 0x04;

inline quint8 decodeReportStyleFlags(quint8 stored)
{
    return (stored & ~quint8(StoredReportBold | StoredReportItalic | StoredReportUnderline))
        | ((stored & StoredReportBold) != 0 ? ReportStyleFlagBold : 0)
        | ((stored & StoredReportItalic) != 0 ? ReportStyleFlagItalic : 0)
        | ((stored & StoredReportUnderline) != 0 ? ReportStyleFlagUnderline : 0);
}

inline ReportFontDefinition decodeReportFont(const QByteArray& bytes, int offset)
{
    ReportFontDefinition font;
    constexpr int DescriptorSize = 50;
    constexpr int FaceOffset = 18;
    if (offset < 0 || offset + DescriptorSize > bytes.size()) return font;
    const auto word = [&](int at) {
        return static_cast<qint16>(static_cast<quint8>(bytes[offset + at])
            | (static_cast<quint8>(bytes[offset + at + 1]) << 8));
    };
    font.legacyHeight = word(0);
    font.width = word(2);
    font.escapementTenths = word(4);
    font.orientationTenths = word(6);
    font.weight = word(8);
    font.italic = bytes[offset + 10] != 0;
    font.underline = bytes[offset + 11] != 0;
    font.strikeOut = bytes[offset + 12] != 0;
    font.characterSet = static_cast<quint8>(bytes[offset + 13]);
    font.outputPrecision = static_cast<quint8>(bytes[offset + 14]);
    font.clipPrecision = static_cast<quint8>(bytes[offset + 15]);
    font.quality = static_cast<quint8>(bytes[offset + 16]);
    font.pitchAndFamily = static_cast<quint8>(bytes[offset + 17]);
    QByteArray face = bytes.mid(offset + FaceOffset, 32);
    const int terminator = face.indexOf('\0');
    if (terminator >= 0) face.truncate(terminator);
    font.faceName = QString::fromLatin1(face);
    return font;
}

inline QString reportFontToJson(const ReportFontDefinition& font)
{
    const QJsonObject object{
        {QStringLiteral("version"), 1}, {QStringLiteral("face"), font.faceName},
        {QStringLiteral("height"), font.legacyHeight}, {QStringLiteral("width"), font.width},
        {QStringLiteral("escapement"), font.escapementTenths}, {QStringLiteral("orientation"), font.orientationTenths},
        {QStringLiteral("weight"), font.weight}, {QStringLiteral("italic"), font.italic},
        {QStringLiteral("underline"), font.underline}, {QStringLiteral("strikeOut"), font.strikeOut},
        {QStringLiteral("characterSet"), font.characterSet}, {QStringLiteral("outputPrecision"), font.outputPrecision},
        {QStringLiteral("clipPrecision"), font.clipPrecision}, {QStringLiteral("quality"), font.quality},
        {QStringLiteral("pitchAndFamily"), font.pitchAndFamily}};
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}
inline ReportFontDefinition reportFontFromJson(const QString& encoded)
{
    ReportFontDefinition font;
    const auto object = QJsonDocument::fromJson(encoded.toUtf8()).object();
    font.faceName = object.value(QStringLiteral("face")).toString();
    font.legacyHeight = object.value(QStringLiteral("height")).toInt();
    font.width = object.value(QStringLiteral("width")).toInt();
    font.escapementTenths = object.value(QStringLiteral("escapement")).toInt();
    font.orientationTenths = object.value(QStringLiteral("orientation")).toInt();
    font.weight = object.value(QStringLiteral("weight")).toInt(400);
    font.italic = object.value(QStringLiteral("italic")).toBool();
    font.underline = object.value(QStringLiteral("underline")).toBool();
    font.strikeOut = object.value(QStringLiteral("strikeOut")).toBool();
    font.characterSet = static_cast<quint8>(object.value(QStringLiteral("characterSet")).toInt());
    font.outputPrecision = static_cast<quint8>(object.value(QStringLiteral("outputPrecision")).toInt(3));
    font.clipPrecision = static_cast<quint8>(object.value(QStringLiteral("clipPrecision")).toInt(2));
    font.quality = static_cast<quint8>(object.value(QStringLiteral("quality")).toInt(1));
    font.pitchAndFamily = static_cast<quint8>(object.value(QStringLiteral("pitchAndFamily")).toInt(0x20 | 0x02));
    return font;
}
} // namespace CardStack
