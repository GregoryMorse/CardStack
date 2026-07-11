#include "LegacyReportReader.h"

#include <QFile>

#include <algorithm>

namespace CardStack {
namespace {

constexpr int CurrentHeaderSize = 0x3f5;
constexpr int OldHeaderSize = 0x3c9;
constexpr int FrameRecordSize = 0x9b;

constexpr int EntrySizeOffset = 0x08;
constexpr int ReportNameOffset = 0x0a;
constexpr int ReportNameLength = 64;
constexpr int FormWidthOffset = 0x4b;
constexpr int FormHeightOffset = 0x4d;
constexpr int FrameCountOffset = 0x53;
constexpr int RowsOffset = 0x55;
constexpr int ColumnsOffset = 0x57;
constexpr int FormTypeOffset = 0x59;
constexpr int DataFontOffset = 0x6c;
constexpr int TextFontOffset = 0x9e;

constexpr int FrameTextOffset = 0x10;
constexpr int FrameTextLength = 0x7a;
constexpr int FrameLineStyleOffset = 0x02;
constexpr int FrameFillPatternOffset = 0x04;
constexpr int FramePrintEntireContentsOffset = 0x8a;
constexpr int FrameValidationFlagsOffset = 0x8c;
constexpr int FrameLineBoxShapeOffset = 0x8e;
constexpr int FrameStyleFlagsOffset = 0x97;
constexpr int FrameCornerRadiusOffset = 0x99;

quint16 readU16(const QByteArray& bytes, int offset)
{
    if (offset < 0 || offset + 1 >= bytes.size()) {
        return 0;
    }

    return static_cast<quint16>(
        static_cast<quint8>(bytes.at(offset)) |
        (static_cast<quint8>(bytes.at(offset + 1)) << 8));
}

QString readNullTerminatedAscii(const QByteArray& bytes, int offset, int maxLength)
{
    if (offset < 0 || offset >= bytes.size() || maxLength <= 0) {
        return {};
    }

    const int available = std::min(maxLength, static_cast<int>(bytes.size()) - offset);
    int length = 0;
    while (length < available && bytes.at(offset + length) != '\0') {
        ++length;
    }

    return QString::fromLatin1(bytes.constData() + offset, length).trimmed();
}

bool remainingBytesAreZero(const QByteArray& bytes, int offset)
{
    for (int index = offset; index < bytes.size(); ++index) {
        if (bytes.at(index) != '\0') {
            return false;
        }
    }
    return true;
}

bool hasMagicAt(const QByteArray& bytes, int offset, const QByteArray& magic)
{
    if (offset < 0 || offset + magic.size() > bytes.size()) {
        return false;
    }
    return bytes.mid(offset, magic.size()) == magic;
}

ReportFormType formTypeFromLegacyValue(int value)
{
    switch (value) {
    case 1:
        return ReportFormType::Card;
    case 2:
        return ReportFormType::Label;
    case 4:
        return ReportFormType::Report;
    default:
        return ReportFormType::Unknown;
    }
}

ReportFontDefinition readFontDefinition(const QByteArray& bytes, int offset)
{
    ReportFontDefinition font;
    if (offset < 0 || offset >= bytes.size()) {
        return font;
    }

    font.legacyHeight = static_cast<qint8>(bytes.at(offset));
    font.faceName = readNullTerminatedAscii(bytes, offset + 1, 31);
    return font;
}

QVector<QString> extractDelimitedTokens(const QString& text, QChar open, QChar close)
{
    QVector<QString> tokens;
    int searchFrom = 0;
    while (searchFrom < text.size()) {
        const int begin = text.indexOf(open, searchFrom);
        if (begin < 0) {
            break;
        }
        const int end = text.indexOf(close, begin + 1);
        if (end < 0) {
            break;
        }

        const QString token = text.mid(begin + 1, end - begin - 1).trimmed();
        if (!token.isEmpty()) {
            tokens.append(token);
        }
        searchFrom = end + 1;
    }
    return tokens;
}

ReportFrameKind classifyFrame(const QString& text, const QVector<QString>& fieldPlaceholders, const QVector<QString>& systemTokens)
{
    if (text.isEmpty()) {
        return ReportFrameKind::LineOrBox;
    }
    if (!fieldPlaceholders.isEmpty()) {
        return ReportFrameKind::Data;
    }
    if (!systemTokens.isEmpty()) {
        return ReportFrameKind::SystemText;
    }
    return ReportFrameKind::Text;
}

ReportFrameDefinition readFrame(const QByteArray& bytes, int offset)
{
    ReportFrameDefinition frame;
    frame.legacyOffset = offset;
    frame.signature = readU16(bytes, offset);
    frame.sourceId = readU16(bytes, offset + 0x02);
    frame.order = readU16(bytes, offset + 0x04);
    frame.band = readU16(bytes, offset + 0x06);

    const int left = readU16(bytes, offset + 0x08);
    const int top = readU16(bytes, offset + 0x0a);
    const int right = readU16(bytes, offset + 0x0c);
    const int bottom = readU16(bytes, offset + 0x0e);
    frame.bounds = QRect(left, top, std::max(0, right - left), std::max(0, bottom - top));

    frame.text = readNullTerminatedAscii(bytes, offset + FrameTextOffset, FrameTextLength);
    frame.fieldPlaceholders = extractDelimitedTokens(frame.text, '[', ']');
    frame.systemTokens = extractDelimitedTokens(frame.text, '{', '}');
    frame.kind = classifyFrame(frame.text, frame.fieldPlaceholders, frame.systemTokens);
    frame.printEntireContentsFlag = static_cast<quint8>(bytes.at(offset + FramePrintEntireContentsOffset));
    frame.validationFlags = static_cast<quint8>(bytes.at(offset + FrameValidationFlagsOffset));
    frame.styleFlags = static_cast<quint8>(bytes.at(offset + FrameStyleFlagsOffset));
    if (frame.kind == ReportFrameKind::LineOrBox) {
        frame.lineBoxShape = static_cast<quint8>(bytes.at(offset + FrameLineBoxShapeOffset));
        frame.lineStyle = readU16(bytes, offset + FrameLineStyleOffset);
        frame.fillPattern = readU16(bytes, offset + FrameFillPatternOffset);
        frame.cornerRadius = readU16(bytes, offset + FrameCornerRadiusOffset);
    }
    return frame;
}

} // namespace

bool LegacyReportReader::Result::ok() const
{
    return errorMessage.isEmpty();
}

LegacyReportReader::Result LegacyReportReader::readFile(const QString& filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {{}, QStringLiteral("Could not open Legacy report store: %1").arg(filePath)};
    }
    return readBytes(file.readAll());
}

