// Window behaviour, plus screenshots into $ECH_SHOTS when set.

#include "bands.h"
#include "desktopintegration.h"
#include "engine.h"
#include "fakecontroller.h"
#include "mainwindow.h"
#include "settingsdialog.h"
#include "testutil.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTest>

using testutil::Rigctld;

class TestUi : public QObject
{
    Q_OBJECT

private:
    static void shot(QWidget *w, const QString &name)
    {
        const QString dir = qEnvironmentVariable("ECH_SHOTS");
        if (dir.isEmpty())
            return;
        QApplication::processEvents();
        QVERIFY(w->grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png")));
    }

    static Config demoConfig()
    {
        Config c;
        c.rigHost = QStringLiteral("127.0.0.1");
        c.pollMs = 200;
        c.antennaNames = {QStringLiteral("Yagi"), QStringLiteral("Dipole"), QStringLiteral("Vertical"),
                          QStringLiteral("Delta loop"), QStringLiteral("Beverage"), QString()};
        for (const QString &band : Bands::names())
            c.routes[band] = {};
        c.routes[QStringLiteral("20m")] = {1, 2};
        c.routes[QStringLiteral("40m")] = {2, 3};
        c.routes[QStringLiteral("80m")] = {4, 3};
        c.routes[QStringLiteral("15m")] = {1, Config::Keep};
        return c;
    }

private slots:
    void mainWindowLive()
    {
        if (!Rigctld::available())
            QSKIP("rigctld not installed");
        const quint16 rigPort = testutil::freePort();
        Rigctld rig(rigPort);
        QVERIFY(rig.start());
        QVERIFY(rig.setFrequency(14'074'000));
        FakeController fake;
        QVERIFY(fake.listen(QHostAddress::LocalHost, 0));

        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("ui.conf")), QSettings::IniFormat);
        Config cfg = demoConfig();
        cfg.rigPort = rigPort;
        cfg.controllerHost = QStringLiteral("127.0.0.1");
        cfg.controllerPort = fake.port();
        int saves = 0;
        Engine engine(cfg);
        MainWindow w(engine, cfg, settings, [&] { ++saves; });
        w.resize(520, 10);
        w.show();
        engine.start();
        QTRY_COMPARE(engine.status(), Engine::Status::Ok);
        QTRY_VERIFY(w.findChildren<QLabel *>().size() > 0);
        bool sawBand = false;
        for (QLabel *l : w.findChildren<QLabel *>())
            sawBand |= l->text() == QLatin1String("20m");
        QVERIFY(sawBand);
        shot(&w, QStringLiteral("main-20m"));

        fake.setPorts(3, 2); // someone presses a front panel button
        QTRY_VERIFY(!engine.differences().isEmpty());
        shot(&w, QStringLiteral("main-differs"));
        engine.applyNow();
        QTRY_VERIFY(engine.differences().isEmpty());

        QVERIFY(rig.setPtt(true));
        QTRY_VERIFY(engine.rig().ptt());
        fake.setPtt(1, true);
        QTRY_VERIFY(engine.controller().ptt(1));
        shot(&w, QStringLiteral("main-tx"));
        QVERIFY(rig.setPtt(false));
        fake.setPtt(1, false);

