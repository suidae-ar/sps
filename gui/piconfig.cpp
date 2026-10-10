#include "piconfig.h"

#include <QStringList>

#include <algorithm>

PiConfig::PiConfig()
{
    inGpio.fill(-1);
    outGpio.fill(-1);
    inBias.fill(s5io::Bias::None);
    applyDefaultPins();
}

void PiConfig::applyDefaultPins()
{
    inGpio.fill(-1);
    outGpio.fill(-1);
    const int in[8] = {5, 6, 12, 13, 16, 19, 20, 21};
    const int out[8] = {17, 18, 27, 22, 23, 24, 25, 26};
    std::copy(std::begin(in), std::end(in), inGpio.begin());
    std::copy(std::begin(out), std::end(out), outGpio.begin());
}

std::vector<s5io::InputConfig> PiConfig::inputs() const
{
    std::vector<s5io::InputConfig> v;
    for (int ch = 0; ch < inBytes * 8; ++ch) {
        if (inGpio[ch] < 0) continue;
        v.push_back({ch, inGpio[ch], inBias[ch], ((inInvert >> ch) & 1u) != 0, debounceMs});
    }
    return v;
}

std::vector<s5io::OutputConfig> PiConfig::outputs() const
{
    std::vector<s5io::OutputConfig> v;
    for (int ch = 0; ch < outBytes * 8; ++ch)
        if (outGpio[ch] >= 0) v.push_back({ch, outGpio[ch]});
    return v;
}

QString PiConfig::inputName(int channel) const
{
    return QStringLiteral("E %1.%2").arg(inAddress + channel / 8).arg(channel % 8);
}

QString PiConfig::outputName(int channel) const
{
    return QStringLiteral("A %1.%2").arg(outAddress + channel / 8).arg(channel % 8);
}

namespace {

QString joinInts(const int* v, int n)
{
    QStringList l;
    for (int i = 0; i < n; ++i) l << QString::number(v[i]);
    return l.join(QLatin1Char(','));
}

}  // namespace

// Format: PI;h=<host>;p=<port>;ia=..;ib=..;oa=..;ob=..;ig=5,6,..;og=..;bi=N,U,D,..;iv=<hex>;db=<ms>
QString PiConfig::encode() const
{
    QStringList bias;
    for (s5io::Bias b : inBias) bias << QString::fromLatin1(s5io::biasName(b)).left(1);
    return QStringLiteral("PI;h=%1;p=%2;ia=%3;ib=%4;oa=%5;ob=%6;ig=%7;og=%8;bi=%9;iv=%10;db=%11")
        .arg(host)
        .arg(port)
        .arg(inAddress)
        .arg(inBytes)
        .arg(outAddress)
        .arg(outBytes)
        .arg(joinInts(inGpio.data(), kMaxChannels), joinInts(outGpio.data(), kMaxChannels), bias.join(QLatin1Char(',')))
        .arg(inInvert, 0, 16)
        .arg(debounceMs);
}

bool PiConfig::decode(const QString& text, PiConfig& cfg)
{
    const QStringList parts = text.split(QLatin1Char(';'));
    if (parts.isEmpty() || parts.first() != QLatin1String("PI")) return false;
    PiConfig c;
    for (const QString& p : parts.mid(1)) {
        const int eq = p.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        const QString k = p.left(eq), v = p.mid(eq + 1);
        auto ints = [&v](std::array<int, kMaxChannels>& arr) {
            const QStringList l = v.split(QLatin1Char(','));
            for (int i = 0; i < kMaxChannels && i < l.size(); ++i) {
                const int g = l[i].toInt();
                arr[i] = s5io::physicalPin(g) ? g : -1;
            }
        };
        if (k == QLatin1String("h")) c.host = v;
        else if (k == QLatin1String("p")) c.port = std::clamp(v.toInt(), 1, 65535);
        else if (k == QLatin1String("ia")) c.inAddress = std::clamp(v.toInt(), 0, 127);
        else if (k == QLatin1String("ib")) c.inBytes = std::clamp(v.toInt(), 0, kMaxBytes);
        else if (k == QLatin1String("oa")) c.outAddress = std::clamp(v.toInt(), 0, 127);
        else if (k == QLatin1String("ob")) c.outBytes = std::clamp(v.toInt(), 0, kMaxBytes);
        else if (k == QLatin1String("ig")) ints(c.inGpio);
        else if (k == QLatin1String("og")) ints(c.outGpio);
        else if (k == QLatin1String("iv")) c.inInvert = v.toUInt(nullptr, 16);
        else if (k == QLatin1String("db")) c.debounceMs = std::clamp(v.toInt(), 0, 1000);
        else if (k == QLatin1String("bi")) {
            const QStringList l = v.split(QLatin1Char(','));
            for (int i = 0; i < kMaxChannels && i < l.size(); ++i)
                c.inBias[i] = l[i] == QLatin1String("U") ? s5io::Bias::PullUp
                            : l[i] == QLatin1String("D") ? s5io::Bias::PullDown
                                                         : s5io::Bias::None;
        }
    }
    c.inAddress = std::min(c.inAddress, 128 - std::max(1, c.inBytes));
    c.outAddress = std::min(c.outAddress, 128 - std::max(1, c.outBytes));
    cfg = c;
    return true;
}
