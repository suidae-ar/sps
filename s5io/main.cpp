// s5io – E/A-Dienst für S5Sim auf dem Raspberry Pi.
//
//   s5io [--port 2511] [--bind 0.0.0.0] [--sim] [--verbose]
//
// Mit --sim werden die Ein- und Ausgänge nur simuliert (zum Testen ohne Pi); Eingänge lassen sich dann
// über die Konsole setzen.
#include "gpio_sim.h"
#include "protocol.h"
#include "server.h"

#ifdef S5IO_HAVE_GPIOD
#include "gpio_gpiod.h"
#endif

#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <poll.h>
#include <string>
#include <unistd.h>

namespace {

volatile std::sig_atomic_t stopRequested = 0;

void onSignal(int)
{
    stopRequested = 1;
}

void logLine(const std::string& text)
{
    char ts[32];
    const std::time_t t = std::time(nullptr);
    std::strftime(ts, sizeof ts, "%H:%M:%S", std::localtime(&t));
    std::fprintf(stderr, "%s  %s\n", ts, text.c_str());
}

void usage()
{
    std::printf(
        "s5io – E/A-Dienst für S5Sim\n\n"
        "  --port N      TCP-Port (Standard %d)\n"
        "  --bind ADR    lokale Adresse (Standard 0.0.0.0 = alle; 127.0.0.1 = nur dieser Rechner)\n"
        "  --sim         Ein-/Ausgänge simulieren (ohne Raspberry Pi)\n"
        "  --verbose     auch den Datenverkehr protokollieren\n"
        "  --help        diese Hilfe\n",
        s5io::kDefaultPort);
}

void simHelp()
{
    std::printf(
        "Simulation – Befehle:\n"
        "  e <gpio> <0|1>   Pegel an einem Eingangspin setzen, z.B. e 5 1\n"
        "  t <gpio>         Eingangspin umschalten\n"
        "  s                Zustand anzeigen\n"
        "  h                Hilfe\n");
}

void simStatus(const s5io::SimGpio& sim)
{
    if (sim.inputs().empty() && sim.outputs().empty()) {
        std::printf("nicht konfiguriert (S5Sim ist nicht verbunden)\n");
        return;
    }
    for (const auto& c : sim.inputs())
        std::printf("  Eingang %2d  GPIO %2d  Pegel %d%s\n", c.channel, c.gpio, sim.pinLevel(c.gpio) ? 1 : 0,
                    c.invert ? "  (invertiert)" : "");
    for (const auto& c : sim.outputs())
        std::printf("  Ausgang %2d  GPIO %2d  Pegel %d\n", c.channel, c.gpio, sim.outputLevel(c.gpio) ? 1 : 0);
}

void simCommand(s5io::SimGpio& sim, const std::string& text)
{
    char cmd = 0;
    int gpio = -1, level = -1;
    const int n = std::sscanf(text.c_str(), " %c %d %d", &cmd, &gpio, &level);
    if (n <= 0) return;
    switch (cmd) {
    case 'e':
        if (n == 3 && s5io::physicalPin(gpio) && (level == 0 || level == 1)) {
            sim.setPinLevel(gpio, level == 1);
            std::printf("GPIO %d = %d\n", gpio, level);
        } else {
            std::printf("Aufruf: e <gpio 2..27> <0|1>\n");
        }
        break;
    case 't':
        if (n >= 2 && s5io::physicalPin(gpio)) {
            sim.setPinLevel(gpio, !sim.pinLevel(gpio));
            std::printf("GPIO %d = %d\n", gpio, sim.pinLevel(gpio) ? 1 : 0);
        } else {
            std::printf("Aufruf: t <gpio 2..27>\n");
        }
        break;
    case 's': simStatus(sim); break;
    default: simHelp(); break;
    }
    std::fflush(stdout);
}

}  // namespace

int main(int argc, char* argv[])
{
    int port = s5io::kDefaultPort;
    std::string bind = "0.0.0.0";
    bool sim = false, verbose = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--bind" && i + 1 < argc) bind = argv[++i];
        else if (a == "--sim") sim = true;
        else if (a == "--verbose") verbose = true;
        else if (a == "--help" || a == "-h") {
            usage();
            return 0;
        } else {
            std::fprintf(stderr, "Unbekannte Option %s\n", a.c_str());
            usage();
            return 2;
        }
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::signal(SIGPIPE, SIG_IGN);

    std::unique_ptr<s5io::Gpio> gpio;
    s5io::SimGpio* simGpio = nullptr;
    if (sim) {
        auto s = std::make_unique<s5io::SimGpio>();
        simGpio = s.get();
        s->onOutputChanged = [](int g, bool level) {
            std::printf("Ausgang GPIO %d → %d\n", g, level ? 1 : 0);
            std::fflush(stdout);
        };
        gpio = std::move(s);
    } else {
#ifdef S5IO_HAVE_GPIOD
        gpio = std::make_unique<s5io::GpiodGpio>();
        if (gpio->availablePins().empty()) {
            std::fprintf(stderr, "Keine GPIO-Pins gefunden (/dev/gpiochip*). Läuft s5io auf einem Raspberry Pi?\n");
            return 1;
        }
#else
        std::fprintf(stderr, "s5io wurde ohne libgpiod gebaut – nur mit --sim verwendbar.\n");
        return 1;
#endif
    }

    s5io::Server server(*gpio);
    server.log = logLine;
    server.trace = verbose;
    std::string error;
    if (!server.listen(port, bind, error)) {
        std::fprintf(stderr, "s5io: %s\n", error.c_str());
        return 1;
    }
    logLine("s5io " + gpio->mode() + " (" + gpio->model() + ") wartet auf S5Sim an " + bind + ":"
            + std::to_string(server.port()));
    if (simGpio) simHelp();

    s5io::LineBuffer console;
    bool stdinOpen = simGpio != nullptr;
    while (!stopRequested) {
        server.poll(10);
        if (stdinOpen) {
            pollfd p{STDIN_FILENO, POLLIN, 0};
            if (::poll(&p, 1, 0) > 0) {
                char buf[256];
                const ssize_t r = ::read(STDIN_FILENO, buf, sizeof buf);
                if (r <= 0) {
                    stdinOpen = false;      // keine Konsole (z.B. als Dienst gestartet)
                } else {
                    console.append(buf, static_cast<size_t>(r));
                    std::string line;
                    while (console.nextLine(line)) simCommand(*simGpio, line);
                }
            }
        }
    }
    server.shutdown();
    logLine("s5io beendet – Ausgänge aus");
    return 0;
}
