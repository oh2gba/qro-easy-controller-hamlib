#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

class WebSocketClient;

// Ports selected on the two banks; 0 means none.
struct ControllerState {
    int a = 0;
    int b = 0;
    bool usedA = false; // the controller's USED LED: no antenna or a collision
    bool usedB = false;
    bool operator==(const ControllerState &o) const
    {
        return a == o.a && b == o.b && usedA == o.usedA && usedB == o.usedB;
    }
    bool operator!=(const ControllerState &o) const { return !(*this == o); }
};

// One command to the controller and the state expected after it.
struct SwitchStep {
    int bank; // 1 = TRX A, 2 = TRX B
    int a;
    int b;
};

// Talks to a QRO.cz Easy Controller 6-2 over its WebSocket (ws://host:59/xxws).
// The protocol was read from the vendor web UI and has not been confirmed on hardware:
//   "G"                   -> {"B0": mask}, bits 0-5 TRX A ports 1-6, bit 6 USED A,
//                            bits 8-13 TRX B ports 1-6, bit 14 USED B
//   "X/0/<mask>/<1|2>"    -> select ports; the last field names the bank being changed
//   pushed                   {"IsPTT1": 0|1}, {"IsPTT2": 0|1}
class EasyController : public QObject
{
    Q_OBJECT
public:
    enum class Link { NotConfigured, Connecting, Connected, Unreachable };
    Q_ENUM(Link)

    explicit EasyController(QObject *parent = nullptr);

    void setEndpoint(const QString &host, quint16 port);
    void start();      // connect and keep reconnecting while the host is set
    void connectNow(); // try at once instead of waiting for the next retry

    Link link() const { return m_link; }
    QString host() const { return m_host; }
    quint16 port() const { return m_port; }
    QString lastError() const { return m_error; }
    ControllerState state() const { return m_state; }
    bool ptt(int bank) const { return bank == 1 ? m_pttA : m_pttB; }
    bool isApplying() const { return m_applying; }

    // Switch both banks; Config::Keep (-1) leaves a bank, 0 deselects it. Needs link() == Connected.
    // Ends with applyFinished().
    void apply(int wantA, int wantB);

    // The commands that reach (wantA, wantB) from (curA, curB) without asking either bank for
    // the antenna the other one holds at that moment. Empty with *error set on a conflict.
    static QList<SwitchStep> plan(int curA, int curB, int wantA, int wantB, QString *error);
    static ControllerState decode(int mask);
    static int encode(int a, int b);

signals:
    void linkChanged(EasyController::Link link);
    void stateChanged(const ControllerState &state);
    void pttChanged(int bank, bool on);
    void applyFinished(bool ok, bool linkLost, const QString &message);

private:
    void onText(const QString &text);
    void onLost(const QString &why);
    void setLink(Link link);
    void clearPtt();
    void sendStep();
    void checkStep();
    void finishApply(bool ok, bool linkLost, const QString &message);

    WebSocketClient *m_ws;
    QTimer m_retry;
    QTimer m_connectTimeout;
    QTimer m_keepAlive;
    QTimer m_stepPoll;
    QTimer m_stepDeadline;
    QElapsedTimer m_lastRx;
    QString m_host;
    quint16 m_port = 59;
    bool m_running = false;
    Link m_link = Link::NotConfigured;
    QString m_error;
    bool m_hasState = false;
    ControllerState m_state;
    bool m_pttA = false;
    bool m_pttB = false;
    bool m_applying = false;
    QList<SwitchStep> m_steps;
    int m_step = 0;
};
