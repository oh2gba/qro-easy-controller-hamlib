#include "bands.h"
#include "config.h"
#include "easycontroller.h"
#include "fakecontroller.h"
#include "websocketclient.h"

#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <QWebSocket>
#include <QWebSocketServer>

class TestCore : public QObject
{
    Q_OBJECT

private slots:
    void bands()
    {
        QCOMPARE(Bands::forFrequency(14'074'000), QStringLiteral("20m"));
        QCOMPARE(Bands::forFrequency(7'000'000), QStringLiteral("40m"));
        QCOMPARE(Bands::forFrequency(1'840'000), QStringLiteral("160m"));
        QCOMPARE(Bands::forFrequency(50'313'000), QStringLiteral("6m"));
        QCOMPARE(Bands::forFrequency(5'357'000), QStringLiteral("60m"));
        QVERIFY(Bands::forFrequency(145'000'000).isEmpty());
        QVERIFY(Bands::forFrequency(9'000'000).isEmpty());
        QCOMPARE(Bands::names().size(), 11);
    }

    void encodeDecode()
    {
        QCOMPARE(EasyController::encode(1, 2), 0x0201);
        QCOMPARE(EasyController::encode(6, 0), 0x20);
        QCOMPARE(EasyController::encode(0, 6), 0x2000);
        const ControllerState s = EasyController::decode(0x0204 | 0x4000);
        QCOMPARE(s.a, 3);
        QCOMPARE(s.b, 2);
        QVERIFY(!s.usedA);
        QVERIFY(s.usedB);
        QCOMPARE(EasyController::decode(0x40).a, 0);
    }

    void plan_data()
    {
        QTest::addColumn<int>("curA");
        QTest::addColumn<int>("curB");
        QTest::addColumn<int>("wantA");
        QTest::addColumn<int>("wantB");
        QTest::addColumn<QString>("steps"); // bank:a,b per step
        QTest::addColumn<bool>("conflict");
        QTest::newRow("nothing") << 1 << 2 << 1 << 2 << "" << false;
        QTest::newRow("keep both") << 1 << 2 << -1 << -1 << "" << false;
        QTest::newRow("A only") << 1 << 2 << 3 << -1 << "1:3,2" << false;
        QTest::newRow("B only") << 1 << 2 << -1 << 4 << "2:1,4" << false;
        QTest::newRow("both, independent") << 1 << 2 << 3 << 4 << "1:3,2 2:3,4" << false;
        QTest::newRow("20m to 40m: B frees A's antenna") << 1 << 2 << 2 << 3 << "2:1,3 1:2,3" << false;
        QTest::newRow("A frees B's antenna") << 1 << 2 << 3 << 1 << "1:3,2 2:3,1" << false;
        QTest::newRow("swap") << 1 << 2 << 2 << 1 << "2:1,0 1:2,0 2:2,1" << false;
        QTest::newRow("off") << 1 << 2 << 0 << 0 << "1:0,2 2:0,0" << false;
        QTest::newRow("from nothing") << 0 << 0 << 1 << 2 << "1:1,0 2:1,2" << false;
        QTest::newRow("same antenna") << 1 << 2 << 3 << 3 << "" << true;
        QTest::newRow("keep collides") << 1 << 2 << 2 << -1 << "" << true;
    }

    void plan()
    {
        QFETCH(int, curA);
        QFETCH(int, curB);
        QFETCH(int, wantA);
        QFETCH(int, wantB);
        QFETCH(QString, steps);
        QFETCH(bool, conflict);
        QString error;
        const QList<SwitchStep> plan = EasyController::plan(curA, curB, wantA, wantB, &error);
        QCOMPARE(!error.isEmpty(), conflict);
        QStringList got;
        for (const SwitchStep &s : plan)
            got << QStringLiteral("%1:%2,%3").arg(s.bank).arg(s.a).arg(s.b);
        QCOMPARE(got.join(QLatin1Char(' ')), steps);
    }

