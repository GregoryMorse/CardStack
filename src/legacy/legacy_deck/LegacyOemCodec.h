#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <algorithm>
#include <array>

namespace CardStack::LegacyOemCodec {

// ButtonFile calls Win16 OEMTOANSI/ANSITOOEM at its file boundaries. Its US
// fixtures therefore use DOS code page 437 rather than ISO-8859-1.
inline constexpr std::array<char16_t, 128> Cp437HighCharacters = {
    0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7,
    0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5,
    0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9,
    0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192,
    0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba,
    0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510,
    0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f,
    0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b,
    0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580,
    0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4,
    0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229,
    0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248,
    0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0,
};

inline QString decode(QByteArrayView bytes)
{
    QString result;
    result.reserve(bytes.size());
    for (const char rawCharacter : bytes) {
        const auto character = static_cast<unsigned char>(rawCharacter);
        if (character == 0) {
            break;
        }
        result.append(character < 0x80
                ? QChar(character)
                : QChar(Cp437HighCharacters.at(character - 0x80)));
    }
    return result;
}

inline QByteArray encode(const QString& text, qsizetype maximumBytes = -1)
{
    QByteArray result;
    const qsizetype limit = maximumBytes < 0 ? text.size() : maximumBytes;
    result.reserve(std::min(text.size(), limit));
    for (const QChar character : text) {
        if (result.size() >= limit) {
            break;
        }
        const char16_t unicode = character.unicode();
        if (unicode < 0x80) {
            result.append(static_cast<char>(unicode));
            continue;
        }
        const auto found = std::find(Cp437HighCharacters.begin(), Cp437HighCharacters.end(), unicode);
        result.append(found == Cp437HighCharacters.end()
                ? '?'
                : static_cast<char>(0x80 + std::distance(Cp437HighCharacters.begin(), found)));
    }
    return result;
}

} // namespace CardStack::LegacyOemCodec
