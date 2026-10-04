#include "bands.h"
#include "engine.h"
#include "fakecontroller.h"
#include "logbus.h"
#include "testutil.h"

#include <QSignalSpy>
#include <QTest>

using testutil::Rigctld;

namespace {

constexpr int Slow = 8000;

Config exampleConfig(quint16 rigPort, const QString &ctlHost, quint16 ctlPort)
{
    Config c;
    c.rigHost = QStringLiteral("127.0.0.1");
    c.rigPort = rigPort;
    c.pollMs = 200;
    c.controllerHost = ctlHost;
    c.controllerPort = ctlPort;
    for (const QString &band : Bands::names())
        c.routes[band] = {};
    c.routes[QStringLiteral("20m")] = {1, 2}; // the example from the request
    c.routes[QStringLiteral("40m")] = {2, 3};
    c.routes[QStringLiteral("30m")] = {2, 1}; // swap of 20m
    c.routes[QStringLiteral("15m")] = {Config::Keep, 4};
    c.routes[QStringLiteral("17m")] = {Config::Off, 5};
    return c;
}

}

class TestEngine : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        LogBus::instance().setEcho(qEnvironmentVariableIsSet("ECH_LOG"));
        if (!Rigctld::available())
            QSKIP("rigctld not installed");
    }

    void followsBands()
    {
        const quint16 rigPort = testutil::freePort();
        Rigctld rig(rigPort);
        QVERIFY(rig.start()); // dummy radio starts on 145 MHz: no band
        FakeController fake;
        QVERIFY(fake.listen(QHostAddress::LocalHost, 0));
        fake.setPorts(6, 5);

        Config cfg = exampleConfig(rigPort, QStringLiteral("127.0.0.1"), fake.port());
        Engine engine(cfg);
        engine.start();
        QTRY_VERIFY(engine.rig().isOnline());
        QTRY_COMPARE(engine.controller().link(), EasyController::Link::Connected);
        QTest::qWait(600);
        QVERIFY(engine.band().isEmpty());
        QCOMPARE(fake.a(), 6); // nothing touched outside the bands
        QCOMPARE(fake.b(), 5);

        auto expect = [&](qint64 hz, const char *band, int a, int b) {
            QVERIFY(rig.setFrequency(hz));
            QTRY_COMPARE(engine.band(), QString::fromLatin1(band));
            QTRY_COMPARE(engine.status(), Engine::Status::Ok);
            QCOMPARE(fake.a(), a);
            QCOMPARE(fake.b(), b);
        };
        expect(14'074'000, "20m", 1, 2);
        expect(7'074'000, "40m", 2, 3);
        expect(14'200'000, "20m", 1, 2);
        expect(10'136'000, "30m", 2, 1);
        expect(21'074'000, "15m", 2, 4);
        expect(18'100'000, "17m", 0, 5);
        QCOMPARE(engine.statusText(), QStringLiteral("TRX A: off, TRX B: ANT5"));

        // Between bands nothing changes; back on 17m is no band change.
        fake.clearCommands();
        QVERIFY(rig.setFrequency(19'000'000));
        QTest::qWait(600);
        QVERIFY(rig.setFrequency(18'110'000));
        QTest::qWait(600);
        for (const QString &c : fake.commands())
            QVERIFY2(!c.startsWith(QLatin1Char('X')), qPrintable(c));

        // An unmapped band leaves the antennas alone.
        QVERIFY(rig.setFrequency(28'074'000));
        QTRY_COMPARE(engine.band(), QStringLiteral("10m"));
        QTRY_COMPARE(engine.status(), Engine::Status::Idle);
        QCOMPARE(fake.a(), 0);
        QCOMPARE(fake.b(), 5);

        // Follow off: band changes are shown but not switched; Apply now switches.
        engine.setFollow(false);
        QVERIFY(rig.setFrequency(14'074'000));
        QTRY_COMPARE(engine.band(), QStringLiteral("20m"));
        QTest::qWait(400);
        QCOMPARE(fake.a(), 0);
        engine.applyNow();
        QTRY_COMPARE(engine.status(), Engine::Status::Ok);
        QCOMPARE(fake.a(), 1);
        QCOMPARE(fake.b(), 2);
    }

    // The radio changes band again while the controller is still switching: the newer band wins.
    void bandChangeDuringSwitch()
    {
        const quint16 rigPort = testutil::freePort();
        Rigctld rig(rigPort);
        QVERIFY(rig.start());
        FakeController fake; // state not pushed: each step waits for a poll
        QVERIFY(fake.listen(QHostAddress::LocalHost, 0));
        fake.setPorts(6, 5);
        Config cfg = exampleConfig(rigPort, QStringLiteral("127.0.0.1"), fake.port());
        Engine engine(cfg);
        engine.start();
        QTRY_COMPARE(engine.controller().link(), EasyController::Link::Connected);

        bool moved = false;
        connect(&fake, &FakeController::commandReceived, this, [&](const QString &c) {
            if (!moved && c.startsWith(QLatin1Char('X'))) {
                moved = true;
                QVERIFY(rig.setFrequency(7'074'000));
            }
        });
        QVERIFY(rig.setFrequency(14'074'000));
        QTRY_VERIFY(moved);
        QTRY_COMPARE(engine.band(), QStringLiteral("40m"));
        QTRY_COMPARE_WITH_TIMEOUT(engine.status(), Engine::Status::Ok, Slow);
        QCOMPARE(fake.a(), 2);
        QCOMPARE(fake.b(), 3);
    }

    // A front panel change is noticed, pushed or only seen by polling; Apply now puts the table back.
    void noticesDifferenceFromTable()
    {
        const quint16 rigPort = testutil::freePort();
        Rigctld rig(rigPort);
        QVERIFY(rig.start());
        QVERIFY(rig.setFrequency(14'074'000));
        FakeController fake;
        QVERIFY(fake.listen(QHostAddress::LocalHost, 0));
        Config cfg = exampleConfig(rigPort, QStringLiteral("127.0.0.1"), fake.port());
        Engine engine(cfg);
        engine.start();
        QTRY_COMPARE(engine.status(), Engine::Status::Ok);
        QVERIFY(engine.differences().isEmpty());

        fake.setPorts(3, 2); // pushed
        QTRY_COMPARE(engine.differences(), QStringList{QStringLiteral("TRX A should be ANT1")});
        engine.applyNow();
        QTRY_VERIFY(engine.differences().isEmpty());
        QCOMPARE(fake.a(), 1);

        fake.setPorts(1, 4, false); // only the poll finds it
        QTRY_COMPARE_WITH_TIMEOUT(engine.differences(), QStringList{QStringLiteral("TRX B should be ANT2")}, 5000);
        QCOMPARE(fake.a(), 1); // not switched back on its own
        QCOMPARE(fake.b(), 4);

        // "unchanged" in the table is never a difference.
        fake.setPorts(1, 2);
        QTRY_VERIFY(engine.differences().isEmpty());
        QVERIFY(rig.setFrequency(21'074'000)); // 15m: TRX A unchanged, TRX B ANT4
        QTRY_COMPARE(engine.status(), Engine::Status::Ok);
        fake.setPorts(6, 4);
        QTest::qWait(300);
        QVERIFY(engine.differences().isEmpty());
    }

    void waitsForReceive()
    {
        const quint16 rigPort = testutil::freePort();
        Rigctld rig(rigPort);
        QVERIFY(rig.start());
        QVERIFY(rig.setFrequency(14'074'000));
        FakeController fake;
        QVERIFY(fake.listen(QHostAddress::LocalHost, 0));
        Config cfg = exampleConfig(rigPort, QStringLiteral("127.0.0.1"), fake.port());
        Engine engine(cfg);
        engine.start();
        QTRY_COMPARE(engine.status(), Engine::Status::Ok);
        QCOMPARE(fake.a(), 1);

        // The radio transmits while it changes band: switching waits for receive.
        QVERIFY(rig.setPtt(true));
        QTRY_VERIFY(engine.rig().ptt());
        QVERIFY(rig.setFrequency(7'074'000));
        QTRY_COMPARE(engine.band(), QStringLiteral("40m"));
        QTRY_VERIFY(engine.statusText().contains(QStringLiteral("Waiting for receive")));
        QTest::qWait(500);
        QCOMPARE(fake.a(), 1);
        QVERIFY(rig.setPtt(false));
        QTRY_COMPARE(engine.status(), Engine::Status::Ok);
        QCOMPARE(fake.a(), 2);
        QCOMPARE(fake.b(), 3);

        // The controller reports PTT on a bank: same.
        fake.setPtt(2, true);
        QTRY_VERIFY(engine.controller().ptt(2));
        QVERIFY(rig.setFrequency(14'074'000));
        QTRY_VERIFY(engine.statusText().contains(QStringLiteral("Waiting for receive")));
        QTest::qWait(500);
        QCOMPARE(fake.a(), 2);
        fake.setPtt(2, false);
        QTRY_COMPARE(engine.status(), Engine::Status::Ok);
        QCOMPARE(fake.a(), 1);
        QCOMPARE(fake.b(), 2);
    }

    // The stored address stops answering: one search, the new address is stored and used.
    void findsMovedController()
    {
        const quint16 rigPort = testutil::freePort();
        Rigctld rig(rigPort);
        QVERIFY(rig.start());
        const quint16 ctlPort = testutil::freePort();
        FakeController fake;
        QVERIFY(fake.listen(QHostAddress(QStringLiteral("127.0.0.2")), ctlPort));

        Config cfg = exampleConfig(rigPort, QStringLiteral("127.0.0.1"), ctlPort); // stale address
        Engine engine(cfg);
        engine.discovery().setCandidates({QStringLiteral("127.0.0.3"), QStringLiteral("127.0.0.2")});
        QSignalSpy stored(&engine, &Engine::configChanged);
        engine.start();
        QTRY_VERIFY(engine.rig().isOnline());

        QVERIFY(rig.setFrequency(14'074'000));
        QTRY_COMPARE_WITH_TIMEOUT(engine.status(), Engine::Status::Ok, Slow);
        QCOMPARE(cfg.controllerHost, QStringLiteral("127.0.0.2"));
        QCOMPARE(stored.count(), 1);
        QCOMPARE(engine.controller().host(), QStringLiteral("127.0.0.2"));
        QCOMPARE(fake.a(), 1);
        QCOMPARE(fake.b(), 2);
    }

    // No address yet: the first band change searches.
    void searchesWhenUnset()
    {
        const quint16 rigPort = testutil::freePort();
        Rigctld rig(rigPort);
        QVERIFY(rig.start());
        const quint16 ctlPort = testutil::freePort();
        FakeController fake;
        QVERIFY(fake.listen(QHostAddress(QStringLiteral("127.0.0.4")), ctlPort));

        Config cfg = exampleConfig(rigPort, QString(), ctlPort);
        Engine engine(cfg);
        engine.discovery().setCandidates({QStringLiteral("127.0.0.4")});
        engine.start();
        QCOMPARE(engine.controller().link(), EasyController::Link::NotConfigured);
        QVERIFY(rig.setFrequency(7'074'000));
        QTRY_COMPARE_WITH_TIMEOUT(engine.status(), Engine::Status::Ok, Slow);
        QCOMPARE(cfg.controllerHost, QStringLiteral("127.0.0.4"));
        QCOMPARE(fake.a(), 2);
        QCOMPARE(fake.b(), 3);
    }

    // Nothing answers anywhere: fail with a message, then switch once the stored address answers again.
    void failsWhenNotFoundThenRecovers()
    {
        const quint16 rigPort = testutil::freePort();
        Rigctld rig(rigPort);
        QVERIFY(rig.start());
        const quint16 ctlPort = testutil::freePort();

        Config cfg = exampleConfig(rigPort, QStringLiteral("127.0.0.1"), ctlPort);
        Engine engine(cfg);
        engine.discovery().setCandidates({QStringLiteral("127.0.0.5")});
        engine.start();
        QTRY_VERIFY(engine.rig().isOnline());
        QVERIFY(rig.setFrequency(14'074'000));
        QTRY_COMPARE_WITH_TIMEOUT(engine.status(), Engine::Status::Failed, Slow);
        QVERIFY2(engine.statusText().contains(QStringLiteral("not found")), qPrintable(engine.statusText()));
        QCOMPARE(cfg.controllerHost, QStringLiteral("127.0.0.1"));

        FakeController fake;
        QVERIFY(fake.listen(QHostAddress::LocalHost, ctlPort));
        QTRY_COMPARE_WITH_TIMEOUT(engine.status(), Engine::Status::Ok, Slow); // background retry
        QCOMPARE(fake.a(), 1);
        QCOMPARE(fake.b(), 2);
    }

    // Two controllers answer: do not guess.
    void twoControllers()
    {
        const quint16 rigPort = testutil::freePort();
        Rigctld rig(rigPort);
        QVERIFY(rig.start());
        const quint16 ctlPort = testutil::freePort();
        FakeController one;
        FakeController two;
        QVERIFY(one.listen(QHostAddress(QStringLiteral("127.0.0.6")), ctlPort));
        QVERIFY(two.listen(QHostAddress(QStringLiteral("127.0.0.7")), ctlPort));

        Config cfg = exampleConfig(rigPort, QString(), ctlPort);
        Engine engine(cfg);
        engine.discovery().setCandidates({QStringLiteral("127.0.0.6"), QStringLiteral("127.0.0.7")});
        engine.start();
        QVERIFY(rig.setFrequency(14'074'000));
        QTRY_COMPARE_WITH_TIMEOUT(engine.status(), Engine::Status::Failed, Slow);
        QVERIFY2(engine.statusText().contains(QStringLiteral("2 controllers")), qPrintable(engine.statusText()));
        QVERIFY(cfg.controllerHost.isEmpty());
        QCOMPARE(one.a(), 0);
        QCOMPARE(two.a(), 0);
    }

    // A port that answers but is not a controller is not taken.
    void ignoresOtherServices()
    {
        QTcpServer other;
        QVERIFY(other.listen(QHostAddress(QStringLiteral("127.0.0.8")), 0));
        connect(&other, &QTcpServer::newConnection, &other, [&] {
            QTcpSocket *s = other.nextPendingConnection();
            connect(s, &QTcpSocket::readyRead, s, [s] {
                s->readAll();
                s->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
            });
        });
        Discovery d;
        d.setCandidates({QStringLiteral("127.0.0.8")});
        QSignalSpy finished(&d, &Discovery::finished);
        d.start(other.serverPort());
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(finished.at(0).at(0).toStringList().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestEngine)
#include "tst_engine.moc"
