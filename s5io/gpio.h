// Zugriff auf die Ein- und Ausgänge im Dienst s5io. Ein Treiber je Hardware:
// GpiodGpio (Raspberry Pi über libgpiod 2) und SimGpio (Simulation zum Testen ohne Pi).
#pragma once

#include "protocol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace s5io {

class Gpio {
public:
    virtual ~Gpio() = default;

    virtual std::string model() const = 0;              // z.B. "Raspberry Pi 4 Model B Rev 1.5"
    virtual std::string mode() const = 0;               // "GPIO" oder "SIM"
    virtual std::vector<int> availablePins() const = 0;

    // Leitungen anfordern und einstellen; Ausgänge starten mit 0
    virtual bool configure(const std::vector<InputConfig>& in, const std::vector<OutputConfig>& out,
                           std::string& error) = 0;
    virtual bool readInputs(uint32_t& mask, std::string& error) = 0;   // Bit = Eingangskanal
    virtual bool writeOutputs(uint32_t mask, std::string& error) = 0;  // Bit = Ausgangskanal
    virtual void release() = 0;     // Ausgänge aus, Leitungen freigeben
};

}  // namespace s5io
