#include "websocketclient.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QTcpSocket>

namespace {

constexpr quint8 OpContinuation = 0x0;
constexpr quint8 OpText = 0x1;
constexpr quint8 OpBinary = 0x2;
constexpr quint8 OpClose = 0x8;
constexpr quint8 OpPing = 0x9;
constexpr quint8 OpPong = 0xA;
constexpr quint64 MaxMessage = 1 << 20;
constexpr int MaxHandshake = 8192;
const QByteArray AcceptGuid = QByteArrayLiteral("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");

QByteArray randomBytes(int n)
{
    QByteArray b(n, Qt::Uninitialized);
    for (int i = 0; i < n; ++i)
        b[i] = char(QRandomGenerator::global()->bounded(256));
    return b;
}

quint8 byteAt(const QByteArray &b, int i)
{
    return quint8(b.at(i));
}

}

WebSocketClient::WebSocketClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
{
    connect(m_socket, &QTcpSocket::connected, this, &WebSocketClient::onConnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &WebSocketClient::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, [this] { finish(tr("connection closed")); });
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this] { finish(m_socket->errorString()); });
}

void WebSocketClient::open(const QString &host, quint16 port, const QString &path)
{
    close();
    m_host = host;
    m_port = port;
    m_path = path;
    m_error.clear();
    m_state = State::Connecting;
    m_socket->connectToHost(host, port);
}

void WebSocketClient::close()
{
    if (m_state == State::Idle)
        return;
    const bool wasOpen = m_state == State::Open;
    m_state = State::Idle;
    if (wasOpen) {
        sendFrame(OpClose, QByteArray("\x03\xe8", 2)); // 1000, normal closure
        m_socket->flush();
    }
    m_socket->abort();
    m_buffer.clear();
    m_message.clear();
}

bool WebSocketClient::sendText(const QString &text)
{
    if (m_state != State::Open)
        return false;
    sendFrame(OpText, text.toUtf8());
    return true;
}

void WebSocketClient::onConnected()
{
    if (m_state != State::Connecting)
        return;
    m_state = State::Handshake;
    m_key = randomBytes(16).toBase64();
    QByteArray request;
    request += "GET " + m_path.toUtf8() + " HTTP/1.1\r\n";
    request += "Host: " + m_host.toUtf8() + ':' + QByteArray::number(m_port) + "\r\n";
    request += "Upgrade: websocket\r\n";
    request += "Connection: Upgrade\r\n";
    request += "Sec-WebSocket-Key: " + m_key + "\r\n";
    request += "Sec-WebSocket-Version: 13\r\n\r\n";
    m_socket->write(request);
    emit socketConnected();
}

void WebSocketClient::onReadyRead()
{
    if (m_state == State::Idle || m_state == State::Connecting)
        return;
    m_buffer += m_socket->readAll();
    if (m_state == State::Handshake && !readHandshake())
        return;
    if (m_state == State::Open)
        readFrames();
}

bool WebSocketClient::readHandshake()
{
    const int end = m_buffer.indexOf("\r\n\r\n");
    if (end < 0) {
        if (m_buffer.size() > MaxHandshake)
            finish(tr("oversized handshake reply"));
        return false;
    }
    const QList<QByteArray> lines = m_buffer.left(end).split('\n');
    m_buffer.remove(0, end + 4);

    const QList<QByteArray> status = lines.value(0).trimmed().split(' ');
    if (status.value(1) != "101") {
        finish(tr("server refused the WebSocket upgrade: %1").arg(QString::fromLatin1(lines.value(0).trimmed())));
        return false;
    }
    QByteArray accept;
    for (const QByteArray &l : lines.mid(1)) {
        const int colon = l.indexOf(':');
        if (colon > 0 && l.left(colon).trimmed().toLower() == "sec-websocket-accept")
            accept = l.mid(colon + 1).trimmed();
    }
    const QByteArray expected = QCryptographicHash::hash(m_key + AcceptGuid, QCryptographicHash::Sha1).toBase64();
    if (accept != expected) {
        finish(tr("invalid WebSocket accept key"));
        return false;
    }
    m_state = State::Open;
    emit opened();
    return m_state == State::Open;
}

void WebSocketClient::readFrames()
{
    while (m_state == State::Open && m_buffer.size() >= 2) {
        const quint8 b0 = byteAt(m_buffer, 0);
        const quint8 b1 = byteAt(m_buffer, 1);
        const bool fin = b0 & 0x80;
        const quint8 opcode = b0 & 0x0F;
        const bool masked = b1 & 0x80;
        quint64 length = b1 & 0x7F;
        int pos = 2;
        if (length == 126) {
            if (m_buffer.size() < 4)
                return;
            length = (quint64(byteAt(m_buffer, 2)) << 8) | byteAt(m_buffer, 3);
            pos = 4;
        } else if (length == 127) {
            if (m_buffer.size() < 10)
                return;
            length = 0;
            for (int i = 2; i < 10; ++i)
                length = (length << 8) | byteAt(m_buffer, i);
            pos = 10;
        }
        if (length > MaxMessage) {
            finish(tr("oversized WebSocket frame"));
            return;
        }
        QByteArray mask;
        if (masked) {
            if (m_buffer.size() < pos + 4)
                return;
            mask = m_buffer.mid(pos, 4);
            pos += 4;
        }
        if (quint64(m_buffer.size()) < pos + length)
            return;
        QByteArray payload = m_buffer.mid(pos, int(length));
        m_buffer.remove(0, pos + int(length));
        if (masked)
            for (int i = 0; i < payload.size(); ++i)
                payload[i] = char(payload.at(i) ^ mask.at(i % 4));

        switch (opcode) {
        case OpText:
        case OpBinary:
        case OpContinuation:
            if (opcode != OpContinuation)
                m_message.clear();
            m_message += payload;
            if (quint64(m_message.size()) > MaxMessage) {
                finish(tr("oversized WebSocket message"));
                return;
            }
            if (fin) {
                const QString text = QString::fromUtf8(m_message);
                m_message.clear();
                emit textReceived(text);
            }
            break;
        case OpPing:
            sendFrame(OpPong, payload);
            break;
        case OpPong:
            break;
        case OpClose:
            sendFrame(OpClose, payload.left(2));
            m_socket->flush();
            finish(tr("server closed the WebSocket"));
            return;
        default:
            finish(tr("unknown WebSocket opcode %1").arg(opcode));
            return;
        }
    }
}

void WebSocketClient::sendFrame(quint8 opcode, const QByteArray &payload)
{
    QByteArray frame;
    frame.append(char(0x80 | opcode));
    const quint64 n = quint64(payload.size());
    if (n < 126) {
        frame.append(char(0x80 | n));
    } else if (n <= 0xFFFF) {
        frame.append(char(0x80 | 126));
        frame.append(char(n >> 8));
        frame.append(char(n & 0xFF));
    } else {
        frame.append(char(0x80 | 127));
        for (int i = 7; i >= 0; --i)
            frame.append(char((n >> (8 * i)) & 0xFF));
    }
    const QByteArray mask = randomBytes(4);
    frame += mask;
    for (int i = 0; i < payload.size(); ++i)
        frame.append(char(payload.at(i) ^ mask.at(i % 4)));
    m_socket->write(frame);
}

void WebSocketClient::finish(const QString &why)
{
    if (m_state == State::Idle)
        return;
    m_state = State::Idle;
    m_error = why;
    m_socket->abort();
    m_buffer.clear();
    m_message.clear();
    emit closed();
}
