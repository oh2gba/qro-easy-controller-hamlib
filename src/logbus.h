#pragma once

#include <QObject>

// Timestamped event lines for the log pane, optionally echoed to stderr.
class LogBus : public QObject
{
    Q_OBJECT
public:
    static LogBus &instance();
    void setEcho(bool on) { m_echo = on; }
    void write(const QString &text);

signals:
    void line(const QString &stamped);

private:
    bool m_echo = false;
};

inline void logLine(const QString &text)
{
    LogBus::instance().write(text);
}
