// Simulierte Ein-/Ausgänge: zum Testen von s5io und S5Sim ohne Raspberry Pi.
#pragma once

#include "gpio.h"

#include <functional>
#include <map>

namespace s5io {

class SimGpio : public Gpio {
public:
    std::string model() const override { return "Simulation"; }
    std::string mode() const override { return "SIM"; }
    std::vector<int> availablePins() const override { return headerGpios(); }

    bool configure(const std::vector<InputConfig>& in, const std::vector<OutputConfig>& out,
                   std::string& error) override;
    bool readInputs(uint32_t& mask, std::string& error) override;
    bool writeOutputs(uint32_t mask, std::string& error) override;
    void release() override;

    // Pegel an einem Eingangspin setzen (wie ein Signal an der Klemme)
    void setPinLevel(int gpio, bool high);
    bool pinLevel(int gpio) const;
    bool outputLevel(int gpio) const;

    const std::vector<InputConfig>& inputs() const { return in_; }
    const std::vector<OutputConfig>& outputs() const { return out_; }

    std::function<void(int gpio, bool level)> onOutputChanged;

private:
    std::vector<InputConfig> in_;
    std::vector<OutputConfig> out_;
    std::map<int, bool> pinLevel_;      // von außen gesetzte Eingangspegel
    std::map<int, bool> outLevel_;
};

}  // namespace s5io
