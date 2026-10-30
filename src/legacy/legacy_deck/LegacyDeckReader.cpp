#include "LegacyDeckReader.h"
#include "LegacyOemCodec.h"
#include "LegacyTemplateLayoutParser.h"
#include "LegacyDeckAppearanceParser.h"
#include "PhoneCallLog.h"

#include "BtrieveAuditReader.h"

#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include "DeckSecurity.h"

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
constexpr int WinIndexFieldCountOffset = 0x59;
constexpr int WinIndexFieldTableOffset = 0x3cc;
constexpr int WinIndexFieldDescriptorSize = 0x39;
constexpr int WinIndexFieldNameSize = 30;
constexpr int WinIndexFieldRoleOffset = 0x1e;
constexpr int WinIndexFieldRecordOffset = 0x22;
constexpr int WinIndexFieldLengthOffset = 0x24;
constexpr int WinIndexFieldFlagsOffset = 0x27;
constexpr int WinIndexMaximumFields = 20;
constexpr int FieldNameSize = 16;
constexpr int MaxLegacyFields = 40;
constexpr int OldFormatOwnerEncryptionKeyOffset = 0x2e;
constexpr int OldFormatOwnerVerificationLength = 9;
constexpr int OldFormatOwnerEncryptionKeyCount = 4;
constexpr int OldFormatOwnerEncryptionWordSize = 2;
constexpr int OldFormatOwnerEncryptionModeOffset = OldFormatOwnerEncryptionKeyOffset
    + OldFormatOwnerEncryptionKeyCount * OldFormatOwnerEncryptionWordSize;
constexpr quint8 OldFormatOwnerBlockedKeyMode = 0x01;
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
    if (offset < 0 || !containsByteRange(bytes.size(), quint64(offset), 2)) {
        return 0;
    }

    const auto low = static_cast<quint8>(bytes.at(offset));
    const auto high = static_cast<quint8>(bytes.at(offset + 1));
    return static_cast<quint16>(low | (high << 8));
}

