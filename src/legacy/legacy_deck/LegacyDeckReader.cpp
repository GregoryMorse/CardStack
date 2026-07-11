#include "LegacyDeckReader.h"

#include "BtrieveAuditReader.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>

#include <algorithm>
#include <utility>

namespace CardStack {
namespace {

constexpr int RecordPayloadOffset = 5;
constexpr int SchemaMagicOffset = 5;
constexpr int SchemaDeckNameOffset = 11;
constexpr int SchemaDeckNameLength = 64;
constexpr int SchemaFieldCountOffset = 87;
constexpr int FieldDefinitionSize = 37;
constexpr int FieldNameSize = 16;
constexpr int MaxLegacyFields = 40;
constexpr int OldFormatOwnerEncryptionKeyOffset = 0x2e;
constexpr int OldFormatOwnerVerificationLength = 9;
constexpr int OldFormatOwnerEncryptionKeyCount = 4;
constexpr int OldFormatOwnerEncryptionSectorSize = 1024;
constexpr quint8 OldFormatOwnerReadAccessFlag = 0x01;
constexpr int OldFormatDataPageHeaderSize = 6;
constexpr int OldFormatEmptyRecordScanOffset = 8;

struct FieldDefinitionLayout {
    int nameOffset = 0;
    int typeOffset = 16;
    int recordOffsetOffset = 20;
    int lengthOffset = 22;
};

struct LegacyField {
    QString name;
    FieldType type = FieldType::Text;
    int recordOffset = 0;
    int length = 0;
};

quint16 readU16(const QByteArray& bytes, int offset)
{
    if (offset < 0 || offset + 1 >= bytes.size()) {
        return 0;
    }

    const auto low = static_cast<quint8>(bytes.at(offset));
    const auto high = static_cast<quint8>(bytes.at(offset + 1));
    return static_cast<quint16>(low | (high << 8));
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

QString readNullTerminatedWindows1252(const QByteArray& bytes, int offset, int maxLength)
{
    if (offset < 0 || offset >= bytes.size() || maxLength <= 0) {
        return {};
    }

    const int available = std::min(maxLength, static_cast<int>(bytes.size()) - offset);
    int length = 0;
    while (length < available && bytes.at(offset + length) != '\0') {
        ++length;
    }

    QString value;
    value.reserve(length);
    for (int index = 0; index < length; ++index) {
        value.append(decodeWindows1252Byte(static_cast<quint8>(bytes.at(offset + index))));
    }
    return value.trimmed();
}

bool isLikelyFieldName(const QString& value)
{
    if (value.isEmpty()) {
        return false;
    }

    for (const QChar ch : value) {
        if (!ch.isPrint()) {
            return false;
        }
    }
    return true;
}

bool parseLegacyFieldAt(const QByteArray& schemaRecord, int offset, LegacyField* field)
{
    if (field == nullptr || offset < 0 || offset + FieldDefinitionSize > schemaRecord.size()) {
        return false;
    }

    const FieldDefinitionLayout layouts[] = {
        {0, 16, 22, 24},
        {0, 18, 22, 24},
        {0, 16, 20, 22},
        {1, 17, 21, 23},
    };

    for (const FieldDefinitionLayout& layout : layouts) {
        if (offset + layout.lengthOffset + 1 >= schemaRecord.size()) {
            continue;
        }

        const QString name = readNullTerminatedWindows1252(schemaRecord, offset + layout.nameOffset, FieldNameSize);
        const int typeByte = static_cast<quint8>(schemaRecord.at(offset + layout.typeOffset));
        const int recordOffset = readU16(schemaRecord, offset + layout.recordOffsetOffset);
        const int length = readU16(schemaRecord, offset + layout.lengthOffset);

        if (!isLikelyFieldName(name) || (typeByte != 1 && typeByte != 3) || length <= 0) {
            continue;
        }
        if (typeByte == 1 && length > 256) {
            continue;
        }
        if (typeByte == 3 && length > 8192) {
            continue;
        }

        *field = {
            name,
            typeByte == 3 ? FieldType::Notes : FieldType::Text,
            recordOffset,
            length,
        };
        return true;
    }

    return false;
}

int declaredFieldCount(const QByteArray& schemaRecord)
{
    if (schemaRecord.size() <= SchemaFieldCountOffset) {
        return 0;
    }

    const int count = static_cast<quint8>(schemaRecord.at(SchemaFieldCountOffset));
    return count > 0 && count <= MaxLegacyFields ? count : 0;
}

QVector<LegacyField> findFieldDefinitions(const QByteArray& schemaRecord)
{
    const int expectedFieldCount = declaredFieldCount(schemaRecord);
    const auto parseSequence = [&schemaRecord](int offset, int expectedCount = 0) {
        QVector<LegacyField> candidate;
        int previousRecordOffset = -1;
        for (int fieldIndex = 0; fieldIndex < MaxLegacyFields; ++fieldIndex) {
            if (expectedCount > 0 && fieldIndex >= expectedCount) {
                break;
            }
            LegacyField field;
            const int fieldOffset = offset + fieldIndex * FieldDefinitionSize;
            if (!parseLegacyFieldAt(schemaRecord, fieldOffset, &field)) {
                break;
            }
            if (field.recordOffset < previousRecordOffset) {
                break;
            }
            previousRecordOffset = field.recordOffset;
            candidate.append(field);
        }
        return candidate;
    };

    const auto matchesDeclaredCount = [expectedFieldCount](const QVector<LegacyField>& fields) {
        return expectedFieldCount > 0 && fields.size() == expectedFieldCount;
    };

    const int productOffset = schemaRecord.indexOf(QByteArrayLiteral("Product"));
    if (productOffset >= 0) {
        QVector<LegacyField> anchored = parseSequence(productOffset, expectedFieldCount);
        if (matchesDeclaredCount(anchored) || (expectedFieldCount == 0 && anchored.size() >= 2)) {
            return anchored;
        }

        anchored = parseSequence(productOffset - 1, expectedFieldCount);
        if (matchesDeclaredCount(anchored) || (expectedFieldCount == 0 && anchored.size() >= 2)) {
            return anchored;
        }
    }

    QVector<LegacyField> bestFields;
    int bestScore = -1;
    for (int offset = 0; offset + FieldDefinitionSize <= schemaRecord.size(); ++offset) {
        const QVector<LegacyField> candidate = parseSequence(offset, expectedFieldCount);

        if (!candidate.isEmpty() && candidate.first().name.front().isLower()) {
            continue;
        }
        if (matchesDeclaredCount(candidate)) {
            return candidate;
        }

        int score = candidate.size() * 100;
        for (const LegacyField& field : candidate) {
            if (!field.name.isEmpty() && field.name.front().isUpper()) {
                score += 3;
            }
            if (!field.name.isEmpty() && field.name.front().isLower()) {
                score -= 2;
            }
        }

        if (candidate.size() > bestFields.size() || (candidate.size() == bestFields.size() && score > bestScore)) {
            bestFields = candidate;
            bestScore = score;
        }
    }

    return bestFields;
}

const QByteArray* findSchemaRecord(const QVector<QByteArray>& records)
{
    const QByteArray* best = nullptr;
    int bestFieldCount = 0;
    for (const QByteArray& record : records) {
        const QVector<LegacyField> fields = findFieldDefinitions(record);
        const int declaredCount = declaredFieldCount(record);
        const int score = fields.size() + (declaredCount > 0 && declaredCount == fields.size() ? MaxLegacyFields : 0);
        if (score > bestFieldCount) {
            best = &record;
            bestFieldCount = score;
        }
    }
    return best;
}

bool isControlRecord(const QByteArray& record)
{
    return record.left(32).contains(QByteArrayLiteral("~BT"));
}

QString fieldValueFromRecord(const QByteArray& record, const LegacyField& field)
{
    return readNullTerminatedWindows1252(record, RecordPayloadOffset + field.recordOffset, field.length);
}

int minimumRecordSize(const QVector<LegacyField>& fields)
{
    int size = RecordPayloadOffset;
    for (const LegacyField& field : fields) {
        size = std::max(size, RecordPayloadOffset + field.recordOffset + field.length);
    }
    return size;
}

QString normalizeLegacyOwnerPassword(const QString& password)
{
    return password.left(8).toUpper();
}

quint16 rotateLeft16(quint16 value, int bits)
{
    return static_cast<quint16>((value << bits) | (value >> (16 - bits)));
}

quint16 rotateRight16(quint16 value, int bits)
{
    return static_cast<quint16>((value >> bits) | (value << (16 - bits)));
}

void writeU16(QByteArray* bytes, int offset, quint16 value)
{
    if (bytes == nullptr || offset < 0 || offset + 1 >= bytes->size()) {
        return;
    }

    (*bytes)[offset] = static_cast<char>(value & 0xff);
    (*bytes)[offset + 1] = static_cast<char>((value >> 8) & 0xff);
}

QByteArray legacyOwnerVerificationBytes(const QString& password)
{
    QByteArray ownerName = normalizeLegacyOwnerPassword(password).toLatin1().left(8);
    while (ownerName.size() < 8) {
        ownerName.append(' ');
    }

    quint8 sum = 0;
    for (const char ch : ownerName) {
        sum = static_cast<quint8>(sum + static_cast<quint8>(ch));
    }

    QByteArray verification(OldFormatOwnerVerificationLength, '\0');
    verification[0] = static_cast<char>(sum);

    const quint16 word0 = readU16(ownerName, 0);
    const quint16 word1 = readU16(ownerName, 2);
    const quint16 word2 = readU16(ownerName, 4);
    const quint16 word3 = readU16(ownerName, 6);

    if ((sum & 1U) == 0) {
        writeU16(&verification, 7, rotateRight16(word0, 3));
        writeU16(&verification, 1, static_cast<quint16>(-word1));
        writeU16(&verification, 5, rotateLeft16(word2, 5));
        writeU16(&verification, 3, rotateRight16(word3, 1));
    } else {
        writeU16(&verification, 3, rotateRight16(word0, 1));
        writeU16(&verification, 5, rotateLeft16(word1, 1));
        writeU16(&verification, 1, static_cast<quint16>(-word2));
        writeU16(&verification, 7, rotateLeft16(word3, 3));
    }

    return verification;
}

bool legacyOwnerPasswordMatches(const BtrieveAuditReader::Audit& audit, const QString& password)
{
    return audit.ownerVerificationBytes.size() == OldFormatOwnerVerificationLength &&
        legacyOwnerVerificationBytes(password) == audit.ownerVerificationBytes;
}

bool legacyOwnerReadAccessAllowed(const BtrieveAuditReader::Audit& audit)
{
    return (audit.ownerFlags & OldFormatOwnerReadAccessFlag) != 0;
}

bool decryptOldFormatOwnerEncryptedBtrieveFile(
    const QString& filePath,
    const BtrieveAuditReader::Audit& audit,
    QTemporaryFile* decryptedFile,
    QString* errorMessage)
{
    if (decryptedFile == nullptr || audit.pageSize <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not prepare temporary legacy Btrieve decrypt buffer.");
        }
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not open encrypted legacy Btrieve file: %1").arg(filePath);
        }
        return false;
    }

