// GPIO des Raspberry Pi über libgpiod 2. Die Pins werden über ihre Namen ("GPIO17") gesucht,
// damit dasselbe Programm auf Pi 4 (BCM2711), Pi 5 (RP1) und Pi Zero läuft.
#pragma once

#include "gpio.h"

#include <map>

struct gpiod_chip;
struct gpiod_line_request;

namespace s5io {

class GpiodGpio : public Gpio {
public:
    GpiodGpio();
    ~GpiodGpio() override;

    std::string model() const override { return model_; }
    std::string mode() const override { return "GPIO"; }
    std::vector<int> availablePins() const override;

    bool configure(const std::vector<InputConfig>& in, const std::vector<OutputConfig>& out,
                   std::string& error) override;
    bool readInputs(uint32_t& mask, std::string& error) override;
    bool writeOutputs(uint32_t mask, std::string& error) override;
    void release() override;

private:
    struct LineRef {
        std::string chip;       // z.B. /dev/gpiochip0
        unsigned int offset = 0;
    };
    struct Request {
        gpiod_chip* chip = nullptr;
        gpiod_line_request* request = nullptr;
    };
    struct Channel {
        int channel;
        std::string chip;
        unsigned int offset;
    };

    gpiod_line_request* requestFor(const std::string& chip) const;

    std::string model_;
    std::map<int, LineRef> lines_;          // GPIO-Nummer → Chip und Offset
    std::map<std::string, Request> requests_;
    std::vector<Channel> inputs_;
    std::vector<Channel> outputs_;
};

}  // namespace s5io
