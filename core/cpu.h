// Simulierte S5-CPU: zyklische Bearbeitung eines übersetzten AWL-Programms.
#pragma once

#include "program.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace s5 {

enum class CpuState { Stop, Run, Halt };

struct TimerState {
    uint16_t value = 0;     // Restzeit in Einheiten der Zeitbasis (0..999)
    uint8_t base = 0;       // 0 = 10 ms, 1 = 100 ms, 2 = 1 s, 3 = 10 s
    int32_t accumMs = 0;
    bool running = false;
    bool q = false;         // Signalzustand (Abfrage U T n)
    bool edge = false;      // Flankenmerker für den Start
    Op mode = Op::SI;
};

struct CounterState {
    uint16_t value = 0;     // 0..999
    bool edgeUp = false, edgeDown = false, edgeSet = false;
};

// "Status Baustein": Werte nach der zuletzt ausgeführten Anweisung einer Zeile
struct LineStatus {
    bool vke = false;
    bool hasSta = false;    // Operand hat einen Signalzustand
    bool sta = false;
    bool akkuOp = false;    // Lade-/Transfer-/Rechenoperation: AKKU anzeigen
    uint16_t akku1 = 0;
    uint16_t akku2 = 0;
};

class Cpu {
public:
    static constexpr int kIoBytes = 128;
    static constexpr int kFlagBytes = 256;
    static constexpr int kTimers = 128;
    static constexpr int kCounters = 128;
    static constexpr size_t kMaxNesting = 16;
    static constexpr size_t kMaxBrackets = 7;
    static constexpr long kMaxInstructionsPerCycle = 1'000'000;

    Cpu();

    // Programm laden. Bei laufender CPU wird am Zyklusanfang weitergearbeitet.
    void load(const Program& program);
    const Program& program() const { return program_; }
    void resetMemory();     // Urlöschen: Merker, Zeiten, Zähler, Prozessabbild, DBs

    bool start(bool halted = false);     // Neuanlauf (OB 21, dann OB 1)
    void stop(const std::string& reason = {});
    void setHalted(bool halted);
    CpuState state() const { return state_; }

    void tick(int ms);      // Zeiten weiterzählen
    bool step();            // eine Anweisung; true wenn damit ein Zyklus endete
    bool runCycle();        // Zyklus zu Ende bearbeiten; false bei Fehler/STOP

    // Peripherie (E/A-Panel)
    void setInput(int byte, int bit, bool on);
    bool input(int byte, int bit) const;
    uint8_t inputByte(int byte) const { return peInputs_[byte]; }
    uint8_t outputByte(int byte) const { return paOutputs_[byte]; }

    // Beobachten
    std::optional<bool> peekBit(const Operand& o) const;
    std::optional<uint16_t> peekValue(const Operand& o) const;
    const TimerState& timer(int n) const { return timers_[n]; }
    const CounterState& counter(int n) const { return counters_[n]; }
    const std::map<int, LineStatus>& lineStatus() const { return lineStatus_; }
    const std::vector<uint16_t>* dataBlock(int n) const;
    int currentLine() const;            // nächste Anweisung (für Einzelschritt)
    const std::string& errorMessage() const { return error_; }
    int errorLine() const { return errorLine_; }
    uint64_t cycleCount() const { return cycleCount_; }
    bool cycleInProgress() const { return !stack_.empty(); }

private:
    struct Logic {
        bool first = true;      // Erstabfrage
        bool andVal = false;    // aktuelle UND-Gruppe
        bool orAcc = false;     // ODER-Verknüpfung abgeschlossener Gruppen
        bool groupStart = false;
    };
    struct Bracket {
        Logic saved;
        bool isOr;
    };
    struct Frame {
        const Block* block;
        size_t pc;
        int db;     // aufgeschlagener DB (-1 = keiner)
        std::vector<Operand> args;  // FB: Aktualoperanden der Parameter
    };
    enum class Cc { Zero, Neg, Pos };

    bool vke() const { return logic_.orAcc || logic_.andVal; }
    void query(bool value, bool isOr);
    void limitVke() { logic_.first = true; }
    void forceVkeOne();

    void exec(const Instruction& ins);
    void beginCycle();
    void endCycle();
    void callBlock(const Operand& o, const Instruction& call);
    void returnFromBlock();
    void jump(const std::string& label);

    bool readBit(const Operand& o);
    void writeBit(const Operand& o, bool v);
    uint16_t readValue(const Operand& o);
    void writeValue(const Operand& o, uint16_t v);
    uint16_t& dataWord(int address);

    void timerOp(int n, Op op, bool v);
    void startTimer(TimerState& t);
    static void expire(TimerState& t);
    void counterOp(int n, Op op, bool v);
    int bcdFromAkku(uint16_t mask, int digits);
    void setCcFromResult(int r);
    void initDataBlocks();

    Program program_;
    CpuState state_ = CpuState::Stop;

    std::array<uint8_t, kIoBytes> peInputs_{};   // Peripherie Eingänge (Panel)
    std::array<uint8_t, kIoBytes> paOutputs_{};  // Peripherie Ausgänge (LEDs)
    std::array<uint8_t, kIoBytes> pae_{};        // Prozessabbild Eingänge
    std::array<uint8_t, kIoBytes> paa_{};        // Prozessabbild Ausgänge
    std::array<uint8_t, kFlagBytes> flags_{};
    std::array<TimerState, kTimers> timers_{};
    std::array<CounterState, kCounters> counters_{};
    std::map<int, std::vector<uint16_t>> dbs_;

    uint16_t akku1_ = 0, akku2_ = 0;
    Logic logic_;
    std::vector<Bracket> brackets_;
    Cc cc_ = Cc::Zero;
    bool ov_ = false;

    std::vector<Frame> stack_;
    bool startupPending_ = false;
    bool stopRequested_ = false;
    long instructionsInCycle_ = 0;
    uint64_t cycleCount_ = 0;

    std::map<int, LineStatus> lineStatus_;
    bool staValid_ = false, staValue_ = false;

    std::string error_;
    int errorLine_ = -1;
};

}  // namespace s5