QString readNullTerminatedText(const QByteArray& bytes, int offset, int maxLength, LegacyTextEncoding encoding, bool trim = true)
{
    if (offset < 0 || offset >= bytes.size() || maxLength <= 0) {
        return {};
    }

    const int available = std::min(maxLength, static_cast<int>(bytes.size()) - offset);
    int length = 0;
    while (length < available && bytes.at(offset + length) != '\0') {
        ++length;
    }

    const QString text = LegacyTextCodec::decode(QByteArrayView(bytes).sliced(offset, length), encoding);
    return trim ? text.trimmed() : text;
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

bool parseLegacyFieldAt(const QByteArray& schemaRecord, int offset, LegacyField* field, LegacyTextEncoding encoding)
{
    if (field == nullptr || offset < 0 || offset + FieldDefinitionSize > schemaRecord.size()) {
        return false;
    }

    using namespace LegacyTemplateLayoutFormat;
    if (schemaRecord.mid(SchemaMagicOffset, 6) == QByteArrayLiteral("WINDEX")) {
        if (offset < WinIndexFieldTableOffset
            || (offset - WinIndexFieldTableOffset) % WinIndexFieldDescriptorSize != 0
            || !containsByteRange(schemaRecord.size(), quint64(offset), WinIndexFieldDescriptorSize)) return false;
        const QString name = readNullTerminatedText(schemaRecord, offset, WinIndexFieldNameSize, encoding);
        const quint16 flags = readU16(schemaRecord, offset + WinIndexFieldFlagsOffset);
        const quint16 role = readU16(schemaRecord, offset + WinIndexFieldRoleOffset);
        const int length = readU16(schemaRecord, offset + WinIndexFieldLengthOffset);
        const FieldType type = (flags & VariableTextFlag) != 0 ? FieldType::Notes : FieldType::Text;
        if (!isLikelyFieldName(name) || role > StandaloneControlRole || length <= 0
            || (type == FieldType::Text && length > 256) || length > DefaultNotesMaximumLength) return false;
        *field = {name, type, readU16(schemaRecord, offset + WinIndexFieldRecordOffset), length};
        return true;
    }
    const int count = schemaRecord.size() > SchemaFieldCountOffset ? quint8(schemaRecord.at(SchemaFieldCountOffset)) : 0;
    const bool currentDescriptor = schemaRecord.mid(SchemaMagicOffset, 4) == QByteArrayLiteral("TN20")
        && count > 0 && count <= MaxLegacyFields && offset >= FieldTableOffset
        && (offset - FieldTableOffset) % FieldDescriptorSize == 0
        && (offset - FieldTableOffset) / FieldDescriptorSize < count;
    if (currentDescriptor) {
        const QString name = readNullTerminatedText(schemaRecord, offset, LegacyTemplateLayoutFormat::FieldNameSize, encoding);
        const quint16 flags = readU16(schemaRecord, offset + FieldFlagsOffset);
        const quint16 role = readU16(schemaRecord, offset + FieldControlRoleOffset);
        const int recordOffset = readU16(schemaRecord, offset + FieldRecordOffsetOffset);
        const int length = readU16(schemaRecord, offset + FieldDataLengthOffset);
        const FieldType type = (flags & VariableTextFlag) != 0 ? FieldType::Notes : FieldType::Text;
        if (!isLikelyFieldName(name) || role > StandaloneControlRole || length <= 0
            || (type == FieldType::Text && length > 256) || (type == FieldType::Notes && length > 8192)) return false;
        *field = {name, type, recordOffset, length};
        return true;
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

        const QString name = readNullTerminatedText(schemaRecord, offset + layout.nameOffset, FieldNameSize, encoding);
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
    if (schemaRecord.mid(SchemaMagicOffset, 6) == QByteArrayLiteral("WINDEX"))
        return readU16(schemaRecord, WinIndexFieldCountOffset);
    if (schemaRecord.size() <= SchemaFieldCountOffset) {
        return 0;
    }

    const int count = static_cast<quint8>(schemaRecord.at(SchemaFieldCountOffset));
    return count > 0 && count <= MaxLegacyFields ? count : 0;
}

QVector<LegacyField> findFieldDefinitions(const QByteArray& schemaRecord, ImportBudget& budget, LegacyTextEncoding encoding)
{
    const int expectedFieldCount = declaredFieldCount(schemaRecord);
    const auto parseSequence = [&schemaRecord, &budget, encoding](int offset, int expectedCount = 0, bool allowUnorderedOffsets = false, int descriptorSize = FieldDefinitionSize) {
        QVector<LegacyField> candidate;
        int previousRecordOffset = -1;
        for (int fieldIndex = 0; fieldIndex < MaxLegacyFields; ++fieldIndex) {
            if (expectedCount > 0 && fieldIndex >= expectedCount) {
                break;
            }
            if (!budget.checkpoint(FieldDefinitionSize)) return QVector<LegacyField>{};
            LegacyField field;
            const int fieldOffset = offset + fieldIndex * descriptorSize;
            if (!parseLegacyFieldAt(schemaRecord, fieldOffset, &field, encoding)) {
                break;
            }
            if (!allowUnorderedOffsets && field.recordOffset < previousRecordOffset) {
                break;
            }
            previousRecordOffset = field.recordOffset;
            candidate.append(field);
        }
        return candidate;
    };

    if (schemaRecord.mid(SchemaMagicOffset, 6) == QByteArrayLiteral("WINDEX")) {
        if (expectedFieldCount <= 0 || expectedFieldCount > WinIndexMaximumFields) return {};
        const auto fields = parseSequence(WinIndexFieldTableOffset, expectedFieldCount, true, WinIndexFieldDescriptorSize);
        return fields.size() == expectedFieldCount ? fields : QVector<LegacyField>{};
    }

    // Current templates have an explicit descriptor table. Never scan past an invalid field
    // and quietly import only a trailing suffix of a declared schema.
    if (schemaRecord.mid(SchemaMagicOffset, 4) == QByteArrayLiteral("TN20")
        && schemaRecord.size() >= LegacyTemplateLayoutFormat::FieldTableOffset) {
        const auto fields = parseSequence(LegacyTemplateLayoutFormat::FieldTableOffset, expectedFieldCount, true);
        return expectedFieldCount > 0 && fields.size() == expectedFieldCount ? fields : QVector<LegacyField>{};
    }

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

const QByteArray* findSchemaRecord(const QVector<QByteArray>& records, ImportBudget& budget, LegacyTextEncoding encoding)
{
    const QByteArray* best = nullptr;
    int bestFieldCount = 0;
    for (const QByteArray& record : records) {
        if (!budget.checkpoint()) return nullptr;
        const QVector<LegacyField> fields = findFieldDefinitions(record, budget, encoding);
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

int fieldValueByteLimit(const QByteArray& record, const LegacyField& field)
{
    if (field.type != FieldType::Notes) return field.length;
    // The descriptor reserves 40 bytes for notes; the final, NUL-terminated field grows with the record.
    return int(std::max<qsizetype>(0, std::min<qsizetype>(DefaultNotesMaximumLength,
        record.size() - RecordPayloadOffset - field.recordOffset)));
}

QString fieldValueFromRecord(const QByteArray& record, const LegacyField& field, LegacyTextEncoding encoding)
{
    return readNullTerminatedText(record, RecordPayloadOffset + field.recordOffset, fieldValueByteLimit(record, field), encoding, false);
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
    QByteArray ownerName = LegacyOemCodec::encode(normalizeLegacyOwnerPassword(password), 8);
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

bool decryptOldFormatOwnerEncryptedBtrieveBytes(
    QByteArray* bytes, const BtrieveAuditReader::Audit& audit, ImportBudget& budget, QString* error)
{
    if (!bytes || audit.pageSize <= 0 || bytes->size() < audit.pageSize || bytes->size() % audit.pageSize != 0) {
        if (error) *error = QStringLiteral("Encrypted Btrieve file has an unsupported page layout.");
        return false;
    }
    quint16 keys[OldFormatOwnerEncryptionKeyCount] = {};
    const auto wipeKeys = qScopeGuard([&] {
        volatile quint16* destination = keys;
        for (int index = 0; index < OldFormatOwnerEncryptionKeyCount; ++index) destination[index] = 0;
    });
    bool hasKey = false;
    for (int index = 0; index < OldFormatOwnerEncryptionKeyCount; ++index) {
        keys[index] = readU16(*bytes, OldFormatOwnerEncryptionKeyOffset + index * 2);
        hasKey = hasKey || keys[index] != 0;
    }
    if (!hasKey) {
        if (error) *error = QStringLiteral("Encrypted Btrieve file has no usable owner encryption key.");
        return false;
    }
    // WBTRCALL 1080:593e selects quarter-page blocks when verifier byte 8 is odd,
    // otherwise cycling four 16-bit keys after every word (not after each sector).
    const bool blockedKeys = (quint8(bytes->at(OldFormatOwnerEncryptionModeOffset)) & OldFormatOwnerBlockedKeyMode) != 0;
    const int keySpan = blockedKeys ? audit.pageSize / OldFormatOwnerEncryptionKeyCount : OldFormatOwnerEncryptionWordSize;
    for (qsizetype page = audit.pageSize; page < bytes->size(); page += audit.pageSize) {
        if (!budget.checkpoint(quint64(audit.pageSize))) { if (error) *error = budget.error(); return false; }
        for (int offset = 0; offset < audit.pageSize; ++offset) {
            const quint16 key = keys[(offset / keySpan) % OldFormatOwnerEncryptionKeyCount];
            (*bytes)[page + offset] = char(quint8(bytes->at(page + offset)) ^ quint8(key >> ((offset & 1) * 8)));
        }
    }
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
    const QByteArray& bytes,
    const BtrieveAuditReader::Audit& audit,
    ImportBudget& budget, QString* errorMessage)
{
    QVector<QByteArray> records;
    if (audit.pageSize <= 0 || audit.physicalRecordLength <= 0 || audit.fixedRecordLength <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Legacy Btrieve audit metadata is not sufficient for raw record extraction.");
        }
        return records;
    }

    const int recordsPerPage = (audit.pageSize - 2) / audit.physicalRecordLength;
    const int recordLength = std::min(audit.fixedRecordLength, audit.physicalRecordLength);
    for (const BtrieveAuditReader::Page& page : audit.pages) {
        if (page.type != BtrieveAuditReader::PageType::Data) {
            continue;
        }

        const int pageOffset = static_cast<int>(page.offset);
        for (int slot = 0; slot < recordsPerPage; ++slot) {
            if (!budget.checkpoint(quint64(audit.physicalRecordLength))) { if (errorMessage) *errorMessage = budget.error(); return {}; }
            const int recordOffset = pageOffset + OldFormatDataPageHeaderSize + slot * audit.physicalRecordLength;
            if (recordOffset < 0 || recordOffset + audit.physicalRecordLength > bytes.size()) {
                break;
            }
            if (!isOccupiedOldFormatRecordSlot(bytes, recordOffset, audit.physicalRecordLength)) {
                continue;
            }
            if (!budget.row() || !budget.decoded(quint64(recordLength)) || !budget.checkpoint(quint64(recordLength))) {
                if (errorMessage) *errorMessage = budget.error();
                return {};
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
    if (result == nullptr || result->processingAborted || !audit.ok() || !audit.audit.owned) {
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
    } else if (!passwordMatches && !readAccessAllowed) {
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
    return errorMessage.isEmpty() && !processingAborted;
}

LegacyDeckReader::Result LegacyDeckReader::readDeck(
    const QString& filePath, const QString& password, ImportBudget* sharedBudget) const
{
    ImportBudget local(m_limits);
    ImportBudget& budget = sharedBudget ? *sharedBudget : local;
    Result result;
    const auto aborted = [&]() { result.processingAborted = true; result.errorMessage = budget.error(); result.rawRecords.clear(); return result; };
    QByteArray source;
    if (!readBoundedFile(filePath, &source, budget, &result.errorMessage))
        return budget.failed() ? aborted() : result;
    // The same immutable image is authenticated, decrypted and parsed. Never write decrypted pages to disk.
    const auto wipe = qScopeGuard([&] { DeckSecurity::wipe(&source); });
    const BtrieveAuditReader::Result audit = BtrieveAuditReader(m_limits).readBytes(source, &budget);
    if (!audit.ok()) { result.errorMessage = audit.errorMessage; return budget.failed() ? aborted() : result; }
    result.legacyPasswordProtected = audit.audit.owned;
    result.legacyDataEncrypted = audit.audit.ownerDataEncrypted;
    if (audit.audit.owned && !legacyOwnerReadAccessAllowed(audit.audit)) {
        if (normalizeLegacyOwnerPassword(password).isEmpty()) {
            result.passwordRequired = true;
            result.errorMessage = QStringLiteral("Legacy Btrieve deck requires a password.");
            return result;
        }
        if (!legacyOwnerPasswordMatches(audit.audit, password)) {
            result.passwordRejected = true;
            result.errorMessage = QStringLiteral("ACCESS DENIED: Wrong Password");
            return result;
        }
    }
    if (audit.audit.ownerDataEncrypted &&
        !decryptOldFormatOwnerEncryptedBtrieveBytes(&source, audit.audit, budget, &result.errorMessage))
        return budget.failed() ? aborted() : result;

    auto btrieve = BtrieveFileSaverReader(m_limits).readMemory(source, int(MaximumNativeRecordBytes), &budget);
    if (btrieve.processingAborted || budget.failed()) return aborted();
    if (!btrieve.ok()) {
        // Fixed-only recovery cannot reconstruct variable fragments. Never return partial native records.
        btrieve.records.clear();
        if (audit.audit.oldFormat && !audit.audit.ownerDataEncrypted && !audit.audit.variableRecordsAllowed) {
            QString recoveryError;
            btrieve.records = extractOldFormatFixedRecordsFromAudit(source, audit.audit, budget, &recoveryError);
            if (budget.failed()) return aborted();
            if (btrieve.records.isEmpty()) {
                result.errorMessage = recoveryError.isEmpty() ? btrieve.errorMessage : recoveryError;
                return result;
            }
            btrieve.metadata.pageSize = audit.audit.pageSize;
            btrieve.metadata.fixedRecordLength = audit.audit.fixedRecordLength;
            btrieve.metadata.internalFixedRecordLength = audit.audit.physicalRecordLength;
            btrieve.metadata.declaredRecordCount = audit.audit.declaredRecordCount;
        } else { result.errorMessage = btrieve.errorMessage; return result; }
    }
    Result mapped = readRecords(btrieve.records, QFileInfo(filePath).completeBaseName(), password, &budget);
    mapped.btrieveMetadata = btrieve.metadata;
    applyLegacySecurityMetadata(&mapped, audit, password);
    if (mapped.ok()) {
        QString warning;
        PhoneCallLog::importLegacySidecar(filePath, &mapped.deck, &warning, m_limits, &budget);
        if (budget.failed()) return aborted();
        if (!warning.isEmpty()) mapped.warningMessages.append(warning);
    }
    return mapped;
}

LegacyDeckReader::Result LegacyDeckReader::readRecords(
    const QVector<QByteArray>& records,
    const QString& fallbackDeckName,
    const QString& password, ImportBudget* sharedBudget) const
{
    Q_UNUSED(password);
    ImportBudget local(m_limits);
    ImportBudget& budget = sharedBudget ? *sharedBudget : local;

    Result result;
    const auto abort = [&]() { result.processingAborted = true; result.errorMessage = budget.error(); result.rawRecords.clear(); return result; };
    if (!budget.rowsFit(quint64(records.size()))) return abort();
    for (const auto& record : records) {
        if (!budget.cell(quint64(record.size())) || !budget.checkpoint(quint64(record.size())) ||
            (!sharedBudget && !budget.decoded(quint64(record.size())))) return abort();
    }
    result.rawRecords = records;
    if (records.isEmpty()) {
        result.errorMessage = QStringLiteral("Legacy Btrieve deck contains no records.");
        return result;
    }

    const QByteArray* schemaRecord = findSchemaRecord(records, budget, m_encoding);
    if (budget.failed()) return abort();
    if (schemaRecord == nullptr) {
        result.errorMessage = QStringLiteral("Could not locate Legacy field definitions.");
        return result;
    }

    const QVector<LegacyField> fields = findFieldDefinitions(*schemaRecord, budget, m_encoding);
    if (budget.failed() || !budget.fields(quint64(fields.size()))) return abort();
    if (fields.isEmpty()) {
        result.errorMessage = QStringLiteral("Legacy field definition table is empty.");
        return result;
    }

    const auto visualTemplate = LegacyTemplateLayoutFormat::find(records, fields.size(), m_encoding);
    if (!budget.checkpoint()) return abort();
    if (visualTemplate.has_value() && !budget.frames(quint64(visualTemplate->layout.frames.size()))) return abort();
    const QString deckName = readNullTerminatedText(*schemaRecord, SchemaDeckNameOffset, SchemaDeckNameLength, m_encoding);
    Deck deck(deckName.isEmpty() ? fallbackDeckName : deckName);
    deck.setLegacyControlRecord(*schemaRecord);
    deck.setAppearance(LegacyDeckAppearanceFormat::parse(*schemaRecord));
    deck.setDescription(QStringLiteral("Imported from a legacy Btrieve deck."));
    if (visualTemplate.has_value()) {
        for (const FieldDefinition& field : visualTemplate->fields) {
            deck.addField(field);
        }
        deck.setSortKeys(visualTemplate->sortKeys);
        deck.setCardTemplateLayout(visualTemplate->layout);
    } else {
        for (const LegacyField& field : fields) {
            deck.addField(FieldDefinition(field.name, field.type, field.type == FieldType::Notes ? DefaultNotesMaximumLength : field.length));
        }
    }

    const int requiredSize = minimumRecordSize(fields);
    bool unreadableCard = false;
    for (const QByteArray& record : records) {
        if (record == *schemaRecord || isControlRecord(record)) continue;
        if (record.size() < requiredSize) {
            unreadableCard = true;
            continue;
        }

        if (!budget.row() || !budget.checkpoint(quint64(record.size()))) return abort();
        CardRecord card;
        for (const LegacyField& field : fields) {
            if (!budget.value(quint64(fieldValueByteLimit(record, field)) * sizeof(QChar))) return abort();
            card.appendValue(fieldValueFromRecord(record, field, m_encoding));
        }
        deck.addCard(std::move(card));
    }

    if (deck.cardCount() == 0 && unreadableCard) {
        result.errorMessage = QStringLiteral("Legacy field definitions were found, but no card records matched them.");
        return result;
    }

    result.deck = std::move(deck);
    return result;
}

} // namespace CardStack
