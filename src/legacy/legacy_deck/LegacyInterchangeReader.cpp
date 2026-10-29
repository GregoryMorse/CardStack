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
constexpr int CardfileDirectoryOffset = CardfileCountOffset + sizeof(quint16);
constexpr int CardfileIndexOffset = CardfileDirectoryOffset + 6;
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

quint16 readU16(const QByteArray& bytes, quint64 offset)
{
    if (!containsByteRange(bytes.size(), offset, sizeof(quint16))) {
        return 0;
    }
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

quint32 readU32(const QByteArray& bytes, quint64 offset)
{
    if (!containsByteRange(bytes.size(), offset, sizeof(quint32))) {
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

QString decodeFixedString(const QByteArray& bytes, qsizetype offset, int length)
{
    if (offset < 0 || length <= 0 || offset >= bytes.size()) {
        return {};
    }
    const qsizetype available = std::min<qsizetype>(length, bytes.size() - offset);
    return decodeWindows1252(bytes.mid(offset, available), true).trimmed();
}

bool readAllBytes(const QString& path, QByteArray* bytes, QString* error, ImportBudget& budget)
{
    return readBoundedFile(path, bytes, budget, error);
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

bool readDBaseMemoValue(const QString& dbfPath, const QString& token, QByteArray* cache, bool* loaded,
                        QString* value, QString* error, ImportBudget& budget)
{
    *value = {};
    if (token.trimmed().isEmpty()) return true;
    bool valid = false;
    const quint64 block = token.trimmed().toULongLong(&valid);
    if (!valid) { *error = QStringLiteral("A dBase memo block number is invalid."); return false; }
    if (block == 0) return true;
    if (!*loaded) {
        const QString path = memoPathForDBasePath(dbfPath);
        if (path.isEmpty()) { *error = QStringLiteral("The dBase memo sidecar is missing."); return false; }
        if (!readAllBytes(path, cache, error, budget)) return false;
        *loaded = true;
    }
    // Check division before multiplication; block numbers are unsigned disk values.
    if (block > quint64(cache->size()) / DBaseMemoBlockSize) {
        *error = QStringLiteral("A dBase memo block is outside its sidecar.");
        return false;
    }
    const quint64 offset = block * DBaseMemoBlockSize;
    if (!containsByteRange(cache->size(), offset, 1)) {
        *error = QStringLiteral("A dBase memo block is outside its sidecar.");
        return false;
    }
    qsizetype end = qsizetype(offset);
    while (end < cache->size() && cache->at(end) != '\0' && cache->at(end) != '\x1a') {
        if (!budget.checkpoint(1) || !budget.cell(quint64(end - qsizetype(offset) + 1) * sizeof(QChar))) {
            *error = budget.error();
            return false;
        }
        ++end;
    }
    *value = decodeWindows1252(cache->mid(qsizetype(offset), end - qsizetype(offset))).trimmed();
    return true;
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
        if (name.isEmpty() || length <= 0 || (type == 'M' && length != 10)) return {};

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
    const QString& originalError, ImportBudget& budget)
{
    LegacyInterchangeReader::Result result;
    result.format = LegacyInterchangeReader::Format::TakeNote;

    const QVector<TakeNoteField> fields = takeNoteSoftwareLibraryFields();
    const auto abort = [&]() { result.processingAborted = true; result.errorMessage = budget.error(); return result; };
    if (!budget.fields(quint64(fields.size()))) return abort();
    Deck deck(QStringLiteral("Software Library"));
    deck.setDescription(QStringLiteral("Imported from a legacy TakeNote-compatible deck file."));
    for (const TakeNoteField& field : fields) {
        deck.addField(FieldDefinition(field.name, field.type, field.length));
    }

    for (const QByteArray& record : records) {
        if (!budget.row() || !budget.checkpoint(quint64(record.size()))) return abort();
        int offset = 5;
        QVector<QString> values;
        values.reserve(fields.size());
        for (const TakeNoteField& field : fields) {
            if (!budget.value(quint64(field.length) * sizeof(QChar))) return abort();
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
    return !processingAborted && errorMessage.isEmpty();
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
    ImportBudget budget(m_limits);
    Result result;
    result.format = Format::DBase;
    const auto aborted = [&] { result.processingAborted = true; result.errorMessage = budget.error(); return result; };
    QByteArray bytes, memo;
    if (!readAllBytes(filePath, &bytes, &result.errorMessage, budget)) {
        result.processingAborted = budget.failed();
        return result;
    }
    if (bytes.size() < DBaseHeaderSize) {
        result.errorMessage = QStringLiteral("The dBase file is too small to contain a header.");
        return result;
    }
    const quint64 count = readU32(bytes, 4);
    const quint64 header = readU16(bytes, 8), recordLength = readU16(bytes, 10);
    if (header < DBaseHeaderSize || header > quint64(bytes.size()) || recordLength <= DBaseRecordDeletedFlagSize
        || count > (quint64(bytes.size()) - header) / recordLength) {
        result.errorMessage = QStringLiteral("The dBase record span is invalid or truncated.");
        return result;
    }
    if (!budget.rowsFit(count)) return aborted();
    const auto fields = readDBaseFields(bytes, int(header));
    if (fields.isEmpty() || !budget.fields(quint64(fields.size()))) {
        result.errorMessage = budget.failed() ? budget.error() : QStringLiteral("The dBase field table is invalid.");
        result.processingAborted = budget.failed();
        return result;
    }
    quint64 storedWidth = DBaseRecordDeletedFlagSize;
    for (const auto& field : fields) storedWidth += field.type == FieldType::Notes ? 10 : field.length;
    if (storedWidth > recordLength) {
        result.errorMessage = QStringLiteral("The dBase fields extend beyond their record.");
        return result;
    }
    Deck deck(deckNameForPath(filePath, QStringLiteral("dBase Import")));
    deck.setDescription(QStringLiteral("Imported from a dBase interchange file."));
    for (const auto& field : fields) deck.addField(FieldDefinition(field.name, field.type, field.length));
    bool memoLoaded = false;
    for (quint64 row = 0; row < count; ++row) {
        if (!budget.checkpoint(recordLength)) return aborted();
        const quint64 offset = header + row * recordLength; // The complete span was validated above.
        if (bytes.at(qsizetype(offset)) == '*') continue;
        if (!budget.row()) return aborted();
        CardRecord card;
        quint64 fieldOffset = offset + DBaseRecordDeletedFlagSize;
        for (const auto& field : fields) {
            const quint64 length = field.type == FieldType::Notes ? 10 : quint64(field.length);
            const QString raw = decodeWindows1252(bytes.mid(qsizetype(fieldOffset), qsizetype(length)), true).trimmed();
            QString value = raw;
            if (field.type == FieldType::Notes
                && !readDBaseMemoValue(filePath, raw, &memo, &memoLoaded, &value, &result.errorMessage, budget)) {
                result.processingAborted = budget.failed();
                return result;
            }
            if (!budget.value(quint64(value.size()) * sizeof(QChar))) return aborted();
            card.appendValue(value);
            fieldOffset += length;
        }
        deck.addCard(std::move(card));
    }
    result.deck = std::move(deck);
    return result;
}

LegacyInterchangeReader::Result LegacyInterchangeReader::readMicrosoftCardfile(const QString& filePath) const
{
    ImportBudget budget(m_limits);
    Result result;
    result.format = Format::MicrosoftCardfile;
    const auto aborted = [&] { result.processingAborted = true; result.errorMessage = budget.error(); return result; };
    QByteArray bytes;
    if (!readAllBytes(filePath, &bytes, &result.errorMessage, budget)) {
        result.processingAborted = budget.failed();
        return result;
    }
    if (!bytes.startsWith("MGC") || !containsByteRange(bytes.size(), 0, CardfileIndexOffset)) {
        result.errorMessage = QStringLiteral("The Cardfile MGC header is invalid or truncated.");
        return result;
    }
    const quint64 count = readU16(bytes, CardfileCountOffset);
    const quint64 indexBytes = count * CardfileIndexEntrySize;
    if (!containsByteRange(bytes.size(), CardfileDirectoryOffset, indexBytes)) {
        result.errorMessage = QStringLiteral("The Cardfile index is truncated.");
        return result;
    }
    if (!budget.rowsFit(count) || !budget.fields(2)) return aborted();
    const quint64 indexEnd = CardfileDirectoryOffset + indexBytes;
    Deck deck(deckNameForPath(filePath, QStringLiteral("Cardfile Import")));
    deck.setDescription(QStringLiteral("Imported from a Microsoft Cardfile interchange file."));
    deck.addField(FieldDefinition(QStringLiteral("Title"), FieldType::Text, CardfileTitleLength));
    deck.addField(FieldDefinition(QStringLiteral("Text"), FieldType::Notes, 8192));
    for (quint64 row = 0; row < count; ++row) {
        const quint64 entry = CardfileIndexOffset + row * CardfileIndexEntrySize;
        const quint64 textOffset = readU32(bytes, entry);
        if (textOffset < indexEnd || !containsByteRange(bytes.size(), textOffset, CardfileTextOffset)) {
            result.errorMessage = QStringLiteral("A Cardfile text offset is outside the file.");
            return result;
        }
        const quint64 length = readU16(bytes, textOffset + CardfileTextLengthOffset);
        const quint64 payload = textOffset + CardfileTextOffset;
        if (!containsByteRange(bytes.size(), payload, length)) {
            result.errorMessage = QStringLiteral("A Cardfile text payload is truncated.");
            return result;
        }
        if (!budget.row() || !budget.checkpoint(length + CardfileTitleLength)
            || !budget.value(CardfileTitleLength * sizeof(QChar)) || !budget.value(length * sizeof(QChar))) return aborted();
        const QString title = decodeFixedString(bytes, qsizetype(entry + CardfileTitleOffsetInEntry), CardfileTitleLength);
        const QString text = decodeWindows1252(bytes.mid(qsizetype(payload), qsizetype(length)), true).trimmed();
        deck.addCard(CardRecord({title, text}));
    }
    result.deck = std::move(deck);
    return result;
}

LegacyInterchangeReader::Result LegacyInterchangeReader::readTakeNoteFile(const QString& filePath, const QString& password) const
{
    Result result;
    result.format = Format::TakeNote;

    ImportBudget budget(m_limits);
    const LegacyDeckReader reader(m_limits);
    const LegacyDeckReader::Result deckResult = reader.readDeck(filePath, password, &budget);
    if (deckResult.processingAborted || budget.failed()) { result.processingAborted = true; result.errorMessage = budget.error(); return result; }
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
        Result fallback = readTakeNoteSoftwareLibraryFallback(filePath, deckResult.rawRecords, deckResult.errorMessage, budget);
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
    ImportBudget budget(m_limits);
    Result result;
    result.format = Format::WordPerfectMerge;
    const auto aborted = [&] { result.processingAborted = true; result.errorMessage = budget.error(); return result; };
    QByteArray bytes;
    if (!readAllBytes(filePath, &bytes, &result.errorMessage, budget)) {
        result.processingAborted = budget.failed();
        return result;
    }
    if (!bytes.startsWith(QByteArray::fromRawData("\xffWPC", 4))) {
        result.errorMessage = QStringLiteral("The WordPerfect WPC merge header is invalid.");
        return result;
    }
    const int start = wordPerfectDataStart(bytes);
    if (start < 0 || start >= bytes.size()) {
        result.errorMessage = QStringLiteral("The WordPerfect merge data has no field separators.");
        return result;
    }
    QVector<QVector<QString>> rows;
    QVector<QString> row;
    for (qsizetype from = start; from <= bytes.size();) {
        const qsizetype separator = bytes.indexOf(WordPerfectFieldSeparator, from);
        const qsizetype end = separator < 0 ? bytes.size() : separator;
        const qsizetype length = end - from;
        if (!budget.checkpoint(quint64(length) + 1) || !budget.cell(quint64(length) * sizeof(QChar))) return aborted();
        if (length == 0 && separator < 0) break;
        const QByteArray segment = bytes.mid(from, length);
        if (isWordPerfectDescriptor(segment, '4')) {
            if (!row.isEmpty()) {
                if (!budget.row()) return aborted();
                rows.append(std::move(row));
                row.clear();
            }
        } else if (!isWordPerfectDescriptor(segment, '1')) {
            if (!budget.fields(quint64(row.size()) + 1) || !budget.value(quint64(length) * sizeof(QChar))) return aborted();
            row.append(decodeWindows1252(segment));
        }
        if (separator < 0) break;
        from = separator + 1;
    }
    if (!row.isEmpty()) {
        if (!budget.row()) return aborted();
        rows.append(std::move(row));
    }
    if (rows.isEmpty()) { result.errorMessage = QStringLiteral("The WordPerfect merge file contains no records."); return result; }
    Deck deck(deckNameForPath(filePath, QStringLiteral("WordPerfect Import")));
    deck.setDescription(QStringLiteral("Imported from a WordPerfect merge interchange file."));
    addGenericFieldsForRows(&deck, rows);
    for (const auto& values : rows) {
        CardRecord card;
        for (int field = 0; field < deck.fieldCount(); ++field) {
            const QString value = values.value(field);
            if (!budget.checkpoint(1) || !budget.value(quint64(value.size()) * sizeof(QChar))) return aborted();
            card.appendValue(value);
        }
        deck.addCard(std::move(card));
    }
    result.deck = std::move(deck);
    return result;
}

} // namespace CardStack
