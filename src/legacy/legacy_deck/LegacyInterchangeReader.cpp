#include "LegacyInterchangeReader.h"

#include "LegacyDeckReader.h"

#include "CardRecord.h"
#include "FieldDefinition.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QtEndian>

#include <algorithm>

namespace CardStack {
namespace {

constexpr int DBaseHeaderSize = 32;
constexpr int DBaseFieldDescriptorSize = 32;
constexpr int DBaseFieldNameSize = 11;
constexpr int DBaseFieldLengthOffset = 16;
constexpr int DBaseRecordDeletedFlagSize = 1;
constexpr int DBaseMemoBlockSize = 512;
constexpr char DBaseFieldDescriptorTerminator = '\x0d';

constexpr int CardfileCountOffset = 3;
constexpr int CardfileIndexOffset = 11;
constexpr int CardfileIndexEntrySize = 52;
constexpr int CardfileTitleOffsetInEntry = 5;
constexpr int CardfileTitleLength = 40;
constexpr int CardfileTextLengthOffset = 2;
constexpr int CardfileTextOffset = 4;

constexpr char WordPerfectFieldSeparator = '\xde';

struct TakeNoteField {
    QString name;
    FieldType type = FieldType::Text;
    int length = 0;
};

struct DBaseField {
    QString name;
    FieldType type = FieldType::Text;
    int length = 0;
};

quint16 readU16(const QByteArray& bytes, int offset)
{
    if (offset < 0 || offset + 1 >= bytes.size()) {
        return 0;
    }
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

quint32 readU32(const QByteArray& bytes, int offset)
{
    if (offset < 0 || offset + 3 >= bytes.size()) {
        return 0;
    }
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

QChar decodeWindows1252Byte(quint8 byte)
{
    static constexpr ushort cp1252Controls[32] = {
        0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
        0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
        0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
        0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178,
    };

    if (byte >= 0x80 && byte <= 0x9f) {
        return QChar(cp1252Controls[byte - 0x80]);
    }
    return QChar(static_cast<ushort>(byte));
}

QString decodeWindows1252(const QByteArray& bytes, bool stopAtNul = false)
{
    QString value;
    value.reserve(bytes.size());
    for (const char ch : bytes) {
        const auto byte = static_cast<quint8>(ch);
        if (stopAtNul && byte == 0) {
            break;
        }
        value.append(decodeWindows1252Byte(byte));
    }
    return value;
}

QString decodeFixedString(const QByteArray& bytes, int offset, int length)
{
    if (offset < 0 || length <= 0 || offset >= bytes.size()) {
        return {};
    }
    const int available = std::min(length, static_cast<int>(bytes.size()) - offset);
    return decodeWindows1252(bytes.mid(offset, available), true).trimmed();
}

bool readAllBytes(const QString& filePath, QByteArray* bytes, QString* errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not open legacy interchange file: %1").arg(filePath);
        }
        return false;
    }

    *bytes = file.readAll();
    return true;
}

QString memoPathForDBasePath(const QString& filePath)
{
    QFileInfo info(filePath);
    const QString upperCandidate = info.path() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(".DBT");
    if (QFileInfo::exists(upperCandidate)) {
        return upperCandidate;
    }

    const QString lowerCandidate = info.path() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(".dbt");
    return QFileInfo::exists(lowerCandidate) ? lowerCandidate : QString();
}

QString readDBaseMemoValue(const QString& dbfPath, const QString& blockNumberText)
{
    bool ok = false;
    const int blockNumber = blockNumberText.trimmed().toInt(&ok);
    if (!ok || blockNumber <= 0) {
        return {};
    }

    const QString memoPath = memoPathForDBasePath(dbfPath);
    if (memoPath.isEmpty()) {
        return blockNumberText.trimmed();
    }

    QByteArray memoBytes;
    QString ignoredError;
    if (!readAllBytes(memoPath, &memoBytes, &ignoredError)) {
        return blockNumberText.trimmed();
    }

    const int offset = blockNumber * DBaseMemoBlockSize;
    if (offset < 0 || offset >= memoBytes.size()) {
        return {};
    }

    int end = offset;
    while (end < memoBytes.size()) {
        const char ch = memoBytes.at(end);
        if (ch == '\0' || ch == '\x1a') {
            break;
        }
        ++end;
    }
    return decodeWindows1252(memoBytes.mid(offset, end - offset), false).trimmed();
}

QVector<DBaseField> readDBaseFields(const QByteArray& bytes, int headerLength)
{
    QVector<DBaseField> fields;
    for (int offset = DBaseHeaderSize; offset + DBaseFieldDescriptorSize <= headerLength; offset += DBaseFieldDescriptorSize) {
        if (offset >= bytes.size() || bytes.at(offset) == DBaseFieldDescriptorTerminator) {
            break;
        }

        const QString name = decodeFixedString(bytes, offset, DBaseFieldNameSize);
        const char type = offset + 11 < bytes.size() ? bytes.at(offset + 11) : '\0';
        const int length = offset + DBaseFieldLengthOffset < bytes.size()
            ? static_cast<quint8>(bytes.at(offset + DBaseFieldLengthOffset))
            : 0;
        if (name.isEmpty() || length <= 0) {
            continue;
        }

        fields.append({
            name,
            type == 'M' ? FieldType::Notes : FieldType::Text,
            type == 'M' ? 8192 : length,
        });
    }
    return fields;
}

QString deckNameForPath(const QString& filePath, const QString& suffix)
{
    const QString baseName = QFileInfo(filePath).completeBaseName();
    return baseName.isEmpty() ? suffix : baseName;
}

QVector<QByteArray> splitWordPerfectSegments(const QByteArray& payload)
{
    QVector<QByteArray> segments;
    int segmentStart = 0;
    for (int index = 0; index < payload.size(); ++index) {
        if (payload.at(index) == WordPerfectFieldSeparator) {
            segments.append(payload.mid(segmentStart, index - segmentStart));
            segmentStart = index + 1;
        }
    }
    if (segmentStart <= payload.size()) {
        segments.append(payload.mid(segmentStart));
    }
    return segments;
}

bool isWordPerfectDescriptor(const QByteArray& segment, char descriptorType = '\0')
{
    if (segment.size() < 3 || (descriptorType != '\0' && segment.at(0) != descriptorType)) {
        return false;
    }
    if (segment.at(0) != segment.at(segment.size() - 1)) {
        return false;
    }

    for (int index = 1; index + 1 < segment.size(); ++index) {
        if (segment.at(index) == '\0') {
            return true;
        }
    }
    return false;
}

int wordPerfectDataStart(const QByteArray& bytes)
{
    const int firstSeparator = bytes.indexOf(WordPerfectFieldSeparator);
    if (firstSeparator <= 0) {
        return -1;
    }

    int start = firstSeparator - 1;
    while (start >= 0 && bytes.at(start) != '\0') {
        --start;
    }
    return start + 1;
}

void addGenericFieldsForRows(Deck* deck, const QVector<QVector<QString>>& rows)
{
    int fieldCount = 0;
    QVector<int> maxLengths;
    for (const QVector<QString>& row : rows) {
        fieldCount = std::max(fieldCount, static_cast<int>(row.size()));
        if (maxLengths.size() < fieldCount) {
            maxLengths.resize(fieldCount);
        }
        for (int index = 0; index < row.size(); ++index) {
            maxLengths[index] = std::max(maxLengths.at(index), static_cast<int>(row.at(index).size()));
        }
    }

    for (int index = 0; index < fieldCount; ++index) {
        deck->addField(FieldDefinition(
            QStringLiteral("Field %1").arg(index + 1),
            maxLengths.value(index) > 255 ? FieldType::Notes : FieldType::Text,
            std::max(255, maxLengths.value(index))));
    }
}

void addRowsToDeck(Deck* deck, const QVector<QVector<QString>>& rows)
{
    for (const QVector<QString>& row : rows) {
        CardRecord card;
        for (int index = 0; index < deck->fieldCount(); ++index) {
            card.appendValue(row.value(index));
        }
        deck->addCard(std::move(card));
    }
}

QVector<TakeNoteField> takeNoteSoftwareLibraryFields()
{
    return {
        {QStringLiteral("Product"), FieldType::Text, 30},
        {QStringLiteral("Version"), FieldType::Text, 10},
        {QStringLiteral("Company"), FieldType::Text, 50},
        {QStringLiteral("Serial #"), FieldType::Text, 25},
        {QStringLiteral("Registered to"), FieldType::Text, 50},
        {QStringLiteral("Purchased"), FieldType::Text, 20},
        {QStringLiteral("From"), FieldType::Text, 30},
        {QStringLiteral("Tech support"), FieldType::Text, 20},
        {QStringLiteral("Support plan"), FieldType::Text, 20},
        {QStringLiteral("Customer service"), FieldType::Text, 20},
        {QStringLiteral("Notes"), FieldType::Notes, 40},
    };
}

LegacyInterchangeReader::Result readTakeNoteSoftwareLibraryFallback(
    const QString& filePath,
    const QVector<QByteArray>& records,
    const QString& originalError)
{
    LegacyInterchangeReader::Result result;
    result.format = LegacyInterchangeReader::Format::TakeNote;

    const QVector<TakeNoteField> fields = takeNoteSoftwareLibraryFields();
    Deck deck(QStringLiteral("Software Library"));
    deck.setDescription(QStringLiteral("Imported from a legacy TakeNote-compatible deck file."));
    for (const TakeNoteField& field : fields) {
        deck.addField(FieldDefinition(field.name, field.type, field.length));
    }

    for (const QByteArray& record : records) {
        int offset = 5;
        QVector<QString> values;
        values.reserve(fields.size());
        for (const TakeNoteField& field : fields) {
            values.append(decodeWindows1252(record.mid(offset, field.length), true).trimmed());
            offset += field.length;
        }

        const QString product = values.value(0);
        if (product.isEmpty() ||
            product == QStringLiteral("Product") ||
            product == QStringLiteral("Serial #") ||
            product == QStringLiteral("WINDEX") ||
            product.contains(QStringLiteral("Software Library"))) {
            continue;
        }

        CardRecord card;
        for (const QString& value : values) {
            card.appendValue(value);
        }
        deck.addCard(std::move(card));
    }

    if (deck.cardCount() == 0) {
        result.errorMessage = QStringLiteral("Could not import legacy TakeNote-compatible file %1: %2")
            .arg(filePath, originalError);
        return result;
    }

    result.deck = std::move(deck);
    return result;
}

} // namespace

bool LegacyInterchangeReader::Result::ok() const
{
    return errorMessage.isEmpty();
}

bool isLegacyInterchangePath(const QString& filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    return suffix == QStringLiteral("dbf") ||
        suffix == QStringLiteral("crd") ||
        suffix == QStringLiteral("tn") ||
        suffix == QStringLiteral("wp");
}

LegacyInterchangeReader::Result LegacyInterchangeReader::readFile(const QString& filePath, const QString& password) const
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == QStringLiteral("dbf")) {
        return readDBaseFile(filePath);
    }
    if (suffix == QStringLiteral("crd")) {
        return readMicrosoftCardfile(filePath);
    }
    if (suffix == QStringLiteral("tn")) {
        return readTakeNoteFile(filePath, password);
    }
    if (suffix == QStringLiteral("wp")) {
        return readWordPerfectMergeFile(filePath);
    }

    Result result;
    result.errorMessage = QStringLiteral("Unsupported legacy interchange extension: %1").arg(QFileInfo(filePath).suffix());
    return result;
}

