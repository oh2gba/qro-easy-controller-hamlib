#include "config.h"
#include "desktopintegration.h"
#include "engine.h"
#include "logbus.h"
#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QSettings>
#include <QTimer>

#include <memory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("qro-easy-controller-hamlib"));
    QApplication::setApplicationDisplayName(QStringLiteral(ECH_DISPLAY_NAME));
    QApplication::setApplicationVersion(QStringLiteral(ECH_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("qro-easy-controller-hamlib"));

    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256})
        icon.addFile(QStringLiteral(":/icons/%1.png").arg(size), QSize(size, size));
    QApplication::setWindowIcon(icon);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Hamlib band decoder for the QRO.cz Easy Controller 6-2: switches its antennas by the band "
                       "of a radio served by rigctld."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption configOption({QStringLiteral("c"), QStringLiteral("config")},
                                          QStringLiteral("Settings file (default: the per-user settings location)."),
                                          QStringLiteral("file"));
    const QCommandLineOption verboseOption(QStringLiteral("verbose"),
                                           QStringLiteral("Write the log to standard error."));
    parser.addOption(configOption);
    parser.addOption(verboseOption);
    parser.process(app);

    std::unique_ptr<QSettings> settings =
        parser.isSet(configOption)
            ? std::make_unique<QSettings>(parser.value(configOption), QSettings::IniFormat)
            : std::make_unique<QSettings>(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("qro-easy-controller-hamlib"),
                                          QStringLiteral("qro-easy-controller-hamlib"));
    LogBus::instance().setEcho(parser.isSet(verboseOption));
    logLine(QStringLiteral(ECH_DISPLAY_NAME " %1, settings in %2").arg(QStringLiteral(ECH_VERSION), settings->fileName()));

    const bool firstRun = !settings->contains(QStringLiteral("radio/host"));
    Config config;
    config.load(*settings);
    DesktopIntegration::apply(config.desktopEntry);

    Engine engine(config);
    auto save = [&] {
        config.save(*settings);
        settings->sync();
    };
    QObject::connect(&engine, &Engine::configChanged, save);

    MainWindow window(engine, config, *settings, save);
    window.show();
    engine.start();
    if (firstRun)
        QTimer::singleShot(0, &window, &MainWindow::openSettings);

    return app.exec();
}
