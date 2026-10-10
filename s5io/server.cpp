#include "server.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace s5io {

namespace {

using std::chrono::duration_cast;
using std::chrono::milliseconds;

void setNonBlocking(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void noSigPipe([[maybe_unused]] int fd)
{
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
}

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

}  // namespace

Server::Server(Gpio& gpio)
    : gpio_(gpio)
{
}

Server::~Server()
{
    shutdown();
}

void Server::note(const std::string& text)
{
    if (log) log(text);
}

bool Server::listen(int port, const std::string& bindAddress, std::string& error)
{
    listen_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_ < 0) {
        error = std::string("socket: ") + std::strerror(errno);
        return false;
    }
    int one = 1;
    setsockopt(listen_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, bindAddress.c_str(), &addr.sin_addr) != 1) {
        error = "ungültige Adresse " + bindAddress;
        return false;
    }
    if (::bind(listen_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) {
        error = "Port " + std::to_string(port) + ": " + std::strerror(errno);
        return false;
    }
    if (::listen(listen_, 2) < 0) {
        error = std::string("listen: ") + std::strerror(errno);
        return false;
    }
    socklen_t len = sizeof addr;
    getsockname(listen_, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    setNonBlocking(listen_);
    return true;
}

void Server::poll(int timeoutMs)
{
    pollfd fds[2];
    nfds_t n = 0;
    if (listen_ >= 0) fds[n++] = {listen_, POLLIN, 0};
    if (client_ >= 0) fds[n++] = {client_, POLLIN, 0};
    ::poll(fds, n, timeoutMs);

    for (nfds_t i = 0; i < n; ++i) {
        if (!fds[i].revents) continue;
        if (fds[i].fd == listen_)
            acceptClient();
        else if (fds[i].fd == client_)
            readClient();
    }
    tick();
}

void Server::acceptClient()
{
    sockaddr_in addr{};
    socklen_t len = sizeof addr;
    const int fd = ::accept(listen_, reinterpret_cast<sockaddr*>(&addr), &len);
    if (fd < 0) return;
    noSigPipe(fd);
    char ip[INET_ADDRSTRLEN] = "?";
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof ip);

    if (client_ >= 0) {     // nur eine Verbindung gleichzeitig
        const std::string msg = "ERR s5io ist bereits mit " + peer_ + " verbunden\n";
        ::send(fd, msg.data(), msg.size(), kSendFlags);
        ::close(fd);
        note(std::string("Verbindung von ") + ip + " abgelehnt (belegt)");
        return;
    }
    client_ = fd;
    peer_ = ip;
    setNonBlocking(client_);
    int one = 1;
    setsockopt(client_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    rx_.clear();
    configured_ = false;
    outputsLive_ = false;
    inputsValid_ = false;
    pendingIn_.clear();
    pendingOut_.clear();
    lastRx_ = Clock::now();
    note("Verbunden mit " + peer_);
    send(helloServer(gpio_.model(), gpio_.mode()));
    send(pinsLine(gpio_.availablePins()));
}

void Server::readClient()
{
    char buf[1024];
    std::string closeReason;
    for (;;) {
        const ssize_t r = ::recv(client_, buf, sizeof buf, 0);
        if (r > 0) {
            rx_.append(buf, static_cast<size_t>(r));
            continue;
        }
        if (r == 0) {
            closeReason = "Verbindung von S5Sim beendet";
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EINTR) continue;
        closeReason = std::string("Lesefehler: ") + std::strerror(errno);
        break;
    }
    // zuerst die empfangenen Zeilen verarbeiten (z.B. BYE kurz vor dem Verbindungsende)
    std::string line;
    while (client_ >= 0 && rx_.nextLine(line)) handleLine(line);
    if (!closeReason.empty()) dropClient(closeReason);
}

void Server::handleLine(const std::string& text)
{
    auto line = parseLine(text);
    if (!line) return;
    lastRx_ = Clock::now();
    const std::string& cmd = line->command;
    std::string error;
    if (trace && cmd != "OUT") note("← " + text);

    if (cmd == "HELLO") {
        if (line->args.size() < 2 || line->args[0] != "S5SIM" || line->args[1] != std::to_string(kProtocolVersion))
            send("ERR Protokollversion " + std::to_string(kProtocolVersion) + " erwartet");
    } else if (cmd == "RESET") {
        gpio_.release();
        configured_ = false;
        outputsLive_ = false;
        pendingIn_.clear();
        pendingOut_.clear();
    } else if (cmd == "CFGIN") {
        InputConfig c;
        if (parseInputConfig(*line, c, error))
            pendingIn_.push_back(c);
        else
            send("ERR CFGIN: " + error);
    } else if (cmd == "CFGOUT") {
        OutputConfig c;
        if (parseOutputConfig(*line, c, error))
            pendingOut_.push_back(c);
        else
            send("ERR CFGOUT: " + error);
    } else if (cmd == "COMMIT") {
        if (gpio_.configure(pendingIn_, pendingOut_, error)) {
            configured_ = true;
            outputsLive_ = true;
            inputsValid_ = false;
            send("OK");
            note("Konfiguration: " + std::to_string(pendingIn_.size()) + " Eingänge, "
                 + std::to_string(pendingOut_.size()) + " Ausgänge");
        } else {
            configured_ = false;
            send("ERR " + error);
            note("Konfiguration abgelehnt: " + error);
        }
        pendingIn_.clear();
        pendingOut_.clear();
    } else if (cmd == "OUT") {
        uint32_t mask = 0;
        if (!configured_) {
            send("ERR nicht konfiguriert");
        } else if (line->args.size() != 1 || !parseMask(line->args[0], mask)) {
            send("ERR OUT erwartet eine Hex-Maske");
        } else if (!gpio_.writeOutputs(mask, error)) {
            send("ERR " + error);
        } else {
            if (!outputsLive_) note("Ausgänge wieder aktiv");
            if (trace && mask != lastOutputs_) note("← " + text);
            lastOutputs_ = mask;
            outputsLive_ = true;
        }
    } else if (cmd == "BYE") {
        dropClient("S5Sim hat sich abgemeldet");
    } else {
        send("ERR unbekannter Befehl " + cmd);
    }
}

void Server::send(const std::string& line)
{
    if (client_ < 0) return;
    if (trace && line.rfind("IN ", 0) != 0) note("→ " + line);
    const std::string data = line + "\n";
    const ssize_t r = ::send(client_, data.data(), data.size(), kSendFlags);
    if (r != static_cast<ssize_t>(data.size())) dropClient("Senden fehlgeschlagen");
}

void Server::dropClient(const std::string& reason)
{
    if (client_ < 0) return;
    ::close(client_);
    client_ = -1;
    gpio_.release();    // Ausgänge aus
    configured_ = false;
    outputsLive_ = false;
    note(reason + " – Ausgänge aus");
}

void Server::tick()
{
    if (client_ < 0 || !configured_) return;
    const auto now = Clock::now();
    std::string error;

    if (outputsLive_ && duration_cast<milliseconds>(now - lastRx_).count() > kWatchdogMs) {
        gpio_.writeOutputs(0, error);
        outputsLive_ = false;
        note("Watchdog: keine Nachricht von S5Sim – Ausgänge aus");
    }

    uint32_t inputs = 0;
    if (!gpio_.readInputs(inputs, error)) {
        send("ERR " + error);
        return;
    }
    if (!inputsValid_ || inputs != lastInputs_
        || duration_cast<milliseconds>(now - lastInSent_).count() >= kKeepAliveMs) {
        if (trace && (!inputsValid_ || inputs != lastInputs_)) note("→ " + maskLine("IN", inputs));
        send(maskLine("IN", inputs));
        lastInputs_ = inputs;
        lastInSent_ = now;
        inputsValid_ = true;
    }
}

void Server::shutdown()
{
    dropClient("s5io wird beendet");
    if (listen_ >= 0) {
        ::close(listen_);
        listen_ = -1;
    }
    gpio_.release();
}

}  // namespace s5io
