#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QTcpSocket;

// Minimal RFC 6455 client: text frames out, text/binary frames in, ping/pong and close handled.
// Enough for the Easy Controller and free of a Qt WebSockets runtime dependency.
class WebSocketClient : public QObject
{
    Q_OBJECT
public:
    explicit WebSocketClient(QObject *parent = nullptr);

    void open(const QString &host, quint16 port, const QString &path);
    // Drops the connection without emitting closed().
    void close();
    bool isOpen() const { return m_state == State::Open; }
    bool sendText(const QString &text);
    QString errorString() const { return m_error; }

signals:
    void socketConnected(); // TCP is up, upgrade request sent
    void opened();          // upgrade accepted
    void textReceived(const QString &text);
    void closed(); // connection failed or ended; see errorString()

private:
    enum class State { Idle, Connecting, Handshake, Open };

    void onConnected();
    void onReadyRead();
    bool readHandshake();
    void readFrames();
    void sendFrame(quint8 opcode, const QByteArray &payload);
    void finish(const QString &why);

    QTcpSocket *m_socket;
    State m_state = State::Idle;
    QString m_host;
    QString m_path;
    quint16 m_port = 0;
    QByteArray m_key;
    QByteArray m_buffer;
    QByteArray m_message;
    QString m_error;
};
