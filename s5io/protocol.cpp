#include "protocol.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>
#include <sstream>

namespace s5io {

namespace {

std::string upper(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool parseInt(const std::string& s, int& v)
{
    if (s.empty() || s.size() > 9) return false;
    size_t i = s[0] == '-' ? 1 : 0;
    if (i == s.size()) return false;
    for (size_t k = i; k < s.size(); ++k)
        if (!std::isdigit(static_cast<unsigned char>(s[k]))) return false;
    v = std::stoi(s);
    return true;
}

}  // namespace

std::string Line::value(const std::string& key, const std::string& fallback) const
{
    auto it = values.find(key);
    return it == values.end() ? fallback : it->second;
}

std::optional<Line> parseLine(const std::string& text)
{
    // Tokens trennen; Werte dürfen in "..." stehen (z.B. MODEL="Raspberry Pi 4")
    std::vector<std::string> tokens;
    std::string cur;
    bool quoted = false, any = false;
    for (char c : text) {
        if (c == '\r' || c == '\n') continue;
        if (c == '"') {
            quoted = !quoted;
            any = true;
            continue;
        }
        if (!quoted && std::isspace(static_cast<unsigned char>(c))) {
            if (any) tokens.push_back(cur);
            cur.clear();
            any = false;
            continue;
        }
        cur += c;
        any = true;
    }
    if (any) tokens.push_back(cur);
    if (tokens.empty()) return std::nullopt;

    Line line;
    line.command = upper(tokens[0]);
    for (size_t i = 1; i < tokens.size(); ++i) {
        const auto eq = tokens[i].find('=');
        if (eq == std::string::npos || eq == 0)
            line.args.push_back(tokens[i]);
        else
            line.values[upper(tokens[i].substr(0, eq))] = tokens[i].substr(eq + 1);
    }
    return line;
}

std::string helloClient()
{
    return "HELLO S5SIM " + std::to_string(kProtocolVersion);
}

std::string helloServer(const std::string& model, const std::string& mode)
{
    std::string m = model;
    std::replace(m.begin(), m.end(), '"', '\'');
    return "HELLO S5IO " + std::to_string(kProtocolVersion) + " MODEL=\"" + m + "\" MODE=" + mode;
}

std::string pinsLine(const std::vector<int>& gpios)
{
    std::string s = "PINS ";
    for (size_t i = 0; i < gpios.size(); ++i) s += (i ? "," : "") + std::to_string(gpios[i]);
    return s;
}

std::string configInputLine(const InputConfig& c)
{
    return "CFGIN " + std::to_string(c.channel) + " GPIO=" + std::to_string(c.gpio) + " BIAS=" + biasName(c.bias)
         + " INVERT=" + (c.invert ? "1" : "0") + " DEBOUNCE=" + std::to_string(c.debounceMs);
}

std::string configOutputLine(const OutputConfig& c)
{
    return "CFGOUT " + std::to_string(c.channel) + " GPIO=" + std::to_string(c.gpio);
}

std::string maskLine(const char* command, uint32_t mask)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%s %X", command, mask);
    return buf;
}

bool parseMask(const std::string& hex, uint32_t& mask)
{
    if (hex.empty() || hex.size() > 8) return false;
    uint32_t v = 0;
    for (char c : hex) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = (v << 4) | static_cast<uint32_t>(d);
    }
    mask = v;
    return true;
}

const char* biasName(Bias b)
{
    switch (b) {
    case Bias::PullUp: return "UP";
    case Bias::PullDown: return "DOWN";
    case Bias::None: break;
    }
    return "NONE";
}

bool parseBias(const std::string& text, Bias& b)
{
    const std::string u = upper(text);
    if (u == "NONE" || u.empty()) b = Bias::None;
    else if (u == "UP") b = Bias::PullUp;
    else if (u == "DOWN") b = Bias::PullDown;
    else return false;
    return true;
}

