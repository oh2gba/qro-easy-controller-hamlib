#include "engine.h"

#include "bands.h"
#include "logbus.h"

Engine::Engine(Config &config, QObject *parent)
    : QObject(parent)
    , m_cfg(config)
{
    connect(&m_rig, &RigClient::frequencyChanged, this, &Engine::onFrequency);
    connect(&m_rig, &RigClient::pttChanged, this, [this] { tryApply(); });
    connect(&m_ctl, &EasyController::pttChanged, this, [this] { tryApply(); });
    connect(&m_ctl, &EasyController::linkChanged, this, [this](EasyController::Link link) {
        if (link != EasyController::Link::Connecting)
            tryApply();
    });
    connect(&m_ctl, &EasyController::applyFinished, this, &Engine::onApplyFinished);
    connect(&m_ctl, &EasyController::stateChanged, this, &Engine::compare);
    connect(&m_ctl, &EasyController::linkChanged, this, &Engine::compare);
    connect(&m_discovery, &Discovery::finished, this, &Engine::onDiscoveryFinished);
}

void Engine::start()
{
    reconfigure();
    m_rig.start();
    m_ctl.start();
}

void Engine::reconfigure()
{
    m_rig.setEndpoint(m_cfg.rigHost, m_cfg.rigPort);
    m_rig.setPollInterval(m_cfg.pollMs);
    m_ctl.setEndpoint(m_cfg.controllerHost, m_cfg.controllerPort);
    compare(); // the table may have changed
}

void Engine::applyNow()
{
    if (m_band.isEmpty()) {
        setStatus(Status::Idle, tr("No band from the radio yet"));
        return;
    }
    request(m_band);
}

void Engine::setFollow(bool on)
{
    if (m_cfg.follow == on)
        return;
    m_cfg.follow = on;
    emit configChanged();
    if (on && !m_band.isEmpty())
        request(m_band);
}

void Engine::onFrequency(qint64 hz)
{
    const QString band = Bands::forFrequency(hz);
    if (band.isEmpty() || band == m_band)
        return; // between bands the last band stays
    m_band = band;
    logLine(tr("Radio on %1").arg(band));
    emit bandChanged(band);
    if (m_cfg.follow)
        request(band);
    compare(); // after request(): a switch on its way is not a difference worth logging
}

void Engine::request(const QString &band)
{
    const BandRoute route = m_cfg.route(band);
    if (route.a == Config::Keep && route.b == Config::Keep) {
        m_pending.reset();
        setStatus(Status::Idle, tr("No antennas set for this band"));
        return;
    }
    m_pending = Pending{band, route, m_nextId++};
    m_triedConnect = false;
    m_searched = false;
    tryApply();
}

void Engine::tryApply()
{
    if (!m_pending || m_ctl.isApplying() || m_discovery.isRunning())
        return;
    const QString band = m_pending->band;
    if (m_rig.ptt() || m_ctl.ptt(1) || m_ctl.ptt(2)) {
        setStatus(Status::Busy, tr("Waiting for receive"));
        return;
    }
    switch (m_ctl.link()) {
    case EasyController::Link::Connected:
        setStatus(Status::Busy, tr("Switching"));
        m_inFlight = m_pending;
        m_ctl.apply(m_pending->route.a, m_pending->route.b);
        return;
    case EasyController::Link::Connecting:
        setStatus(Status::Busy, tr("Connecting to the controller"));
        return;
    case EasyController::Link::Unreachable:
        if (!m_triedConnect) {
            m_triedConnect = true;
            setStatus(Status::Busy, tr("Connecting to the controller"));
            m_ctl.connectNow();
            return;
        }
        startSearch();
        return;
    case EasyController::Link::NotConfigured:
        startSearch();
        return;
    }
}

void Engine::startSearch()
{
    if (m_searched) {
        if (m_status != Status::Failed) // keep the search result as the reason
            setStatus(Status::Failed, tr("Controller not reachable"));
        return;
    }
    m_searched = true;
    setStatus(Status::Busy, tr("Searching the network for the controller"));
    m_discovery.start(m_cfg.controllerPort);
}

void Engine::onDiscoveryFinished(const QStringList &hosts)
{
    if (hosts.size() == 1) {
        if (m_cfg.controllerHost != hosts.first()) {
            m_cfg.controllerHost = hosts.first();
            logLine(tr("Stored controller address %1").arg(hosts.first()));
            emit configChanged();
        }
        m_triedConnect = true;
        m_ctl.setEndpoint(m_cfg.controllerHost, m_cfg.controllerPort);
        tryApply();
        return;
    }
    if (hosts.isEmpty())
        setStatus(Status::Failed, tr("Controller not found on the network"));
    else
        setStatus(Status::Failed,
                  tr("%1 controllers found (%2), choose one in Settings").arg(hosts.size()).arg(hosts.join(QStringLiteral(", "))));
}

void Engine::onApplyFinished(bool ok, bool linkLost, const QString &message)
{
    if (!m_inFlight)
        return;
    const Pending done = *m_inFlight;
    m_inFlight.reset();
    // The radio may have changed band while the controller was switching.
    const bool superseded = !m_pending || m_pending->id != done.id;
    if (ok) {
        if (!superseded)
            m_pending.reset();
        setStatus(Status::Ok, describe());
    } else {
        if (!superseded && !linkLost)
            m_pending.reset(); // the controller refused; retrying would not help
        setStatus(Status::Failed, message);
    }
    if (superseded)
        tryApply();
}

void Engine::setStatus(Status status, const QString &text)
{
    if (status == m_status && text == m_statusText)
        return;
    m_status = status;
    m_statusText = text;
    logLine(text);
    emit statusChanged(status, text);
}

QStringList Engine::differences() const
{
    if (m_band.isEmpty() || m_ctl.link() != EasyController::Link::Connected)
        return {};
    const BandRoute route = m_cfg.route(m_band);
    const ControllerState s = m_ctl.state();
    QStringList out;
    if (route.a != Config::Keep && route.a != s.a)
        out << tr("TRX A should be %1").arg(m_cfg.shortLabel(route.a));
    if (route.b != Config::Keep && route.b != s.b)
        out << tr("TRX B should be %1").arg(m_cfg.shortLabel(route.b));
    return out;
}

void Engine::compare()
{
    const QStringList now = differences();
    if (now == m_differences)
        return;
    m_differences = now;
    // Steps of a switch pass through states that differ; only settled states are worth a log line.
    if (!now.isEmpty() && !m_ctl.isApplying() && !m_pending)
        logLine(tr("Not as in the table: %1").arg(now.join(QStringLiteral(", "))));
    emit comparisonChanged();
}

QString Engine::describe() const
{
    const ControllerState s = m_ctl.state();
    return tr("TRX A: %1, TRX B: %2").arg(m_cfg.shortLabel(s.a), m_cfg.shortLabel(s.b));
}
