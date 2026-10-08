#include "cpu.h"

#include "parser.h"

#include <stdexcept>
#include <utility>

namespace s5 {

namespace {

struct RuntimeError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

constexpr int kTimeBaseMs[4] = {10, 100, 1000, 10000};

}  // namespace

Cpu::Cpu() = default;

void Cpu::load(const Program& program)
{
    program_ = program;
    stack_.clear();
    brackets_.clear();
    logic_ = {};
    instructionsInCycle_ = 0;
    lineStatus_.clear();
    initDataBlocks();
    if (state_ != CpuState::Stop && !program_.find(BlockType::OB, 1))
        stop("OB 1 fehlt");
}

void Cpu::initDataBlocks()
{
    dbs_.clear();
    for (const auto& [key, block] : program_.blocks) {
        if (block.type == BlockType::DB) dbs_[block.number] = block.data;
    }
}

void Cpu::resetMemory()
{
    stop();
    pae_.fill(0);
    paa_.fill(0);
    flags_.fill(0);
    timers_.fill({});
    counters_.fill({});
    akku1_ = akku2_ = 0;
    initDataBlocks();
    lineStatus_.clear();
    cycleCount_ = 0;
}

bool Cpu::start(bool halted)
{
    error_.clear();
    errorLine_ = -1;
    if (!program_.find(BlockType::OB, 1)) {
        error_ = "OB 1 fehlt – CPU bleibt in STOP";
        state_ = CpuState::Stop;
        return false;
    }
    // Neuanlauf: Prozessabbild der Ausgänge, Merker, Zeiten und Zähler löschen
    paa_.fill(0);
    flags_.fill(0);
    timers_.fill({});
    counters_.fill({});
    akku1_ = akku2_ = 0;
    stack_.clear();
    brackets_.clear();
    logic_ = {};
    lineStatus_.clear();
    stopRequested_ = false;
    startupPending_ = program_.find(BlockType::OB, 21) != nullptr;
    cycleCount_ = 0;
    state_ = halted ? CpuState::Halt : CpuState::Run;
    return true;
}

void Cpu::stop(const std::string& reason)
{
    state_ = CpuState::Stop;
    stack_.clear();
    brackets_.clear();
    paOutputs_.fill(0);     // Befehlsausgabesperre im STOP
    if (!reason.empty()) error_ = reason;
}

void Cpu::setHalted(bool halted)
{
    if (state_ == CpuState::Stop) return;
    state_ = halted ? CpuState::Halt : CpuState::Run;
}

void Cpu::setInput(int byte, int bit, bool on)
{
    if (byte < 0 || byte >= kIoBytes || bit < 0 || bit > 7) return;
    if (on)
        peInputs_[byte] |= static_cast<uint8_t>(1u << bit);
    else
        peInputs_[byte] &= static_cast<uint8_t>(~(1u << bit));
}

bool Cpu::input(int byte, int bit) const
{
    return (peInputs_[byte] >> bit) & 1;
}

// ---------------------------------------------------------------- Zeiten

void Cpu::tick(int ms)
{
    if (state_ == CpuState::Stop || ms <= 0) return;
    for (auto& t : timers_) {
        if (!t.running) continue;
        t.accumMs += ms;
        const int baseMs = kTimeBaseMs[t.base];
        while (t.running && t.accumMs >= baseMs) {
            t.accumMs -= baseMs;
            if (t.value > 0) --t.value;
            if (t.value == 0) expire(t);
        }
    }
}

void Cpu::expire(TimerState& t)
{
    t.running = false;
    t.accumMs = 0;
    switch (t.mode) {
    case Op::SE:
    case Op::SS: t.q = true; break;
    default: t.q = false; break;
    }
}

void Cpu::startTimer(TimerState& t)
{
    if (!isValidBcd(akku1_ & 0x0FFF, 3))
        throw RuntimeError("Ungültiger Zeitwert in AKKU 1 (KT erwartet, z.B. L KT 050.1)");
    t.base = static_cast<uint8_t>((akku1_ >> 12) & 3);
    t.value = static_cast<uint16_t>(fromBcd(akku1_ & 0x0FFF));
    t.accumMs = 0;
    t.running = true;
    if (t.value == 0) expire(t);
}

void Cpu::timerOp(int n, Op op, bool v)
{
    TimerState& t = timers_[n];
    const bool rising = v && !t.edge;
    const bool falling = !v && t.edge;
    t.edge = v;

    switch (op) {
    case Op::SI:    // Impuls
        if (rising) {
            t.mode = op;
            t.q = true;
            startTimer(t);
        }
        if (!v) {
            t.running = false;
            t.q = false;
        }
        break;
    case Op::SV:    // verlängerter Impuls
        if (rising) {
            t.mode = op;
            t.q = true;
            startTimer(t);
        }
        break;
    case Op::SE:    // Einschaltverzögerung
        if (rising) {
            t.mode = op;
            t.q = false;
            startTimer(t);
        }
        if (!v) {
            t.running = false;
            t.q = false;
        }
        break;
    case Op::SS:    // speichernde Einschaltverzögerung
        if (rising) {
            t.mode = op;
            startTimer(t);
        }
        break;
    case Op::SA:    // Ausschaltverzögerung
        t.mode = op;
        if (v) {
            t.running = false;
            t.q = true;
        } else if (falling) {
            startTimer(t);
            if (t.running) t.q = true;
        }
        break;
    default: break;
    }
}

// ---------------------------------------------------------------- Zähler

int Cpu::bcdFromAkku(uint16_t mask, int digits)
{
    uint16_t v = akku1_ & mask;
    if (!isValidBcd(v, digits))
        throw RuntimeError("Ungültiger Zählwert in AKKU 1 (KZ erwartet, z.B. L KZ 010)");
    return fromBcd(v);
}

void Cpu::counterOp(int n, Op op, bool v)
{
    CounterState& c = counters_[n];
    switch (op) {
    case Op::ZV:
        if (v && !c.edgeUp && c.value < 999) ++c.value;
        c.edgeUp = v;
        break;
    case Op::ZR:
        if (v && !c.edgeDown && c.value > 0) --c.value;
        c.edgeDown = v;
        break;
    case Op::S:
        if (v && !c.edgeSet) c.value = static_cast<uint16_t>(bcdFromAkku(0x0FFF, 3));
        c.edgeSet = v;
        break;
    case Op::R:
        if (v) c.value = 0;
        break;
    default: break;
    }
}

// ---------------------------------------------------------------- Operanden

uint16_t& Cpu::dataWord(int address)
{
    int db = stack_.empty() ? -1 : stack_.back().db;
    if (db < 0) throw RuntimeError("Kein Datenbaustein aufgeschlagen (A DB n fehlt)");
    auto& data = dbs_.at(db);
    if (address >= static_cast<int>(data.size()))
        throw RuntimeError("DW " + std::to_string(address) + " liegt außerhalb von DB " + std::to_string(db)
                           + " (Länge " + std::to_string(data.size()) + " Wörter)");
    return data[address];
}

bool Cpu::readBit(const Operand& o)
{
    switch (o.kind) {
    case OpKind::InputBit: return (pae_[o.address] >> o.bit) & 1;
    case OpKind::OutputBit: return (paa_[o.address] >> o.bit) & 1;
    case OpKind::FlagBit: return (flags_[o.address] >> o.bit) & 1;
    case OpKind::DataBit: return (dataWord(o.address) >> o.bit) & 1;
    case OpKind::Timer: return timers_[o.address].q;
    case OpKind::Counter: return counters_[o.address].value != 0;
    default: throw RuntimeError("Operand ist kein Bit: " + toString(o));
    }
}

void Cpu::writeBit(const Operand& o, bool v)
{
    auto set8 = [&](uint8_t& b) {
        if (v)
            b |= static_cast<uint8_t>(1u << o.bit);
        else
            b &= static_cast<uint8_t>(~(1u << o.bit));
    };
    switch (o.kind) {
    case OpKind::InputBit: set8(pae_[o.address]); break;
    case OpKind::OutputBit: set8(paa_[o.address]); break;
    case OpKind::FlagBit: set8(flags_[o.address]); break;
    case OpKind::DataBit: {
        uint16_t& w = dataWord(o.address);
        if (v)
            w |= static_cast<uint16_t>(1u << o.bit);
        else
            w &= static_cast<uint16_t>(~(1u << o.bit));
        break;
    }
    default: throw RuntimeError("Operand ist kein Bit: " + toString(o));
    }
}

uint16_t Cpu::readValue(const Operand& o)
{
    auto word = [](const auto& arr, int a) { return static_cast<uint16_t>((arr[a] << 8) | arr[a + 1]); };
    switch (o.kind) {
    case OpKind::InputByte: return pae_[o.address];
    case OpKind::OutputByte: return paa_[o.address];
    case OpKind::FlagByte: return flags_[o.address];
    case OpKind::InputWord: return word(pae_, o.address);
    case OpKind::OutputWord: return word(paa_, o.address);
    case OpKind::FlagWord: return word(flags_, o.address);
    case OpKind::DataWord: return dataWord(o.address);
    case OpKind::DataLeft: return dataWord(o.address) >> 8;
    case OpKind::DataRight: return dataWord(o.address) & 0xFF;
    case OpKind::Constant: return o.value;
    case OpKind::Timer: return timers_[o.address].value;
    case OpKind::Counter: return counters_[o.address].value;
    default: throw RuntimeError("Operand kann nicht geladen werden: " + toString(o));
    }
}

void Cpu::writeValue(const Operand& o, uint16_t v)
{
    auto setWord = [v](auto& arr, int a) {
        arr[a] = static_cast<uint8_t>(v >> 8);
        arr[a + 1] = static_cast<uint8_t>(v & 0xFF);
    };
    switch (o.kind) {
    case OpKind::InputByte: pae_[o.address] = static_cast<uint8_t>(v); break;
    case OpKind::OutputByte: paa_[o.address] = static_cast<uint8_t>(v); break;
    case OpKind::FlagByte: flags_[o.address] = static_cast<uint8_t>(v); break;
    case OpKind::InputWord: setWord(pae_, o.address); break;
    case OpKind::OutputWord: setWord(paa_, o.address); break;
    case OpKind::FlagWord: setWord(flags_, o.address); break;
    case OpKind::DataWord: dataWord(o.address) = v; break;
    case OpKind::DataLeft: {
        uint16_t& w = dataWord(o.address);
        w = static_cast<uint16_t>((w & 0x00FF) | ((v & 0xFF) << 8));
        break;
    }
    case OpKind::DataRight: {
        uint16_t& w = dataWord(o.address);
        w = static_cast<uint16_t>((w & 0xFF00) | (v & 0xFF));
        break;
    }
    default: throw RuntimeError("Auf Operand kann nicht transferiert werden: " + toString(o));
    }
}

std::optional<bool> Cpu::peekBit(const Operand& o) const
{
    switch (o.kind) {
    case OpKind::InputBit: return (pae_[o.address] >> o.bit) & 1;
    case OpKind::OutputBit: return (paa_[o.address] >> o.bit) & 1;
    case OpKind::FlagBit: return (flags_[o.address] >> o.bit) & 1;
    case OpKind::Timer: return timers_[o.address].q;
    case OpKind::Counter: return counters_[o.address].value != 0;
    default: return std::nullopt;
    }
}

std::optional<uint16_t> Cpu::peekValue(const Operand& o) const
{
    auto word = [](const auto& arr, int a) { return static_cast<uint16_t>((arr[a] << 8) | arr[a + 1]); };
    switch (o.kind) {
    case OpKind::InputByte: return pae_[o.address];
    case OpKind::OutputByte: return paa_[o.address];
    case OpKind::FlagByte: return flags_[o.address];
    case OpKind::InputWord: return word(pae_, o.address);
    case OpKind::OutputWord: return word(paa_, o.address);
    case OpKind::FlagWord: return word(flags_, o.address);
    case OpKind::Timer: return timers_[o.address].value;
    case OpKind::Counter: return counters_[o.address].value;
    default: return std::nullopt;
    }
}

const std::vector<uint16_t>* Cpu::dataBlock(int n) const
{
    auto it = dbs_.find(n);
    return it == dbs_.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------- Ablauf

void Cpu::query(bool value, bool isOr)
{
    Logic& l = logic_;
    if (l.first) {
        l.andVal = value;
        l.orAcc = false;
        l.groupStart = false;
        l.first = false;
    } else if (l.groupStart) {
        l.andVal = value;
        l.groupStart = false;
    } else if (isOr) {
        l.andVal = l.orAcc || l.andVal || value;
        l.orAcc = false;
    } else {
        l.andVal = l.andVal && value;
    }
}

void Cpu::forceVkeOne()
{
    logic_.andVal = true;
    logic_.orAcc = false;
    logic_.groupStart = false;
    logic_.first = true;
}

void Cpu::setCcFromResult(int r)
{
    cc_ = r == 0 ? Cc::Zero : (r < 0 ? Cc::Neg : Cc::Pos);
}

void Cpu::beginCycle()
{
    lineStatus_.clear();
    instructionsInCycle_ = 0;
    pae_ = peInputs_;
    logic_ = {};
    brackets_.clear();
    const Block* ob = nullptr;
    if (startupPending_) ob = program_.find(BlockType::OB, 21);
    if (!ob) {
        startupPending_ = false;
        ob = program_.find(BlockType::OB, 1);
    }
    stack_.push_back({ob, 0, -1, {}});
}

void Cpu::endCycle()
{
    paOutputs_ = paa_;
    startupPending_ = false;
    ++cycleCount_;
    if (stopRequested_) {
        stopRequested_ = false;
        stop("STOP durch Operation STP");
    }
}

void Cpu::callBlock(const Operand& o, const Instruction& call)
{
    if (o.kind != OpKind::Block || o.blockType == BlockType::DB || o.blockType == BlockType::OB)
        throw RuntimeError("Aufruf nur für PB, FB oder SB möglich (" + toString(o) + ")");
    const Block* b = program_.find(o.blockType, o.address);
    if (!b) throw RuntimeError(std::string("Baustein ") + blockTypeName(o.blockType) + " "
                               + std::to_string(o.address) + " ist nicht vorhanden");
    if (stack_.size() >= kMaxNesting)
        throw RuntimeError("Schachtelungstiefe überschritten (max. " + std::to_string(kMaxNesting) + " Ebenen)");

    const Frame& caller = stack_.back();
    std::vector<Operand> args;
    if (!b->params.empty()) {
        if (call.args.size() != b->params.size())
            throw RuntimeError(b->name() + " hat Parameter und muss direkt mit Parameterversorgung aufgerufen werden");
        args = call.args;
        for (Operand& a : args) {   // durchgereichte Formaloperanden auflösen
            if (a.kind == OpKind::Param) a = caller.args.at(a.address);
        }
    }
    int db = caller.db;
    stack_.push_back({b, 0, db, std::move(args)});
    limitVke();
}

void Cpu::returnFromBlock()
{
    stack_.pop_back();
    brackets_.clear();
    limitVke();
}

void Cpu::jump(const std::string& label)
{
    Frame& f = stack_.back();
    f.pc = f.block->labels.at(label);
}

int Cpu::currentLine() const
{
    if (state_ == CpuState::Stop) return -1;
    if (stack_.empty()) {
        const Block* ob = startupPending_ ? program_.find(BlockType::OB, 21) : nullptr;
        if (!ob) ob = program_.find(BlockType::OB, 1);
        if (!ob || ob->code.empty()) return -1;
        return ob->code.front().line;
    }
    const Frame& f = stack_.back();
    if (f.pc < f.block->code.size()) return f.block->code[f.pc].line;
    return f.block->code.empty() ? f.block->line : f.block->code.back().line;
}

bool Cpu::step()
{
    if (state_ == CpuState::Stop) return false;
    if (stack_.empty()) beginCycle();

    Frame& f = stack_.back();
    if (f.pc >= f.block->code.size()) {     // Bausteinende ohne BE
        returnFromBlock();
        if (stack_.empty()) {
            endCycle();
            return true;
        }
        return false;
    }

    const Instruction& ins = f.block->code[f.pc++];
    try {
        if (++instructionsInCycle_ > kMaxInstructionsPerCycle)
            throw RuntimeError("Zykluszeit überschritten (Endlosschleife?)");
        staValid_ = false;
        if (ins.operand.kind == OpKind::Param) {
            // Formaloperand durch den Aktualoperanden des Aufrufs ersetzen
            const Frame& cur = stack_.back();
            if (ins.operand.address >= cur.args.size())
                throw RuntimeError("Formaloperand '=" + ins.operand.label + "' ist nicht versorgt");
            Instruction actual = ins;
            actual.operand = cur.args[ins.operand.address];
            exec(actual);
        } else {
            exec(ins);
        }
    } catch (const std::exception& e) {
        errorLine_ = ins.line;
        stop(e.what());
        return false;
    }

    if (ins.op == Op::BLD || ins.op == Op::NOP) {   // Netzwerktrenner u.ä. ohne Status
        if (stack_.empty()) {
            endCycle();
            return true;
        }
        return false;
    }
    LineStatus& st = lineStatus_[ins.line];
    st.vke = vke();
    st.hasSta = staValid_;
    st.sta = staValue_;
    st.akkuOp = (ins.op >= Op::L && ins.op <= Op::T) || (ins.op >= Op::AddF && ins.op <= Op::TAK);
    st.akku1 = akku1_;
    st.akku2 = akku2_;

    if (stack_.empty()) {
        endCycle();
        return true;
    }
    return false;
}

bool Cpu::runCycle()
{
    while (state_ != CpuState::Stop) {
        if (step()) return state_ != CpuState::Stop;
    }
    return false;
}

void Cpu::exec(const Instruction& ins)
{
    const Operand& o = ins.operand;
    auto sta = [this](bool b) {
        staValid_ = true;
        staValue_ = b;
        return b;
    };

    switch (ins.op) {
    case Op::U:
    case Op::P: query(sta(readBit(o)), false); break;
    case Op::UN:
    case Op::PN: query(!sta(readBit(o)), false); break;
    case Op::O: query(sta(readBit(o)), true); break;
    case Op::ON: query(!sta(readBit(o)), true); break;
    case Op::OAlone:
        if (!logic_.first) {
            logic_.orAcc = vke();
            logic_.andVal = false;
            logic_.groupStart = true;
        }
        break;
    case Op::UOpen:
    case Op::OOpen:
        if (brackets_.size() >= kMaxBrackets)
            throw RuntimeError("Mehr als " + std::to_string(kMaxBrackets) + " Klammerebenen");
        brackets_.push_back({logic_, ins.op == Op::OOpen});
        logic_ = {};
        break;
    case Op::Close: {
        if (brackets_.empty()) throw RuntimeError("')' ohne zugehöriges 'U(' bzw. 'O('");
        bool inner = vke();
        Bracket b = brackets_.back();
        brackets_.pop_back();
        logic_ = b.saved;
        query(inner, b.isOr);
        break;
    }
    case Op::Assign:
        writeBit(o, vke());
        sta(vke());
        limitVke();
        break;
    case Op::S:
        if (o.kind == OpKind::Counter)
            counterOp(o.address, Op::S, vke());
        else if (vke())
            writeBit(o, true);
        limitVke();
        break;
    case Op::R:
        if (o.kind == OpKind::Timer) {
            if (vke()) {
                TimerState& t = timers_[o.address];
                t.running = false;
                t.q = false;
                t.value = 0;
            }
        } else if (o.kind == OpKind::Counter) {
            counterOp(o.address, Op::R, vke());
        } else if (vke()) {
            writeBit(o, false);
        }
        limitVke();
        break;
    case Op::SU: writeBit(o, true); break;
    case Op::RU: writeBit(o, false); break;

    case Op::L:
        akku2_ = akku1_;
        akku1_ = readValue(o);
        break;
    case Op::LC:
        akku2_ = akku1_;
        if (o.kind == OpKind::Timer) {
            const TimerState& t = timers_[o.address];
            akku1_ = static_cast<uint16_t>((t.base << 12) | toBcd(t.value));
        } else {
            akku1_ = toBcd(counters_[o.address].value);
        }
        break;
    case Op::T: writeValue(o, akku1_); break;

    case Op::SI:
    case Op::SV:
    case Op::SE:
    case Op::SS:
    case Op::SA:
        timerOp(o.address, ins.op, vke());
        limitVke();
        break;
    case Op::ZV:
    case Op::ZR:
        counterOp(o.address, ins.op, vke());
        limitVke();
        break;

    case Op::AddF:
    case Op::SubF:
    case Op::MulF: {
        int a = static_cast<int16_t>(akku2_);
        int b = static_cast<int16_t>(akku1_);
        int r = ins.op == Op::AddF ? a + b : ins.op == Op::SubF ? a - b : a * b;
        ov_ = r < -32768 || r > 32767;
        akku1_ = static_cast<uint16_t>(r);
        setCcFromResult(static_cast<int16_t>(akku1_));
        break;
    }
    case Op::DivF: {
        int a = static_cast<int16_t>(akku2_);
        int b = static_cast<int16_t>(akku1_);
        if (b == 0) {
            ov_ = true;
            cc_ = Cc::Zero;
            break;
        }
        int q = a / b;
        ov_ = q > 32767;
        akku2_ = static_cast<uint16_t>(a % b);
        akku1_ = static_cast<uint16_t>(q);
        setCcFromResult(static_cast<int16_t>(akku1_));
        break;
    }
    case Op::CmpEq:
    case Op::CmpNe:
    case Op::CmpGt:
    case Op::CmpGe:
    case Op::CmpLt:
    case Op::CmpLe: {
        int a = static_cast<int16_t>(akku2_);
        int b = static_cast<int16_t>(akku1_);
        bool r = false;
        switch (ins.op) {
        case Op::CmpEq: r = a == b; break;
        case Op::CmpNe: r = a != b; break;
        case Op::CmpGt: r = a > b; break;
        case Op::CmpGe: r = a >= b; break;
        case Op::CmpLt: r = a < b; break;
        default: r = a <= b; break;
        }
        setCcFromResult(a - b);
        ov_ = false;
        query(r, false);
        break;
    }

    case Op::UW: akku1_ &= akku2_; cc_ = akku1_ ? Cc::Pos : Cc::Zero; break;
    case Op::OW: akku1_ |= akku2_; cc_ = akku1_ ? Cc::Pos : Cc::Zero; break;
    case Op::XOW: akku1_ ^= akku2_; cc_ = akku1_ ? Cc::Pos : Cc::Zero; break;
    case Op::KEW: akku1_ = static_cast<uint16_t>(~akku1_); break;
    case Op::KZW:
        ov_ = akku1_ == 0x8000;
        akku1_ = static_cast<uint16_t>(-static_cast<int16_t>(akku1_));
        setCcFromResult(static_cast<int16_t>(akku1_));
        break;
    case Op::SLW:
        akku1_ = static_cast<uint16_t>(akku1_ << o.value);
        cc_ = akku1_ ? Cc::Pos : Cc::Zero;
        break;
    case Op::SRW:
        akku1_ = static_cast<uint16_t>(akku1_ >> o.value);
        cc_ = akku1_ ? Cc::Pos : Cc::Zero;
        break;
    case Op::Inc:
        akku1_ = static_cast<uint16_t>((akku1_ & 0xFF00) | ((akku1_ + o.value) & 0xFF));
        break;
    case Op::Dec:
        akku1_ = static_cast<uint16_t>((akku1_ & 0xFF00) | ((akku1_ - o.value) & 0xFF));
        break;
    case Op::TAK: std::swap(akku1_, akku2_); break;

    case Op::SPA:
        if (o.kind == OpKind::Label)
            jump(o.label);
        else
            callBlock(o, ins);
        break;
    case Op::SPB: {
        bool doIt = vke();
        forceVkeOne();
        if (doIt) {
            if (o.kind == OpKind::Label)
                jump(o.label);
            else
                callBlock(o, ins);
        }
        break;
    }
    case Op::SPZ: if (cc_ == Cc::Zero) jump(o.label); break;
    case Op::SPN: if (cc_ != Cc::Zero) jump(o.label); break;
    case Op::SPP: if (cc_ == Cc::Pos) jump(o.label); break;
    case Op::SPM: if (cc_ == Cc::Neg) jump(o.label); break;
    case Op::SPO: if (ov_) jump(o.label); break;

    case Op::BE:
    case Op::BEA: returnFromBlock(); break;
    case Op::BEB: {
        bool doIt = vke();
        forceVkeOne();
        if (doIt) returnFromBlock();
        break;
    }
    case Op::ADb:
        if (o.kind != OpKind::Block || o.blockType != BlockType::DB)
            throw RuntimeError("A erwartet einen Datenbaustein, Parameter liefert " + toString(o));
        if (!dbs_.count(o.address))
            throw RuntimeError("Datenbaustein DB " + std::to_string(o.address) + " ist nicht vorhanden");
        stack_.back().db = o.address;
        break;

    case Op::B:     // Bausteinparameter: aufschlagen bzw. aufrufen
        if (o.kind == OpKind::Block && o.blockType == BlockType::DB) {
            if (!dbs_.count(o.address))
                throw RuntimeError("Datenbaustein DB " + std::to_string(o.address) + " ist nicht vorhanden");
            stack_.back().db = o.address;
        } else {
            callBlock(o, ins);
        }
        break;
    case Op::NOP:
    case Op::BLD: break;
    case Op::STP: stopRequested_ = true; break;
    }
}

}  // namespace s5
