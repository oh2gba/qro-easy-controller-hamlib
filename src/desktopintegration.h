#pragma once

#include <QString>
#include <QStringList>

// Linux desktop entry for a program run from a build folder or an AppImage. Wayland compositors take
// the window icon from the desktop entry named like the application id, so without one the window
// shows a generic icon. The entry goes to ~/.local/share/applications and the icons to
// ~/.local/share/icons/hicolor; both are marked as written by the program and removed when turned off.
// An entry installed by a package or by "cmake --install" is left alone.
namespace DesktopIntegration {

// True on Linux with a real windowing system.
bool supported();

// Writes or removes the entry for the running program in the user's data folder.
void apply(bool enabled);

// The steps of apply(), with the folder, program path and entries elsewhere given (tests).
// Returns true when something was written or removed.
bool update(const QString &dataDir, const QString &executable, bool enabled, const QStringList &otherEntries);

// The desktop entry for executable, made from the installed template.
QString entryText(const QString &templateText, const QString &executable);

}
