# Deck security

CardStack 1.1 supports case-sensitive passwords with spaces and Unicode, up to
1,024 characters. Newly configured passwords are never truncated or uppercased.
Passwords are stored as salted Argon2id verifiers (64 MiB, three passes), not as
plaintext.

Enable **data encryption** to protect the whole saved deck. CardStack uses a
versioned authenticated XChaCha20-Poly1305 container containing the SQLite table
snapshot. Decrypted data stays in an in-memory SQLite database; encrypted saves
do not create a plaintext temporary database. A fresh nonce is generated on each
save. The pinned Monocypher library supplies the cryptographic primitives.

Password-only protection is an application access check: without data encryption,
other SQLite tools can still read the deck. Template/report exports are
unencrypted sharing files and can contain static text and other sensitive
metadata. Deck Save As preserves the deck's security settings.

Existing unencrypted files remain readable. Previous password-protected
CardStack files retain their original comparison rule during migration and use
the new storage representation on save. Remove and add security to set a new,
exact modern password. Keep a backup before upgrading: encrypted 1.1 files are
not readable by CardStack 1.0. After saving any deck with 1.1, use 1.1 or newer:
earlier versions can discard new report/font metadata even from unencrypted files.
Replacing a previously plaintext file cannot
guarantee erasure of older copies, filesystem snapshots, backups, or deleted
disk blocks.

CardStack cannot recover a forgotten encryption password. Encryption does not
protect data while the deck is open against a compromised operating system,
process memory inspection, screenshots, or unencrypted exports.

Saving uses a temporary replacement and atomic commit. CardStack prevents a
second editing instance from opening the same deck and detects changes made
since the document was loaded. Do not edit an open deck with another database
tool; external tools do not participate in CardStack's document locks.

The container and verifier formats are versioned. Unknown storage versions are
rejected without migration or modification. Salt, verifier, nonce, file size,
and format identifiers are not secret; record contents are encrypted.

Cryptographic implementation reference:
[Monocypher authenticated encryption](https://monocypher.org/manual/aead),
[password derivation](https://monocypher.org/manual/argon2).