    QByteArray bytes = file.readAll();
    if (bytes.size() < audit.pageSize || bytes.size() % audit.pageSize != 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Encrypted legacy Btrieve file has an unsupported page layout.");
        }
        return false;
    }

    quint16 keys[OldFormatOwnerEncryptionKeyCount] = {};
    bool hasKey = false;
    for (int index = 0; index < OldFormatOwnerEncryptionKeyCount; ++index) {
        keys[index] = readU16(bytes, OldFormatOwnerEncryptionKeyOffset + index * 2);
        hasKey = hasKey || keys[index] != 0;
    }

    if (!hasKey) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Encrypted legacy Btrieve file does not contain a usable owner encryption key.");
        }
        return false;
    }

    for (int pageOffset = audit.pageSize; pageOffset < bytes.size(); pageOffset += audit.pageSize) {
        for (int offset = 0; offset < audit.pageSize; ++offset) {
            const quint16 key = keys[(offset / OldFormatOwnerEncryptionSectorSize) % OldFormatOwnerEncryptionKeyCount];
            const char lowKey = static_cast<char>(key & 0xff);
            const char highKey = static_cast<char>((key >> 8) & 0xff);
            bytes[pageOffset + offset] = static_cast<char>(
                bytes.at(pageOffset + offset) ^ ((offset & 1) == 0 ? lowKey : highKey));
        }
    }

    if (!decryptedFile->open()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not create temporary decrypted legacy Btrieve file.");
        }
        return false;
    }

    if (decryptedFile->write(bytes) != bytes.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not write temporary decrypted legacy Btrieve file.");
        }
        return false;
    }

    decryptedFile->close();
    return true;
}

