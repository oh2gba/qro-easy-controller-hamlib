#include "easycontroller.h"

#include "logbus.h"
#include "websocketclient.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace {

constexpr int RetryMs = 5000;
constexpr int ConnectTimeoutMs = 4000;
constexpr int PollMs = 2000; // state poll; also notices front panel changes
constexpr int DeadAfterMs = 12000;
constexpr int StepPollMs = 400;
constexpr int StepTimeoutMs = 3000;
const QString Path = QStringLiteral("/xxws");

int portFromBits(int bits)
{
    for (int p = 0; p < 6; ++p)
        if (bits & (1 << p))
            return p + 1;
    return 0;
}

int jsonInt(const QJsonValue &v, bool *ok)
{
    if (v.isDouble()) {
        *ok = true;
        return v.toInt();
    }
    if (v.isString())
        return v.toString().toInt(ok);
    *ok = false;
    return 0;
}

QString bankName(int bank)
{
    return bank == 1 ? QStringLiteral("TRX A") : QStringLiteral("TRX B");
}

}

EasyController::EasyController(QObject *parent)
    : QObject(parent)
    , m_ws(new WebSocketClient(this))
{
    connect(m_ws, &WebSocketClient::opened, this, [this] {
        m_lastRx.restart();
        m_ws->sendText(QStringLiteral("G"));
    });
    connect(m_ws, &WebSocketClient::textReceived, this, &EasyController::onText);
    connect(m_ws, &WebSocketClient::closed, this, [this] { onLost(m_ws->errorString()); });

    m_retry.setSingleShot(true);
    m_retry.setInterval(RetryMs);
    connect(&m_retry, &QTimer::timeout, this, &EasyController::connectNow);

    m_connectTimeout.setSingleShot(true);
    connect(&m_connectTimeout, &QTimer::timeout, this, [this] { onLost(tr("no answer")); });

    m_keepAlive.setInterval(PollMs);
    connect(&m_keepAlive, &QTimer::timeout, this, [this] {
        if (m_lastRx.elapsed() > DeadAfterMs)
            onLost(tr("stopped answering"));
        else
            m_ws->sendText(QStringLiteral("G"));
    });

    m_stepPoll.setInterval(StepPollMs);
    connect(&m_stepPoll, &QTimer::timeout, this, [this] { m_ws->sendText(QStringLiteral("G")); });

    m_stepDeadline.setSingleShot(true);
    connect(&m_stepDeadline, &QTimer::timeout, this, [this] {
        const SwitchStep &s = m_steps.at(m_step);
        const int port = s.bank == 1 ? s.a : s.b;
        finishApply(false, false,
                    port ? tr("%1 did not switch to ANT%2 (transmitting, or antenna held by the other radio?)")
                               .arg(bankName(s.bank))
                               .arg(port)
                         : tr("%1 did not switch off").arg(bankName(s.bank)));
    });
}

void EasyController::setEndpoint(const QString &host, quint16 port)
{
    const bool same = host == m_host && port == m_port;
    if (same && (m_link == Link::Connected || m_link == Link::Connecting))
        return;
    if (!same && m_applying)
        finishApply(false, true, tr("controller address changed"));
    m_host = host;
    m_port = port;
    m_ws->close();
    m_connectTimeout.stop();
    m_keepAlive.stop();
    m_retry.stop();
    m_hasState = false;
    clearPtt();
    if (m_host.isEmpty()) {
        setLink(Link::NotConfigured);
    } else if (m_running) {
        m_link = Link::Unreachable; // straight to Connecting, without announcing a failure
        connectNow();
    } else {
        setLink(Link::Unreachable);
    }
}

void EasyController::start()
{
    m_running = true;
    connectNow();
}

void EasyController::connectNow()
{
    m_retry.stop();
    if (m_host.isEmpty()) {
        setLink(Link::NotConfigured);
        return;
    }
    if (m_link == Link::Connecting || m_link == Link::Connected)
        return;
    setLink(Link::Connecting);
    m_ws->open(m_host, m_port, Path);
    m_connectTimeout.start(ConnectTimeoutMs);
}

