#include "DeckSecurity.h"

#include <QRandomGenerator>
#include <QStringList>
#include <monocypher.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace CardStack::DeckSecurity {
namespace {
constexpr uint32_t PasswordMemoryBlocks = 65536;
constexpr uint32_t PasswordPasses = 3;
constexpr int SaltBytes = 16;
constexpr int NonceBytes = 24;
constexpr int TagBytes = 16;
constexpr int MaximumRecordBytes = 256;
const QByteArray ContainerMagic("CSENC01\n", 8);

struct ParsedRecord {
    PasswordPolicy policy = PasswordPolicy::Exact;
    QByteArray salt;
    QByteArray verifier;
};

QByteArray randomBytes(int size)
{
    QByteArray result(size, '\0');
    for (int offset = 0; offset < size; offset += sizeof(quint32)) {
        const quint32 random = QRandomGenerator::system()->generate();
        std::memcpy(result.data() + offset, &random,
                    std::min<int>(sizeof(random), size - offset));
    }
    return result;
}

bool parseRecord(const QString& record, ParsedRecord* parsed)
{
    const QStringList parts = record.split(QLatin1Char('$'));
    if (parts.size() != 6 || parts[0] != QLatin1String("cs1")
        || (parts[1] != QLatin1String("exact") && parts[1] != QLatin1String("previous8"))
        || parts[2] != QString::number(PasswordMemoryBlocks)
        || parts[3] != QString::number(PasswordPasses)
        || parts[4].size() != SaltBytes * 2 || parts[5].size() != 64) {
        return false;
    }
    parsed->salt = QByteArray::fromHex(parts[4].toLatin1());
    parsed->verifier = QByteArray::fromHex(parts[5].toLatin1());
    if (parsed->salt.toHex() != parts[4].toLatin1()
        || parsed->verifier.toHex() != parts[5].toLatin1()) {
        return false;
    }
    parsed->policy = parts[1] == QLatin1String("previous8")
        ? PasswordPolicy::PreviousEightCharacter : PasswordPolicy::Exact;
    return true;
}

bool derive(const QString& password, const QByteArray& salt, PasswordPolicy policy,
            std::array<uint8_t, 64>* result)
{
    if (password.isEmpty() || password.size() > MaximumPasswordCharacters || salt.size() != SaltBytes) {
        return false;
    }
    QByteArray encoded = (policy == PasswordPolicy::PreviousEightCharacter
        ? password.left(8).toUpper() : password).toUtf8();
    void* work = std::malloc(static_cast<size_t>(PasswordMemoryBlocks) * 1024);
    if (work == nullptr) {
        wipe(&encoded);
        return false;
    }
    const crypto_argon2_config config{CRYPTO_ARGON2_ID, PasswordMemoryBlocks, PasswordPasses, 1};
    const crypto_argon2_inputs inputs{
        reinterpret_cast<const uint8_t*>(encoded.constData()),
        reinterpret_cast<const uint8_t*>(salt.constData()),
        static_cast<uint32_t>(encoded.size()), static_cast<uint32_t>(salt.size())};
    crypto_argon2(result->data(), static_cast<uint32_t>(result->size()), work, config, inputs, {});
    std::free(work); // Argon2 wipes its work area.
    wipe(&encoded);
    return true;
}
} // namespace

SecretKey::~SecretKey()
{
    crypto_wipe(bytes.data(), bytes.size());
}

void wipe(QByteArray* bytes)
{
    if (bytes != nullptr && !bytes->isEmpty()) {
        crypto_wipe(bytes->data(), static_cast<size_t>(bytes->size()));
        bytes->clear();
    }
}

Credentials createCredentials(const QString& password, PasswordPolicy policy)
{
    Credentials result;
    const QByteArray salt = randomBytes(SaltBytes);
    std::array<uint8_t, 64> derived{};
    if (!derive(password, salt, policy, &derived)) {
        return result;
    }
    result.key = std::make_shared<SecretKey>();
    std::copy_n(derived.begin(), result.key->bytes.size(), result.key->bytes.begin());
    const QByteArray verifier(reinterpret_cast<const char*>(derived.data() + 32), 32);
    result.record = QStringLiteral("cs1$%1$%2$%3$%4$%5")
        .arg(policy == PasswordPolicy::Exact ? QStringLiteral("exact") : QStringLiteral("previous8"))
        .arg(PasswordMemoryBlocks).arg(PasswordPasses)
        .arg(QString::fromLatin1(salt.toHex()), QString::fromLatin1(verifier.toHex()));
    crypto_wipe(derived.data(), derived.size());
    return result;
}

