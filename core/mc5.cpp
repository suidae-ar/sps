#include "mc5.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>

namespace s5::mc5 {

namespace {

struct AsmFail {
    std::string message;
};

[[noreturn]] void fail(const std::string& m) { throw AsmFail{m}; }

constexpr uint16_t W(int hi, int lo)
{
    return static_cast<uint16_t>(((hi & 0xFF) << 8) | (lo & 0xFF));
}

constexpr int kHeaderWords = 5;

bool is135(CpuFamily f) { return f == CpuFamily::S5_135U; }

void need135(CpuFamily f, const std::string& what)
{
    if (!is135(f)) fail(what + " gibt es nur bei S5-135U/155U");
}

// ------------------------------------------------------------------ Tabellen

// Reihenfolge der Siemens-Tabelle: U, O, S, =, UN, ON, R (Abstand 8 im ersten Byte)
int bitOpIndex(Op op)
{
    switch (op) {
    case Op::U: return 0;
    case Op::O: return 1;
    case Op::S: return 2;
    case Op::Assign: return 3;
    case Op::UN: return 4;
    case Op::ON: return 5;
    case Op::R: return 6;
    default: return -1;
    }
}

// Datenbit (135U, 783F): Kennung im zweiten Wort
int dataBitOpCode(Op op)
{
    switch (op) {
    case Op::U: return 0;
    case Op::O: return 1;
    case Op::UN: return 2;
    case Op::ON: return 3;
    case Op::S: return 4;
    case Op::R: return 5;
    case Op::Assign: return 6;
    default: return -1;
    }
}

uint8_t constantSubcode(const std::string& fmt)
{
    if (fmt == "KZ") return 0x01;
    if (fmt == "KT") return 0x02;
    if (fmt == "KF") return 0x04;
    if (fmt == "KC") return 0x10;
    if (fmt == "KY") return 0x20;
    if (fmt == "KH") return 0x40;
    if (fmt == "KM") return 0x80;
    return 0;
}

// Parameterblock im FB: Art- und Formatkennung
uint8_t paramKindCode(const ParamDecl& d)
{
    switch (d.kind) {
    case ParamKind::E: return 2;
    case ParamKind::D: return 4;
    case ParamKind::T: return 5;
    case ParamKind::B: return 7;
    case ParamKind::A: return 8;
    case ParamKind::Z: break;
    }
    fail("Parameter '" + d.name + "': Kennung der Parameterart Z im MC5-Code ist nicht dokumentiert");
}

uint8_t paramFormatCode(const ParamDecl& d)
{
    if (d.kind == ParamKind::E || d.kind == ParamKind::A) {
        if (d.type == "BI") return 0x80;
        if (d.type == "BY") return 0x40;
        if (d.type == "W") return 0x20;
    }
    if (d.kind == ParamKind::D) return constantSubcode(d.type);
    return 0;
}

uint8_t checkedByte(int v, const std::string& what)
{
    if (v < 0 || v > 255) fail(what + " " + std::to_string(v) + " ist im MC5-Code nicht darstellbar (max. 255)");
    return static_cast<uint8_t>(v);
}

// ------------------------------------------------------------------ Assembler

class BlockAssembler {
public:
    BlockAssembler(const Program& program, const Block& block, CpuFamily family)
        : program_(program), block_(block), family_(family)
    {
    }

    // here = Wortadresse der Anweisung, labelAddr = Adressen der Sprungmarken (nur Pass 2)
    std::vector<uint16_t> encode(const Instruction& ins, int here, const std::map<std::string, int>* labelAddr)
    {
        std::vector<uint16_t> out;
        const Operand& o = ins.operand;
        auto one = [&](uint16_t w) { out.push_back(w); };

        if (o.kind == OpKind::Param) {
            encodeFormal(ins, out);
            return out;
        }

        switch (ins.op) {
        case Op::U:
        case Op::UN:
        case Op::O:
        case Op::ON:
        case Op::Assign:
        case Op::S:
        case Op::R: encodeBit(ins.op, o, out); break;
        case Op::OAlone: one(0xFB00); break;
        case Op::UOpen: one(0xBA00); break;
        case Op::OOpen: one(0xBB00); break;
        case Op::Close: one(0xBF00); break;

        case Op::P:
        case Op::PN:
        case Op::SU:
        case Op::RU: {
            const int q = ins.op == Op::P ? 0xC : ins.op == Op::PN ? 0x8 : ins.op == Op::SU ? 0x4 : 0x0;
            if (o.kind == OpKind::DataBit) {
                one(0x7046);
                one(static_cast<uint16_t>((q << 12) | (o.bit << 8) | checkedByte(o.address, "Datenwort")));
            } else if (o.kind == OpKind::InputBit || o.kind == OpKind::OutputBit) {
                need135(family_, "P/PN/SU/RU mit E/A");
                one(0x7038);
                one(static_cast<uint16_t>((q << 12) | (o.bit << 8) | o.address | (o.kind == OpKind::OutputBit ? 0x80 : 0)));
            } else {
                need135(family_, "P/PN/SU/RU mit M");
                one(0x7049);
                one(static_cast<uint16_t>((q << 12) | (o.bit << 8) | o.address));
            }
            break;
        }

        case Op::L: encodeLoad(o, out); break;
        case Op::LC: one(W(o.kind == OpKind::Timer ? 0x0C : 0x4C, o.address)); break;
        case Op::T: encodeTransfer(o, out); break;

        case Op::SI: one(W(0x34, o.address)); break;
        case Op::SV: one(W(0x1C, o.address)); break;
        case Op::SE: one(W(0x24, o.address)); break;
        case Op::SS: one(W(0x2C, o.address)); break;
        case Op::SA: one(W(0x14, o.address)); break;
        case Op::ZV: one(W(0x6C, o.address)); break;
        case Op::ZR: one(W(0x54, o.address)); break;

        case Op::AddF: one(0x7900); break;
        case Op::SubF: one(0x5900); break;
        case Op::MulF: need135(family_, "XF"); one(0x6004); break;
        case Op::DivF: need135(family_, ":F"); one(0x6000); break;
        case Op::CmpGt: one(0x2120); break;
        case Op::CmpLt: one(0x2140); break;
        case Op::CmpNe: one(0x2160); break;
        case Op::CmpEq: one(0x2180); break;
        case Op::CmpGe: one(0x21A0); break;
        case Op::CmpLe: one(0x21C0); break;

        case Op::UW: one(0x4100); break;
        case Op::OW: one(0x4900); break;
        case Op::XOW: one(0x5100); break;
        case Op::KEW: one(0x0100); break;
        case Op::KZW: one(0x0900); break;
        case Op::SLW: one(W(0x61, o.value & 0x0F)); break;
        case Op::SRW: one(W(0x69, o.value & 0x0F)); break;
        case Op::Inc: one(W(0x11, o.value)); break;
        case Op::Dec: one(W(0x19, o.value)); break;
        case Op::TAK: one(0x7002); break;
        case Op::STP: one(0x7003); break;
        case Op::NOP:
            if (o.kind == OpKind::Number && o.value > 1) fail("NOP 0 oder NOP 1 erwartet");
            one(o.kind == OpKind::Number && o.value == 1 ? 0xFFFF : 0x0000);
            break;
        case Op::BLD: one(W(0x10, checkedByte(o.value, "BLD"))); break;

        case Op::SPA:
        case Op::SPB:
            if (o.kind == OpKind::Label) {
                one(W(ins.op == Op::SPA ? 0x2D : 0xFA, displacement(o.label, here, labelAddr)));
            } else {
                encodeCall(ins, out);
            }
            break;
        case Op::SPZ: one(W(0x45, displacement(o.label, here, labelAddr))); break;
        case Op::SPN: one(W(0x35, displacement(o.label, here, labelAddr))); break;
        case Op::SPP: one(W(0x15, displacement(o.label, here, labelAddr))); break;
        case Op::SPM: one(W(0x25, displacement(o.label, here, labelAddr))); break;
        case Op::SPO: one(W(0x0D, displacement(o.label, here, labelAddr))); break;

        case Op::BE: one(0x6500); break;
        case Op::BEA: one(0x6501); break;
        case Op::BEB: one(0x0500); break;
        case Op::ADb: one(W(0x20, o.address)); break;
        case Op::B: fail("B ohne Formaloperand ist nicht darstellbar");
        }
        return out;
    }

private:
    int displacement(const std::string& label, int here, const std::map<std::string, int>* labelAddr)
    {
        if (!labelAddr) return 0;
        int d = labelAddr->at(label) - here;
        if (d < -128 || d > 127)
            fail("Sprungweite zu '" + label + "' (" + std::to_string(d) + " Wörter) außerhalb -128..+127");
        return d & 0xFF;
    }