void EasyController::onText(const QString &text)
{
    m_lastRx.restart();
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isObject()) {
        logLine(tr("Controller sent something unexpected: %1").arg(text.left(120)));
        return;
    }
    const QJsonObject obj = doc.object();

    bool ok = false;
    if (obj.contains(QLatin1String("B0"))) {
        const int mask = jsonInt(obj.value(QLatin1String("B0")), &ok);
        if (ok) {
            const ControllerState s = decode(mask);
            const bool changed = !m_hasState || s != m_state;
            m_state = s;
            m_hasState = true;
            if (m_link != Link::Connected) {
                m_connectTimeout.stop();
                m_keepAlive.start();
                logLine(tr("Easy Controller connected at %1:%2").arg(m_host).arg(m_port));
                setLink(Link::Connected);
            }
            if (changed)
                emit stateChanged(s);
            if (m_applying)
                checkStep();
        }
    }
    for (int bank = 1; bank <= 2; ++bank) {
        const QString key = QStringLiteral("IsPTT%1").arg(bank);
        if (!obj.contains(key))
            continue;
        const bool on = jsonInt(obj.value(key), &ok) != 0 && ok;
        bool &ptt = bank == 1 ? m_pttA : m_pttB;
        if (on != ptt) {
            ptt = on;
            emit pttChanged(bank, on);
        }
    }
}

void EasyController::onLost(const QString &why)
{
    const bool wasConnected = m_link == Link::Connected;
    m_connectTimeout.stop();
    m_keepAlive.stop();
    m_ws->close();
    m_hasState = false;
    m_error = why;
    clearPtt();
    if (m_applying)
        finishApply(false, true, tr("connection to the controller lost"));
    if (wasConnected)
        logLine(tr("Easy Controller connection lost: %1").arg(why));
    setLink(m_host.isEmpty() ? Link::NotConfigured : Link::Unreachable);
    if (m_running && !m_host.isEmpty())
        m_retry.start();
}

void EasyController::clearPtt()
{
    for (int bank = 1; bank <= 2; ++bank) {
        bool &ptt = bank == 1 ? m_pttA : m_pttB;
        if (ptt) {
            ptt = false;
            emit pttChanged(bank, false);
        }
    }
}

void EasyController::setLink(Link link)
{
    if (link == m_link)
        return;
    m_link = link;
    emit linkChanged(link);
}

void EasyController::apply(int wantA, int wantB)
{
    if (m_link != Link::Connected || !m_hasState || m_applying) {
        emit applyFinished(false, m_link != Link::Connected, tr("controller not ready"));
        return;
    }
    QString error;
    const QList<SwitchStep> steps = plan(m_state.a, m_state.b, wantA, wantB, &error);
    if (!error.isEmpty()) {
        emit applyFinished(false, false, error);
        return;
    }
    if (steps.isEmpty()) {
        emit applyFinished(true, false, {});
        return;
    }
    m_steps = steps;
    m_step = 0;
    m_applying = true;
    sendStep();
}

void EasyController::sendStep()
{
    const SwitchStep &s = m_steps.at(m_step);
    const QString command = QStringLiteral("X/0/%1/%2").arg(encode(s.a, s.b)).arg(s.bank);
    logLine(tr("Controller ← %1").arg(command));
    m_ws->sendText(command);
    m_stepPoll.start();
    m_stepDeadline.start(StepTimeoutMs);
}

void EasyController::checkStep()
{
    const SwitchStep &s = m_steps.at(m_step);
    if (m_state.a != s.a || m_state.b != s.b)
        return;
    if (++m_step < m_steps.size())
        sendStep();
    else
        finishApply(true, false, {});
}

void EasyController::finishApply(bool ok, bool linkLost, const QString &message)
{
    m_applying = false;
    m_stepPoll.stop();
    m_stepDeadline.stop();
    m_steps.clear();
    emit applyFinished(ok, linkLost, message);
}

QList<SwitchStep> EasyController::plan(int curA, int curB, int wantA, int wantB, QString *error)
{
    if (wantA < 0)
        wantA = curA;
    if (wantB < 0)
        wantB = curB;
    if (wantA > 0 && wantA == wantB) {
        *error = tr("TRX A and TRX B are both mapped to ANT%1").arg(wantA);
        return {};
    }
    const bool changeA = wantA != curA;
    const bool changeB = wantB != curB;
    if (changeA && changeB) {
        if (wantA > 0 && wantA == curB && wantB > 0 && wantB == curA) // swap: park B first
            return {{2, curA, 0}, {1, wantA, 0}, {2, wantA, wantB}};
        if (wantA > 0 && wantA == curB) // B frees A's new antenna first
            return {{2, curA, wantB}, {1, wantA, wantB}};
        return {{1, wantA, curB}, {2, wantA, wantB}};
    }
    if (changeA)
        return {{1, wantA, curB}};
    if (changeB)
        return {{2, curA, wantB}};
    return {};
}

ControllerState EasyController::decode(int mask)
{
    ControllerState s;
    s.a = portFromBits(mask & 0x3F);
    s.b = portFromBits((mask >> 8) & 0x3F);
    s.usedA = mask & (1 << 6);
    s.usedB = mask & (1 << 14);
    return s;
}

int EasyController::encode(int a, int b)
{
    return (a > 0 ? 1 << (a - 1) : 0) | (b > 0 ? 1 << (b - 1 + 8) : 0);
}
