#include "bands.h"

#include <array>

namespace {

struct Band {
    const char *name;
    qint64 lowHz;
    qint64 highHz;
};

constexpr std::array<Band, 11> Table{{
    {"160m", 1'800'000, 2'000'000},
    {"80m", 3'500'000, 4'000'000},
    {"60m", 5'060'000, 5'450'000},
    {"40m", 7'000'000, 7'300'000},
    {"30m", 10'100'000, 10'150'000},
    {"20m", 14'000'000, 14'350'000},
    {"17m", 18'068'000, 18'168'000},
    {"15m", 21'000'000, 21'450'000},
    {"12m", 24'890'000, 24'990'000},
    {"10m", 28'000'000, 29'700'000},
    {"6m", 50'000'000, 54'000'000},
}};

}

namespace Bands {

QStringList names()
{
    QStringList out;
    for (const Band &b : Table)
        out << QString::fromLatin1(b.name);
    return out;
}

QString forFrequency(qint64 hz)
{
    for (const Band &b : Table)
        if (hz >= b.lowHz && hz <= b.highHz)
            return QString::fromLatin1(b.name);
    return {};
}

}
