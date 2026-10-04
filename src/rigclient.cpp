#include "rigclient.h"

#include "logbus.h"

#include <QTcpSocket>

#include <cmath>

namespace {

constexpr int ReplyTimeoutMs = 2000;

}

RigClient::RigClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
{
    m_pollTimer.setInterval(1000);
    connect(&m_pollTimer, &QTimer::timeout, this, &RigClient::poll);
    m_replyTimer.setSingleShot(true);
    connect(&m_replyTimer, &QTimer::timeout, this, [this] { drop(tr("no reply")); });

    connect(m_socket, &QTcpSocket::connected, this, [this] {
        m_replyTimer.stop();
        m_vfoChecked = false;
        poll();
    });
    connect(m_socket, &QTcpSocket::readyRead, this, &RigClient::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this] { drop(m_socket->errorString()); });
    connect(m_socket, &QTcpSocket::disconnected, this, [this] { drop(tr("connection closed")); });
}

void RigClient::setEndpoint(const QString &host, quint16 port)
{
    if (host == m_host && port == m_port)
        return;
    m_host = host;
    m_port = port;
    drop(tr("endpoint changed"));
    if (m_pollTimer.isActive())
        poll();
}

void RigClient::setPollInterval(int ms)
{
    m_pollTimer.setInterval(ms);
}

void RigClient::start()
{
    m_pollTimer.start();
    poll();
}

void RigClient::stop()
{
    m_pollTimer.stop();
    drop(tr("stopped"));
}

void RigClient::poll()
{
    switch (m_socket->state()) {
    case QAbstractSocket::UnconnectedState:
        m_socket->connectToHost(m_host, m_port);
        m_replyTimer.start(ReplyTimeoutMs);
        return;
    case QAbstractSocket::ConnectedState:
        if (m_pending != Command::None || !m_queue.isEmpty())
            return; // the previous cycle is still waiting; the reply timer guards it
        if (!m_vfoChecked)
            m_queue << Command::CheckVfo;
        m_queue << Command::Frequency << Command::Ptt;
        sendNext();
        return;
    default:
        return; // connecting; the reply timer guards it
    }
}

void RigClient::sendNext()
{
    if (m_queue.isEmpty())
        return;
    m_pending = m_queue.takeFirst();
    const QByteArray vfo = m_vfoMode ? QByteArrayLiteral(" currVFO") : QByteArray();
    QByteArray line;
    switch (m_pending) {
    case Command::CheckVfo:
        line = QByteArrayLiteral("\\chk_vfo");
        break;
    case Command::Frequency:
        line = "f" + vfo;
        break;
    case Command::Ptt:
        line = "t" + vfo;
        break;
    case Command::None:
        return;
    }
    m_socket->write(line + '\n');
    m_replyTimer.start(ReplyTimeoutMs);
}

void RigClient::onReadyRead()
{
    while (m_socket->canReadLine()) {
        const QString reply = QString::fromUtf8(m_socket->readLine()).trimmed();
        if (m_pending == Command::None)
            continue; // nothing asked; ignore
        const Command command = m_pending;
        m_pending = Command::None;
        m_replyTimer.stop();
        handle(command, reply);
        if (m_socket->state() != QAbstractSocket::ConnectedState)
            return;
        sendNext();
    }
}

void RigClient::handle(Command command, const QString &reply)
{
    const bool error = reply.startsWith(QLatin1String("RPRT"));
    switch (command) {
    case Command::CheckVfo:
        // "0"/"1", or "CHKVFO 0"/"CHKVFO 1" from older versions.
        m_vfoChecked = true;
        m_vfoMode = !error && reply.endsWith(QLatin1Char('1'));
        break;
    case Command::Frequency: {
        bool ok = false;
        const double hz = reply.toDouble(&ok);
        if (error || !ok || hz <= 0) {
            if (m_online)
                logLine(tr("Radio not answering (%1)").arg(reply));
            m_error = tr("radio not answering (%1)").arg(reply);
            setOnline(false);
            return;
        }
        setOnline(true);
        const qint64 f = qint64(std::llround(hz));
        if (f != m_frequency) {
            m_frequency = f;
            emit frequencyChanged(f);
        }
        break;
    }
    case Command::Ptt: {
        bool ok = false;
        const int v = reply.toInt(&ok);
        setPtt(!error && ok && v != 0);
        break;
    }
    case Command::None:
        break;
    }
}

void RigClient::drop(const QString &why)
{
    m_replyTimer.stop();
    m_queue.clear();
    m_pending = Command::None;
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();
    if (m_online)
        logLine(tr("Radio connection lost: %1").arg(why));
    m_error = why;
    setOnline(false);
}

void RigClient::setOnline(bool online)
{
    if (!online)
        setPtt(false);
    if (online == m_online)
        return;
    m_online = online;
    if (online)
        logLine(tr("Radio online at %1:%2").arg(m_host).arg(m_port));
    emit onlineChanged(online);
}

void RigClient::setPtt(bool on)
{
    if (on == m_ptt)
        return;
    m_ptt = on;
    emit pttChanged(on);
}