    void encodeBit(Op op, const Operand& o, std::vector<uint16_t>& out)
    {
        const int idx = bitOpIndex(op);
        switch (o.kind) {
        case OpKind::FlagBit: out.push_back(W(0x80 + idx * 8 + o.bit, o.address)); return;
        case OpKind::InputBit: out.push_back(W(0xC0 + idx * 8 + o.bit, o.address)); return;
        case OpKind::OutputBit: out.push_back(W(0xC0 + idx * 8 + o.bit, 0x80 | o.address)); return;
        case OpKind::DataBit:
            need135(family_, "Verknüpfungen mit Datenbits (D n.m)");
            out.push_back(0x783F);
            out.push_back(static_cast<uint16_t>((dataBitOpCode(op) << 12) | (o.bit << 8)
                                                | checkedByte(o.address, "Datenwort")));
            return;
        case OpKind::Timer:
            switch (op) {
            case Op::U: out.push_back(W(0xF8, o.address)); return;
            case Op::O: out.push_back(W(0xF9, o.address)); return;
            case Op::UN: out.push_back(W(0xFC, o.address)); return;
            case Op::ON: out.push_back(W(0xFD, o.address)); return;
            case Op::R: out.push_back(W(0x3C, o.address)); return;
            default: break;
            }
            break;
        case OpKind::Counter:
            switch (op) {
            case Op::U: out.push_back(W(0xB8, o.address)); return;
            case Op::O: out.push_back(W(0xB9, o.address)); return;
            case Op::UN: out.push_back(W(0xBC, o.address)); return;
            case Op::ON: out.push_back(W(0xBD, o.address)); return;
            case Op::R: out.push_back(W(0x7C, o.address)); return;
            case Op::S: out.push_back(W(0x5C, o.address)); return;
            default: break;
            }
            break;
        default: break;
        }
        fail("Operand " + toString(o) + " ist hier im MC5-Code nicht darstellbar");
    }

    void encodeLoad(const Operand& o, std::vector<uint16_t>& out)
    {
        switch (o.kind) {
        case OpKind::InputByte: out.push_back(W(0x4A, o.address)); return;
        case OpKind::OutputByte: out.push_back(W(0x4A, 0x80 | o.address)); return;
        case OpKind::InputWord: out.push_back(W(0x52, o.address)); return;
        case OpKind::OutputWord: out.push_back(W(0x52, 0x80 | o.address)); return;
        case OpKind::FlagByte: out.push_back(W(0x0A, o.address)); return;
        case OpKind::FlagWord: out.push_back(W(0x12, o.address)); return;
        case OpKind::DataLeft: out.push_back(W(0x22, checkedByte(o.address, "DL"))); return;
        case OpKind::DataRight: out.push_back(W(0x2A, checkedByte(o.address, "DR"))); return;
        case OpKind::DataWord: out.push_back(W(0x32, checkedByte(o.address, "DW"))); return;
        case OpKind::Timer: out.push_back(W(0x02, o.address)); return;
        case OpKind::Counter: out.push_back(W(0x42, o.address)); return;
        case OpKind::Constant:
            if (o.label == "KB") {
                out.push_back(W(0x28, o.value));
            } else {
                out.push_back(W(0x30, constantSubcode(o.label)));
                out.push_back(o.value);
            }
            return;
        default: fail("L " + toString(o) + " ist im MC5-Code nicht darstellbar");
        }
    }

    void encodeTransfer(const Operand& o, std::vector<uint16_t>& out)
    {
        switch (o.kind) {
        case OpKind::InputByte: out.push_back(W(0x4B, o.address)); return;
        case OpKind::OutputByte: out.push_back(W(0x4B, 0x80 | o.address)); return;
        case OpKind::InputWord: out.push_back(W(0x53, o.address)); return;
        case OpKind::OutputWord: out.push_back(W(0x53, 0x80 | o.address)); return;
        case OpKind::FlagByte: out.push_back(W(0x0B, o.address)); return;
        case OpKind::FlagWord: out.push_back(W(0x13, o.address)); return;
        case OpKind::DataLeft: out.push_back(W(0x23, checkedByte(o.address, "DL"))); return;
        case OpKind::DataRight: out.push_back(W(0x2B, checkedByte(o.address, "DR"))); return;
        case OpKind::DataWord: out.push_back(W(0x33, checkedByte(o.address, "DW"))); return;
        default: fail("T " + toString(o) + " ist im MC5-Code nicht darstellbar");
        }
    }

