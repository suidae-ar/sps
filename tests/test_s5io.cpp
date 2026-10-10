// Tests für das s5io-Protokoll und den Dienst (mit simulierten GPIOs über eine echte TCP-Verbindung).
#include "gpio_sim.h"
#include "protocol.h"
#include "server.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

using namespace s5io;

namespace {

int failures = 0;
std::string currentTest;

#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            std::printf("  FEHLER [%s] %s:%d: %s\n", currentTest.c_str(), __FILE__, __LINE__, #cond); \
            ++failures;                                                                              \
        }                                                                                            \
    } while (0)

void testParse()
{
    auto l = parseLine("HELLO S5IO 1 MODEL=\"Raspberry Pi 4 Model B\" MODE=GPIO");
    CHECK(l && l->command == "HELLO");
    CHECK(l && l->args.size() == 2 && l->args[0] == "S5IO" && l->args[1] == "1");
    CHECK(l && l->value("MODEL") == "Raspberry Pi 4 Model B" && l->value("MODE") == "GPIO");
    CHECK(!parseLine("   "));

    InputConfig in{3, 13, Bias::PullUp, true, 20};
    auto p = parseLine(configInputLine(in));
    InputConfig back;
    std::string err;
    CHECK(p && p->command == "CFGIN" && parseInputConfig(*p, back, err));
    CHECK(back.channel == 3 && back.gpio == 13 && back.bias == Bias::PullUp && back.invert && back.debounceMs == 20);
    CHECK(!parseInputConfig(*parseLine("CFGIN 40 GPIO=5"), back, err));
    CHECK(!parseInputConfig(*parseLine("CFGIN 1 GPIO=x"), back, err));
    CHECK(!parseInputConfig(*parseLine("CFGIN 1 GPIO=5 BIAS=SIDEWAYS"), back, err));

    uint32_t m = 0;
    CHECK(maskLine("OUT", 0x81) == "OUT 81");
    CHECK(parseMask("81", m) && m == 0x81);
    CHECK(parseMask("ffffffff", m) && m == 0xFFFFFFFFu);
    CHECK(!parseMask("", m) && !parseMask("12G", m) && !parseMask("123456789", m));

    CHECK(parsePins(*parseLine(pinsLine({4, 5, 17}))) == (std::vector<int>{4, 5, 17}));

    LineBuffer b;
    std::string line;
    b.append("IN 1\r\nOU", 8);
    CHECK(b.nextLine(line) && line == "IN 1");
    CHECK(!b.nextLine(line));
    b.append("T 2\n", 4);
    CHECK(b.nextLine(line) && line == "OUT 2");
}

void testPinsAndValidate()
{
    CHECK(physicalPin(17) == 11 && physicalPin(5) == 29 && physicalPin(26) == 37 && physicalPin(0) == 0);
    CHECK(specialFunction(2) && specialFunction(14) && specialFunction(10) && !specialFunction(17));
    const auto avail = headerGpios();
    CHECK(validate({{0, 5}, {1, 6}}, {{0, 17}}, avail).empty());
    CHECK(!validate({{0, 5}}, {{0, 5}}, avail).empty());      // Pin doppelt
    CHECK(!validate({{0, 5}, {0, 6}}, {}, avail).empty());     // Kanal doppelt
    CHECK(!validate({{0, 40}}, {}, avail).empty());            // Pin gibt es nicht
}

// Kleiner Testclient
struct Client {
    int fd = -1;
    LineBuffer rx;
    std::vector<std::string> lines;

    bool connectTo(int port)
    {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(static_cast<uint16_t>(port));
        inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
        return ::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0;
    }
    void send(const std::string& s)
    {
        const std::string d = s + "\n";
        ::send(fd, d.data(), d.size(), 0);
    }
    bool closedByPeer = false;
    void read()
    {
        pollfd p{fd, POLLIN, 0};
        while (::poll(&p, 1, 0) > 0) {
            char buf[512];
            const ssize_t r = ::recv(fd, buf, sizeof buf, 0);
            if (r <= 0) {
                closedByPeer = true;
                break;
            }
            rx.append(buf, static_cast<size_t>(r));
        }
        std::string l;
        while (rx.nextLine(l)) lines.push_back(l);
    }
    // letzte Zeile, die mit prefix beginnt
    std::string last(const std::string& prefix) const
    {
        for (auto it = lines.rbegin(); it != lines.rend(); ++it)
            if (it->rfind(prefix, 0) == 0) return *it;
        return {};
    }
    void close()
    {
        if (fd >= 0) ::close(fd);
        fd = -1;
    }
};

void pump(Server& srv, std::vector<Client*> clients, int ms)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        srv.poll(5);
        for (Client* c : clients) c->read();
    }
}

