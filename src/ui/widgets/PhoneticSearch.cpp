#include "PhoneticSearch.h"

#include <QByteArray>

namespace CardStack {
namespace {

bool isAsciiLetter(char value)
{
    return value >= 'A' && value <= 'Z';
}

bool isVowel(char value)
{
    return value == 'A' || value == 'E' || value == 'I' || value == 'O' || value == 'U';
}

bool isFrontVowel(char value)
{
    return value == 'E' || value == 'I' || value == 'Y';
}

bool isHSkipPrefix(char value)
{
    return value == 'C' || value == 'G' || value == 'P' || value == 'S' || value == 'T';
}

char charAt(const QByteArray& text, int index)
{
    if (index < 0 || index >= text.size()) {
        return '\0';
    }
    return text.at(index);
}

void setCharAt(QByteArray& text, int index, char value)
{
    if (index >= 0 && index < text.size()) {
        text[index] = value;
    }
}

void appendByte(QByteArray& output, char value)
{
    if (value != '\0') {
        output.append(value);
    }
}

} // namespace

QString soundsLikeKey(const QString& text)
{
    QByteArray source = text.toUpper().toLatin1();
    source.append('\0');

    QByteArray output;
    int sourceIndex = 0;
    int phoneticLength = 1;

    while (charAt(source, sourceIndex) != '\0') {
        while (charAt(source, sourceIndex) != '\0' && !isAsciiLetter(charAt(source, sourceIndex))) {
            appendByte(output, charAt(source, sourceIndex));
            ++sourceIndex;
        }
        if (charAt(source, sourceIndex) == '\0') {
            break;
        }

        char current = charAt(source, sourceIndex);
        if ((current == 'W' && charAt(source, sourceIndex + 1) == 'R') ||
            (current == 'A' && charAt(source, sourceIndex + 1) == 'E') ||
            ((current == 'G' || current == 'K' || current == 'P') && charAt(source, sourceIndex + 1) == 'N')) {
            ++sourceIndex;
            current = charAt(source, sourceIndex);
        }

        if (current == 'W' && charAt(source, sourceIndex + 1) == 'H') {
            ++sourceIndex;
            setCharAt(source, sourceIndex, 'W');
            current = 'W';
        } else if (current == 'X') {
            setCharAt(source, sourceIndex, 'S');
            current = 'S';
        }

        appendByte(output, current);
        ++sourceIndex;

        int sourceLetterPosition = 1;
        while (charAt(source, sourceIndex) != '\0') {
            current = charAt(source, sourceIndex);
            if (!isAsciiLetter(current)) {
                appendByte(output, current);
                ++sourceIndex;
                break;
            }

            bool skipCurrent = false;
            switch (current) {
            case 'B':
                skipCurrent = !isAsciiLetter(charAt(source, sourceIndex + 1)) && charAt(source, sourceIndex - 1) == 'M';
                break;
            case 'C':
                if (sourceLetterPosition > 1 &&
                    ((charAt(source, sourceIndex + 1) == 'I' && charAt(source, sourceIndex + 2) == 'A') ||
                     (charAt(source, sourceIndex + 1) == 'H' && charAt(source, sourceIndex - 1) != 'S'))) {
                    setCharAt(source, sourceIndex, 'X');
                } else if ((sourceLetterPosition > 1 && isFrontVowel(charAt(source, sourceIndex + 1))) ||
                           (sourceLetterPosition > 2 && charAt(source, sourceIndex - 1) == 'S' &&
                            isFrontVowel(charAt(source, sourceIndex + 1)))) {
                    skipCurrent = true;
                } else {
                    setCharAt(source, sourceIndex, 'K');
                }
                break;
            case 'D':
                setCharAt(
                    source,
                    sourceIndex,
                    (charAt(source, sourceIndex + 1) == 'G' && isFrontVowel(charAt(source, sourceIndex + 2))) ? 'J' : 'T');
                break;
            case 'F':
                skipCurrent = !isAsciiLetter(charAt(source, sourceIndex + 1)) && charAt(source, sourceIndex - 1) == 'P';
                break;
            case 'G':
                if ((charAt(source, sourceIndex + 1) == 'N' &&
                     (!isAsciiLetter(charAt(source, sourceIndex + 2)) ||
                      (charAt(source, sourceIndex + 3) == 'E' && charAt(source, sourceIndex + 4) == 'D'))) ||
                    (charAt(source, sourceIndex + 1) == 'H' && !isVowel(charAt(source, sourceIndex + 2))) ||
                    (charAt(source, sourceIndex - 1) == 'J' && isFrontVowel(charAt(source, sourceIndex + 1)))) {
                    skipCurrent = true;
                } else {
                    setCharAt(source, sourceIndex, (charAt(source, sourceIndex - 1) != 'G' &&
                                                        isFrontVowel(charAt(source, sourceIndex + 1)))
                            ? 'J'
                            : 'K');
                }
                break;
            case 'H': {
                const char previousSource = charAt(source, sourceIndex - 1);
                const char previousOutput = output.isEmpty() ? '\0' : output.back();
                if (isHSkipPrefix(previousSource) || previousSource == 'F' || previousOutput == '0' ||
                    previousOutput == 'X' || previousOutput == 'K') {
                    skipCurrent = true;
                } else if (isVowel(previousSource)) {
                    skipCurrent = !isVowel(charAt(source, sourceIndex + 1));
                }
                break;
            }
            case 'J':
            case 'L':
            case 'M':
            case 'N':
            case 'R':
                break;
            case 'K':
                skipCurrent = charAt(source, sourceIndex - 1) == 'C';
                break;
            case 'P':
                if (charAt(source, sourceIndex + 1) == 'F' && charAt(source, sourceIndex + 2) != '\0') {
                    skipCurrent = true;
                } else if (charAt(source, sourceIndex + 1) == 'H') {
                    setCharAt(source, sourceIndex, 'F');
                }
                break;
            case 'Q':
                setCharAt(source, sourceIndex, 'K');
                break;
            case 'S':
                if (charAt(source, sourceIndex + 1) == 'C' && charAt(source, sourceIndex + 2) == 'H') {
                    if (!isVowel(charAt(source, sourceIndex + 3))) {
                        appendByte(output, 'X');
                        sourceIndex += 3;
                        ++phoneticLength;
                        ++sourceLetterPosition;
                        continue;
                    }
                    appendByte(output, 'S');
                    appendByte(output, 'K');
                    phoneticLength += 2;
                    sourceIndex += 3;
                    ++sourceLetterPosition;
                    continue;
                }
                if (charAt(source, sourceIndex + 1) == 'H' ||
                    (charAt(source, sourceIndex + 1) == 'I' &&
                     (charAt(source, sourceIndex + 2) == 'O' || charAt(source, sourceIndex + 2) == 'A'))) {
                    setCharAt(source, sourceIndex, 'X');
                }
                break;
            case 'T':
                if (charAt(source, sourceIndex + 1) == 'I' &&
                    (charAt(source, sourceIndex + 2) == 'A' || charAt(source, sourceIndex + 2) == 'O')) {
                    setCharAt(source, sourceIndex, 'X');
                } else if (charAt(source, sourceIndex + 1) == 'H') {
                    setCharAt(source, sourceIndex, '0');
                } else if (charAt(source, sourceIndex + 1) == 'C' && charAt(source, sourceIndex + 2) == 'H') {
                    skipCurrent = true;
                }
                break;
            case 'V':
                setCharAt(source, sourceIndex, 'F');
                break;
            case 'W':
            case 'Y':
                skipCurrent = !isVowel(charAt(source, sourceIndex + 1));
                break;
            case 'X':
                if (output.size() >= 2 && output.at(output.size() - 2) == 'K' && output.back() == 'S') {
                    skipCurrent = true;
                } else {
                    appendByte(output, 'K');
                    ++phoneticLength;
                    skipCurrent = phoneticLength > 3;
                    setCharAt(source, sourceIndex, 'S');
                }
                break;
            case 'Z':
                setCharAt(source, sourceIndex, 'S');
                break;
            default:
                skipCurrent = true;
                break;
            }

            current = charAt(source, sourceIndex);
            if (skipCurrent || (!output.isEmpty() && output.back() == current)) {
                ++sourceIndex;
            } else {
                appendByte(output, current);
                ++sourceIndex;
                ++phoneticLength;
            }
            ++sourceLetterPosition;
        }
    }

    return QString::fromLatin1(output);
}

} // namespace CardStack
