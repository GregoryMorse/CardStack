#include "PhoneticSearch.h"

#include <QTest>

namespace CardStack {

class PhoneticSearchTests : public QObject {
    Q_OBJECT

private slots:
    void matchesLegacySoundsLikePairs()
    {
        QCOMPARE(soundsLikeKey(QStringLiteral("Smith")), soundsLikeKey(QStringLiteral("Smyth")));
        QCOMPARE(soundsLikeKey(QStringLiteral("Knight")), soundsLikeKey(QStringLiteral("nite")));
        QCOMPARE(soundsLikeKey(QStringLiteral("Wright")), soundsLikeKey(QStringLiteral("right")));
        QCOMPARE(soundsLikeKey(QStringLiteral("Xavier")), soundsLikeKey(QStringLiteral("Savior")));
    }

    void keepsDifferentWordsApart()
    {
        QVERIFY(soundsLikeKey(QStringLiteral("CardStack")) != soundsLikeKey(QStringLiteral("ProjectKit")));
        QVERIFY(soundsLikeKey(QStringLiteral("report")) != soundsLikeKey(QStringLiteral("phone")));
    }
};

} // namespace CardStack

#ifdef CARDSTACK_TEST_SUITE
int runPhoneticSearchTests(int argc, char** argv)
{
    CardStack::PhoneticSearchTests test;
    return QTest::qExec(&test, argc, argv);
}
#else
QTEST_GUILESS_MAIN(CardStack::PhoneticSearchTests)
#endif

#include "PhoneticSearchTests.moc"

