#pragma once

#include "config.h"
#include "discovery.h"
#include "easycontroller.h"
#include "rigclient.h"

#include <QObject>
#include <optional>

// Follows the radio's band and puts the controller's banks on the antennas mapped for it.
// When the stored controller address does not answer, searches the local network once per
// switch request and stores the address it finds.
class Engine : public QObject
{
    Q_OBJECT
public:
    enum class Status { Idle, Busy, Ok, Failed };
    Q_ENUM(Status)

    explicit Engine(Config &config, QObject *parent = nullptr);

    RigClient &rig() { return m_rig; }
    EasyController &controller() { return m_ctl; }
    Discovery &discovery() { return m_discovery; }

    void start();
    void reconfigure(); // after the settings changed
    void applyNow();    // put the current band's antennas back
    void setFollow(bool on);

    QString band() const { return m_band; }

    // How the controller differs from the table for the current band, one line per radio;
    // empty when it matches or nothing can be compared (no band, no controller, nothing mapped).
    QStringList differences() const;

    Status status() const { return m_status; }
    QString statusText() const { return m_statusText; }
    bool isSearching() const { return m_discovery.isRunning(); }

signals:
    void bandChanged(const QString &band);
    void statusChanged(Engine::Status status, const QString &text);
    void configChanged(); // the engine stored a new controller address
    void comparisonChanged(); // differences() may have changed

private:
    struct Pending {
        QString band;
        BandRoute route;
        quint64 id;
    };

    void onFrequency(qint64 hz);
    void request(const QString &band);
    void tryApply();
    void startSearch();
    void onDiscoveryFinished(const QStringList &hosts);
    void onApplyFinished(bool ok, bool linkLost, const QString &message);
    void setStatus(Status status, const QString &text);
    QString describe() const; // "TRX A: Yagi, TRX B: GP"
    void compare();

    Config &m_cfg;
    RigClient m_rig;
    EasyController m_ctl;
    Discovery m_discovery;
    QString m_band;
    std::optional<Pending> m_pending;  // the latest request, until it is on the controller
    std::optional<Pending> m_inFlight; // the request the controller is working on
    quint64 m_nextId = 1;
    bool m_triedConnect = false;
    bool m_searched = false;
    Status m_status = Status::Idle;
    QString m_statusText;
    QStringList m_differences;
};
