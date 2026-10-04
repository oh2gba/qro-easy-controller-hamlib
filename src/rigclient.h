#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

class QTcpSocket;

// Polls frequency and PTT from a rigctld (or anything speaking its protocol) over TCP.
// Keeps reconnecting while the radio or rigctld is away.
class RigClient : public QObject
{
    Q_OBJECT
public:
    explicit RigClient(QObject *parent = nullptr);

    void setEndpoint(const QString &host, quint16 port);
    void setPollInterval(int ms);
    void start();
    void stop();

    bool isOnline() const { return m_online; }
    qint64 frequency() const { return m_frequency; }
    bool ptt() const { return m_ptt; }
    QString host() const { return m_host; }
    quint16 port() const { return m_port; }
    QString lastError() const { return m_error; }

signals:
    void onlineChanged(bool online);
    void frequencyChanged(qint64 hz);
    void pttChanged(bool on);

private:
    enum class Command { None, CheckVfo, Frequency, Ptt };

    void poll();
    void sendNext();
    void onReadyRead();
    void handle(Command command, const QString &reply);
    void drop(const QString &why);
    void setOnline(bool online);
    void setPtt(bool on);

    QTcpSocket *m_socket;
    QTimer m_pollTimer;
    QTimer m_replyTimer;
    QString m_host = QStringLiteral("localhost");
    quint16 m_port = 4532;
    QList<Command> m_queue;
    Command m_pending = Command::None;
    bool m_vfoChecked = false;
    bool m_vfoMode = false;
    bool m_online = false;
    bool m_ptt = false;
    qint64 m_frequency = 0;
    QString m_error;
};