    void configRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("t.conf"));
        Config c;
        c.rigHost = QStringLiteral("shack-pc");
        c.rigPort = 4534;
        c.pollMs = 3000;
        c.controllerHost = QStringLiteral("192.168.1.45");
        c.antennaNames[0] = QStringLiteral("Yagi");
        c.routes[QStringLiteral("20m")] = {1, 2};
        c.routes[QStringLiteral("40m")] = {2, Config::Off};
        c.follow = false;
        {
            QSettings s(path, QSettings::IniFormat);
            c.save(s);
        }
        Config d;
        QSettings s(path, QSettings::IniFormat);
        d.load(s);
        QCOMPARE(d.rigHost, c.rigHost);
        QCOMPARE(d.rigPort, c.rigPort);
        QCOMPARE(d.pollMs, 3000);
        QCOMPARE(d.controllerHost, c.controllerHost);
        QCOMPARE(d.controllerPort, quint16(59));
        QCOMPARE(d.antennaNames.value(0), QStringLiteral("Yagi"));
        QCOMPARE(d.route(QStringLiteral("20m")), (BandRoute{1, 2}));
        QCOMPARE(d.route(QStringLiteral("40m")), (BandRoute{2, 0}));
        QCOMPARE(d.route(QStringLiteral("10m")), (BandRoute{-1, -1}));
        QCOMPARE(d.follow, false);
        QCOMPARE(d.portLabel(1), QStringLiteral("ANT1 Yagi"));
        QCOMPARE(d.portLabel(2), QStringLiteral("ANT2"));
        QVERIFY(d.conflicts().isEmpty());
        d.routes[QStringLiteral("15m")] = {3, 3};
        QCOMPARE(d.conflicts().size(), 1);
    }

    // The hand-written WebSocket client against Qt's independent server implementation.
    void webSocketClient()
    {
        QWebSocketServer server(QStringLiteral("t"), QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QWebSocket *peer = nullptr;
        QString path;
        connect(&server, &QWebSocketServer::newConnection, this, [&] {
            peer = server.nextPendingConnection();
            path = peer->requestUrl().path();
            peer->setOutgoingFrameSize(1000); // force fragmented messages
            connect(peer, &QWebSocket::textMessageReceived, peer,
                    [&](const QString &t) { peer->sendTextMessage(QStringLiteral("echo:") + t); });
        });

        WebSocketClient client;
        QSignalSpy opened(&client, &WebSocketClient::opened);
        QSignalSpy received(&client, &WebSocketClient::textReceived);
        QSignalSpy closed(&client, &WebSocketClient::closed);
        client.open(QStringLiteral("127.0.0.1"), server.serverPort(), QStringLiteral("/xxws"));
        QTRY_COMPARE(opened.count(), 1);
        QCOMPARE(path, QStringLiteral("/xxws"));

        QVERIFY(client.sendText(QStringLiteral("G")));
        QTRY_COMPARE(received.count(), 1);
        QCOMPARE(received.at(0).at(0).toString(), QStringLiteral("echo:G"));

        const QString big(70000, QLatin1Char('x')); // 64-bit length, sent masked
        QVERIFY(client.sendText(big));
        QTRY_COMPARE(received.count(), 2);
        QCOMPARE(received.at(1).at(0).toString(), QStringLiteral("echo:") + big);

        const QString utf8 = QStringLiteral("ä€😀");
        client.sendText(utf8);
        QTRY_COMPARE(received.count(), 3);
        QCOMPARE(received.at(2).at(0).toString(), QStringLiteral("echo:") + utf8);

        QSignalSpy pong(peer, &QWebSocket::pong);
        peer->ping("hello");
        QTRY_COMPARE(pong.count(), 1);
        QCOMPARE(pong.at(0).at(1).toByteArray(), QByteArray("hello"));

        peer->close();
        QTRY_COMPARE(closed.count(), 1);
        QVERIFY(!client.isOpen());
    }

    void webSocketRefused()
    {
        WebSocketClient client;
        QSignalSpy closed(&client, &WebSocketClient::closed);
        QSignalSpy opened(&client, &WebSocketClient::opened);
        client.open(QStringLiteral("127.0.0.1"), testPortNobodyUses(), QStringLiteral("/xxws"));
        QTRY_COMPARE(closed.count(), 1);
        QCOMPARE(opened.count(), 0);
        QVERIFY(!client.errorString().isEmpty());
    }

    void controllerApply_data()
    {
        QTest::addColumn<bool>("push");
        QTest::newRow("state polled") << false;
        QTest::newRow("state pushed") << true;
    }

    void controllerApply()
    {
        QFETCH(bool, push);
        FakeController fake;
        fake.setPushAfterSet(push);
        QVERIFY(fake.listen(QHostAddress::LocalHost, 0));
        fake.setPorts(1, 2);

        EasyController ctl;
        QSignalSpy finished(&ctl, &EasyController::applyFinished);
        ctl.setEndpoint(QStringLiteral("127.0.0.1"), fake.port());
        ctl.start();
        QTRY_COMPARE(ctl.link(), EasyController::Link::Connected);
        QCOMPARE(ctl.state().a, 1);
        QCOMPARE(ctl.state().b, 2);
        QCOMPARE(fake.paths().value(0), QStringLiteral("/xxws"));

        // 20m -> 40m from the example: TRX A to ANT2 needs TRX B off ANT2 first.
        fake.clearCommands();
        ctl.apply(2, 3);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY2(finished.at(0).at(0).toBool(), qPrintable(finished.at(0).at(2).toString()));
        QCOMPARE(fake.a(), 2);
        QCOMPARE(fake.b(), 3);
        QStringList sets;
        for (const QString &c : fake.commands())
            if (c.startsWith(QLatin1Char('X')))
                sets << c;
        QCOMPARE(sets, (QStringList{QStringLiteral("X/0/1025/2"), QStringLiteral("X/0/1026/1")}));

        // Swap.
        ctl.apply(3, 2);
        QTRY_COMPARE(finished.count(), 2);
        QVERIFY(finished.at(1).at(0).toBool());
        QCOMPARE(fake.a(), 3);
        QCOMPARE(fake.b(), 2);

        // Conflict is refused before anything is sent.
        fake.clearCommands();
        ctl.apply(4, 4);
        QCOMPARE(finished.count(), 3);
        QVERIFY(!finished.at(2).at(0).toBool());
        QVERIFY(!finished.at(2).at(1).toBool());
        QTest::qWait(200);
        for (const QString &c : fake.commands())
            QVERIFY(!c.startsWith(QLatin1Char('X')));

        // The box refuses while TRX A transmits: reported, not hung.
        fake.setPtt(1, true);
        QTRY_VERIFY(ctl.ptt(1));
        ctl.apply(5, -1);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 4, 5000);
        QVERIFY(!finished.at(3).at(0).toBool());
        QCOMPARE(fake.a(), 3);
        fake.setPtt(1, false);
        QTRY_VERIFY(!ctl.ptt(1));

        // Front panel change is seen.
        QSignalSpy state(&ctl, &EasyController::stateChanged);
        fake.setPorts(6, 1);
        QTRY_VERIFY(state.count() >= 1);
        QCOMPARE(ctl.state().a, 6);
        QCOMPARE(ctl.state().b, 1);
    }

    void controllerLinkLossAndReturn()
    {
        FakeController fake;
        QVERIFY(fake.listen(QHostAddress::LocalHost, 0));
        const quint16 port = fake.port();
        EasyController ctl;
        ctl.setEndpoint(QStringLiteral("127.0.0.1"), port);
        ctl.start();
        QTRY_COMPARE(ctl.link(), EasyController::Link::Connected);
        fake.close();
        QTRY_COMPARE(ctl.link(), EasyController::Link::Unreachable);
        QVERIFY(fake.listen(QHostAddress::LocalHost, port));
        QTRY_COMPARE_WITH_TIMEOUT(ctl.link(), EasyController::Link::Connected, 8000); // retry every 5 s
    }

private:
    static quint16 testPortNobodyUses()
    {
        QTcpServer s;
        s.listen(QHostAddress::LocalHost, 0);
        const quint16 p = s.serverPort();
        s.close();
        return p;
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "tst_core.moc"