    // Formaloperanden im FB: Operationscode + Parameternummer (1-basiert)
    void encodeFormal(const Instruction& ins, std::vector<uint16_t>& out)
    {
        const ParamDecl& d = block_.params.at(ins.operand.address);
        const int c = ins.operand.address + 1;
        const bool tz = d.kind == ParamKind::T || d.kind == ParamKind::Z;
        int code = -1;
        switch (ins.op) {
        case Op::U: code = 0x07; break;
        case Op::O: code = 0x0F; break;
        case Op::UN: code = 0x27; break;
        case Op::ON: code = 0x2F; break;
        case Op::Assign: code = 0x1F; break;
        case Op::S: code = d.kind == ParamKind::Z ? 0x1E : 0x17; break;  // S= bzw. SVZ= (S Z)
        case Op::R: code = tz ? 0x3E : 0x37; break;                     // RD= bzw. RB=
        case Op::SI: code = 0x36; break;
        case Op::SE: code = 0x26; break;
        case Op::SV: code = 0x1E; break;    // SVZ=
        case Op::SS: code = 0x2E; break;    // SSV=
        case Op::SA: code = 0x16; break;    // SAR=
        case Op::ZV: code = 0x2E; break;    // SSV=
        case Op::ZR: code = 0x16; break;    // SAR=
        case Op::L: code = 0x46; break;
        case Op::LC: code = 0x0E; break;
        case Op::T: code = 0x66; break;
        case Op::B:
        case Op::SPA:
        case Op::ADb: code = 0x76; break;   // B= (Bausteinparameter)
        default: break;
        }
        if (code < 0)
            fail("Formaloperand '=" + ins.operand.label + "' ist bei dieser Operation im MC5-Code nicht darstellbar");
        out.push_back(W(code, c));
    }

    void encodeCall(const Instruction& ins, std::vector<uint16_t>& out)
    {
        const Operand& o = ins.operand;
        const bool spa = ins.op == Op::SPA;
        switch (o.blockType) {
        case BlockType::PB: out.push_back(W(spa ? 0x75 : 0x55, o.address)); return;
        case BlockType::SB: out.push_back(W(spa ? 0x7D : 0x5D, o.address)); return;
        case BlockType::OB: out.push_back(W(spa ? 0x6D : 0x4D, o.address)); return;
        case BlockType::DB: fail("Datenbaustein kann nicht aufgerufen werden");
        case BlockType::FB: break;
        }
        out.push_back(W(spa ? 0x3D : 0x1D, o.address));
        const Block* fb = program_.find(BlockType::FB, o.address);
        if (!fb) fail("FB " + std::to_string(o.address) + " ist nicht vorhanden");
        // Der PG fügt nach dem Aufruf einen Sprung über die Parameterliste ein
        out.push_back(W(0x2D, static_cast<int>(fb->params.size()) + 1));
        for (size_t i = 0; i < fb->params.size(); ++i) {
            if (i >= ins.args.size()) fail("Parameter von FB " + std::to_string(o.address) + " nicht versorgt");
            out.push_back(encodeActual(fb->params[i], ins.args[i]));
        }
    }

    // Aktualoperand eines FB-Aufrufs: ein Wort, Operationscode der entsprechenden Anweisung
    // (U/L/A/SPA) bzw. bei D der Konstantenwert. Für T/Z ist nur das Nummernbyte belegt.
    uint16_t encodeActual(const ParamDecl& d, const Operand& a)
    {
        if (a.kind == OpKind::Param)
            fail("Parameter '" + d.name + "': Durchreichen von Formaloperanden ist im MC5-Code nicht dokumentiert");
        switch (a.kind) {
        case OpKind::InputBit: return W(0xC0 | a.bit, a.address);
        case OpKind::OutputBit: return W(0xC0 | a.bit, 0x80 | a.address);
        case OpKind::FlagBit: return W(0x80 | a.bit, a.address);
        case OpKind::InputByte: return W(0x4A, a.address);
        case OpKind::OutputByte: return W(0x4A, 0x80 | a.address);
        case OpKind::FlagByte: return W(0x0A, a.address);
        case OpKind::DataLeft: return W(0x22, checkedByte(a.address, "DL"));
        case OpKind::DataRight: return W(0x2A, checkedByte(a.address, "DR"));
        case OpKind::InputWord: return W(0x52, a.address);
        case OpKind::OutputWord: return W(0x52, 0x80 | a.address);
        case OpKind::FlagWord: return W(0x12, a.address);
        case OpKind::DataWord: return W(0x32, checkedByte(a.address, "DW"));
        case OpKind::Constant: return a.value;
        case OpKind::Timer: return W(0xF8, a.address);
        case OpKind::Counter: return W(0xB8, a.address);
        case OpKind::Block:
            switch (a.blockType) {
            case BlockType::DB: return W(0x20, a.address);
            case BlockType::PB: return W(0x75, a.address);
            case BlockType::FB: return W(0x3D, a.address);
            case BlockType::SB: return W(0x7D, a.address);
            default: break;
            }
            break;
        default: break;
        }
        fail("Parameter '" + d.name + "': Aktualoperand " + toString(a) + " ist im MC5-Code nicht darstellbar");
    }

