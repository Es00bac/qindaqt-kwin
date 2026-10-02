// SPDX-License-Identifier: GPL-2.0-or-later
#include "blankleases.h"
#include <QtTest>
using namespace KWin;
class BlankLeaseTests final : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void releasePreservesSameModeExternalOff()
    {
        BlankLeases state;
        QVERIFY(state.acquire(QStringLiteral(":1.4/idle"), 0, 30000));
        QVERIFY(state.effectiveOff());
        state.externalRequest(true, 100);
        QVERIFY(state.release(QStringLiteral(":1.4/idle"), 200));
        QVERIFY(state.effectiveOff());
        QVERIFY(state.externallyOff());
    }
    void preexistingExternalOffIsNeverRestored()
    {
        BlankLeases state(true);
        QVERIFY(state.acquire(QStringLiteral(":1.4/lid"), 0, 30000));
        state.retireAll(10);
        QVERIFY(state.effectiveOff());
        QVERIFY(state.ids().isEmpty());
    }
    void independentCausesAndExpiryRestoreOnlyOwnedBaseline()
    {
        BlankLeases state;
        QVERIFY(state.acquire(QStringLiteral(":1.4/idle"), 0, 1000));
        QVERIFY(state.acquire(QStringLiteral(":1.4/lid"), 0, 2000));
        state.expire(1000);
        QCOMPARE(state.ids(), QStringList{QStringLiteral(":1.4/lid")});
        QVERIFY(state.effectiveOff());
        state.expire(2000);
        QVERIFY(!state.effectiveOff());
        QVERIFY(!state.acquire(QStringLiteral(":1.4/idle"), 2001, 1000));
    }
    void wakeConsumesClosedEpisodeAndLateRenewal()
    {
        BlankLeases state;
        QVERIFY(state.acquire(QStringLiteral(":1.4/lid"), 0, 30000));
        state.externalRequest(false, 100);
        QVERIFY(!state.effectiveOff());
        QVERIFY(!state.acquire(QStringLiteral(":1.4/lid"), 101, 30000));
        QVERIFY(state.acquire(QStringLiteral(":1.4/new-close"), 102, 30000));
    }
    void cancelBeforeAcquireAndFiniteCapacityFailClosed()
    {
        BlankLeases state;
        QVERIFY(!state.release(QStringLiteral(":1.4/late"), 0));
        QVERIFY(!state.acquire(QStringLiteral(":1.4/late"), 1, 1000));
        QVERIFY(!state.acquire(QStringLiteral("bad-lifetime"), 1, 60001));
        for (int i = 0; i < 64; ++i) QVERIFY(state.acquire(QString::number(i), 1, 1000));
        QVERIFY(!state.acquire(QStringLiteral("overflow"), 1, 1000));
        QVERIFY(state.acquire(QStringLiteral("0"), 2, 30000));
        state.retireAll(3);
        QVERIFY(!state.effectiveOff());
    }
};
QTEST_GUILESS_MAIN(BlankLeaseTests)
#include "blankleases_test.moc"
