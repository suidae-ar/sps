// Tests für Parser und CPU (ohne Testframework).
#include "cpu.h"
#include "parser.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <functional>
#include <string>
#include <vector>

using namespace s5;

namespace {

int failures = 0;
std::string currentTest;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::printf("  FEHLER [%s] %s:%d: %s\n", currentTest.c_str(), __FILE__,      \
                        __LINE__, #cond);                                                 \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

#define CHECK_EQ(a, b)                                                                    \
    do {                                                                                  \
        auto va = (a);                                                                    \
        auto vb = (b);                                                                    \
        if (!(va == vb)) {                                                                \
            std::printf("  FEHLER [%s] %s:%d: %s == %s (%lld != %lld)\n",                 \
                        currentTest.c_str(), __FILE__, __LINE__, #a, #b,                  \
                        static_cast<long long>(va), static_cast<long long>(vb));          \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

Cpu loaded(const std::string& src)
{
    ParseResult r = parse(src);
    for (const auto& e : r.errors) std::printf("  Parserfehler Zeile %d: %s\n", e.line, e.message.c_str());
    if (!r.ok()) ++failures;
    Cpu cpu;
    cpu.load(r.program);
    cpu.start();
    return cpu;
}

bool out(const Cpu& cpu, int byte, int bit) { return (cpu.outputByte(byte) >> bit) & 1; }

uint16_t flagWord(const Cpu& cpu, int addr)
{
    Operand o;
    o.kind = OpKind::FlagWord;
    o.address = static_cast<uint16_t>(addr);
    return *cpu.peekValue(o);
}

void testAndOr()
{
    Cpu cpu = loaded(R"(
OB 1
:U  E 0.0
:U  E 0.1
:=  A 0.0
:O  E 0.0
:O  E 0.1
:=  A 0.1
:UN E 0.0
:=  A 0.2
:BE
)");
    cpu.runCycle();
    CHECK(!out(cpu, 0, 0));
    CHECK(!out(cpu, 0, 1));
    CHECK(out(cpu, 0, 2));
    cpu.setInput(0, 0, true);
    cpu.runCycle();
    CHECK(!out(cpu, 0, 0));
    CHECK(out(cpu, 0, 1));
    CHECK(!out(cpu, 0, 2));
    cpu.setInput(0, 1, true);
    cpu.runCycle();
    CHECK(out(cpu, 0, 0));
}

void testAndBeforeOrAndBrackets()
{
    Cpu cpu = loaded(R"(
OB 1
; UND vor ODER
:U  E 0.0
:U  E 0.1
:O
:U  E 0.2
:U  E 0.3
:=  A 0.0
; XOR mit Klammern
:U(
:U  E 0.0
:UN E 0.1
:)
:O(
:UN E 0.0
:U  E 0.1
:)
:=  A 0.1
:BE
)");
    struct Case { bool e0, e1, e2, e3; };
    for (int m = 0; m < 16; ++m) {
        Case c{bool(m & 1), bool(m & 2), bool(m & 4), bool(m & 8)};
        cpu.setInput(0, 0, c.e0);
        cpu.setInput(0, 1, c.e1);
        cpu.setInput(0, 2, c.e2);
        cpu.setInput(0, 3, c.e3);
        cpu.runCycle();
        CHECK_EQ(out(cpu, 0, 0), (c.e0 && c.e1) || (c.e2 && c.e3));
        CHECK_EQ(out(cpu, 0, 1), c.e0 != c.e1);
    }
}

void testSetReset()
{
    Cpu cpu = loaded(R"(
OB 1
:U  E 0.0
:S  M 1.0
:U  E 0.1
:R  M 1.0
:U  M 1.0
:=  A 0.0
:BE
)");
    cpu.setInput(0, 0, true);
    cpu.runCycle();
    CHECK(out(cpu, 0, 0));
    cpu.setInput(0, 0, false);
    cpu.runCycle();
    CHECK(out(cpu, 0, 0));
    cpu.setInput(0, 0, true);
    cpu.setInput(0, 1, true);   // rücksetzdominant
    cpu.runCycle();
    CHECK(!out(cpu, 0, 0));
}

void testTimerSE()
{
    Cpu cpu = loaded(R"(
OB 1
:U  E 0.0
:L  KT 020.1
:SE T 1
:U  T 1
:=  A 0.0
:BE
)");
    cpu.setInput(0, 0, true);
    cpu.runCycle();
    CHECK(!out(cpu, 0, 0));
    cpu.tick(1900);
    cpu.runCycle();
    CHECK(!out(cpu, 0, 0));
    CHECK_EQ(cpu.timer(1).value, 1);
    cpu.tick(100);
    cpu.runCycle();
    CHECK(out(cpu, 0, 0));
    cpu.setInput(0, 0, false);
    cpu.runCycle();
    CHECK(!out(cpu, 0, 0));
}

void testTimerSA()
{
    Cpu cpu = loaded(R"(
OB 1
:U  E 0.0
:L  KT 005.2
:SA T 2
:U  T 2
:=  A 0.0
:BE
)");
    cpu.setInput(0, 0, true);
    cpu.runCycle();
    CHECK(out(cpu, 0, 0));
    cpu.setInput(0, 0, false);
    cpu.runCycle();
    CHECK(out(cpu, 0, 0));
    cpu.tick(4000);
    cpu.runCycle();
    CHECK(out(cpu, 0, 0));
    cpu.tick(1000);
    cpu.runCycle();
    CHECK(!out(cpu, 0, 0));
}

void testBlinker()
{
    Cpu cpu = loaded(R"(
OB 1
:UN T 2
:L  KT 050.0
:SE T 1
:U  T 1
:L  KT 050.0
:SE T 2
:U  T 1
:=  A 0.0
:BE
)");
    int changes = 0;
    bool last = false;
    for (int i = 0; i < 300; ++i) {     // 3 s in 10-ms-Schritten
        cpu.tick(10);
        cpu.runCycle();
        if (out(cpu, 0, 0) != last) {
            ++changes;
            last = !last;
        }
    }
    CHECK(changes >= 5 && changes <= 7);
}

void testCounter()
{
    Cpu cpu = loaded(R"(
OB 1
:U  E 0.0
:ZV Z 1
:U  E 0.1
:ZR Z 1
:U  E 0.2
:L  KZ 005
:S  Z 1
:L  Z 1
:T  MW 20
:L  MW 20
:L  KF +3
:>=F
:=  A 0.0
:BE
)");
    for (int i = 0; i < 3; ++i) {
        cpu.setInput(0, 0, true);
        cpu.runCycle();
        cpu.setInput(0, 0, false);
        cpu.runCycle();
    }
    CHECK_EQ(cpu.counter(1).value, 3);
    CHECK(out(cpu, 0, 0));
    cpu.setInput(0, 1, true);
    cpu.runCycle();
    CHECK_EQ(cpu.counter(1).value, 2);
    CHECK(!out(cpu, 0, 0));
    cpu.setInput(0, 2, true);
    cpu.runCycle();
    CHECK_EQ(cpu.counter(1).value, 5);
    CHECK_EQ(flagWord(cpu, 20), 5);
}

void testArithmeticAndLoop()
{
    Cpu cpu = loaded(R"(
OB 1
:SPA FB 1
:A   DB 10
:L   DW 0
:L   DW 1
:+F
:T   DW 2
:L   DW 2
:T   MW 40
:BE

FB 1
:L  KF +0
:T  MW 30
:L  KF +10
:T  MW 32
M001:L  MW 30
:L  MW 32
:+F
:T  MW 30
:L  MW 32
:L  KF +1
:-F
:T  MW 32
:SPN =M001
:BE

DB 10
0: KF +100
1: KF -23
2: KF +0
)");
    cpu.runCycle();
    CHECK_EQ(flagWord(cpu, 30), 55);
    CHECK_EQ(flagWord(cpu, 40), 77);
    CHECK_EQ((*cpu.dataBlock(10))[2], 77);
}

void testBlockCallConditional()
{
    Cpu cpu = loaded(R"(
OB 1
:U  E 0.0
:SPB PB 2
:U  M 0.0
:=  A 0.1
:BE
PB 2
:UN M 0.0
:=  M 0.0
:BE
)");
    cpu.runCycle();
    CHECK(!out(cpu, 0, 1));
    cpu.setInput(0, 0, true);
    cpu.runCycle();
    CHECK(out(cpu, 0, 1));
    cpu.runCycle();
    CHECK(!out(cpu, 0, 1));
}

void testStartupOb21()
{
    Cpu cpu = loaded(R"(
OB 21
:L  KF +42
:T  MW 10
:BE
OB 1
:L  MW 10
:L  KF +1
:+F
:T  MW 10
:BE
)");
    cpu.runCycle();     // OB 21
    cpu.runCycle();     // OB 1
    CHECK_EQ(flagWord(cpu, 10), 43);
}

void testStepMode()
{
    ParseResult r = parse("OB 1\n:U E 0.0\n:= A 0.0\n:BE\n");
    Cpu cpu;
    cpu.load(r.program);
    cpu.start(true);
    CHECK_EQ(cpu.currentLine(), 2);
    cpu.setInput(0, 0, true);
    CHECK(!cpu.step());
    CHECK_EQ(cpu.currentLine(), 3);
    CHECK(cpu.lineStatus().at(2).vke);
    CHECK(!cpu.step());
    CHECK(!out(cpu, 0, 0));     // Ausgabe erst am Zyklusende
    CHECK(cpu.step());
    CHECK(out(cpu, 0, 0));
}

void testRuntimeErrors()
{
    Cpu cpu = loaded("OB 1\n:L DW 0\n:BE\n");
    CHECK(!cpu.runCycle());
    CHECK(cpu.state() == CpuState::Stop);
    CHECK_EQ(cpu.errorLine(), 2);

    Cpu loop = loaded("OB 1\n:SPA FB 1\n:BE\nFB 1\nM1: SPA =M1\n:BE\n");
    CHECK(!loop.runCycle());
    CHECK(loop.errorMessage().find("Zykluszeit") != std::string::npos);
}

void testParseErrors()
{
    ParseResult r = parse(R"(
OB 1
:U  E 0.9
:XYZ M 1.0
:L  KT 100.5
:SPA =NIRGENDS
:=  KF +1
:BE
)");
    CHECK_EQ(r.errors.size(), 5u);
    if (r.errors.size() == 5) {
        CHECK_EQ(r.errors[0].line, 3);
        CHECK_EQ(r.errors[1].line, 4);
        CHECK_EQ(r.errors[2].line, 5);
        CHECK_EQ(r.errors[3].line, 6);
        CHECK_EQ(r.errors[4].line, 7);
    }
    CHECK(!parse("PB 1\n:BE\n").ok());   // OB 1 fehlt
}

void testConstants()
{
    Cpu cpu = loaded(R"(
OB 1
:L  KH 00FF
:T  MW 0
:L  KM 1000000000000001
:T  MW 2
:L  KY 1,2
:T  MW 4
:L  KC AB
:T  MW 6
:L  KF -1
:T  MW 8
:BE
)");
    cpu.runCycle();
    CHECK_EQ(flagWord(cpu, 0), 0x00FF);
    CHECK_EQ(flagWord(cpu, 2), 0x8001);
    CHECK_EQ(flagWord(cpu, 4), 0x0102);
    CHECK_EQ(flagWord(cpu, 6), 0x4142);
    CHECK_EQ(flagWord(cpu, 8), 0xFFFF);
}

void testFbBitParams()
{
    Cpu cpu = loaded(R"(
OB 1
:SPA FB 10
NAME :MOTOR
EIN  :E 0.0
AUS  :E 0.1
MOT  :A 0.0
:SPA FB 10
NAME :MOTOR
MOT  :A 0.4         ; Reihenfolge beliebig
EIN  :E 1.0
AUS  :E 1.1
:BE

FB 10
NAME :MOTOR
BEZ  :EIN  E/A/D/B/T/Z: E  BI/BY/W/K: BI
BEZ  :AUS  E/A/D/B/T/Z: E  BI/BY/W/K: BI
BEZ  :MOT  E/A/D/B/T/Z: A  BI/BY/W/K: BI
:U  =EIN
:O  =MOT
:UN =AUS
:=  =MOT
:BE
)");
    cpu.setInput(0, 0, true);
    cpu.runCycle();
    cpu.setInput(0, 0, false);
    cpu.runCycle();
    CHECK(out(cpu, 0, 0));      // Motor 1 hält sich selbst
    CHECK(!out(cpu, 0, 4));     // Motor 2 unabhängig
    cpu.setInput(1, 0, true);
    cpu.runCycle();
    CHECK(out(cpu, 0, 4));
    cpu.setInput(0, 1, true);
    cpu.runCycle();
    CHECK(!out(cpu, 0, 0));
    CHECK(out(cpu, 0, 4));
}

void testFbTimerConstBlockWordParams()
{
    Cpu cpu = loaded(R"(
OB 1
:SPA FB 20
NAME :VERZ
STRT :E 0.0
ZEIT :KT 020.1
TIM  :T 5
AUS  :A 0.0
:L   KF +40
:T   MW 10
:L   KF +2
:T   MW 12
:SPA FB 30
X    :MW 10
Y    :MW 12
ERG  :MW 14
DBNR :DB 7
:A   DB 7
:L   DW 1
:T   MW 16
:BE

FB 20
NAME :VERZ
BEZ  :STRT  E BI
BEZ  :ZEIT  D KT
BEZ  :TIM   T
BEZ  :AUS   A BI
:U  =STRT
:L  =ZEIT
:SE =TIM
:U  =TIM
:=  =AUS
:BE

FB 30
BEZ :X    E/A/D/B/T/Z: E  BI/BY/W/K: W
BEZ :Y    E/A/D/B/T/Z: E  BI/BY/W/K: W
BEZ :ERG  E/A/D/B/T/Z: A  BI/BY/W/K: W
BEZ :DBNR E/A/D/B/T/Z: B
:L  =X
:L  =Y
:XF
:T  =ERG
:A  =DBNR
:T  DW 1
:BE

DB 7
KF +0
KF +0
)");
    cpu.setInput(0, 0, true);
    cpu.runCycle();
    CHECK(!out(cpu, 0, 0));
    cpu.tick(2000);
    cpu.runCycle();
    CHECK(out(cpu, 0, 0));
    CHECK_EQ(cpu.timer(5).q, true);
    CHECK_EQ(flagWord(cpu, 14), 80);
    CHECK_EQ(flagWord(cpu, 16), 80);
    CHECK_EQ((*cpu.dataBlock(7))[1], 80);
}

void testFbPassThrough()
{
    Cpu cpu = loaded(R"(
OB 1
:SPA FB 1
IN   :E 0.3
OUT  :A 0.7
:BE

FB 1
BEZ :IN  E BI
BEZ :OUT A BI
:SPA FB 2
X    :=IN
Y    :=OUT
:BE

FB 2
BEZ :X E BI
BEZ :Y A BI
:UN =X
:=  =Y
:BE
)");
    cpu.runCycle();
    CHECK(out(cpu, 0, 7));
    cpu.setInput(0, 3, true);
    cpu.runCycle();
    CHECK(!out(cpu, 0, 7));
}

void testFbLabelsAndParams()
{
    // Sprungmarken und Formaloperanden nebeneinander
    Cpu cpu = loaded(R"(
OB 1
:SPA FB 3
N    :KF +5
SUM  :MW 0
:BE
FB 3
BEZ :N   D KF
BEZ :SUM A W
:L  KF +0
:T  =SUM
:L  =N
:T  MW 2
M001:L  =SUM
:L  MW 2
:+F
:T  =SUM
:L  MW 2
:L  KF +1
:-F
:T  MW 2
:SPN =M001
:BE
)");
    cpu.runCycle();
    CHECK_EQ(flagWord(cpu, 0), 15);
}

void testFbParseErrors()
{
    const char* fb = R"(
FB 10
NAME :MOTOR
BEZ  :EIN  E BI
BEZ  :WERT D KF
BEZ  :ZEIT T
:U  =EIN
:L  =WERT
:SE =ZEIT
:BE
)";
    auto errorsFor = [&](const std::string& ob) {
        ParseResult r = parse(ob + fb);
        return r.errors;
    };
    // korrekt
    CHECK(errorsFor("OB 1\n:SPA FB 10\nNAME :MOTOR\nEIN :E 0.0\nWERT :KF +3\nZEIT :T 1\n:BE\n").empty());
    // fehlender Parameter
    auto e = errorsFor("OB 1\n:SPA FB 10\nEIN :E 0.0\nWERT :KF +3\n:BE\n");
    CHECK(e.size() == 1 && e[0].line == 2 && e[0].message.find("ZEIT") != std::string::npos);
    // falscher Typ
    e = errorsFor("OB 1\n:SPA FB 10\nEIN :MW 0\nWERT :KH 0003\nZEIT :Z 1\n:BE\n");
    CHECK_EQ(e.size(), 3u);
    // unbekannter Parameter, doppelt, falscher Name
    e = errorsFor("OB 1\n:SPA FB 10\nNAME :PUMPE\nEIN :E 0.0\nEIN :E 0.1\nWERT :KF +3\nZEIT :T 1\nFOO :E 0.2\n:BE\n");
    CHECK_EQ(e.size(), 3u);
    // ungültiger Aktualoperand wird verständlich gemeldet
    e = errorsFor("OB 1\n:SPA FB 10\nEIN :E 0.9\nWERT :KF +3\nZEIT :T 1\n:BE\n");
    CHECK(e.size() == 1 && e[0].line == 3 && e[0].message.find("Aktualoperand") != std::string::npos);

    // Deklarationsfehler und Verwendung
    ParseResult r = parse(R"(
OB 1
:U  =EIN
:BE
FB 2
BEZ :A1 E XX
BEZ :A2 D KG
BEZ :A3 T
:U  =A3
:L  =A3
:SE =A3
BEZ :A4 E BI
:U  =NIX
:BE
PB 1
BEZ :X E BI
:BE
)");
    std::vector<int> lines;
    for (const auto& d : r.errors) lines.push_back(d.line);
    CHECK((lines == std::vector<int>{3, 6, 7, 12, 13, 16}));
}

std::string readFile(const std::string& path)
{
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void testExamples()
{
    for (const char* name : {"grundlagen", "zeiten", "zaehler", "ampel", "bausteine", "fb_parameter"}) {
        std::string src = readFile(std::string(EXAMPLES_DIR) + "/" + name + ".awl");
        CHECK(!src.empty());
        ParseResult r = parse(src);
        for (const auto& e : r.errors) std::printf("  %s.awl Zeile %d: %s\n", name, e.line, e.message.c_str());
        CHECK(r.ok());
        Cpu cpu;
        cpu.load(r.program);
        cpu.start();
        for (int i = 0; i < 200; ++i) {
            cpu.setInput(0, i % 8, (i / 8) % 2);
            cpu.tick(10);
            CHECK(cpu.runCycle());
        }
    }

    // Ampel: Anforderung -> gelb -> Fußgänger grün -> rot-gelb -> grün
    Cpu cpu = loaded(readFile(std::string(EXAMPLES_DIR) + "/ampel.awl"));
    auto run = [&](int ms) {
        for (int t = 0; t < ms; t += 10) {
            cpu.tick(10);
            cpu.runCycle();
        }
    };
    run(100);
    CHECK(out(cpu, 0, 2));      // Auto grün
    CHECK(out(cpu, 1, 0));      // Fußgänger rot
    cpu.setInput(0, 0, true);
    run(50);
    cpu.setInput(0, 0, false);
    CHECK(out(cpu, 0, 1));      // gelb
    CHECK(!out(cpu, 0, 2));
    run(3100);
    CHECK(out(cpu, 0, 0));      // Auto rot
    CHECK(out(cpu, 1, 1));      // Fußgänger grün
    run(8100);
    CHECK(out(cpu, 0, 0) && out(cpu, 0, 1));    // rot-gelb
    CHECK(out(cpu, 1, 0));
    run(2100);
    CHECK(out(cpu, 0, 2));      // wieder grün

    // FB-Beispiel: Motor ein, Laufzähler, Störung mit Blinkmeldung
    Cpu fb = loaded(readFile(std::string(EXAMPLES_DIR) + "/fb_parameter.awl"));
    auto runFb = [&](int ms) {
        for (int t = 0; t < ms; t += 10) {
            fb.tick(10);
            fb.runCycle();
        }
    };
    fb.setInput(0, 0, true);
    runFb(50);
    fb.setInput(0, 0, false);
    runFb(50);
    CHECK(out(fb, 0, 0));
    CHECK(!out(fb, 0, 4));
    CHECK_EQ(flagWord(fb, 20), 1);
    fb.setInput(0, 2, true);    // Störung
    int blinks = 0;
    bool last = false;
    for (int i = 0; i < 200; ++i) {
        runFb(10);
        CHECK(!out(fb, 0, 0));
        if (out(fb, 0, 1) != last) {
            ++blinks;
            last = !last;
        }
    }
    CHECK(blinks >= 3);
}

}  // namespace

int main()
{
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"UND/ODER", testAndOr},
        {"UND vor ODER, Klammern", testAndBeforeOrAndBrackets},
        {"Setzen/Rücksetzen", testSetReset},
        {"Zeit SE", testTimerSE},
        {"Zeit SA", testTimerSA},
        {"Blinker", testBlinker},
        {"Zähler", testCounter},
        {"Arithmetik, Schleife, DB", testArithmeticAndLoop},
        {"Bedingter Bausteinaufruf", testBlockCallConditional},
        {"Anlauf OB 21", testStartupOb21},
        {"Einzelschritt", testStepMode},
        {"Laufzeitfehler", testRuntimeErrors},
        {"Syntaxfehler", testParseErrors},
        {"Konstanten", testConstants},
        {"FB-Parameter Bits", testFbBitParams},
        {"FB-Parameter Zeit/Konstante/Wort/Baustein", testFbTimerConstBlockWordParams},
        {"FB-Parameter durchreichen", testFbPassThrough},
        {"FB-Parameter und Sprungmarken", testFbLabelsAndParams},
        {"FB-Parameter Syntaxfehler", testFbParseErrors},
        {"Beispielprogramme", testExamples},
    };
    for (const auto& [name, fn] : tests) {
        currentTest = name;
        int before = failures;
        fn();
        std::printf("%s %s\n", failures == before ? "ok    " : "FEHLER", name);
    }
    std::printf("\n%s (%d Fehler)\n", failures ? "TESTS FEHLGESCHLAGEN" : "Alle Tests bestanden", failures);
    return failures ? 1 : 0;
}