    const Program& program_;
    const Block& block_;
    CpuFamily family_;
};

Mc5Block assembleBlock(const Program& program, const Block& block, CpuFamily family, AssembleResult& result)
{
    Mc5Block mb;
    mb.type = block.type;
    mb.number = block.number;
    mb.words.assign(kHeaderWords, 0);
    mb.lines.assign(kHeaderWords, block.line);

    auto error = [&](int line, const std::string& m) { result.errors.push_back({line, m}); };

    if (block.type == BlockType::DB) {
        for (uint16_t v : block.data) {
            mb.words.push_back(v);
            mb.lines.push_back(block.line);
        }
    } else {
        if (block.type == BlockType::FB) {
            const int n = static_cast<int>(block.params.size());
            mb.words.push_back(W(0x2D, 5 + 3 * n));
            std::string name = block.fbName;
            name.resize(8, ' ');
            for (int i = 0; i < 8; i += 2) mb.words.push_back(W(name[i], name[i + 1]));
            for (const ParamDecl& d : block.params) {
                try {
                    mb.words.push_back(W(paramKindCode(d), paramFormatCode(d)));
                } catch (const AsmFail& f) {
                    error(d.line, f.message);
                    mb.words.push_back(0);
                }
                std::string pn = d.name;
                pn.resize(4, ' ');
                mb.words.push_back(W(pn[0], pn[1]));
                mb.words.push_back(W(pn[2], pn[3]));
            }
            mb.lines.resize(mb.words.size(), block.line);
        }

        BlockAssembler as(program, block, family);
        const int start = static_cast<int>(mb.words.size());

        // Pass 1: Adressen
        std::vector<int> addr(block.code.size() + 1, start);
        int a = start;
        for (size_t i = 0; i < block.code.size(); ++i) {
            addr[i] = a;
            try {
                a += static_cast<int>(as.encode(block.code[i], a, nullptr).size());
            } catch (const AsmFail&) {
                a += 1;
            }
        }
        addr[block.code.size()] = a;
        std::map<std::string, int> labelAddr;
        for (const auto& [name, idx] : block.labels) labelAddr[name] = addr[idx];

        // Pass 2: Code
        for (size_t i = 0; i < block.code.size(); ++i) {
            const Instruction& ins = block.code[i];
            std::vector<uint16_t> w;
            try {
                w = as.encode(ins, addr[i], &labelAddr);
            } catch (const AsmFail& f) {
                error(ins.line, f.message);
                w.assign(1, 0x0000);
            }
            for (uint16_t x : w) {
                mb.words.push_back(x);
                mb.lines.push_back(ins.line);
            }
        }
        const bool endsWithBe = !block.code.empty() && block.code.back().op == Op::BE;
        if (!endsWithBe) {
            mb.words.push_back(0x6500);
            mb.lines.push_back(block.code.empty() ? block.line : block.code.back().line);
            result.warnings.push_back({block.line, block.name() + ": BE am Bausteinende ergänzt"});
        }
    }

    mb.words[0] = 0x7070;
    mb.words[1] = W(blockTypeCode(block.type), block.number);
    mb.words[2] = 0;    // PG-Kennung / Bibliotheksnummer: nicht belegt
    mb.words[3] = 0;
    if (mb.words.size() > 0xFFFF) error(block.line, block.name() + " ist zu lang");
    mb.words[4] = static_cast<uint16_t>(mb.words.size());
    return mb;
}

// ------------------------------------------------------------------ Disassembler

std::string hex4(uint16_t w)
{
    char b[8];
    std::snprintf(b, sizeof b, "%04X", w);
    return b;
}

std::string bitText(const char* area, int byte, int bit)
{
    return std::string(area) + " " + std::to_string(byte) + "." + std::to_string(bit);
}

std::string constText(const std::string& fmt, uint16_t v)
{
    char b[48];
    if (fmt == "KF") {
        std::snprintf(b, sizeof b, "KF %+d", static_cast<int16_t>(v));
    } else if (fmt == "KM") {
        std::string s = "KM ";
        for (int i = 15; i >= 0; --i) s += ((v >> i) & 1) ? '1' : '0';
        return s;
    } else if (fmt == "KY") {
        std::snprintf(b, sizeof b, "KY %u,%u", v >> 8, v & 0xFF);
    } else if (fmt == "KT" && isValidBcd(v & 0x0FFF, 3) && !(v & 0xC000)) {
        std::snprintf(b, sizeof b, "KT %03d.%d", fromBcd(v & 0x0FFF), (v >> 12) & 3);
    } else if (fmt == "KZ" && isValidBcd(v, 3) && v <= 0x0999) {
        std::snprintf(b, sizeof b, "KZ %03d", fromBcd(v));
    } else if (fmt == "KC" && (v >> 8) >= 0x20 && (v >> 8) < 0x7F && (v & 0xFF) >= 0x20 && (v & 0xFF) < 0x7F
               && (v >> 8) != '\'' && (v & 0xFF) != '\'' && (v >> 8) != ';' && (v & 0xFF) != ';') {
        std::snprintf(b, sizeof b, "KC '%c%c'", v >> 8, v & 0xFF);
    } else if (fmt == "KB") {
        std::snprintf(b, sizeof b, "KB %u", v);
    } else {
        std::snprintf(b, sizeof b, "KH %04X", v);
    }
    return b;
}

std::string subcodeFormat(uint8_t sub)
{
    switch (sub) {
    case 0x01: return "KZ";
    case 0x02: return "KT";
    case 0x04: return "KF";
    case 0x10: return "KC";
    case 0x20: return "KY";
    case 0x40: return "KH";
    case 0x80: return "KM";
    default: return {};
    }
}

struct FbInfo {
    std::string name;
    std::vector<ParamDecl> params;
    bool valid = false;
};

// Parameterblock am Anfang eines FB lesen
FbInfo readFbHeader(const Mc5Block& b, std::vector<std::string>& warnings, int& codeStart)
{
    FbInfo info;
    codeStart = kHeaderWords;
    const auto& w = b.words;
    if (w.size() < kHeaderWords + 5 || (w[5] >> 8) != 0x2D) {
        warnings.push_back("FB " + std::to_string(b.number) + ": Parameterblock fehlt");
        return info;
    }
    const int jump = w[5] & 0xFF;
    if (jump < 5 || (jump - 5) % 3 != 0 || kHeaderWords + jump > static_cast<int>(w.size())) {
        warnings.push_back("FB " + std::to_string(b.number) + ": Parameterblock unlesbar");
        return info;
    }
    for (int i = 6; i < 10; ++i) {
        info.name += static_cast<char>(w[i] >> 8);
        info.name += static_cast<char>(w[i] & 0xFF);
    }
    while (!info.name.empty() && (info.name.back() == ' ' || info.name.back() == '\0')) info.name.pop_back();
    const int n = (jump - 5) / 3;
    for (int p = 0; p < n; ++p) {
        const uint16_t kf = w[10 + 3 * p];
        ParamDecl d;
        for (int k = 1; k <= 2; ++k) {
            d.name += static_cast<char>(w[10 + 3 * p + k] >> 8);
            d.name += static_cast<char>(w[10 + 3 * p + k] & 0xFF);
        }
        while (!d.name.empty() && (d.name.back() == ' ' || d.name.back() == '\0')) d.name.pop_back();
        const uint8_t kind = kf >> 8, fmt = kf & 0xFF;
        switch (kind) {
        case 2: d.kind = ParamKind::E; break;
        case 4: d.kind = ParamKind::D; break;
        case 5: d.kind = ParamKind::T; break;
        case 7: d.kind = ParamKind::B; break;
        case 8: d.kind = ParamKind::A; break;
        default:
            warnings.push_back("FB " + std::to_string(b.number) + ", Parameter " + d.name + ": unbekannte Art "
                               + std::to_string(kind) + " (Z?)");
            d.kind = ParamKind::Z;
            break;
        }
        if (d.kind == ParamKind::E || d.kind == ParamKind::A)
            d.type = fmt == 0x80 ? "BI" : fmt == 0x40 ? "BY" : fmt == 0x20 ? "W" : "?";
        else if (d.kind == ParamKind::D)
            d.type = subcodeFormat(fmt);
        info.params.push_back(d);
    }
    info.valid = true;
    codeStart = kHeaderWords + jump;
    return info;
}

std::string formatInstr(const std::string& mnemonic, const std::string& operand = {})
{
    std::string s = ":" + mnemonic;
    if (!operand.empty()) {
        s.resize(std::max<size_t>(s.size() + 1, 5), ' ');
        s += operand;
    }
    return s;
}

class BlockDisassembler {
public:
    BlockDisassembler(const Mc5Block& b, const std::map<int, FbInfo>& fbs, std::vector<std::string>& warnings)
        : b_(b), w_(b.words), fbs_(fbs), warnings_(warnings)
    {
    }

