#include "BtrieveAuditReader.h"

#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace CardStack {
namespace {

constexpr int FcrMinimumSize = 0x110;
constexpr int OldFormatKeySpecOffset = 0x110;
constexpr int OldFormatKeySpecSize = 0x1e;
constexpr int OldFormatOwnerVerificationOffset = 0x2e;
constexpr int OldFormatOwnerVerificationLength = 9;
constexpr int OldFormatOwnerFlagsOffset = 0x37;
constexpr quint8 OldFormatOwnerDataEncryptedFlag = 0x02;
constexpr quint16 OldFormatDataPageFlag = 0x8000;
constexpr int OldFormatDataPageHeaderSize = 6;
constexpr int OldFormatEmptyRecordScanOffset = 8;

quint16 readU16(const QByteArray& bytes, int offset)
{
    if (offset < 0 || offset + 1 >= bytes.size()) {
        return 0;
    }

    return static_cast<quint16>(
        static_cast<quint8>(bytes.at(offset)) |
        (static_cast<quint8>(bytes.at(offset + 1)) << 8));
}

quint32 readU32(const QByteArray& bytes, int offset)
{
    if (offset < 0 || offset + 3 >= bytes.size()) {
        return 0;
    }

    return static_cast<quint32>(readU16(bytes, offset)) |
        (static_cast<quint32>(readU16(bytes, offset + 2)) << 16);
}

quint32 readWordSwappedU32(const QByteArray& bytes, int offset)
{
    const quint32 value = readU32(bytes, offset);
    return ((value >> 16) & 0xffffU) | ((value & 0xffffU) << 16);
}

quint32 readBestCountU32(const QByteArray& bytes, int offset, quint32 sanityLimit)
{
    const quint32 littleEndian = readU32(bytes, offset);
    const quint32 wordSwapped = readWordSwappedU32(bytes, offset);
    if (littleEndian <= sanityLimit && wordSwapped <= sanityLimit) {
        return std::min(littleEndian, wordSwapped);
    }

    if (littleEndian <= sanityLimit) {
        return littleEndian;
    }

    return wordSwapped <= sanityLimit ? wordSwapped : littleEndian;
}

QString readFixedAscii(const QByteArray& bytes, int offset, int length)
{
    if (offset < 0 || offset >= bytes.size() || length <= 0) {
        return {};
    }

    const int available = std::min(length, static_cast<int>(bytes.size()) - offset);
    int actualLength = 0;
    while (actualLength < available && bytes.at(offset + actualLength) != '\0') {
        ++actualLength;
    }
    return QString::fromLatin1(bytes.constData() + offset, actualLength).trimmed();
}

quint32 readOldFormatPageNumber(const QByteArray& bytes, int offset)
{
    const quint32 high = readU16(bytes, offset);
    const quint32 low = readU16(bytes, offset + 2);
    return (high << 16) | low;
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

int occupiedFixedSlots(const QByteArray& bytes, const BtrieveAuditReader::Audit& audit)
{
    if (audit.pageSize <= 0 || audit.physicalRecordLength <= 0) {
        return 0;
    }

    const int recordsPerPage = (audit.pageSize - 2) / audit.physicalRecordLength;
    int occupied = 0;
    for (const BtrieveAuditReader::Page& page : audit.pages) {
        if (page.type != BtrieveAuditReader::PageType::Data) {
            continue;
        }

        const int pageOffset = static_cast<int>(page.offset);
        for (int slot = 0; slot < recordsPerPage; ++slot) {
            const int recordOffset = pageOffset + OldFormatDataPageHeaderSize + slot * audit.physicalRecordLength;
            if (recordOffset + audit.physicalRecordLength > bytes.size()) {
                break;
            }
            if (isOccupiedOldFormatRecordSlot(bytes, recordOffset, audit.physicalRecordLength)) {
                ++occupied;
            }
        }
    }
    return occupied;
}

BtrieveAuditReader::KeySpec readOldFormatKeySpec(const QByteArray& bytes, int index)
{
    const int offset = OldFormatKeySpecOffset + index * OldFormatKeySpecSize;
    BtrieveAuditReader::KeySpec spec;
    spec.index = index;
    spec.fcrOffset = offset;
    spec.rawEntryPointer = readU32(bytes, offset);
    spec.rootPage = readOldFormatPageNumber(bytes, offset);
    spec.uniqueValueCount = readBestCountU32(bytes, offset + 4, 0x00ffffffU);
    spec.flags = readU16(bytes, offset + 8);
    spec.totalSegmentLength = readU16(bytes, offset + 10);
    spec.keyLength = readU16(bytes, offset + 12);
    spec.maxItemsPerPage = readU16(bytes, offset + 14);
    spec.minItemsPerPage = readU16(bytes, offset + 16);
    spec.duplicatePointerOffset = readU16(bytes, offset + 18);
    spec.recordOffset = readU16(bytes, offset + 20);
    spec.segmentLength = readU16(bytes, offset + 22);
    return spec;
}

} // namespace

bool BtrieveAuditReader::Result::ok() const
{
    return errorMessage.isEmpty();
}

BtrieveAuditReader::Result BtrieveAuditReader::readFile(const QString& filePath) const
{
    Result result;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.errorMessage = QStringLiteral("Could not open Btrieve file: %1").arg(filePath);
        return result;
    }

