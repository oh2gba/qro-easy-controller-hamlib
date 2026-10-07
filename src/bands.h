#pragma once

#include <QString>
#include <QStringList>

// Amateur bands the 6-to-2 switch covers (DC to 80 MHz, including 11m CB and a broad 4m at 60-80 MHz), edges wide enough for all three IARU regions.
namespace Bands {

QStringList names();

// The band containing hz, or an empty string between bands.
QString forFrequency(qint64 hz);

}