LegacyReportReader::Result LegacyReportReader::readBytes(const QByteArray& bytes) const
{
    Result result;
    int offset = 0;
    while (offset < bytes.size()) {
        if (remainingBytesAreZero(bytes, offset)) {
            break;
        }

        const bool currentFormat = hasMagicAt(bytes, offset, QByteArrayLiteral("RPT@#$B"));
        const bool oldFormat = hasMagicAt(bytes, offset, QByteArrayLiteral("RPT@#$A"));
        if (!currentFormat && !oldFormat) {
            result.errorMessage = QStringLiteral("Invalid Legacy report magic at offset 0x%1.")
                                      .arg(offset, 0, 16);
            return result;
        }

        const int headerSize = currentFormat ? CurrentHeaderSize : OldHeaderSize;
        if (offset + headerSize > bytes.size()) {
            result.errorMessage = QStringLiteral("Truncated LegacyDeck report header at offset 0x%1.")
                                      .arg(offset, 0, 16);
            return result;
        }

        const int entrySize = readU16(bytes, offset + EntrySizeOffset);
        const int frameCount = readU16(bytes, offset + FrameCountOffset);
        const int requiredSize = headerSize + frameCount * FrameRecordSize;
        if (entrySize < requiredSize || offset + entrySize > bytes.size()) {
            result.errorMessage = QStringLiteral("Invalid Legacy report entry size at offset 0x%1.")
                                      .arg(offset, 0, 16);
            return result;
        }

        ReportDefinition report;
        report.legacyOffset = offset;
        report.formatMagic = QString::fromLatin1(bytes.constData() + offset, 7);
        report.entrySize = entrySize;
        report.headerSize = headerSize;
        report.declaredFrameCount = frameCount;
        report.name = readNullTerminatedAscii(bytes, offset + ReportNameOffset, ReportNameLength);
        report.formWidth = readU16(bytes, offset + FormWidthOffset);
        report.formHeight = readU16(bytes, offset + FormHeightOffset);
        report.rows = readU16(bytes, offset + RowsOffset);
        report.columns = readU16(bytes, offset + ColumnsOffset);
        report.formType = formTypeFromLegacyValue(readU16(bytes, offset + FormTypeOffset));
        report.dataFont = readFontDefinition(bytes, offset + DataFontOffset);
        report.textFont = readFontDefinition(bytes, offset + TextFontOffset);

        const int frameBase = offset + headerSize;
        report.frames.reserve(frameCount);
        for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
            report.frames.append(readFrame(bytes, frameBase + frameIndex * FrameRecordSize));
        }

        result.reports.append(report);
        offset += entrySize;
    }

    if (result.reports.isEmpty()) {
        result.errorMessage = QStringLiteral("Legacy report store does not contain any report designs.");
    }
    return result;
}

} // namespace CardStack