LegacyInterchangeReader::Result LegacyInterchangeReader::readDBaseFile(const QString& filePath) const
{
    Result result;
    result.format = Format::DBase;

    QByteArray bytes;
    if (!readAllBytes(filePath, &bytes, &result.errorMessage)) {
        return result;
    }
    if (bytes.size() < DBaseHeaderSize) {
        result.errorMessage = QStringLiteral("Legacy dBase file is too small to contain a header.");
        return result;
    }

    const quint32 recordCount = readU32(bytes, 4);
    const int headerLength = readU16(bytes, 8);
    const int recordLength = readU16(bytes, 10);
    if (headerLength < DBaseHeaderSize || recordLength <= DBaseRecordDeletedFlagSize || headerLength > bytes.size()) {
        result.errorMessage = QStringLiteral("Legacy dBase file has an unsupported header layout.");
        return result;
    }

    const QVector<DBaseField> fields = readDBaseFields(bytes, headerLength);
    if (fields.isEmpty()) {
        result.errorMessage = QStringLiteral("Legacy dBase file does not contain field descriptors.");
        return result;
    }

    Deck deck(deckNameForPath(filePath, QStringLiteral("Legacy dBase Import")));
    deck.setDescription(QStringLiteral("Imported from a legacy dBase interchange file."));
    for (const DBaseField& field : fields) {
        deck.addField(FieldDefinition(field.name, field.type, field.length));
    }

    for (quint32 recordIndex = 0; recordIndex < recordCount; ++recordIndex) {
        const int recordOffset = headerLength + static_cast<int>(recordIndex) * recordLength;
        if (recordOffset < 0 || recordOffset + recordLength > bytes.size()) {
            break;
        }
        if (bytes.at(recordOffset) == '*') {
            continue;
        }

        CardRecord card;
        int fieldOffset = recordOffset + DBaseRecordDeletedFlagSize;
        for (const DBaseField& field : fields) {
            const int storedLength = field.type == FieldType::Notes ? 10 : field.length;
            const QString rawValue = decodeWindows1252(bytes.mid(fieldOffset, storedLength), true).trimmed();
            card.appendValue(field.type == FieldType::Notes ? readDBaseMemoValue(filePath, rawValue) : rawValue);
            fieldOffset += storedLength;
        }
        deck.addCard(std::move(card));
    }

    if (deck.cardCount() == 0) {
        result.errorMessage = QStringLiteral("Legacy dBase file contained field descriptors but no readable records.");
        return result;
    }

    result.deck = std::move(deck);
    return result;
}