bool isPasswordRecord(const QString& record)
{
    ParsedRecord parsed;
    return parseRecord(record, &parsed);
}

bool unlock(const QString& record, const QString& password, Key* key)
{
    ParsedRecord parsed;
    std::array<uint8_t, 64> derived{};
    if (!parseRecord(record, &parsed) || !derive(password, parsed.salt, parsed.policy, &derived)) {
        return false;
    }
    const bool matches = crypto_verify32(derived.data() + 32,
        reinterpret_cast<const uint8_t*>(parsed.verifier.constData())) == 0;
    if (matches && key != nullptr) {
        *key = std::make_shared<SecretKey>();
        std::copy_n(derived.begin(), (*key)->bytes.size(), (*key)->bytes.begin());
    }
    crypto_wipe(derived.data(), derived.size());
    return matches;
}

bool isEncryptedContainer(const QByteArray& bytes)
{
    return bytes.startsWith(ContainerMagic);
}

QByteArray encrypt(const QByteArray& plaintext, const QString& record, const Key& key)
{
    const QByteArray encodedRecord = record.toUtf8();
    if (!key || !isPasswordRecord(record) || encodedRecord.size() > MaximumRecordBytes
        || plaintext.size() > MaximumContainerBytes) {
        return {};
    }
    const QByteArray nonce = randomBytes(NonceBytes);
    QByteArray header = ContainerMagic;
    header.append(static_cast<char>(encodedRecord.size() & 0xff));
    header.append(static_cast<char>((encodedRecord.size() >> 8) & 0xff));
    header.append(encodedRecord);
    header.append(nonce);
    QByteArray tag(TagBytes, '\0');
    QByteArray ciphertext(plaintext.size(), '\0');
    crypto_aead_lock(reinterpret_cast<uint8_t*>(ciphertext.data()),
        reinterpret_cast<uint8_t*>(tag.data()), key->bytes.data(),
        reinterpret_cast<const uint8_t*>(nonce.constData()),
        reinterpret_cast<const uint8_t*>(header.constData()), static_cast<size_t>(header.size()),
        reinterpret_cast<const uint8_t*>(plaintext.constData()), static_cast<size_t>(plaintext.size()));
    return header + tag + ciphertext;
}

bool decrypt(const QByteArray& container, const QString& password, QByteArray* plaintext,
             Credentials* credentials, Error* error)
{
    const auto fail = [error](Error value) {
        if (error != nullptr) *error = value;
        return false;
    };
    if (!isEncryptedContainer(container) || container.size() < ContainerMagic.size() + 2
        || container.size() > MaximumContainerBytes + 1024 || plaintext == nullptr || credentials == nullptr) {
        return fail(Error::InvalidFormat);
    }
    const int lengthOffset = ContainerMagic.size();
    const int recordSize = static_cast<quint8>(container[lengthOffset])
        | (static_cast<quint8>(container[lengthOffset + 1]) << 8);
    const int nonceOffset = lengthOffset + 2 + recordSize;
    const int tagOffset = nonceOffset + NonceBytes;
    const int textOffset = tagOffset + TagBytes;
    if (recordSize < 1 || recordSize > MaximumRecordBytes || textOffset > container.size()) {
        return fail(Error::InvalidFormat);
    }
    const QString record = QString::fromUtf8(container.constData() + lengthOffset + 2, recordSize);
    if (!isPasswordRecord(record)) {
        return fail(Error::InvalidFormat);
    }
    if (password.isNull()) {
        return fail(Error::PasswordRequired);
    }
    Key key;
    if (!unlock(record, password, &key)) {
        return fail(Error::WrongPassword);
    }
    QByteArray decoded(container.size() - textOffset, '\0');
    if (crypto_aead_unlock(reinterpret_cast<uint8_t*>(decoded.data()),
            reinterpret_cast<const uint8_t*>(container.constData() + tagOffset),
            key->bytes.data(),
            reinterpret_cast<const uint8_t*>(container.constData() + nonceOffset),
            reinterpret_cast<const uint8_t*>(container.constData()), tagOffset,
            reinterpret_cast<const uint8_t*>(container.constData() + textOffset),
            static_cast<size_t>(decoded.size())) != 0) {
        return fail(Error::AuthenticationFailed);
    }
    *plaintext = std::move(decoded);
    *credentials = {record, std::move(key)};
    if (error != nullptr) *error = Error::None;
    return true;
}

} // namespace CardStack::DeckSecurity
