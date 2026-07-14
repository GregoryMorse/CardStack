#include "PhoneCallLog.h"
#include "SQLiteDeckStore.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace CardStack;

class PhoneCallLogTests : public QObject {
    Q_OBJECT

private slots:
    void importsPersistsAndExportsLegacySidecarLosslessly()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString deckPath = directory.filePath(QStringLiteral("Contacts.BTN"));
        const QString logPath = directory.filePath(QStringLiteral("contacts.log"));
        const QByteArray legacyLine = QByteArray("01/02/24,03:04,555-0100,Alpha,Beta,")
            + QByteArray(1, static_cast<char>(0x80)) + QByteArray("uro");

        QFile log(logPath);
        QVERIFY(log.open(QIODevice::WriteOnly));
        QCOMPARE(log.write(legacyLine + QByteArray("\r\n")), legacyLine.size() + 2);
        log.close();

        Deck deck(QStringLiteral("Contacts"));
        QString warning;
        QCOMPARE(PhoneCallLog::importLegacySidecar(deckPath, &deck, &warning), 1);
        QVERIFY2(warning.isEmpty(), qPrintable(warning));
        QCOMPARE(deck.phoneCallLogEntryCount(), 1);
        QCOMPARE(deck.phoneCallLogEntries().first().phoneNumber, QStringLiteral("555-0100"));
        QCOMPARE(deck.phoneCallLogEntries().first().cardSummaryValues.at(2), QString::fromUtf8("\xe2\x82\xacuro"));
        QCOMPARE(deck.phoneCallLogEntries().first().rawLegacyBytes, legacyLine);

        Deck explicitlyImported;
        QCOMPARE(PhoneCallLog::importLegacyFile(logPath, &explicitlyImported, &warning), 1);
        QCOMPARE(explicitlyImported.phoneCallLogEntries(), deck.phoneCallLogEntries());

        const QString databasePath = directory.filePath(QStringLiteral("contacts.cardstack"));
        QString error;
        SQLiteDeckStore store;
        QVERIFY2(store.open(databasePath, &error), qPrintable(error));
        QVERIFY2(store.saveDeck(deck, &error), qPrintable(error));
        Deck loaded;
        QVERIFY2(store.loadDeck(&loaded, &error), qPrintable(error));
        QCOMPARE(loaded.phoneCallLogEntries(), deck.phoneCallLogEntries());

        const QString exportedPath = directory.filePath(QStringLiteral("exported.LOG"));
        QVERIFY2(PhoneCallLog::writeLegacyFile(loaded, exportedPath, &error), qPrintable(error));
        QFile exported(exportedPath);
        QVERIFY(exported.open(QIODevice::ReadOnly));
        QCOMPARE(exported.readAll(), legacyLine + QByteArray("\r\n"));
    }

    void exportsModernEntriesWithCsvEscaping()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Deck deck(QStringLiteral("Calls"));
        PhoneCallLogEntry entry;
        entry.calledAtUtc = QStringLiteral("2024-01-02T03:04:00.000Z");
        entry.phoneNumber = QStringLiteral("555-0101");
        entry.cardSummaryValues = {QStringLiteral("Smith, Jane"), QStringLiteral("Sales"), QStringLiteral("Follow-up")};
        deck.addPhoneCallLogEntry(entry);

        const QString path = directory.filePath(QStringLiteral("calls.LOG"));
        QString error;
        QVERIFY2(PhoneCallLog::writeLegacyFile(deck, path, &error), qPrintable(error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray output = file.readAll();
        QVERIFY(output.contains("555-0101"));
        QVERIFY(output.contains("\"Smith, Jane\""));
        QVERIFY(output.endsWith("\r\n"));
    }

    void removesSingleAndMultipleEntriesByIndex()
    {
        Deck deck(QStringLiteral("Calls"));
        for (int index = 0; index < 4; ++index) {
            PhoneCallLogEntry entry;
            entry.phoneNumber = QString::number(index);
            deck.addPhoneCallLogEntry(entry);
        }

        QCOMPARE(deck.removePhoneCallLogEntries({1}), 1);
        QCOMPARE(deck.phoneCallLogEntryCount(), 3);
        QCOMPARE(deck.phoneCallLogEntries().at(1).phoneNumber, QStringLiteral("2"));

        QCOMPARE(deck.removePhoneCallLogEntries({2, 0, 2, -1, 99}), 2);
        QCOMPARE(deck.phoneCallLogEntryCount(), 1);
        QCOMPARE(deck.phoneCallLogEntries().first().phoneNumber, QStringLiteral("2"));
    }
};

#ifdef CARDSTACK_TEST_SUITE
int runPhoneCallLogTests(int argc, char** argv)
{
    PhoneCallLogTests test;
    return QTest::qExec(&test, argc, argv);
}
#else
QTEST_GUILESS_MAIN(PhoneCallLogTests)
#endif

#include "PhoneCallLogTests.moc"