bool isOccupiedOldFormatRecordSlot(const QByteArray& bytes, int offset, int physicalLength)
{
    const int scanStart = offset + OldFormatEmptyRecordScanOffset;
    const int scanEnd = std::min(offset + physicalLength, static_cast<int>(bytes.size()));
    for (int index = scanStart; index < scanEnd; ++index) {
        if (bytes.at(index) != '\0') {
            return true;
        }
    }
    return false;
}

QVector<QByteArray> extractOldFormatFixedRecordsFromAudit(
    const QString& filePath,
    const BtrieveAuditReader::Audit& audit,
    QString* errorMessage)
{
    QVector<QByteArray> records;
    if (audit.pageSize <= 0 || audit.physicalRecordLength <= 0 || audit.fixedRecordLength <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Legacy Btrieve audit metadata is not sufficient for raw record extraction.");
        }
        return records;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not open legacy Btrieve file for raw extraction: %1").arg(filePath);
        }
        return records;
    }

    const QByteArray bytes = file.readAll();
    const int recordsPerPage = (audit.pageSize - 2) / audit.physicalRecordLength;
    const int recordLength = std::min(audit.fixedRecordLength, audit.physicalRecordLength);
    for (const BtrieveAuditReader::Page& page : audit.pages) {
        if (page.type != BtrieveAuditReader::PageType::Data) {
            continue;
        }

        const int pageOffset = static_cast<int>(page.offset);
        for (int slot = 0; slot < recordsPerPage; ++slot) {
            const int recordOffset = pageOffset + OldFormatDataPageHeaderSize + slot * audit.physicalRecordLength;
            if (recordOffset < 0 || recordOffset + audit.physicalRecordLength > bytes.size()) {
                break;
            }
            if (!isOccupiedOldFormatRecordSlot(bytes, recordOffset, audit.physicalRecordLength)) {
                continue;
            }
            records.append(bytes.mid(recordOffset, recordLength));
        }
    }

    if (records.isEmpty() && errorMessage != nullptr) {
        *errorMessage = QStringLiteral("Legacy Btrieve raw extraction found no occupied fixed records.");
    }
    return records;
}

