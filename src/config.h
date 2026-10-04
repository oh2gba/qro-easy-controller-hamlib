#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

class QSettings;

// Antenna ports for the two banks of the controller for one band.
// Values: Config::Keep, Config::Off or a port 1..6.
struct BandRoute {
    int a = -1;
    int b = -1;
    bool operator==(const BandRoute &o) const { return a == o.a && b == o.b; }
};

class Config
{
public:
    static constexpr int Keep = -1;
    static constexpr int Off = 0;
    static constexpr int PortCount = 6;

    QString rigHost = QStringLiteral("localhost");
    quint16 rigPort = 4532;
    int pollMs = 1000;

    QString controllerHost;
    quint16 controllerPort = 59;

    QStringList antennaNames; // PortCount entries, empty means unnamed
    QMap<QString, BandRoute> routes;
    bool follow = true;
    bool desktopEntry = true; // Linux: application menu entry and window icon

    Config();

    void load(QSettings &s);
    void save(QSettings &s) const;

    BandRoute route(const QString &band) const { return routes.value(band); }

    // "ANT2" or "ANT2 Yagi"; "off" and "unchanged" for the special values.
    QString portLabel(int port) const;
    // The antenna's name alone, "ANT2" when unnamed; "off" and "unchanged" for the special values.
    QString shortLabel(int port) const;

    // One line per band that puts both radios on the same antenna.
    QStringList conflicts() const;
};
