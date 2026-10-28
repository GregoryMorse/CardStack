#pragma once

#include <QByteArray>
#include <QString>
#include <array>
#include <cstdint>
#include <memory>

namespace CardStack::DeckSecurity {

inline constexpr int MaximumPasswordCharacters = 1024;
inline constexpr qsizetype MaximumContainerBytes = 512 * 1024 * 1024;

struct SecretKey {
    std::array<uint8_t, 32> bytes{};
    ~SecretKey();
};

using Key = std::shared_ptr<SecretKey>;
struct Credentials {
    QString record;
    Key key;
};

enum class PasswordPolicy { Exact, PreviousEightCharacter };
enum class Error { None, PasswordRequired, WrongPassword, InvalidFormat, AuthenticationFailed };

Credentials createCredentials(const QString& password, PasswordPolicy policy = PasswordPolicy::Exact);
bool unlock(const QString& record, const QString& password, Key* key = nullptr);
bool isPasswordRecord(const QString& record);
bool isEncryptedContainer(const QByteArray& bytes);
QByteArray encrypt(const QByteArray& plaintext, const QString& record, const Key& key);
bool decrypt(const QByteArray& container, const QString& password, QByteArray* plaintext,
             Credentials* credentials, Error* error);
void wipe(QByteArray* bytes);

} // namespace CardStack::DeckSecurity