    std::string run()
    {
        std::ostringstream out;
        out << blockTypeName(b_.type) << " " << b_.number << "\n";
        int pos = kHeaderWords;
        if (b_.type == BlockType::FB) {
            FbInfo info = readFbHeader(b_, warnings_, pos);
            self_ = info;
            if (!info.name.empty()) out << "NAME :" << info.name << "\n";
            for (const ParamDecl& d : info.params) {
                std::string n = d.name;
                n.resize(4, ' ');
                out << "BEZ  :" << n << " E/A/D/B/T/Z: " << d.describe().substr(0, 1);
                if (d.kind == ParamKind::E || d.kind == ParamKind::A) out << "  BI/BY/W/K: " << d.type;
                if (d.kind == ParamKind::D) out << "  KM/KH/KY/KC/KF/KT/KZ/KG: " << d.type;
                out << "\n";
            }
        }

        // Pass 1: Anweisungen zerlegen, Sprungziele sammeln
        struct Line {
            int addr;
            std::vector<std::string> text;  // erste Zeile = Anweisung, weitere = Parameterzeilen
            int target = -1;
            std::string mnemonic;
        };
        std::vector<Line> lines;
        while (pos < static_cast<int>(w_.size())) {
            Line l;
            l.addr = pos;
            int len = decode(pos, l.text, l.mnemonic, l.target);
            pos += len;
            lines.push_back(l);
        }

        std::map<int, std::string> labels;
        std::set<int> starts;
        for (const Line& l : lines) starts.insert(l.addr);
        starts.insert(static_cast<int>(w_.size()));
        for (const Line& l : lines) {
            if (l.target < 0) continue;
            if (!starts.count(l.target)) {
                warnings_.push_back(blockName() + ": Sprungziel " + std::to_string(l.target)
                                    + " liegt nicht auf einem Anweisungsanfang");
                continue;
            }
            labels[l.target];
        }
        int n = 1;
        for (auto& [addr, name] : labels) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "M%03d", n++);
            name = buf;
        }

        // Pass 2: Text
        for (Line& l : lines) {
            if (l.target >= 0 && labels.count(l.target)) {
                std::string& t = l.text.front();
                t.resize(std::max<size_t>(t.size() + 1, 5), ' ');
                t += "=" + labels[l.target];
            } else if (l.target >= 0) {
                l.text.front() = "; " + l.text.front() + " ? (Sprungziel ungültig)";
            }
            std::string prefix;
            if (auto it = labels.find(l.addr); it != labels.end()) prefix = it->second;
            for (size_t i = 0; i < l.text.size(); ++i) {
                if (i == 0 && l.text[0] == "***") {
                    if (!prefix.empty()) out << prefix << ":\n";
                    out << "***\n";
                    continue;
                }
                out << (i == 0 ? prefix : std::string()) << l.text[i] << "\n";
            }
        }
        if (auto it = labels.find(static_cast<int>(w_.size())); it != labels.end()) out << it->second << ":\n";
        return out.str();
    }

    std::string runDb()
    {
        std::ostringstream out;
        out << "DB " << b_.number << "\n";
        for (size_t i = kHeaderWords; i < w_.size(); ++i)
            out << "   " << (i - kHeaderWords) << ": " << constText("KH", w_[i]) << "\n";
        return out.str();
    }

private:
    std::string blockName() const { return std::string(blockTypeName(b_.type)) + " " + std::to_string(b_.number); }

    uint16_t word(int pos) const { return pos < static_cast<int>(w_.size()) ? w_[pos] : 0; }

    std::string formal(int c) const
    {
        if (c >= 1 && c <= static_cast<int>(self_.params.size())) return "=" + self_.params[c - 1].name;
        warnings_.push_back(blockName() + ": Formaloperand Nr. " + std::to_string(c) + " unbekannt");
        return "=P" + std::to_string(c);
    }

    const ParamDecl* formalDecl(int c) const
    {
        if (c >= 1 && c <= static_cast<int>(self_.params.size())) return &self_.params[c - 1];
        return nullptr;
    }

    int unsupported(std::vector<std::string>& text, const std::string& what, int len)
    {
        std::string codes;
        for (int i = 0; i < len; ++i) codes += (i ? " " : "") + hex4(word(pos_ + i));
        text.push_back("; nicht unterstützt: " + what + " (" + codes + ")");
        warnings_.push_back(blockName() + ": " + what + " wird nicht unterstützt");
        return len;
    }