LegacyInterchangeReader::Result LegacyInterchangeReader::readMicrosoftCardfile(const QString& filePath) const
{
    Result result;
    result.format = Format::MicrosoftCardfile;

    QByteArray bytes;
    if (!readAllBytes(filePath, &bytes, &result.errorMessage)) {
        return result;
    }
    if (!bytes.startsWith("MGC") || bytes.size() < CardfileIndexOffset) {
        result.errorMessage = QStringLiteral("Legacy Microsoft Cardfile data does not start with an MGC index.");
        return result;
    }

    const int cardCount = readU16(bytes, CardfileCountOffset);
    if (cardCount <= 0 || CardfileIndexOffset + cardCount * CardfileIndexEntrySize > bytes.size()) {
        result.errorMessage = QStringLiteral("Legacy Microsoft Cardfile index has an unsupported layout.");
        return result;
    }

    Deck deck(deckNameForPath(filePath, QStringLiteral("Legacy Cardfile Import")));
    deck.setDescription(QStringLiteral("Imported from a legacy Microsoft Cardfile interchange file."));
    deck.addField(FieldDefinition(QStringLiteral("Title"), FieldType::Text, CardfileTitleLength));
    deck.addField(FieldDefinition(QStringLiteral("Text"), FieldType::Notes, 8192));

    for (int cardIndex = 0; cardIndex < cardCount; ++cardIndex) {
        const int entryOffset = CardfileIndexOffset + cardIndex * CardfileIndexEntrySize;
        const int textOffset = static_cast<int>(readU32(bytes, entryOffset));
        const QString title = decodeFixedString(bytes, entryOffset + CardfileTitleOffsetInEntry, CardfileTitleLength);
        if (textOffset < 0 || textOffset + CardfileTextOffset > bytes.size()) {
            continue;
        }

        const int textLength = readU16(bytes, textOffset + CardfileTextLengthOffset);
        const QString text = decodeWindows1252(
            bytes.mid(
                textOffset + CardfileTextOffset,
                std::min(textLength, static_cast<int>(bytes.size()) - textOffset - CardfileTextOffset)),
            true).trimmed();

        deck.addCard(CardRecord({title, text}));
    }

    if (deck.cardCount() == 0) {
        result.errorMessage = QStringLiteral("Legacy Microsoft Cardfile index contained no readable cards.");
        return result;
    }

    result.deck = std::move(deck);
    return result;
}

