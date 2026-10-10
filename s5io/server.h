// TCP-Server von s5io: eine Verbindung zu S5Sim, Konfiguration der Kanäle, zyklisches Lesen der
// Eingänge und Watchdog für die Ausgänge.
#pragma once

#include "gpio.h"

#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace s5io {

class Server {
public:
    using Clock = std::chrono::steady_clock;

    explicit Server(Gpio& gpio);
    ~Server();

    bool listen(int port, const std::string& bindAddress, std::string& error);
    int port() const { return port_; }

    void poll(int timeoutMs);   // eine Runde: Verbindungen, Nachrichten, Eingänge lesen, Watchdog
    void shutdown();            // Ausgänge aus, alle Verbindungen schließen

    bool clientConnected() const { return client_ >= 0; }
    bool configured() const { return configured_; }
    bool outputsLive() const { return outputsLive_; }

    std::function<void(const std::string&)> log;
    bool trace = false;     // Datenverkehr protokollieren (IN/OUT nur bei Änderung)

private:
    void acceptClient();
    void readClient();
    void handleLine(const std::string& text);
    void send(const std::string& line);
    void dropClient(const std::string& reason);
    void tick();
    void note(const std::string& text);

    Gpio& gpio_;
    int listen_ = -1;
    int client_ = -1;
    int port_ = 0;
    std::string peer_;
    LineBuffer rx_;
    std::vector<InputConfig> pendingIn_;
    std::vector<OutputConfig> pendingOut_;
    bool configured_ = false;
    bool outputsLive_ = false;
    bool inputsValid_ = false;
    uint32_t lastInputs_ = 0;
    uint32_t lastOutputs_ = 0;
    Clock::time_point lastRx_;
    Clock::time_point lastInSent_;
};

}  // namespace s5io
