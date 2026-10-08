#include "format.h"

#include <cstdlib>

namespace fmt {

QString kf(uint16_t v)
{
    int s = static_cast<int16_t>(v);
    return QStringLiteral("%1%2").arg(s < 0 ? QLatin1Char('-') : QLatin1Char('+')).arg(std::abs(s), 5, 10, QLatin1Char('0'));
}

QString kh(uint16_t v, int digits)
{
    return QStringLiteral("%1").arg(v, digits, 16, QLatin1Char('0')).toUpper();
}

QString km(uint16_t v, int bits)
{
    QString s;
    for (int i = bits - 1; i >= 0; --i) {
        s += ((v >> i) & 1) ? QLatin1Char('1') : QLatin1Char('0');
        if (i % 4 == 0 && i > 0) s += QLatin1Char(' ');
    }
    return s;
}

QString kt(const s5::TimerState& t)
{
    return QStringLiteral("%1.%2").arg(t.value, 3, 10, QLatin1Char('0')).arg(t.base);
}

QString kz(uint16_t v)
{
    return QStringLiteral("%1").arg(v, 3, 10, QLatin1Char('0'));
}

}  // namespace fmt
