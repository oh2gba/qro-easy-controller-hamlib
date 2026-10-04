#include "rigclient.h"
#include "testutil.h"

#include <QSignalSpy>
#include <QTest>

using testutil::Rigctld;

class TestRig : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if (!Rigctld::available())
            QSKIP("rigctld not installed");
    }

    void pollsFrequencyAndPtt_data()
    {
        QTest::addColumn<bool>("vfoMode");
        QTest::newRow("plain") << false;
        QTest::newRow("rigctld -o (vfo mode)") << true;
    }

    void pollsFrequencyAndPtt()
    {
        QFETCH(bool, vfoMode);
        const quint16 port = testutil::freePort();
        Rigctld rig(port, vfoMode);
        QVERIFY(rig.start());
        QVERIFY(rig.setFrequency(14'074'000));

        RigClient client;
        client.setEndpoint(QStringLiteral("127.0.0.1"), port);
        client.setPollInterval(200);
        client.start();
        QTRY_VERIFY(client.isOnline());
        QTRY_COMPARE(client.frequency(), qint64(14'074'000));
        QVERIFY(!client.ptt());

        QVERIFY(rig.setFrequency(7'030'000));
        QTRY_COMPARE(client.frequency(), qint64(7'030'000));

        QVERIFY(rig.setPtt(true));
        QTRY_VERIFY(client.ptt());
        QVERIFY(rig.setPtt(false));
        QTRY_VERIFY(!client.ptt());
    }

    void keepsPollingWhileAway()
    {
        const quint16 port = testutil::freePort();
        RigClient client;
        QSignalSpy online(&client, &RigClient::onlineChanged);
        client.setEndpoint(QStringLiteral("127.0.0.1"), port);
        client.setPollInterval(200);
        client.start();

        QTest::qWait(1000); // nothing listening yet
        QVERIFY(!client.isOnline());
        QVERIFY(!client.lastError().isEmpty());

        Rigctld rig(port);
        QVERIFY(rig.start());
        QVERIFY(rig.setFrequency(21'074'000));
        QTRY_VERIFY(client.isOnline());
        QTRY_COMPARE(client.frequency(), qint64(21'074'000));

        rig.stop();
        QTRY_VERIFY(!client.isOnline());

        QVERIFY(rig.start());
        QVERIFY(rig.setFrequency(28'074'000));
        QTRY_VERIFY(client.isOnline());
        QTRY_COMPARE(client.frequency(), qint64(28'074'000));
        QCOMPARE(online.count(), 3); // on, off, on
    }

    // A server that accepts but never answers must not leave the client "online".
    void silentServer()
    {
        QTcpServer silent;
        QVERIFY(silent.listen(QHostAddress::LocalHost, 0));
        RigClient client;
        client.setEndpoint(QStringLiteral("127.0.0.1"), silent.serverPort());
        client.setPollInterval(200);
        client.start();
        QTRY_VERIFY(silent.hasPendingConnections() || silent.isListening());
        QTest::qWait(2500);
        QVERIFY(!client.isOnline());
        QVERIFY(!client.lastError().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestRig)
#include "tst_rig.moc"
