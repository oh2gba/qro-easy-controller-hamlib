// Stand-in Easy Controller for trying qro-easy-controller-hamlib without the hardware:
//   fake-easycontroller --listen 127.0.0.1 --port 5959 [--push]
// then set the controller address in qro-easy-controller-hamlib to 127.0.0.1, port 5959.

#include "fakecontroller.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Simulated QRO.cz Easy Controller 6-2"));
    parser.addHelpOption();
    const QCommandLineOption listen(QStringLiteral("listen"), QStringLiteral("Address to listen on."),
                                    QStringLiteral("address"), QStringLiteral("127.0.0.1"));
    const QCommandLineOption port(QStringLiteral("port"), QStringLiteral("Port (the real box uses 59)."),
                                  QStringLiteral("port"), QStringLiteral("5959"));
    const QCommandLineOption push(QStringLiteral("push"), QStringLiteral("Push the state to clients after each switch."));
    parser.addOptions({listen, port, push});
    parser.process(app);

    FakeController fake;
    fake.setPushAfterSet(parser.isSet(push));
    QTextStream out(stdout);
    if (!fake.listen(QHostAddress(parser.value(listen)), quint16(parser.value(port).toUInt()))) {
        out << "cannot listen on " << parser.value(listen) << ':' << parser.value(port) << Qt::endl;
        return 1;
    }
    out << "fake Easy Controller on ws://" << parser.value(listen) << ':' << fake.port() << "/xxws" << Qt::endl;
    QObject::connect(&fake, &FakeController::commandReceived, &app, [&](const QString &command) {
        if (command != QLatin1String("G"))
            out << command << "  ->  TRX A " << fake.a() << ", TRX B " << fake.b() << Qt::endl;
    });
    return app.exec();
}
