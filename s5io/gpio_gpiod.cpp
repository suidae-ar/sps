#include "gpio_gpiod.h"

#include <gpiod.h>

#include <algorithm>
#include <dirent.h>
#include <fstream>
#include <memory>
#include <sstream>

namespace s5io {

namespace {

std::vector<std::string> chipPaths()
{
    std::vector<std::string> paths;
    if (DIR* d = opendir("/dev")) {
        while (dirent* e = readdir(d)) {
            const std::string name = e->d_name;
            if (name.rfind("gpiochip", 0) == 0) paths.push_back("/dev/" + name);
        }
        closedir(d);
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

std::string readModel()
{
    std::ifstream f("/proc/device-tree/model");
    std::string m;
    std::getline(f, m, '\0');
    return m.empty() ? std::string("Linux") : m;
}

gpiod_line_bias toBias(Bias b)
{
    switch (b) {
    case Bias::PullUp: return GPIOD_LINE_BIAS_PULL_UP;
    case Bias::PullDown: return GPIOD_LINE_BIAS_PULL_DOWN;
    case Bias::None: break;
    }
    return GPIOD_LINE_BIAS_DISABLED;
}

template <typename T, void (*Free)(T*)>
struct Deleter {
    void operator()(T* p) const
    {
        if (p) Free(p);
    }
};
using SettingsPtr = std::unique_ptr<gpiod_line_settings, Deleter<gpiod_line_settings, gpiod_line_settings_free>>;
using LineConfigPtr = std::unique_ptr<gpiod_line_config, Deleter<gpiod_line_config, gpiod_line_config_free>>;
using RequestConfigPtr =
    std::unique_ptr<gpiod_request_config, Deleter<gpiod_request_config, gpiod_request_config_free>>;

}  // namespace

GpiodGpio::GpiodGpio()
    : model_(readModel())
{
    // Alle Pins der Stiftleiste über ihre Namen suchen
    for (const std::string& path : chipPaths()) {
        gpiod_chip* chip = gpiod_chip_open(path.c_str());
        if (!chip) continue;
        for (int gpio : headerGpios()) {
            if (lines_.count(gpio)) continue;
            const std::string name = "GPIO" + std::to_string(gpio);
            const int offset = gpiod_chip_get_line_offset_from_name(chip, name.c_str());
            if (offset >= 0) lines_[gpio] = {path, static_cast<unsigned int>(offset)};
        }
        gpiod_chip_close(chip);
    }
}

GpiodGpio::~GpiodGpio()
{
    release();
}

std::vector<int> GpiodGpio::availablePins() const
{
    std::vector<int> pins;
    for (const auto& [gpio, ref] : lines_) pins.push_back(gpio);
    return pins;
}

gpiod_line_request* GpiodGpio::requestFor(const std::string& chip) const
{
    auto it = requests_.find(chip);
    return it == requests_.end() ? nullptr : it->second.request;
}

bool GpiodGpio::configure(const std::vector<InputConfig>& in, const std::vector<OutputConfig>& out,
                          std::string& error)
{
    error = validate(in, out, availablePins());
    if (!error.empty()) return false;
    release();

    // je Chip eine Leitungskonfiguration aufbauen
    std::map<std::string, LineConfigPtr> configs;
    auto configFor = [&](const std::string& chip) -> gpiod_line_config* {
        auto& cfg = configs[chip];
        if (!cfg) cfg.reset(gpiod_line_config_new());
        return cfg.get();
    };

    for (const InputConfig& c : in) {
        const LineRef& ref = lines_.at(c.gpio);
        SettingsPtr s(gpiod_line_settings_new());
        if (!s) {
            error = "libgpiod: kein Speicher";
            release();
            return false;
        }
        gpiod_line_settings_set_direction(s.get(), GPIOD_LINE_DIRECTION_INPUT);
        gpiod_line_settings_set_bias(s.get(), toBias(c.bias));
        gpiod_line_settings_set_active_low(s.get(), c.invert);
        if (c.debounceMs > 0)
            gpiod_line_settings_set_debounce_period_us(s.get(), static_cast<unsigned long>(c.debounceMs) * 1000UL);
        const unsigned int offset = ref.offset;
        if (gpiod_line_config_add_line_settings(configFor(ref.chip), &offset, 1, s.get()) < 0) {
            error = "GPIO " + std::to_string(c.gpio) + " lässt sich nicht als Eingang einstellen";
            release();
            return false;
        }
        inputs_.push_back({c.channel, ref.chip, offset});
    }
    for (const OutputConfig& c : out) {
        const LineRef& ref = lines_.at(c.gpio);
        SettingsPtr s(gpiod_line_settings_new());
        if (!s) {
            error = "libgpiod: kein Speicher";
            release();
            return false;
        }
        gpiod_line_settings_set_direction(s.get(), GPIOD_LINE_DIRECTION_OUTPUT);
        gpiod_line_settings_set_output_value(s.get(), GPIOD_LINE_VALUE_INACTIVE);
        const unsigned int offset = ref.offset;
        if (gpiod_line_config_add_line_settings(configFor(ref.chip), &offset, 1, s.get()) < 0) {
            error = "GPIO " + std::to_string(c.gpio) + " lässt sich nicht als Ausgang einstellen";
            release();
            return false;
        }
        outputs_.push_back({c.channel, ref.chip, offset});
    }

    RequestConfigPtr rc(gpiod_request_config_new());
    if (rc) gpiod_request_config_set_consumer(rc.get(), "s5io");
    for (auto& [path, cfg] : configs) {
        gpiod_chip* chip = gpiod_chip_open(path.c_str());
        if (!chip) {
            error = "GPIO-Chip " + path + " lässt sich nicht öffnen (Rechte? Benutzer in Gruppe gpio?)";
            release();
            return false;
        }
        gpiod_line_request* req = gpiod_chip_request_lines(chip, rc.get(), cfg.get());
        if (!req) {
            gpiod_chip_close(chip);
            error = "GPIO-Leitungen auf " + path + " sind belegt oder nicht freigegeben";
            release();
            return false;
        }
        requests_[path] = {chip, req};
    }
    return true;
}

bool GpiodGpio::readInputs(uint32_t& mask, std::string& error)
{
    mask = 0;
    for (const Channel& c : inputs_) {
        gpiod_line_request* req = requestFor(c.chip);
        if (!req) continue;
        const gpiod_line_value v = gpiod_line_request_get_value(req, c.offset);
        if (v == GPIOD_LINE_VALUE_ERROR) {
            error = "Eingang Kanal " + std::to_string(c.channel) + " nicht lesbar";
            return false;
        }
        if (v == GPIOD_LINE_VALUE_ACTIVE) mask |= 1u << c.channel;
    }
    return true;
}

bool GpiodGpio::writeOutputs(uint32_t mask, std::string& error)
{
    bool ok = true;
    for (const Channel& c : outputs_) {
        gpiod_line_request* req = requestFor(c.chip);
        if (!req) continue;
        const gpiod_line_value v = ((mask >> c.channel) & 1u) ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE;
        if (gpiod_line_request_set_value(req, c.offset, v) < 0) {
            error = "Ausgang Kanal " + std::to_string(c.channel) + " nicht schreibbar";
            ok = false;
        }
    }
    return ok;
}

void GpiodGpio::release()
{
    std::string ignore;
    writeOutputs(0, ignore);
    for (auto& [path, r] : requests_) {
        if (r.request) gpiod_line_request_release(r.request);
        if (r.chip) gpiod_chip_close(r.chip);
    }
    requests_.clear();
    inputs_.clear();
    outputs_.clear();
}

}  // namespace s5io