    const QByteArray bytes = file.readAll();
    if (bytes.size() < FcrMinimumSize) {
        result.errorMessage = QStringLiteral("File is too small to contain a Btrieve FCR.");
        return result;
    }

    Audit audit;
    const quint32 fcrPageId = readU32(bytes, 0);
    audit.oldFormat = fcrPageId == 0;
    if (!audit.oldFormat) {
        result.errorMessage = QStringLiteral("Only old-format Btrieve files are currently audited.");
        return result;
    }

    const qint16 signedVersion = static_cast<qint16>(readU16(bytes, 6));
    audit.owned = signedVersion < 0;
    audit.version = std::abs(static_cast<int>(signedVersion));
    audit.ownerFlags = static_cast<quint8>(bytes.at(OldFormatOwnerFlagsOffset));
    audit.ownerDataEncrypted = audit.owned && (audit.ownerFlags & OldFormatOwnerDataEncryptedFlag) != 0;
    audit.ownerVerificationBytes = bytes.mid(OldFormatOwnerVerificationOffset, OldFormatOwnerVerificationLength);
    audit.pageSize = readU16(bytes, 8);
    audit.keyCount = readU16(bytes, 0x14);
    audit.fixedRecordLength = readU16(bytes, 0x16);
    audit.physicalRecordLength = readU16(bytes, 0x18);
    audit.declaredRecordCount = static_cast<int>(readWordSwappedU32(bytes, 0x1a));
    audit.consistentFlag = readU16(bytes, 0x22);
    audit.variableRecordsAllowed = static_cast<quint8>(bytes.at(0x38)) != 0;
    audit.acsName = readFixedAscii(bytes, 0x3c, 8);
    audit.userFlags = readU16(bytes, 0x106);

    if (audit.pageSize <= 0 || bytes.size() < audit.pageSize || bytes.size() % audit.pageSize != 0) {
        result.errorMessage = QStringLiteral("Invalid or unsupported Btrieve page size.");
        return result;
    }
    if (audit.physicalRecordLength <= 0) {
        result.errorMessage = QStringLiteral("Invalid Btrieve physical record length.");
        return result;
    }

    audit.pageCount = bytes.size() / audit.pageSize;
    for (int pageIndex = 0; pageIndex < audit.pageCount; ++pageIndex) {
        const int pageOffset = pageIndex * audit.pageSize;
        Page page;
        page.index = pageIndex;
        page.offset = pageOffset;
        page.pageNumber = readOldFormatPageNumber(bytes, pageOffset);
        page.usageCount = readU16(bytes, pageOffset + 4);
        if (pageIndex == 0) {
            page.type = PageType::FileControlRecord;
        } else if ((page.usageCount & OldFormatDataPageFlag) != 0) {
            page.type = PageType::Data;
            page.usageCount = static_cast<quint16>(page.usageCount & ~OldFormatDataPageFlag);
        }
        audit.pages.append(page);
    }

    const int keyCount = std::clamp(audit.keyCount, 0, 119);
    for (int index = 0; index < keyCount; ++index) {
        const int keyOffset = OldFormatKeySpecOffset + index * OldFormatKeySpecSize;
        if (keyOffset + OldFormatKeySpecSize > bytes.size()) {
            break;
        }
        audit.keySpecs.append(readOldFormatKeySpec(bytes, index));
    }

    audit.occupiedFixedRecordSlots = occupiedFixedSlots(bytes, audit);
    result.audit = audit;
    return result;
}

} // namespace CardStack
