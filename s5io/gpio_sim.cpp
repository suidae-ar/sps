#include "gpio_sim.h"

namespace s5io {

bool SimGpio::configure(const std::vector<InputConfig>& in, const std::vector<OutputConfig>& out,
                        std::string& error)
{
    error = validate(in, out, availablePins());
    if (!error.empty()) return false;
    release();
    in_ = in;
    out_ = out;
    for (const auto& o : out_) outLevel_[o.gpio] = false;
    return true;
}

bool SimGpio::readInputs(uint32_t& mask, std::string&)
{
    mask = 0;
    for (const auto& c : in_) {
        bool level;
        if (auto it = pinLevel_.find(c.gpio); it != pinLevel_.end())
            level = it->second;
        else
            level = c.bias == Bias::PullUp;     // offener Eingang folgt dem Pull-Widerstand
        if (level != c.invert) mask |= 1u << c.channel;
    }
    return true;
}

bool SimGpio::writeOutputs(uint32_t mask, std::string&)
{
    for (const auto& o : out_) {
        const bool level = (mask >> o.channel) & 1u;
        bool& cur = outLevel_[o.gpio];
        if (cur != level) {
            cur = level;
            if (onOutputChanged) onOutputChanged(o.gpio, level);
        }
    }
    return true;
}

void SimGpio::release()
{
    std::string ignore;
    writeOutputs(0, ignore);
    in_.clear();
    out_.clear();
}

void SimGpio::setPinLevel(int gpio, bool high)
{
    pinLevel_[gpio] = high;
}

bool SimGpio::pinLevel(int gpio) const
{
    auto it = pinLevel_.find(gpio);
    return it != pinLevel_.end() && it->second;
}

bool SimGpio::outputLevel(int gpio) const
{
    auto it = outLevel_.find(gpio);
    return it != outLevel_.end() && it->second;
}

}  // namespace s5io