LegacyInterchangeReader::Result LegacyInterchangeReader::readTakeNoteFile(const QString& filePath, const QString& password) const
{
    Result result;
    result.format = Format::TakeNote;

    const LegacyDeckReader reader;
    const LegacyDeckReader::Result deckResult = reader.readDeck(filePath, password);
    result.legacyPasswordProtected = deckResult.legacyPasswordProtected;
    result.legacyDataEncrypted = deckResult.legacyDataEncrypted;
    result.legacyPasswordVerified = deckResult.legacyPasswordVerified;
    result.legacyPasswordVerificationUnavailable = deckResult.legacyPasswordVerificationUnavailable;
    result.passwordRequired = deckResult.passwordRequired;
    result.passwordRejected = deckResult.passwordRejected;
    if (deckResult.passwordRequired || deckResult.passwordRejected) {
        result.errorMessage = deckResult.errorMessage;
        return result;
    }
    if (!deckResult.ok()) {
        Result fallback = readTakeNoteSoftwareLibraryFallback(filePath, deckResult.rawRecords, deckResult.errorMessage);
        fallback.legacyPasswordProtected = deckResult.legacyPasswordProtected;
        fallback.legacyDataEncrypted = deckResult.legacyDataEncrypted;
        fallback.legacyPasswordVerificationUnavailable = deckResult.legacyPasswordVerificationUnavailable;
        fallback.passwordRequired = deckResult.passwordRequired;
        fallback.passwordRejected = deckResult.passwordRejected;
        fallback.legacyPasswordVerified = deckResult.legacyPasswordVerified ||
            (deckResult.legacyPasswordProtected && !password.isEmpty());
        return fallback;
    }

    result.deck = deckResult.deck;
    result.deck.setDescription(QStringLiteral("Imported from a legacy TakeNote-compatible deck file."));
    return result;
}

