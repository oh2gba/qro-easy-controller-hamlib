#pragma once

#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QStringList>

class QWebSocket;
class QWebSocketServer;

// Simulated QRO.cz Easy Controller 6-2, speaking the protocol qro-easy-controller-hamlib expects:
// "G" returns {"B0": mask}, "X/0/<mask>/<bank>" selects the bank's port from mask,
// PTT changes are pushed as {"IsPTT1": 0|1} / {"IsPTT2": 0|1}.
// Like the real box it keeps the first radio on an antenna (first win) and refuses to
// switch a bank while that bank transmits.
class FakeController : public QObject
{
    Q_OBJECT
public:
    explicit FakeController(QObject *parent = nullptr);
    ~FakeController() override;

    bool listen(const QHostAddress &address, quint16 port);
    void close();
    quint16 port() const;

    int a() const { return m_a; }
    int b() const { return m_b; }
    void setPorts(int a, int b, bool announce = true); // front panel buttons
    void setPtt(int bank, bool on);
    void setPushAfterSet(bool on) { m_pushAfterSet = on; }
    QStringList commands() const { return m_commands; }
    QStringList paths() const { return m_paths; }
    void clearCommands() { m_commands.clear(); }
    int mask() const;

signals:
    void commandReceived(const QString &command);

private:
    void onConnection();
    void onText(QWebSocket *client, const QString &text);
    void broadcast(const QString &text);
    QString stateJson() const;

    QWebSocketServer *m_server;
    QList<QWebSocket *> m_clients;
    int m_a = 0;
    int m_b = 0;
    bool m_pttA = false;
    bool m_pttB = false;
    bool m_pushAfterSet = false;
    QStringList m_commands;
    QStringList m_paths;
};