    // Zerlegt die Anweisung an pos; liefert die Länge in Wörtern
    int decode(int pos, std::vector<std::string>& text, std::string& mn, int& target)
    {
        pos_ = pos;
        const uint16_t w = word(pos);
        const int hi = w >> 8, lo = w & 0xFF;
        auto put = [&](const std::string& m, const std::string& op = {}) {
            mn = m;
            text.push_back(formatInstr(m, op));
            return 1;
        };
        auto jump = [&](const std::string& m) {
            mn = m;
            target = pos + static_cast<int8_t>(lo);
            text.push_back(":" + m);
            return 1;
        };
        auto num = [&](const char* area, int n) { return std::string(area) + " " + std::to_string(n); };

        // Bitverknüpfungen E/A/M, T, Z
        static const char* bitOps[] = {"U", "O", "S", "=", "UN", "ON", "R"};
        if (hi >= 0x80) {
            switch (w) {
            case 0xFB00: return put("O");
            case 0xBA00: return put("U(");
            case 0xBB00: return put("O(");
            case 0xBF00: return put(")");
            case 0xFFFF: return put("NOP", "1");
            default: break;
            }
            switch (hi) {
            case 0xF8: return put("U", num("T", lo));
            case 0xF9: return put("O", num("T", lo));
            case 0xFC: return put("UN", num("T", lo));
            case 0xFD: return put("ON", num("T", lo));
            case 0xB8: return put("U", num("Z", lo));
            case 0xB9: return put("O", num("Z", lo));
            case 0xBC: return put("UN", num("Z", lo));
            case 0xBD: return put("ON", num("Z", lo));
            case 0xFA: return jump("SPB");
            default: break;
            }
            const int idx = ((hi & 0x38) >> 3);
            if (hi < 0xC0 && idx < 7) return put(bitOps[idx], bitText("M", lo, hi & 7));
            if (hi >= 0xC0 && ((hi - 0xC0) >> 3) < 7)
                return put(bitOps[(hi - 0xC0) >> 3], bitText(lo & 0x80 ? "A" : "E", lo & 0x7F, hi & 7));
            return unsupported(text, "Code " + hex4(w), 1);
        }

        switch (hi) {
        case 0x00:
            if (lo == 0) return put("NOP", "0");
            break;
        case 0x01: if (lo == 0) return put("KEW"); break;
        case 0x02: return put("L", num("T", lo));
        case 0x05: if (lo == 0) return put("BEB"); break;
        case 0x07: return put("U", formal(lo));
        case 0x09: if (lo == 0) return put("KZW"); break;
        case 0x0A: return put("L", num("MB", lo));
        case 0x0B: return put("T", num("MB", lo));
        case 0x0C: return put("LC", num("T", lo));
        case 0x0D: return jump("SPO");
        case 0x0E: return put("LC", formal(lo));
        case 0x0F: return put("O", formal(lo));
        case 0x10: return lo == 0xFF ? (text.push_back("***"), mn = "BLD", 1) : put("BLD", std::to_string(lo));
        case 0x11: return put("I", std::to_string(lo));
        case 0x12: return put("L", num("MW", lo));
        case 0x13: return put("T", num("MW", lo));
        case 0x14: return put("SA", num("T", lo));
        case 0x15: return jump("SPP");
        case 0x16: {
            const ParamDecl* d = formalDecl(lo);
            return put(d && d->kind == ParamKind::Z ? "ZR" : "SA", formal(lo));
        }
        case 0x17: return put("S", formal(lo));
        case 0x19: return put("D", std::to_string(lo));
        case 0x1C: return put("SV", num("T", lo));
        case 0x1D: return call("SPB", lo, text, mn);
        case 0x1E: {
            const ParamDecl* d = formalDecl(lo);
            return put(d && d->kind == ParamKind::Z ? "S" : "SV", formal(lo));
        }
        case 0x1F: return put("=", formal(lo));
        case 0x20: return put("A", num("DB", lo));
        case 0x21:
            switch (lo) {
            case 0x20: return put(">F");
            case 0x40: return put("<F");
            case 0x60: return put("><F");
            case 0x80: return put("!=F");
            case 0xA0: return put(">=F");
            case 0xC0: return put("<=F");
            default: break;
            }
            break;
        case 0x22: return put("L", num("DL", lo));
        case 0x23: return put("T", num("DL", lo));
        case 0x24: return put("SE", num("T", lo));
        case 0x25: return jump("SPM");
        case 0x26: return put("SE", formal(lo));
        case 0x27: return put("UN", formal(lo));
        case 0x28: return put("L", constText("KB", lo));
        case 0x2A: return put("L", num("DR", lo));
        case 0x2B: return put("T", num("DR", lo));
        case 0x2C: return put("SS", num("T", lo));
        case 0x2D: return jump("SPA");
        case 0x2E: {
            const ParamDecl* d = formalDecl(lo);
            return put(d && d->kind == ParamKind::Z ? "ZV" : "SS", formal(lo));
        }
        case 0x2F: return put("ON", formal(lo));
        case 0x30: {
            const std::string fmt = subcodeFormat(static_cast<uint8_t>(lo));
            if (fmt.empty()) break;
            put("L", constText(fmt, word(pos + 1)));
            return 2;
        }
        case 0x32: return put("L", num("DW", lo));
        case 0x33: return put("T", num("DW", lo));
        case 0x34: return put("SI", num("T", lo));
        case 0x35: return jump("SPN");
        case 0x36: return put("SI", formal(lo));
        case 0x37: return put("R", formal(lo));
        case 0x3C: return put("R", num("T", lo));
        case 0x3D: return call("SPA", lo, text, mn);
        case 0x3E: return put("R", formal(lo));
        case 0x41: if (lo == 0) return put("UW"); break;
        case 0x42: return put("L", num("Z", lo));
        case 0x45: return jump("SPZ");
        case 0x46: return put("L", formal(lo));
        case 0x49: if (lo == 0) return put("OW"); break;
        case 0x4A: return put("L", num(lo & 0x80 ? "AB" : "EB", lo & 0x7F));
        case 0x4B: return put("T", num(lo & 0x80 ? "AB" : "EB", lo & 0x7F));
        case 0x4C: return put("LC", num("Z", lo));
        case 0x4D: return put("SPB", num("OB", lo));
        case 0x51: if (lo == 0) return put("XOW"); break;
        case 0x52: return put("L", num(lo & 0x80 ? "AW" : "EW", lo & 0x7F));
        case 0x53: return put("T", num(lo & 0x80 ? "AW" : "EW", lo & 0x7F));
        case 0x54: return put("ZR", num("Z", lo));
        case 0x55: return put("SPB", num("PB", lo));
        case 0x59: if (lo == 0) return put("-F"); break;
        case 0x5C: return put("S", num("Z", lo));
        case 0x5D: return put("SPB", num("SB", lo));
        case 0x60:
            if (lo == 0x00) return put(":F");
            if (lo == 0x04) return put("XF");
            break;
        case 0x61: if (lo <= 15) return put("SLW", std::to_string(lo)); break;
        case 0x65:
            if (lo == 0) return put("BE");
            if (lo == 1) return put("BEA");
            break;
        case 0x66: return put("T", formal(lo));
        case 0x69: if (lo <= 15) return put("SRW", std::to_string(lo)); break;
        case 0x6C: return put("ZV", num("Z", lo));
        case 0x6D: return put("SPA", num("OB", lo));
        case 0x70: return decode70(lo, text, mn);
        case 0x75: return put("SPA", num("PB", lo));
        case 0x76: return put("B", formal(lo));
        case 0x78:
            if (lo == 0x3F) {
                const uint16_t w2 = word(pos + 1);
                static const char* ops[] = {"U", "O", "UN", "ON", "S", "R", "="};
                const int o = w2 >> 12;
                if (o < 7) {
                    put(ops[o], bitText("D", w2 & 0xFF, (w2 >> 8) & 0x0F));
                    return 2;
                }
            }
            return unsupported(text, "Code " + hex4(w), 2);
        case 0x79: if (lo == 0) return put("+F"); break;
        case 0x7C: return put("R", num("Z", lo));
        case 0x7D: return put("SPA", num("SB", lo));
        // bekannte, im Simulator nicht unterstützte Operationen
        case 0x04: return unsupported(text, "FR T", 1);
        case 0x44: return unsupported(text, "FR Z", 1);
        case 0x06: return unsupported(text, "FR =", 1);
        case 0x03: return unsupported(text, "TNB", 1);
        case 0x3F: return unsupported(text, "LW =", 1);
        case 0x4E: return unsupported(text, "B MW", 1);
        case 0x6E: return unsupported(text, "B DW", 1);
        case 0x50: return unsupported(text, "ADD BF", 1);
        case 0x58: return unsupported(text, "ADD KF", 2);
        case 0x62: return unsupported(text, "L BS", 1);
        case 0x63: return unsupported(text, "T BS", 1);
        case 0x72: return unsupported(text, "L PB", 1);
        case 0x73: return unsupported(text, "T PB", 1);
        case 0x7A: return unsupported(text, "L PW", 1);
        case 0x7B: return unsupported(text, "T PW", 1);
        case 0x7E: return unsupported(text, "BI", 1);
        case 0x38: return unsupported(text, "L KG/DH", 3);
        default: break;
        }
        return unsupported(text, "Code " + hex4(w), 1);
    }

