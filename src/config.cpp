#include "config.h"

#include "bands.h"

#include <QCoreApplication>
#include <QSettings>

namespace {

int clampPort(int v)
{
    return (v >= Config::Keep && v <= Config::PortCount) ? v : Config::Keep;
}

}

Config::Config()
{
    for (int i = 0; i < PortCount; ++i)
        antennaNames << QString();
}

void Config::load(QSettings &s)
{
    rigHost = s.value(QStringLiteral("radio/host"), rigHost).toString();
    rigPort = quint16(s.value(QStringLiteral("radio/port"), rigPort).toUInt());
    pollMs = qBound(1000, s.value(QStringLiteral("radio/poll_ms"), pollMs).toInt(), 60000);

    controllerHost = s.value(QStringLiteral("controller/host"), controllerHost).toString();
    controllerPort = quint16(s.value(QStringLiteral("controller/port"), controllerPort).toUInt());

    for (int i = 0; i < PortCount; ++i)
        antennaNames[i] = s.value(QStringLiteral("antennas/%1").arg(i + 1)).toString();

    routes.clear();
    for (const QString &band : Bands::names()) {
        BandRoute r;
        r.a = clampPort(s.value(QStringLiteral("routes/%1/a").arg(band), Keep).toInt());
        r.b = clampPort(s.value(QStringLiteral("routes/%1/b").arg(band), Keep).toInt());
        routes.insert(band, r);
    }

    follow = s.value(QStringLiteral("switching/follow"), follow).toBool();
    desktopEntry = s.value(QStringLiteral("desktop/menu_entry"), desktopEntry).toBool();
}

void Config::save(QSettings &s) const
{
    s.setValue(QStringLiteral("radio/host"), rigHost);
    s.setValue(QStringLiteral("radio/port"), rigPort);
    s.setValue(QStringLiteral("radio/poll_ms"), pollMs);
    s.setValue(QStringLiteral("controller/host"), controllerHost);
    s.setValue(QStringLiteral("controller/port"), controllerPort);
    for (int i = 0; i < PortCount; ++i)
        s.setValue(QStringLiteral("antennas/%1").arg(i + 1), antennaNames.value(i));
    for (auto it = routes.cbegin(); it != routes.cend(); ++it) {
        s.setValue(QStringLiteral("routes/%1/a").arg(it.key()), it.value().a);
        s.setValue(QStringLiteral("routes/%1/b").arg(it.key()), it.value().b);
    }
    s.setValue(QStringLiteral("switching/follow"), follow);
    s.setValue(QStringLiteral("desktop/menu_entry"), desktopEntry);
}

QString Config::portLabel(int port) const
{
    if (port == Keep)
        return QCoreApplication::translate("Config", "unchanged");
    if (port == Off)
        return QCoreApplication::translate("Config", "off");
    const QString name = antennaNames.value(port - 1).trimmed();
    return name.isEmpty() ? QStringLiteral("ANT%1").arg(port) : QStringLiteral("ANT%1 %2").arg(port).arg(name);
}

QString Config::shortLabel(int port) const
{
    const QString name = port > 0 ? antennaNames.value(port - 1).trimmed() : QString();
    return name.isEmpty() ? portLabel(port) : name;
}

QStringList Config::conflicts() const
{
    QStringList out;
    for (const QString &band : Bands::names()) {
        const BandRoute r = route(band);
        if (r.a > 0 && r.a == r.b)
            out << QCoreApplication::translate("Config", "%1: both radios on %2").arg(band, portLabel(r.a));
    }
    return out;
}