void applyLegacySecurityMetadata(
    LegacyDeckReader::Result* result,
    const BtrieveAuditReader::Result& audit,
    const QString& password)
{
    if (result == nullptr || !audit.ok() || !audit.audit.owned) {
        return;
    }

    result->legacyPasswordProtected = true;
    result->legacyDataEncrypted = audit.audit.ownerDataEncrypted;
    const bool readAccessAllowed = legacyOwnerReadAccessAllowed(audit.audit);
    const bool hasPassword = !normalizeLegacyOwnerPassword(password).isEmpty();
    const bool passwordMatches = hasPassword && legacyOwnerPasswordMatches(audit.audit, password);

    if (result->ok()) {
        if (!hasPassword && !readAccessAllowed) {
            result->passwordRequired = true;
            result->errorMessage = QStringLiteral("Legacy Btrieve deck requires a password.");
            return;
        }

        result->legacyPasswordVerified = passwordMatches;
        result->legacyPasswordVerificationUnavailable = !passwordMatches && readAccessAllowed;

        const QString description = result->deck.description();
        result->deck.setDescription(
            description.isEmpty()
                ? QStringLiteral("Imported from a password-protected legacy Btrieve deck.")
                : QStringLiteral("%1 Source deck was password-protected.").arg(description));
        return;
    }

    result->legacyDataEncrypted = audit.audit.ownerDataEncrypted;
    if (!hasPassword && !readAccessAllowed) {
        result->passwordRequired = true;
        result->errorMessage = QStringLiteral("Legacy Btrieve deck requires a password.");
    } else if (audit.audit.ownerDataEncrypted) {
        result->passwordRejected = true;
        result->errorMessage = QStringLiteral(
            "Legacy Btrieve deck appears to use owner-name data encryption. "
            "The password was normalized with the legacy 8-character uppercase rule, "
            "but the legacy old-format data-page decrypt pass could not decode this file.");
    }
}

} // namespace

bool LegacyDeckReader::Result::ok() const
{
    return errorMessage.isEmpty();
}