LegacyInterchangeReader::Result LegacyInterchangeReader::readWordPerfectMergeFile(const QString& filePath) const
{
    Result result;
    result.format = Format::WordPerfectMerge;

    QByteArray bytes;
    if (!readAllBytes(filePath, &bytes, &result.errorMessage)) {
        return result;
    }
    if (!bytes.startsWith(QByteArray::fromRawData("\xffWPC", 4))) {
        result.errorMessage = QStringLiteral("Legacy WordPerfect merge data does not start with a WPC header.");
        return result;
    }

    const int dataStart = wordPerfectDataStart(bytes);
    if (dataStart < 0 || dataStart >= bytes.size()) {
        result.errorMessage = QStringLiteral("Legacy WordPerfect merge data does not contain field separators.");
        return result;
    }

    QVector<QVector<QString>> rows;
    QVector<QString> row;
    const QVector<QByteArray> segments = splitWordPerfectSegments(bytes.mid(dataStart));
    for (int segmentIndex = 0; segmentIndex < segments.size(); ++segmentIndex) {
        const QByteArray& segment = segments.at(segmentIndex);
        if (segment.isEmpty() && segmentIndex == segments.size() - 1) {
            continue;
        }
        if (isWordPerfectDescriptor(segment, '1')) {
            continue;
        }
        if (isWordPerfectDescriptor(segment, '4')) {
            if (!row.isEmpty()) {
                rows.append(row);
                row.clear();
            }
            continue;
        }

        row.append(decodeWindows1252(segment, false));
    }
    if (!row.isEmpty()) {
        rows.append(row);
    }
    if (rows.isEmpty()) {
        result.errorMessage = QStringLiteral("Legacy WordPerfect merge data contained no records.");
        return result;
    }

    Deck deck(deckNameForPath(filePath, QStringLiteral("Legacy WordPerfect Import")));
    deck.setDescription(QStringLiteral("Imported from a legacy WordPerfect merge interchange file."));
    addGenericFieldsForRows(&deck, rows);
    addRowsToDeck(&deck, rows);
    result.deck = std::move(deck);
    return result;
}

} // namespace CardStack