    int decode70(int lo, std::vector<std::string>& text, std::string& mn)
    {
        auto put = [&](const std::string& m, const std::string& op = {}) {
            mn = m;
            text.push_back(formatInstr(m, op));
        };
        switch (lo) {
        case 0x02: put("TAK"); return 1;
        case 0x03: put("STP"); return 1;
        case 0x00: return unsupported(text, "STS", 1);
        case 0x0B: return unsupported(text, "SPR", 2);
        default: break;
        }
        const uint16_t w2 = word(pos_ + 1);
        const int q = w2 >> 12, bit = (w2 >> 8) & 0x0F, n = w2 & 0xFF;
        const char* op = q == 0xC ? "P" : q == 0x8 ? "PN" : q == 0x4 ? "SU" : q == 0x0 ? "RU" : nullptr;
        if (!op) return unsupported(text, "Code " + hex4(word(pos_)), 2);
        switch (lo) {
        case 0x46: put(op, bitText("D", n, bit)); return 2;
        case 0x38: put(op, bitText(n & 0x80 ? "A" : "E", n & 0x7F, bit)); return 2;
        case 0x49: put(op, bitText("M", n, bit)); return 2;
        case 0x15: return unsupported(text, std::string(op) + " Z", 2);
        case 0x25: return unsupported(text, std::string(op) + " T", 2);
        case 0x57: return unsupported(text, std::string(op) + " BS", 2);
        default: return unsupported(text, "Code " + hex4(word(pos_)), 2);
        }
    }

    // Bausteinaufruf; bei FB folgt "SPA= k" und k-1 Aktualoperanden
    int call(const std::string& m, int nr, std::vector<std::string>& text, std::string& mn)
    {
        mn = m;
        text.push_back(formatInstr(m, "FB " + std::to_string(nr)));
        const uint16_t next = word(pos_ + 1);
        if ((next >> 8) != 0x2D) return 1;
        const int k = next & 0xFF;
        if (k < 1) return 1;
        auto it = fbs_.find(nr);
        if (it == fbs_.end() || !it->second.valid) {
            warnings_.push_back(blockName() + ": Aufruf FB " + std::to_string(nr)
                                + " – Baustein fehlt, Parameter können nicht zugeordnet werden");
            for (int i = 1; i < k; ++i) text.push_back("; Parameter " + std::to_string(i) + ": KH " + hex4(word(pos_ + 1 + i)));
            return 1 + k;
        }
        const FbInfo& fb = it->second;
        if (static_cast<int>(fb.params.size()) != k - 1) return 1;  // kein Parameterblock, sondern ein Sprung
        if (!fb.name.empty()) text.push_back("NAME :" + fb.name);
        for (int i = 1; i < k; ++i) {
            const uint16_t a = word(pos_ + 1 + i);
            const ParamDecl* d = i - 1 < static_cast<int>(fb.params.size()) ? &fb.params[i - 1] : nullptr;
            std::string name = d ? d->name : "P" + std::to_string(i);
            name.resize(std::max<size_t>(name.size(), 4), ' ');
            text.push_back(name + " :" + actualText(d, a));
        }
        return 1 + k;
    }

