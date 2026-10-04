// Subnet scan against real interfaces. Runs only inside the test container, which has no network
// but a dummy interface with 10.99.0.1/24 and 10.99.0.7/24 (see scripts/dev.sh), so nothing real
// is ever scanned.

#include "discovery.h"
#include "fakecontroller.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

class TestDiscovery : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if (qEnvironmentVariable("ECH_NETNS_TEST") != QLatin1String("1"))
            QSKIP("needs the isolated network of scripts/dev.sh test");
    }

    void subnetHosts()
    {
        const QStringList hosts = Discovery::subnetHosts();
        QCOMPARE(hosts.size(), 254); // one /24, loopback skipped, addresses de-duplicated
        QVERIFY(hosts.contains(QStringLiteral("10.99.0.7")));
        QVERIFY(!hosts.contains(QStringLiteral("10.99.0.0")));
        QVERIFY(!hosts.contains(QStringLiteral("10.99.0.255")));
    }

    void findsControllerOnSubnet()
    {
        FakeController fake;
        QVERIFY(fake.listen(QHostAddress(QStringLiteral("10.99.0.7")), 59));
        Discovery d;
        QSignalSpy finished(&d, &Discovery::finished);
        QElapsedTimer t;
        t.start();
        d.start(59);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 20000);
        QCOMPARE(finished.at(0).at(0).toStringList(), QStringList{QStringLiteral("10.99.0.7")});
        qInfo("scanned 254 addresses in %lld ms", t.elapsed());
    }
};

QTEST_GUILESS_MAIN(TestDiscovery)
#include "tst_discovery.moc"