LegacyDeckReader::Result LegacyDeckReader::readDeck(const QString& filePath, const QString& password) const
{
    Result result;
    const BtrieveAuditReader auditReader;
    const BtrieveAuditReader::Result audit = auditReader.readFile(filePath);

    if (audit.ok() && audit.audit.owned &&
        normalizeLegacyOwnerPassword(password).isEmpty() &&
        !legacyOwnerReadAccessAllowed(audit.audit)) {
        result.legacyPasswordProtected = true;
        result.legacyDataEncrypted = audit.audit.ownerDataEncrypted;
        result.passwordRequired = true;
        result.errorMessage = QStringLiteral("Legacy Btrieve deck requires a password.");
        return result;
    }

    if (audit.ok() && audit.audit.owned &&
        !legacyOwnerPasswordMatches(audit.audit, password) &&
        !legacyOwnerReadAccessAllowed(audit.audit)) {
        result.legacyPasswordProtected = true;
        result.legacyDataEncrypted = audit.audit.ownerDataEncrypted;
        result.passwordRejected = true;
        result.errorMessage = QStringLiteral("ACCESS DENIED: Wrong Password");
        return result;
    }

    QTemporaryFile decryptedFile;
    QString btrievePath = filePath;
    if (audit.ok() && audit.audit.ownerDataEncrypted) {
        QString decryptError;
        if (!decryptOldFormatOwnerEncryptedBtrieveFile(filePath, audit.audit, &decryptedFile, &decryptError)) {
            result.errorMessage = decryptError;
            applyLegacySecurityMetadata(&result, audit, password);
            return result;
        }
        btrievePath = decryptedFile.fileName();
    }

    const BtrieveFileSaverReader reader;
    const BtrieveFileSaverReader::Result btrieve = reader.readAllRecords(btrievePath);
    result.btrieveMetadata = btrieve.metadata;
    result.rawRecords = btrieve.records;
    if (!btrieve.ok()) {
        if (audit.ok() && audit.audit.oldFormat && !audit.audit.ownerDataEncrypted) {
            QString extractionError;
            const QVector<QByteArray> records = extractOldFormatFixedRecordsFromAudit(filePath, audit.audit, &extractionError);
            if (!records.isEmpty()) {
                Result mapped = readRecords(records, QFileInfo(filePath).completeBaseName(), password);
                mapped.rawRecords = records;
                mapped.btrieveMetadata.pageSize = audit.audit.pageSize;
                mapped.btrieveMetadata.fixedRecordLength = audit.audit.fixedRecordLength;
                mapped.btrieveMetadata.internalFixedRecordLength = audit.audit.physicalRecordLength;
                mapped.btrieveMetadata.declaredRecordCount = audit.audit.declaredRecordCount;
                applyLegacySecurityMetadata(&mapped, audit, password);
                return mapped;
            }
            result.errorMessage = extractionError.isEmpty() ? btrieve.errorMessage : extractionError;
        } else {
            result.errorMessage = btrieve.errorMessage;
        }
        applyLegacySecurityMetadata(&result, audit, password);
        return result;
    }

    const QString fallbackName = QFileInfo(filePath).completeBaseName();
    Result mapped = readRecords(btrieve.records, fallbackName, password);
    mapped.btrieveMetadata = btrieve.metadata;
    mapped.rawRecords = btrieve.records;
    applyLegacySecurityMetadata(&mapped, audit, password);
    return mapped;
}

LegacyDeckReader::Result LegacyDeckReader::readRecords(
    const QVector<QByteArray>& records,
    const QString& fallbackDeckName,
    const QString& password) const
{
    Q_UNUSED(password);

    Result result;
    result.rawRecords = records;
    if (records.isEmpty()) {
        result.errorMessage = QStringLiteral("Legacy Btrieve deck contains no records.");
        return result;
    }

    const QByteArray* schemaRecord = findSchemaRecord(records);
    if (schemaRecord == nullptr) {
        result.errorMessage = QStringLiteral("Could not locate Legacy field definitions.");
        return result;
    }

    const QVector<LegacyField> fields = findFieldDefinitions(*schemaRecord);
    if (fields.isEmpty()) {
        result.errorMessage = QStringLiteral("Legacy field definition table is empty.");
        return result;
    }

    const QString deckName = readNullTerminatedWindows1252(*schemaRecord, SchemaDeckNameOffset, SchemaDeckNameLength);
    Deck deck(deckName.isEmpty() ? fallbackDeckName : deckName);
    deck.setDescription(QStringLiteral("Imported from a legacy Btrieve deck."));
    for (const LegacyField& field : fields) {
        deck.addField(FieldDefinition(field.name, field.type, field.length));
    }

    const int requiredSize = minimumRecordSize(fields);
    for (const QByteArray& record : records) {
        if (record == *schemaRecord || isControlRecord(record) || record.size() < requiredSize) {
            continue;
        }

        CardRecord card;
        for (const LegacyField& field : fields) {
            card.appendValue(fieldValueFromRecord(record, field));
        }
        deck.addCard(std::move(card));
    }

    if (deck.cardCount() == 0) {
        result.errorMessage = QStringLiteral("Legacy field definitions were found, but no card records matched them.");
        return result;
    }

    result.deck = std::move(deck);
    return result;
}

} // namespace CardStack