void testServer()
{
    SimGpio sim;
    Server srv(sim);
    std::string err;
    CHECK(srv.listen(0, "127.0.0.1", err));
    CHECK(srv.port() > 0);

    Client c;
    CHECK(c.connectTo(srv.port()));
    pump(srv, {&c}, 50);
    CHECK(c.last("HELLO").rfind("HELLO S5IO 1", 0) == 0);
    CHECK(c.last("HELLO").find("MODE=SIM") != std::string::npos);
    CHECK(!c.last("PINS").empty());

    // Konfiguration: E0 = GPIO 5 mit Pull-down, E1 = GPIO 6 mit Pull-up invertiert; A0 = GPIO 17, A1 = GPIO 18
    c.send(helloClient());
    c.send("RESET");
    c.send(configInputLine({0, 5, Bias::PullDown, false, 10}));
    c.send(configInputLine({1, 6, Bias::PullUp, true, 0}));
    c.send(configOutputLine({0, 17}));
    c.send(configOutputLine({1, 18}));
    c.send("COMMIT");
    pump(srv, {&c}, 50);
    CHECK(c.last("OK") == "OK");
    CHECK(c.last("ERR").empty());
    CHECK(srv.configured());
    CHECK(c.last("IN ") == "IN 0");     // offen: Pull-down → 0, Pull-up invertiert → 0

    sim.setPinLevel(5, true);
    sim.setPinLevel(6, false);          // Taster gegen Masse gedrückt → invertiert 1
    pump(srv, {&c}, 30);
    CHECK(c.last("IN ") == "IN 3");

    c.send("OUT 2");
    pump(srv, {&c}, 30);
    CHECK(!sim.outputLevel(17) && sim.outputLevel(18));

    // Watchdog: 0,7 s keine Nachricht → Ausgänge aus
    pump(srv, {&c}, 700);
    CHECK(!sim.outputLevel(18));
    CHECK(!srv.outputsLive());
    c.send("OUT 3");
    pump(srv, {&c}, 30);
    CHECK(sim.outputLevel(17) && sim.outputLevel(18) && srv.outputsLive());

    // Eingänge werden auch ohne Änderung mindestens alle 100 ms gesendet
    const size_t before = c.lines.size();
    for (int i = 0; i < 5; ++i) {
        c.send("OUT 3");
        pump(srv, {&c}, 60);
    }
    CHECK(c.lines.size() - before >= 2);

    // zweite Verbindung wird abgelehnt
    Client c2;
    CHECK(c2.connectTo(srv.port()));
    pump(srv, {&c, &c2}, 50);
    CHECK(c2.last("ERR").find("bereits") != std::string::npos);
    CHECK(c2.closedByPeer);
    c2.close();

    // ungültige Konfiguration: Pin doppelt
    c.send("RESET");
    c.send(configInputLine({0, 5, Bias::None, false, 0}));
    c.send(configOutputLine({0, 5}));
    c.send("COMMIT");
    pump(srv, {&c}, 50);
    CHECK(c.last("ERR").find("doppelt") != std::string::npos);
    CHECK(!srv.configured());
    c.send("OUT 1");
    pump(srv, {&c}, 30);
    CHECK(c.last("ERR").find("nicht konfiguriert") != std::string::npos);

    // neu konfigurieren, Ausgang setzen, Verbindung trennen → Ausgänge aus
    c.send("RESET");
    c.send(configOutputLine({0, 17}));
    c.send("COMMIT");
    c.send("OUT 1");
    pump(srv, {&c}, 50);
    CHECK(sim.outputLevel(17));
    c.close();
    pump(srv, {}, 50);
    CHECK(!srv.clientConnected());
    CHECK(!sim.outputLevel(17));

    // danach ist wieder eine Verbindung möglich
    Client c3;
    CHECK(c3.connectTo(srv.port()));
    pump(srv, {&c3}, 50);
    CHECK(c3.last("HELLO").rfind("HELLO S5IO", 0) == 0);
    c3.close();
    srv.shutdown();
}

}  // namespace

int main()
{
    const std::vector<std::pair<const char*, void (*)()>> tests = {
        {"Protokoll", testParse},
        {"Pins und Prüfung", testPinsAndValidate},
        {"Dienst mit Simulation", testServer},
    };
    for (const auto& [name, fn] : tests) {
        currentTest = name;
        const int before = failures;
        fn();
        std::printf("%s %s\n", failures == before ? "ok    " : "FEHLER", name);
    }
    std::printf("\n%s (%d Fehler)\n", failures ? "TESTS FEHLGESCHLAGEN" : "Alle Tests bestanden", failures);
    return failures ? 1 : 0;
}
