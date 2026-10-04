#pragma once

#include <QElapsedTimer>
#include <QProcess>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <memory>

namespace testutil {

inline quint16 freePort()
{
    QTcpServer s;
    s.listen(QHostAddress::LocalHost, 0);
    return s.serverPort();
}

// A Hamlib dummy radio behind rigctld on 127.0.0.1:port.
class Rigctld
{
public:
    explicit Rigctld(quint16 port, bool vfoMode = false)
        : m_port(port)
        , m_vfoMode(vfoMode)
    {
    }
    ~Rigctld() { stop(); }

    static bool available() { return !QStandardPaths::findExecutable(QStringLiteral("rigctld")).isEmpty(); }

    bool start()
    {
        m_process = std::make_unique<QProcess>();
        QStringList args{QStringLiteral("-m"), QStringLiteral("1"), QStringLiteral("-T"), QStringLiteral("127.0.0.1"),
                         QStringLiteral("-t"), QString::number(m_port), QStringLiteral("-P"), QStringLiteral("RIG")};
        if (m_vfoMode)
            args << QStringLiteral("-o");
        if (qEnvironmentVariableIsSet("ECH_RIGCTLD_VERBOSE"))
            args << QStringLiteral("-vvvvv") << QStringLiteral("-Z");
        m_process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        m_process->start(QStringLiteral("rigctld"), args);
        if (!m_process->waitForStarted(3000))
            return false;
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 5000) {
            QTcpSocket s;
            s.connectToHost(QHostAddress::LocalHost, m_port);
            if (s.waitForConnected(200)) {
                s.disconnectFromHost();
                // rigctld 4.6 can lose a connection opened while another one closes
                // ("fdopen ... Bad file descriptor"); let the probe close first.
                QTest::qWait(300);
                return true;
            }
            QTest::qWait(100);
        }
        return false;
    }

    void stop()
    {
        if (!m_process)
            return;
        m_process->kill();
        m_process->waitForFinished(3000);
        m_process.reset();
    }

    // Sends one command on its own connection, as another rigctld client would; returns the reply.
    // Retried once on silence, for the rigctld 4.6 connection race above.
    QString command(const QString &line)
    {
        const QString reply = commandOnce(line);
        return reply.isEmpty() ? commandOnce(line) : reply;
    }

    QString commandOnce(const QString &line)
    {
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, m_port);
        if (!s.waitForConnected(2000))
            return QStringLiteral("no connection");
        QString cmd = line;
        if (m_vfoMode) {
            const int space = cmd.indexOf(QLatin1Char(' '));
            cmd = space < 0 ? cmd + QStringLiteral(" currVFO")
                            : cmd.left(space) + QStringLiteral(" currVFO") + cmd.mid(space);
        }
        s.write(cmd.toUtf8() + '\n');
        s.waitForBytesWritten(1000);
        QByteArray reply;
        while (!reply.endsWith('\n') && s.waitForReadyRead(2000))
            reply += s.readAll();
        return QString::fromUtf8(reply).trimmed();
    }

    bool setFrequency(qint64 hz) { return ok(command(QStringLiteral("F %1").arg(hz))); }
    bool setPtt(bool on) { return ok(command(QStringLiteral("T %1").arg(on ? 1 : 0))); }

    static bool ok(const QString &reply)
    {
        if (reply == QLatin1String("RPRT 0"))
            return true;
        qWarning("rigctld replied '%s'", qPrintable(reply));
        return false;
    }

private:
    quint16 m_port;
    bool m_vfoMode;
    std::unique_ptr<QProcess> m_process;
};

}