        rig.stop();
        fake.close();
        QTRY_VERIFY(!engine.rig().isOnline());
        QTRY_COMPARE(engine.controller().link(), EasyController::Link::Unreachable);
        shot(&w, QStringLiteral("main-offline"));
    }

    // Opening the log grows the window; closing it gives the height back.
    void logToggleRestoresHeight()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("ui.conf")), QSettings::IniFormat);
        Config cfg = demoConfig();
        Engine engine(cfg);
        MainWindow w(engine, cfg, settings, [] {});
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QTest::qWait(50);
        const int compact = w.height();

        QToolButton *toggle = nullptr;
        for (QToolButton *b : w.findChildren<QToolButton *>())
            if (b->text() == QLatin1String("Log"))
                toggle = b;
        QVERIFY(toggle);
        for (int round = 0; round < 2; ++round) {
            toggle->click();
            QTRY_VERIFY(w.height() > compact + 100);
            shot(&w, QStringLiteral("main-log"));
            toggle->click();
            QTRY_COMPARE(w.height(), compact);
        }
    }

    // Screenshots for the README and the project page: a short session on the dummy radio.
    void pageScreenshots()
    {
        const QString dir = qEnvironmentVariable("ECH_SHOTS");
        if (dir.isEmpty())
            QSKIP("set ECH_SHOTS to a directory");
        if (!Rigctld::available())
            QSKIP("rigctld not installed");
        // Inside scripts/dev.sh shots the usual addresses are free: rigctld on 4532 and the
        // controller on 192.168.1.45:59, an address of the container's dummy interface.
        const bool usual = QTcpServer().listen(QHostAddress::LocalHost, 4532);
        const quint16 rigPort = usual ? 4532 : testutil::freePort();
        Rigctld rig(rigPort);
        QVERIFY(rig.start());
        FakeController fake;
        QString ctlHost = QStringLiteral("192.168.1.45");
        if (!fake.listen(QHostAddress(ctlHost), 59)) {
            ctlHost = QStringLiteral("127.0.0.1");
            QVERIFY(fake.listen(QHostAddress::LocalHost, 0));
        }

        QTemporaryDir tmp;
        QSettings settings(tmp.filePath(QStringLiteral("page.conf")), QSettings::IniFormat);
        Config cfg;
        cfg.rigHost = QStringLiteral("localhost");
        cfg.rigPort = rigPort;
        cfg.controllerHost = ctlHost;
        cfg.controllerPort = fake.port();
        cfg.antennaNames = {QStringLiteral("Yagi"),       QStringLiteral("Dipole"),  QStringLiteral("Vertical"),
                            QStringLiteral("Delta loop"), QStringLiteral("Hexbeam"), QStringLiteral("Beverage")};
        const QList<QPair<QString, BandRoute>> table{
            {QStringLiteral("160m"), {3, 6}}, {QStringLiteral("80m"), {3, 2}}, {QStringLiteral("60m"), {2, 3}},
            {QStringLiteral("40m"), {2, 3}},  {QStringLiteral("30m"), {4, 3}}, {QStringLiteral("20m"), {1, 2}},
            {QStringLiteral("17m"), {5, 4}},  {QStringLiteral("15m"), {1, 5}}, {QStringLiteral("12m"), {5, 4}},
            {QStringLiteral("10m"), {1, 5}},  {QStringLiteral("6m"), {5, Config::Off}}};
        for (const auto &row : table)
            cfg.routes[row.first] = row.second;

        Engine engine(cfg);
        MainWindow w(engine, cfg, settings, [] {});
        w.resize(540, 10);
        w.show();
        engine.start();
        QVERIFY(rig.setFrequency(7'074'000));
        QTRY_COMPARE(engine.status(), Engine::Status::Ok);
        QVERIFY(rig.setFrequency(14'074'000));
        QTRY_COMPARE(engine.band(), QStringLiteral("20m"));
        QTRY_COMPARE(engine.status(), Engine::Status::Ok);
        shot(&w, QStringLiteral("page-main"));

        QVERIFY(rig.setPtt(true));
        fake.setPtt(1, true);
        QTRY_VERIFY(engine.rig().ptt());
        QTRY_VERIFY(engine.controller().ptt(1));
        shot(&w, QStringLiteral("page-tx"));
        QVERIFY(rig.setPtt(false));
        fake.setPtt(1, false);
        QTRY_VERIFY(!engine.controller().ptt(1));

        QVERIFY(rig.setFrequency(21'074'000));
        QTRY_COMPARE(engine.band(), QStringLiteral("15m"));
        QTRY_COMPARE(engine.status(), Engine::Status::Ok);
        const int compact = w.height();
        for (QToolButton *b : w.findChildren<QToolButton *>())
            if (b->text() == QLatin1String("Log"))
                b->click();
        QTRY_VERIFY(w.height() > compact + 100);
        shot(&w, QStringLiteral("page-log"));

        SettingsDialog dialog(cfg);
        dialog.show();
        shot(&dialog, QStringLiteral("page-settings"));
    }

    // Desktop entry in a data folder: written, kept current, left alone or removed.
    void desktopEntry()
    {
        QTemporaryDir dir;
        const QString data = dir.path();
        const QString entry = data + QStringLiteral("/applications/qro-easy-controller-hamlib.desktop");
        const QString icon = data + QStringLiteral("/icons/hicolor/48x48/apps/qro-easy-controller-hamlib.png");
        auto read = [](const QString &path) {
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };

        QVERIFY(DesktopIntegration::update(data, QStringLiteral("/opt/ech/app"), true, {}));
        QVERIFY(read(entry).contains(QStringLiteral("\nExec=\"/opt/ech/app\"\n")));
        QVERIFY(read(entry).contains(QStringLiteral("\nIcon=qro-easy-controller-hamlib\n")));
        QVERIFY(read(entry).contains(QStringLiteral("Name=Easy Controller Hamlib")));
        QVERIFY(QFile::exists(icon));
        QVERIFY(QImage(icon).size() == QSize(48, 48));
        QVERIFY(!DesktopIntegration::update(data, QStringLiteral("/opt/ech/app"), true, {})); // nothing to do

        QVERIFY(DesktopIntegration::update(data, QStringLiteral("/home/x/My Apps/a$b.AppImage"), true, {}));
        QVERIFY(read(entry).contains(QStringLiteral("Exec=\"/home/x/My Apps/a\\\\$b.AppImage\"")));
        QVERIFY(read(entry).contains(QStringLiteral("TryExec=/home/x/My Apps/a$b.AppImage")));

        // A package installs its own entry: ours goes.
        QVERIFY(DesktopIntegration::update(data, QStringLiteral("/opt/ech/app"), true,
                                           {QStringLiteral("/usr/share/applications/qro-easy-controller-hamlib.desktop")}));
        QVERIFY(!QFile::exists(entry));
        QVERIFY(!QFile::exists(icon));

        // Turned off: removed; a file that is not ours is never touched.
        QVERIFY(DesktopIntegration::update(data, QStringLiteral("/opt/ech/app"), true, {}));
        QVERIFY(DesktopIntegration::update(data, QStringLiteral("/opt/ech/app"), false, {}));
        QVERIFY(!QFile::exists(entry));
        {
            QFile own(entry);
            QVERIFY(own.open(QIODevice::WriteOnly));
            own.write("[Desktop Entry]\nName=Mine\nExec=mine\n");
        }
        QVERIFY(!DesktopIntegration::update(data, QStringLiteral("/opt/ech/app"), true, {}));
        QVERIFY(!DesktopIntegration::update(data, QStringLiteral("/opt/ech/app"), false, {}));
        QCOMPARE(read(entry), QStringLiteral("[Desktop Entry]\nName=Mine\nExec=mine\n"));
    }

    void settingsDialog()
    {
        Config cfg = demoConfig();
        cfg.controllerHost = QStringLiteral("192.168.1.45");
        SettingsDialog dialog(cfg);
        dialog.show();
        auto *table = dialog.findChild<QTableWidget *>();
        QVERIFY(table);
        QCOMPARE(table->rowCount(), Bands::names().size());
        auto *ok = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
        QVERIFY(ok->isEnabled());
        shot(&dialog, QStringLiteral("settings"));

        // Both radios on one antenna is refused.
        const int row20 = Bands::names().indexOf(QStringLiteral("20m"));
        auto *b20 = qobject_cast<QComboBox *>(table->cellWidget(row20, 1));
        b20->setCurrentIndex(b20->findData(1));
        QVERIFY(!ok->isEnabled());
        shot(&dialog, QStringLiteral("settings-conflict"));
        b20->setCurrentIndex(b20->findData(Config::Off));
        QVERIFY(ok->isEnabled());

        // Renaming an antenna relabels the choices.
        QLineEdit *name6 = nullptr;
        for (QLineEdit *e : dialog.findChildren<QLineEdit *>())
            if (e->placeholderText() == QLatin1String("ANT6"))
                name6 = e;
        QVERIFY(name6);
        name6->setText(QStringLiteral("Hexbeam"));
        QCOMPARE(b20->itemText(b20->findData(6)), QStringLiteral("ANT6 Hexbeam"));

        const Config out = dialog.config();
        QCOMPARE(out.route(QStringLiteral("20m")), (BandRoute{1, Config::Off}));
        QCOMPARE(out.antennaNames.value(5), QStringLiteral("Hexbeam"));
        QCOMPARE(out.controllerHost, QStringLiteral("192.168.1.45"));
    }

    void settingsSearchFillsAddress()
    {
        const quint16 port = testutil::freePort();
        FakeController fake;
        QVERIFY(fake.listen(QHostAddress(QStringLiteral("127.0.0.9")), port));
        Config cfg = demoConfig();
        cfg.controllerPort = port;
        SettingsDialog dialog(cfg);
        dialog.discovery().setCandidates({QStringLiteral("127.0.0.10"), QStringLiteral("127.0.0.9")});
        dialog.show();
        dialog.search();
        QTRY_COMPARE(dialog.config().controllerHost, QStringLiteral("127.0.0.9"));
        shot(&dialog, QStringLiteral("settings-found"));
    }
};

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