    std::string actualText(const ParamDecl* d, uint16_t a)
    {
        const int hi = a >> 8, lo = a & 0xFF;
        auto num = [](const char* area, int n) { return std::string(area) + " " + std::to_string(n); };
        if (d) {
            switch (d->kind) {
            case ParamKind::E:
            case ParamKind::A:
                if (d->type == "BI") {
                    if ((hi & 0xF8) == 0xC0) return bitText(lo & 0x80 ? "A" : "E", lo & 0x7F, hi & 7);
                    if ((hi & 0xF8) == 0x80) return bitText("M", lo, hi & 7);
                } else {
                    switch (hi) {
                    case 0x4A: return num(lo & 0x80 ? "AB" : "EB", lo & 0x7F);
                    case 0x0A: return num("MB", lo);
                    case 0x22: return num("DL", lo);
                    case 0x2A: return num("DR", lo);
                    case 0x52: return num(lo & 0x80 ? "AW" : "EW", lo & 0x7F);
                    case 0x12: return num("MW", lo);
                    case 0x32: return num("DW", lo);
                    default: break;
                    }
                }
                break;
            case ParamKind::D: return constText(d->type, a);
            case ParamKind::B:
                switch (hi) {
                case 0x20: return num("DB", lo);
                case 0x75: return num("PB", lo);
                case 0x3D: return num("FB", lo);
                case 0x7D: return num("SB", lo);
                default: break;
                }
                break;
            case ParamKind::T: return num("T", lo);
            case ParamKind::Z: return num("Z", lo);
            }
        }
        warnings_.push_back(blockName() + ": Aktualoperand " + hex4(a) + " nicht zuzuordnen");
        return "KH " + hex4(a);
    }

    const Mc5Block& b_;
    const std::vector<uint16_t>& w_;
    const std::map<int, FbInfo>& fbs_;
    std::vector<std::string>& warnings_;
    FbInfo self_;
    int pos_ = 0;
};

}  // namespace

// ------------------------------------------------------------------ öffentliche Funktionen

const char* familyName(CpuFamily f)
{
    switch (f) {
    case CpuFamily::S5_90_95U: return "S5-90U/95U";
    case CpuFamily::S5_100U: return "S5-100U";
    case CpuFamily::S5_115U: return "S5-115U";
    case CpuFamily::S5_135U: return "S5-135U/155U";
    }
    return "?";
}

uint8_t blockTypeCode(BlockType t)
{
    switch (t) {
    case BlockType::DB: return 0x01;
    case BlockType::SB: return 0x02;
    case BlockType::PB: return 0x04;
    case BlockType::FB: return 0x08;
    case BlockType::OB: return 0x10;
    }
    return 0;
}

bool blockTypeFromCode(uint8_t code, BlockType& t)
{
    switch (code & 0x3F) {   // Bits 15..14 = Gültigkeitskennung
    case 0x01: t = BlockType::DB; return true;
    case 0x02: t = BlockType::SB; return true;
    case 0x04: t = BlockType::PB; return true;
    case 0x08: t = BlockType::FB; return true;
    case 0x10: t = BlockType::OB; return true;
    default: return false;
    }
}

AssembleResult assemble(const Program& program, CpuFamily family)
{
    AssembleResult result;
    for (const auto& [key, block] : program.blocks) {
        if (block.type == BlockType::FB) {
            for (const ParamDecl& d : block.params) {
                if (d.name.size() > 4) result.errors.push_back({d.line, "Parametername '" + d.name + "' länger als 4 Zeichen"});
            }
        }
        result.blocks.push_back(assembleBlock(program, block, family, result));
    }
    std::sort(result.errors.begin(), result.errors.end(),
              [](const Diagnostic& a, const Diagnostic& b) { return a.line < b.line; });
    return result;
}

DisassembleResult disassemble(const std::vector<Mc5Block>& blocks)
{
    DisassembleResult r;
    std::map<int, FbInfo> fbs;
    for (const Mc5Block& b : blocks) {
        if (b.type != BlockType::FB) continue;
        int start = 0;
        std::vector<std::string> ignore;
        fbs[b.number] = readFbHeader(b, ignore, start);
    }
    std::ostringstream out;
    out << "; Aus MC5-Code rückübersetzt\n";
    for (const Mc5Block& b : blocks) {
        if (b.words.size() < kHeaderWords) continue;
        if (b.words[4] != b.words.size())
            r.warnings.push_back(std::string(blockTypeName(b.type)) + " " + std::to_string(b.number)
                                 + ": Längenangabe im Kopf passt nicht");
        BlockDisassembler d(b, fbs, r.warnings);
        out << "\n" << (b.type == BlockType::DB ? d.runDb() : d.run());
    }
    r.source = out.str();
    return r;
}

std::vector<uint8_t> toBytes(const std::vector<Mc5Block>& blocks)
{
    std::vector<uint8_t> bytes;
    for (const Mc5Block& b : blocks) {
        for (uint16_t w : b.words) {
            bytes.push_back(static_cast<uint8_t>(w >> 8));
            bytes.push_back(static_cast<uint8_t>(w & 0xFF));
        }
    }
    return bytes;
}

std::vector<Mc5Block> fromBytes(const std::vector<uint8_t>& bytes, std::vector<std::string>& warnings)
{
    std::vector<Mc5Block> blocks;
    size_t pos = 0;
    auto wordAt = [&](size_t p) { return static_cast<uint16_t>((bytes[p] << 8) | bytes[p + 1]); };
    while (pos + 2 * kHeaderWords <= bytes.size()) {
        if (wordAt(pos) != 0x7070) {   // Füllbytes überspringen
            pos += 2;
            continue;
        }
        const uint16_t typeNr = wordAt(pos + 2);
        const uint16_t len = wordAt(pos + 8);
        Mc5Block b;
        if (len < kHeaderWords || pos + 2 * static_cast<size_t>(len) > bytes.size()) {
            warnings.push_back("Baustein an Byte " + std::to_string(pos) + ": ungültige Länge");
            pos += 2;
            continue;
        }
        if (!blockTypeFromCode(static_cast<uint8_t>(typeNr >> 8), b.type)) {
            warnings.push_back("Baustein an Byte " + std::to_string(pos) + ": Typ " + hex4(typeNr)
                               + " wird nicht unterstützt (übersprungen)");
            pos += 2 * static_cast<size_t>(len);
            continue;
        }
        b.number = typeNr & 0xFF;
        for (size_t i = 0; i < len; ++i) b.words.push_back(wordAt(pos + 2 * i));
        b.lines.assign(len, 0);
        blocks.push_back(std::move(b));
        pos += 2 * static_cast<size_t>(len);
    }
    if (blocks.empty()) warnings.push_back("Keine S5-Bausteine (Kennung 7070) gefunden");
    return blocks;
}

}  // namespace s5::mc5
