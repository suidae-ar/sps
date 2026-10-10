// Protokoll zwischen S5Sim und dem E/A-Dienst s5io auf dem Raspberry Pi.
// Textzeilen über TCP (UTF-8, Zeilenende '\n'). Ohne Qt, damit s5io auch ohne Qt gebaut werden kann.
//
//   s5io  → S5Sim  HELLO S5IO 1 MODEL="Raspberry Pi 4 Model B" MODE=GPIO
//   s5io  → S5Sim  PINS 2,3,4,...,27
//   S5Sim → s5io   HELLO S5SIM 1
//   S5Sim → s5io   RESET
//   S5Sim → s5io   CFGIN 0 GPIO=5 BIAS=DOWN INVERT=0 DEBOUNCE=10
//   S5Sim → s5io   CFGOUT 0 GPIO=17
//   S5Sim → s5io   COMMIT
//   s5io  → S5Sim  OK                      bzw.  ERR <Text>
//   S5Sim → s5io   OUT 5                   Ausgangskanäle (Hex-Maske), bei Änderung, mind. alle 100 ms
//   s5io  → S5Sim  IN 81                   Eingangskanäle (Hex-Maske), bei Änderung, mind. alle 100 ms
//   S5Sim → s5io   BYE
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace s5io {

constexpr int kDefaultPort = 2511;
constexpr int kProtocolVersion = 1;
constexpr int kMaxChannels = 32;
constexpr int kWatchdogMs = 500;    // so lange ohne Nachricht von S5Sim → Ausgänge aus
constexpr int kKeepAliveMs = 100;   // IN/OUT werden mindestens so oft gesendet

enum class Bias { None, PullUp, PullDown };

struct InputConfig {
    int channel = 0;
    int gpio = -1;
    Bias bias = Bias::None;
    bool invert = false;
    int debounceMs = 0;
};

struct OutputConfig {
    int channel = 0;
    int gpio = -1;
};

// Zerlegte Protokollzeile: Befehl, Argumente ohne '=', Schlüssel=Wert-Paare (Schlüssel in Großbuchstaben)
struct Line {
    std::string command;
    std::vector<std::string> args;
    std::map<std::string, std::string> values;

    std::string value(const std::string& key, const std::string& fallback = {}) const;
};

std::optional<Line> parseLine(const std::string& text);    // nullopt bei leerer Zeile

// Zeilen erzeugen (ohne Zeilenende)
std::string helloClient();
std::string helloServer(const std::string& model, const std::string& mode);
std::string pinsLine(const std::vector<int>& gpios);
std::string configInputLine(const InputConfig& c);
std::string configOutputLine(const OutputConfig& c);
std::string maskLine(const char* command, uint32_t mask);   // "IN 81", "OUT 5"

bool parseMask(const std::string& hex, uint32_t& mask);
bool parseInputConfig(const Line& line, InputConfig& c, std::string& error);
bool parseOutputConfig(const Line& line, OutputConfig& c, std::string& error);
std::vector<int> parsePins(const Line& line);

const char* biasName(Bias b);   // NONE, UP, DOWN
bool parseBias(const std::string& text, Bias& b);

// Prüft eine Konfiguration; leerer Text = in Ordnung
std::string validate(const std::vector<InputConfig>& in, const std::vector<OutputConfig>& out,
                     const std::vector<int>& availablePins);

// Stiftleiste des Raspberry Pi (40-polig): GPIO → physischer Pin, Sonderfunktion
int physicalPin(int gpio);                  // 0 = kein Pin der Stiftleiste
const char* specialFunction(int gpio);      // "I²C", "UART", "SPI" oder nullptr
std::vector<int> headerGpios();             // GPIO 2 … 27

// Zerlegt empfangene Bytes in Zeilen
class LineBuffer {
public:
    void append(const char* data, size_t size);
    bool nextLine(std::string& line);
    size_t pending() const { return buffer_.size(); }
    void clear() { buffer_.clear(); }

private:
    std::string buffer_;
};

}  // namespace s5io
