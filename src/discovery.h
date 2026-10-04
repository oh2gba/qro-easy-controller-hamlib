#pragma once

#include <QObject>
#include <QSet>
#include <QStringList>

// Finds Easy Controllers on the local IPv4 networks: resolves the names the controller announces
// (sixbytwo.local, Impero) and probes every address of each attached subnet (at most a /24 per
// interface) on the controller port. A host counts only if it answers "G" with a B0 state.
class Discovery : public QObject
{
    Q_OBJECT
public:
    explicit Discovery(QObject *parent = nullptr);

    void start(quint16 port);
    void cancel();
    bool isRunning() const { return m_running; }
    QStringList found() const { return m_found; }

    // Probe exactly these hosts instead of names and subnets (tests).
    void setCandidates(const QStringList &hosts) { m_candidates = hosts; }

    static QStringList subnetHosts();

signals:
    void progress(int done, int total);
    void hostFound(const QString &host);
    void finished(const QStringList &hosts);

private:
    void enqueue(const QString &host);
    void pump();
    void probe(const QString &host);
    void probeDone(const QString &host, bool ok);
    void maybeFinish();

    QStringList m_candidates;
    quint16 m_port = 59;
    bool m_running = false;
    int m_generation = 0;
    QStringList m_queue;
    QSet<QString> m_seen;
    QStringList m_found;
    int m_active = 0;
    int m_lookups = 0;
    int m_done = 0;
    int m_total = 0;
};
