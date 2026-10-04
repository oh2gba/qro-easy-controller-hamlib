#include "desktopintegration.h"

#include "logbus.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStandardPaths>

namespace {

const QString AppId = QStringLiteral("qro-easy-controller-hamlib");
const QString Marker = QStringLiteral("X-Easy-Controller-Hamlib-Generated=true");
const int IconSizes[] = {16, 24, 32, 48, 64, 128, 256};

QString entryPath(const QString &dataDir)
{
    return dataDir + QStringLiteral("/applications/") + AppId + QStringLiteral(".desktop");
}

QString iconPath(const QString &dataDir, int size)
{
    return dataDir + QStringLiteral("/icons/hicolor/%1x%1/apps/").arg(size) + AppId + QStringLiteral(".png");
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool writeFile(const QString &path, const QByteArray &data)
{
    if (readFile(path) == data)
        return false;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(data);
    return true;
}

bool isOurs(const QString &path)
{
    return readFile(path).contains(Marker.toUtf8());
}

// Exec quoting of the desktop entry specification: quote, escape ", `, $ and \, then escape \ again
// for the string value.
QString quoteExec(const QString &path)
{
    QString quoted;
    for (const QChar c : path) {
        if (c == QLatin1Char('"') || c == QLatin1Char('`') || c == QLatin1Char('$') || c == QLatin1Char('\\'))
            quoted += QLatin1Char('\\');
        quoted += c;
    }
    quoted.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    return QLatin1Char('"') + quoted + QLatin1Char('"');
}

}

namespace DesktopIntegration {

bool supported()
{
#ifdef Q_OS_LINUX
    const QString platform = QGuiApplication::platformName();
    return platform == QLatin1String("wayland") || platform == QLatin1String("xcb");
#else
    return false;
#endif
}

QString entryText(const QString &templateText, const QString &executable)
{
    QStringList lines;
    for (const QString &line : templateText.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("Exec=")))
            lines << QStringLiteral("Exec=") + quoteExec(executable) << QStringLiteral("TryExec=") + executable;
        else if (!line.isEmpty())
            lines << line;
    }
    lines << Marker;
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

bool update(const QString &dataDir, const QString &executable, bool enabled, const QStringList &otherEntries)
{
    const QString entry = entryPath(dataDir);
    const bool ours = isOurs(entry);

    if (!enabled || !otherEntries.isEmpty()) { // turned off, or a package provides the entry
        if (!ours)
            return false;
        QFile::remove(entry);
        for (int size : IconSizes)
            QFile::remove(iconPath(dataDir, size));
        logLine(QCoreApplication::translate("DesktopIntegration", "Removed the application menu entry %1").arg(entry));
        return true;
    }
    if (QFile::exists(entry) && !ours)
        return false; // someone else's file at our name

    bool changed = false;
    for (int size : IconSizes)
        changed |= writeFile(iconPath(dataDir, size), readFile(QStringLiteral(":/icons/%1.png").arg(size)));
    const QString text = entryText(QString::fromUtf8(readFile(QStringLiteral(":/desktop/") + AppId + QStringLiteral(".desktop"))),
                                   executable);
    if (writeFile(entry, text.toUtf8())) {
        changed = true;
        logLine(QCoreApplication::translate("DesktopIntegration", "Application menu entry %1 points to %2").arg(entry, executable));
    }
    return changed;
}

void apply(bool enabled)
{
    if (!supported())
        return;
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    const QString executable = appImage.isEmpty() ? QCoreApplication::applicationFilePath() : appImage;
    QStringList others = QStandardPaths::locateAll(QStandardPaths::ApplicationsLocation, AppId + QStringLiteral(".desktop"));
    others.removeAll(entryPath(dataDir));
    update(dataDir, executable, enabled, others);
}

}
