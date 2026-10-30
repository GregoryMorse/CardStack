#pragma once

#include "LegacyOemCodec.h"

#include <QByteArrayView>
#include <QString>

namespace CardStack {

enum class LegacyTextEncoding { Windows1252, Dos437 };

namespace LegacyTextCodec {

inline QString decode(QByteArrayView bytes, LegacyTextEncoding encoding = LegacyTextEncoding::Windows1252)
{
    if (encoding == LegacyTextEncoding::Dos437) return LegacyOemCodec::decode(bytes);
    static constexpr ushort ExtendedControls[] = {
        0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
        0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
        0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
        0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178
    };
    QString text;
    text.reserve(bytes.size());
    for (char raw : bytes) {
        const auto value = static_cast<quint8>(raw);
        text.append(QChar(value >= 0x80 && value <= 0x9f ? ExtendedControls[value - 0x80] : ushort(value)));
    }
    return text;
}

} // namespace LegacyTextCodec
} // namespace CardStack
