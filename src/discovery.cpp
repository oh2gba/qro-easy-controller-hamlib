#include "discovery.h"

#include "logbus.h"
#include "websocketclient.h"

#include <QHostAddress>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QTimer>

namespace {

constexpr int MaxParallel = 64;
constexpr int ConnectTimeoutMs = 900;
constexpr int AnswerTimeoutMs = 2000;
const QStringList HostNames{QStringLiteral("sixbytwo.local"), QStringLiteral("sixbytwo"),
                            QStringLiteral("impero.local"), QStringLiteral("impero")};
// Bridges and adapters of containers, virtual machines and VPNs: never where the controller is.
const QStringList VirtualPrefixes{QStringLiteral("docker"), QStringLiteral("br-"),     QStringLiteral("veth"),
                                  QStringLiteral("virbr"),  QStringLiteral("vboxnet"), QStringLiteral("vmnet"),
                                  QStringLiteral("lxc"),    QStringLiteral("lxd"),     QStringLiteral("cni"),
                                  QStringLiteral("podman"), QStringLiteral("flannel"), QStringLiteral("bridge"),
                                  QStringLiteral("utun"),   QStringLiteral("awdl"),    QStringLiteral("llw")};
const QStringList VirtualWords{QStringLiteral("vEthernet"), QStringLiteral("Hyper-V"), QStringLiteral("VirtualBox"),
                               QStringLiteral("VMware"),    QStringLiteral("WSL"),     QStringLiteral("Loopback")};

bool isVirtual(const QNetworkInterface &nif)
{
    for (const QString &p : VirtualPrefixes)
        if (nif.name().startsWith(p))
            return true;
    for (const QString &w : VirtualWords) // Windows names adapters for people
        if (nif.humanReadableName().contains(w, Qt::CaseInsensitive))
            return true;
    return false;
}

bool isControllerReply(const QString &text)
{
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    return doc.isObject() && doc.object().contains(QLatin1String("B0"));
}

}

Discovery::Discovery(QObject *parent)
    : QObject(parent)
{
}

QStringList Discovery::subnetHosts()
{
    QStringList hosts;
    QSet<quint32> seen;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &nif : interfaces) {
        const auto f = nif.flags();
        if (!(f & QNetworkInterface::IsUp) || !(f & QNetworkInterface::IsRunning)
            || (f & QNetworkInterface::IsLoopBack) || (f & QNetworkInterface::IsPointToPoint) || isVirtual(nif))
            continue;
        const auto entries = nif.addressEntries();
        for (const QNetworkAddressEntry &e : entries) {
            if (e.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            int prefix = e.prefixLength();
            if (prefix > 30)
                continue;
            prefix = qMax(prefix, 24);
            const quint32 mask = 0xFFFFFFFFu << (32 - prefix);
            const quint32 net = e.ip().toIPv4Address() & mask;
            const quint32 broadcast = net | ~mask;
            for (quint32 a = net + 1; a < broadcast; ++a)
                if (!seen.contains(a)) {
                    seen.insert(a);
                    hosts << QHostAddress(a).toString();
                }
        }
    }
    return hosts;
}

void Discovery::start(quint16 port)
{
    cancel();
    m_port = port;
    m_running = true;
    m_found.clear();
    m_seen.clear();
    m_queue.clear();
    m_done = 0;
    m_total = 0;
    m_active = 0;
    m_lookups = 0;

    if (!m_candidates.isEmpty()) {
        for (const QString &h : std::as_const(m_candidates))
            enqueue(h);
    } else {
        const int gen = m_generation;
        for (const QString &name : HostNames) {
            ++m_lookups;
            QHostInfo::lookupHost(name, this, [this, gen](const QHostInfo &info) {
                if (gen != m_generation)
                    return;
                --m_lookups;
                const auto addresses = info.addresses();
                for (const QHostAddress &a : addresses)
                    if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback())
                        enqueue(a.toString());
                pump();
                maybeFinish();
            });
        }
        const QStringList hosts = subnetHosts();
        for (const QString &h : hosts)
            enqueue(h);
    }
    logLine(tr("Searching for the Easy Controller on port %1 (%2 addresses)").arg(m_port).arg(m_total));
    emit progress(0, m_total);
    pump();
    QTimer::singleShot(0, this, [this, gen = m_generation] {
        if (gen == m_generation)
            maybeFinish();
    });
}

void Discovery::cancel()
{
    ++m_generation; // in-flight probes and lookups see the change and drop their results
    m_running = false;
    m_queue.clear();
    m_active = 0;
}

void Discovery::enqueue(const QString &host)
{
    if (m_seen.contains(host))
        return;
    m_seen.insert(host);
    m_queue.prepend(host); // named and explicit hosts first; subnet order does not matter
    ++m_total;
}

void Discovery::pump()
{
    while (m_running && m_active < MaxParallel && !m_queue.isEmpty())
        probe(m_queue.takeFirst());
}

void Discovery::probe(const QString &host)
{
    ++m_active;
    auto *ws = new WebSocketClient(this);
    auto *timer = new QTimer(ws);
    timer->setSingleShot(true);
    const int gen = m_generation;

    auto done = [this, ws, host, gen](bool ok) {
        if (ws->property("done").toBool())
            return;
        ws->setProperty("done", true);
        ws->close();
        ws->deleteLater();
        if (gen == m_generation)
            probeDone(host, ok);
    };
    connect(timer, &QTimer::timeout, ws, [done] { done(false); });
    connect(ws, &WebSocketClient::socketConnected, ws, [timer] { timer->start(AnswerTimeoutMs); });
    connect(ws, &WebSocketClient::opened, ws, [ws] { ws->sendText(QStringLiteral("G")); });
    connect(ws, &WebSocketClient::textReceived, ws, [done](const QString &text) {
        if (isControllerReply(text))
            done(true);
    });
    connect(ws, &WebSocketClient::closed, ws, [done] { done(false); });
    timer->start(ConnectTimeoutMs);
    ws->open(host, m_port, QStringLiteral("/xxws"));
}

void Discovery::probeDone(const QString &host, bool ok)
{
    --m_active;
    ++m_done;
    if (ok && !m_found.contains(host)) {
        m_found << host;
        logLine(tr("Easy Controller answered at %1:%2").arg(host).arg(m_port));
        emit hostFound(host);
    }
    emit progress(m_done, m_total);
    pump();
    maybeFinish();
}

void Discovery::maybeFinish()
{
    if (!m_running || m_active > 0 || !m_queue.isEmpty() || m_lookups > 0)
        return;
    m_running = false;
    if (m_found.isEmpty())
        logLine(tr("No Easy Controller found"));
    emit finished(m_found);
}
