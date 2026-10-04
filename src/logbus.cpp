#include "logbus.h"

#include <QDateTime>

#include <cstdio>

LogBus &LogBus::instance()
{
    static LogBus bus;
    return bus;
}

void LogBus::write(const QString &text)
{
    const QString stamped = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss  ")) + text;
    if (m_echo) {
        std::fprintf(stderr, "%s\n", qPrintable(stamped));
        std::fflush(stderr);
    }
    emit line(stamped);
}