namespace {

bool parseChannelAndGpio(const Line& line, int& channel, int& gpio, std::string& error)
{
    if (line.args.size() != 1 || !parseInt(line.args[0], channel) || channel < 0 || channel >= kMaxChannels) {
        error = "Kanalnummer 0.." + std::to_string(kMaxChannels - 1) + " erwartet";
        return false;
    }
    if (!parseInt(line.value("GPIO"), gpio) || gpio < 0) {
        error = "GPIO-Nummer fehlt oder ist ungültig";
        return false;
    }
    return true;
}

}  // namespace

bool parseInputConfig(const Line& line, InputConfig& c, std::string& error)
{
    if (!parseChannelAndGpio(line, c.channel, c.gpio, error)) return false;
    if (!parseBias(line.value("BIAS", "NONE"), c.bias)) {
        error = "BIAS muss NONE, UP oder DOWN sein";
        return false;
    }
    c.invert = line.value("INVERT", "0") == "1";
    int deb = 0;
    if (!parseInt(line.value("DEBOUNCE", "0"), deb) || deb < 0 || deb > 1000) {
        error = "DEBOUNCE muss 0..1000 ms sein";
        return false;
    }
    c.debounceMs = deb;
    return true;
}

bool parseOutputConfig(const Line& line, OutputConfig& c, std::string& error)
{
    return parseChannelAndGpio(line, c.channel, c.gpio, error);
}

std::vector<int> parsePins(const Line& line)
{
    std::vector<int> pins;
    if (line.args.empty()) return pins;
    std::stringstream ss(line.args[0]);
    for (std::string item; std::getline(ss, item, ',');) {
        int v;
        if (parseInt(item, v)) pins.push_back(v);
    }
    return pins;
}

std::string validate(const std::vector<InputConfig>& in, const std::vector<OutputConfig>& out,
                     const std::vector<int>& availablePins)
{
    std::set<int> inChannels, outChannels, gpios;
    auto checkGpio = [&](int gpio) -> std::string {
        if (std::find(availablePins.begin(), availablePins.end(), gpio) == availablePins.end())
            return "GPIO " + std::to_string(gpio) + " ist nicht verfügbar";
        if (!gpios.insert(gpio).second) return "GPIO " + std::to_string(gpio) + " ist doppelt belegt";
        return {};
    };
    for (const auto& c : in) {
        if (!inChannels.insert(c.channel).second) return "Eingangskanal " + std::to_string(c.channel) + " doppelt";
        if (auto e = checkGpio(c.gpio); !e.empty()) return e;
    }
    for (const auto& c : out) {
        if (!outChannels.insert(c.channel).second) return "Ausgangskanal " + std::to_string(c.channel) + " doppelt";
        if (auto e = checkGpio(c.gpio); !e.empty()) return e;
    }
    return {};
}

int physicalPin(int gpio)
{
    static const std::map<int, int> pins = {
        {2, 3},   {3, 5},   {4, 7},   {5, 29},  {6, 31},  {7, 26},  {8, 24},  {9, 21},  {10, 19},
        {11, 23}, {12, 32}, {13, 33}, {14, 8},  {15, 10}, {16, 36}, {17, 11}, {18, 12}, {19, 35},
        {20, 38}, {21, 40}, {22, 15}, {23, 16}, {24, 18}, {25, 22}, {26, 37}, {27, 13}};
    auto it = pins.find(gpio);
    return it == pins.end() ? 0 : it->second;
}

const char* specialFunction(int gpio)
{
    switch (gpio) {
    case 2:
    case 3: return "I²C";
    case 14:
    case 15: return "UART";
    case 7:
    case 8:
    case 9:
    case 10:
    case 11: return "SPI";
    default: return nullptr;
    }
}

std::vector<int> headerGpios()
{
    std::vector<int> v;
    for (int g = 2; g <= 27; ++g) v.push_back(g);
    return v;
}

void LineBuffer::append(const char* data, size_t size)
{
    buffer_.append(data, size);
    if (buffer_.size() > 64 * 1024) buffer_.clear();   // Schutz gegen Müll ohne Zeilenende
}

bool LineBuffer::nextLine(std::string& line)
{
    const auto nl = buffer_.find('\n');
    if (nl == std::string::npos) return false;
    line = buffer_.substr(0, nl);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    buffer_.erase(0, nl + 1);
    return true;
}

}  // namespace s5io
