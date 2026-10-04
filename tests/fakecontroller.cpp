#include "fakecontroller.h"

#include <QWebSocket>
#include <QWebSocketServer>

namespace {

int portFromBits(int bits)
{
    for (int p = 0; p < 6; ++p)
        if (bits & (1 << p))
            return p + 1;
    return 0;
}

}

FakeController::FakeController(QObject *parent)
    : QObject(parent)
    , m_server(new QWebSocketServer(QStringLiteral("fake-easycontroller"), QWebSocketServer::NonSecureMode, this))
{
    connect(m_server, &QWebSocketServer::newConnection, this, &FakeController::onConnection);
}

FakeController::~FakeController()
{
    close();
}

bool FakeController::listen(const QHostAddress &address, quint16 port)
{
    return m_server->listen(address, port);
}

void FakeController::close()
{
    m_server->close();
    const auto clients = m_clients;
    m_clients.clear();
    for (QWebSocket *c : clients) {
        c->abort();
        c->deleteLater();
    }
}

quint16 FakeController::port() const
{
    return m_server->serverPort();
}

int FakeController::mask() const
{
    int m = 0;
    if (m_a)
        m |= 1 << (m_a - 1);
    else
        m |= 1 << 6; // USED A
    if (m_b)
        m |= 1 << (m_b - 1 + 8);
    else
        m |= 1 << 14; // USED B
    return m;
}

void FakeController::setPorts(int a, int b, bool announce)
{
    m_a = a;
    m_b = b;
    if (announce)
        broadcast(stateJson());
}

void FakeController::setPtt(int bank, bool on)
{
    (bank == 1 ? m_pttA : m_pttB) = on;
    broadcast(QStringLiteral("{\"IsPTT%1\":%2}").arg(bank).arg(on ? 1 : 0));
}

void FakeController::onConnection()
{
    while (QWebSocket *client = m_server->nextPendingConnection()) {
        m_paths << client->requestUrl().path();
        m_clients << client;
        connect(client, &QWebSocket::textMessageReceived, this,
                [this, client](const QString &text) { onText(client, text); });
        connect(client, &QWebSocket::disconnected, this, [this, client] {
            m_clients.removeAll(client);
            client->deleteLater();
        });
    }
}

void FakeController::onText(QWebSocket *client, const QString &text)
{
    m_commands << text;
    emit commandReceived(text);
    if (text == QLatin1String("G")) {
        client->sendTextMessage(stateJson());
        return;
    }
    const QStringList parts = text.split(QLatin1Char('/'));
    if (parts.size() != 4 || parts[0] != QLatin1String("X"))
        return;
    const int mask = parts[2].toInt();
    const int bank = parts[3].toInt();
    if (bank == 1 && !m_pttA) {
        const int want = portFromBits(mask & 0x3F);
        if (want == 0 || want != m_b)
            m_a = want;
    } else if (bank == 2 && !m_pttB) {
        const int want = portFromBits((mask >> 8) & 0x3F);
        if (want == 0 || want != m_a)
            m_b = want;
    }
    if (m_pushAfterSet)
        broadcast(stateJson());
}

void FakeController::broadcast(const QString &text)
{
    for (QWebSocket *c : std::as_const(m_clients))
        c->sendTextMessage(text);
}

QString FakeController::stateJson() const
{
    return QStringLiteral("{\"B0\":%1}").arg(mask());
}
