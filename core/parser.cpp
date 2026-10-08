#include "parser.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace s5 {

bool isValidBcd(uint16_t bcd, int digits)
{
    for (int i = 0; i < digits; ++i) {
        if (((bcd >> (4 * i)) & 0xF) > 9)
            return false;
    }
    return true;
}

uint16_t toBcd(int value)
{
    uint16_t r = 0;
    for (int i = 0; i < 4; ++i) {
        r |= static_cast<uint16_t>((value % 10) << (4 * i));
        value /= 10;
    }
    return r;
}

int fromBcd(uint16_t bcd)
{
    return ((bcd >> 12) & 0xF) * 1000 + ((bcd >> 8) & 0xF) * 100 + ((bcd >> 4) & 0xF) * 10
         + (bcd & 0xF);
}

namespace {

struct ParseFail {
    std::string message;
};

[[noreturn]] void fail(const std::string& msg) { throw ParseFail{msg}; }

std::string trim(const std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string upper(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string removeSpaces(const std::string& s)
{
    std::string r;
    for (char c : s)
        if (!std::isspace(static_cast<unsigned char>(c))) r += c;
    return r;
}

bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Entfernt Kommentare (';' oder '//'), aber nicht innerhalb von '...'
std::string stripComment(const std::string& line)
{
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '\'') quoted = !quoted;
        if (quoted) continue;
        if (c == ';') return line.substr(0, i);
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') return line.substr(0, i);
    }
    return line;
}

int parseNumber(const std::string& text, int min, int max, const std::string& what)
{
    std::string s = removeSpaces(text);
    if (s.empty()) fail(what + " fehlt");
    for (char c : s)
        if (!std::isdigit(static_cast<unsigned char>(c))) fail("Ungültige Zahl '" + s + "' für " + what);
    if (s.size() > 6) fail(what + " außerhalb des Bereichs " + std::to_string(min) + ".." + std::to_string(max));
    int v = std::stoi(s);
    if (v < min || v > max)
        fail(what + " " + std::to_string(v) + " außerhalb des Bereichs " + std::to_string(min) + ".."
             + std::to_string(max));
    return v;
}

uint16_t parseConstant(const std::string& fmt, const std::string& raw)
{
    std::string text = trim(raw);
    if (!text.empty() && text[0] == '=') text = trim(text.substr(1));   // "KH = 00FF"
    std::string s = removeSpaces(upper(text));

    if (fmt == "KF") {
        if (s.empty()) fail("Wert für KF fehlt");
        bool neg = false;
        size_t i = 0;
        if (s[0] == '+' || s[0] == '-') {
            neg = s[0] == '-';
            i = 1;
        }
        std::string digits = s.substr(i);
        if (digits.empty() || digits.size() > 5
            || !std::all_of(digits.begin(), digits.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
            fail("Ungültige Festpunktzahl KF " + text);
        int v = std::stoi(digits) * (neg ? -1 : 1);
        if (v < -32768 || v > 32767) fail("KF " + text + " außerhalb -32768..+32767");
        return static_cast<uint16_t>(static_cast<int16_t>(v));
    }
    if (fmt == "KH") {
        if (s.empty() || s.size() > 4
            || !std::all_of(s.begin(), s.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); }))
            fail("Ungültige Hexzahl KH " + text + " (0000..FFFF)");
        return static_cast<uint16_t>(std::stoul(s, nullptr, 16));
    }
    if (fmt == "KM") {
        if (s.empty() || s.size() > 16
            || !std::all_of(s.begin(), s.end(), [](char c) { return c == '0' || c == '1'; }))
            fail("Ungültiges Bitmuster KM " + text + " (max. 16 Stellen 0/1)");
        return static_cast<uint16_t>(std::stoul(s, nullptr, 2));
    }
    if (fmt == "KY") {
        auto comma = s.find(',');
        if (comma == std::string::npos) fail("KY erwartet zwei Bytes, z.B. KY 10,20");
        int hi = parseNumber(s.substr(0, comma), 0, 255, "KY-Byte");
        int lo = parseNumber(s.substr(comma + 1), 0, 255, "KY-Byte");
        return static_cast<uint16_t>((hi << 8) | lo);
    }
    if (fmt == "KB") {
        return static_cast<uint16_t>(parseNumber(s, 0, 255, "KB"));
    }
    if (fmt == "KT") {
        auto dot = s.find('.');
        if (dot == std::string::npos) fail("KT erwartet Wert.Zeitbasis, z.B. KT 100.2");
        int v = parseNumber(s.substr(0, dot), 0, 999, "Zeitwert");
        int b = parseNumber(s.substr(dot + 1), 0, 3, "Zeitbasis");
        return static_cast<uint16_t>((b << 12) | toBcd(v));
    }
    if (fmt == "KZ") {
        return toBcd(parseNumber(s, 0, 999, "Zählwert KZ"));
    }
    if (fmt == "KC") {
        std::string c = text;
        if (c.size() >= 2 && c.front() == '\'' && c.back() == '\'') c = c.substr(1, c.size() - 2);
        if (c.empty() || c.size() > 2) fail("KC erwartet 1 oder 2 Zeichen");
        if (c.size() == 1) c += ' ';
        return static_cast<uint16_t>((static_cast<unsigned char>(c[0]) << 8) | static_cast<unsigned char>(c[1]));
    }
    fail("Unbekanntes Konstantenformat " + fmt);
}

void parseBitAddress(const std::string& rest, int maxByte, int maxBit, Operand& o)
{
    std::string s = removeSpaces(rest);
    auto dot = s.find('.');
    if (dot == std::string::npos) fail("Bitadresse erwartet (Byte.Bit), z.B. 1.0");
    o.address = static_cast<uint16_t>(parseNumber(s.substr(0, dot), 0, maxByte, "Byteadresse"));
    o.bit = static_cast<uint8_t>(parseNumber(s.substr(dot + 1), 0, maxBit, "Bitnummer"));
}

Operand parseOperand(const std::string& raw)
{
    Operand o;
    std::string s = trim(raw);
    if (s.empty()) return o;

    if (s[0] == '=') {
        std::string name = upper(trim(s.substr(1)));
        if (name.empty() || !isIdentStart(name[0])
            || !std::all_of(name.begin(), name.end(), isIdentChar))
            fail("Ungültige Sprungmarke '" + s + "'");
        o.kind = OpKind::Label;
        o.label = name;
        return o;
    }

    std::string u = upper(s);
    size_t i = 0;
    while (i < u.size() && std::isalpha(static_cast<unsigned char>(u[i]))) ++i;
    std::string pre = u.substr(0, i);
    std::string rest = trim(u.substr(i));
    std::string restRaw = trim(s.substr(i));

    if (pre.empty()) {
        o.kind = OpKind::Number;
        o.value = static_cast<uint16_t>(parseNumber(rest, 0, 65535, "Parameter"));
        return o;
    }

    static const std::map<std::string, OpKind> bits = {
        {"E", OpKind::InputBit}, {"A", OpKind::OutputBit}, {"M", OpKind::FlagBit}, {"D", OpKind::DataBit}};
    static const std::map<std::string, OpKind> bytesWords = {
        {"EB", OpKind::InputByte},  {"AB", OpKind::OutputByte}, {"MB", OpKind::FlagByte},
        {"EW", OpKind::InputWord},  {"AW", OpKind::OutputWord}, {"MW", OpKind::FlagWord},
        {"DW", OpKind::DataWord},   {"DL", OpKind::DataLeft},   {"DR", OpKind::DataRight}};
    static const std::map<std::string, BlockType> blocks = {
        {"OB", BlockType::OB}, {"PB", BlockType::PB}, {"FB", BlockType::FB},
        {"SB", BlockType::SB}, {"DB", BlockType::DB}};
    static const std::set<std::string> constants = {"KF", "KH", "KM", "KY", "KT", "KZ", "KC", "KB"};

    if (auto it = bits.find(pre); it != bits.end()) {
        o.kind = it->second;
        if (o.kind == OpKind::DataBit)
            parseBitAddress(rest, 2047, 15, o);
        else
            parseBitAddress(rest, o.kind == OpKind::FlagBit ? 255 : 127, 7, o);
        return o;
    }
    if (auto it = bytesWords.find(pre); it != bytesWords.end()) {
        o.kind = it->second;
        int max = 127;
        switch (o.kind) {
        case OpKind::FlagByte: max = 255; break;
        case OpKind::FlagWord: max = 254; break;
        case OpKind::InputWord:
        case OpKind::OutputWord: max = 126; break;
        case OpKind::DataWord:
        case OpKind::DataLeft:
        case OpKind::DataRight: max = 2047; break;
        default: break;
        }
        o.address = static_cast<uint16_t>(parseNumber(rest, 0, max, "Adresse"));
        return o;
    }
    if (pre == "T" || pre == "Z") {
        o.kind = pre == "T" ? OpKind::Timer : OpKind::Counter;
        o.address = static_cast<uint16_t>(parseNumber(rest, 0, 127, pre == "T" ? "Zeitnummer" : "Zählernummer"));
        return o;
    }
    if (auto it = blocks.find(pre); it != blocks.end()) {
        o.kind = OpKind::Block;
        o.blockType = it->second;
        o.address = static_cast<uint16_t>(parseNumber(rest, 0, 255, "Bausteinnummer"));
        return o;
    }
    if (constants.count(pre)) {
        o.kind = OpKind::Constant;
        o.label = pre;
        o.value = parseConstant(pre, restRaw);
        return o;
    }
    fail("Unbekannter Operand '" + s + "'");
}

// Erlaubte Operandenklassen je Operation
enum : unsigned {
    C_NONE = 1u << 0,
    C_BIT = 1u << 1,
    C_TIMER = 1u << 2,
    C_COUNTER = 1u << 3,
    C_BYTEWORD = 1u << 4,
    C_CONST = 1u << 5,
    C_CODEBLOCK = 1u << 6,
    C_DB = 1u << 7,
    C_LABEL = 1u << 8,
    C_NUMBER = 1u << 9,
};

unsigned classOf(const Operand& o)
{
    switch (o.kind) {
    case OpKind::None: return C_NONE;
    case OpKind::InputBit:
    case OpKind::OutputBit:
    case OpKind::FlagBit:
    case OpKind::DataBit: return C_BIT;
    case OpKind::Timer: return C_TIMER;
    case OpKind::Counter: return C_COUNTER;
    case OpKind::Constant: return C_CONST;
    case OpKind::Label: return C_LABEL;
    case OpKind::Param: return 0;   // wird über paramClass() geprüft
    case OpKind::Number: return C_NUMBER;
    case OpKind::Block:
        if (o.blockType == BlockType::DB) return C_DB;
        if (o.blockType == BlockType::OB) return 0;
        return C_CODEBLOCK;
    default: return C_BYTEWORD;
    }
}

struct OpDef {
    Op op;
    unsigned allowed;
};

const std::map<std::string, OpDef>& mnemonics()
{
    static const std::map<std::string, OpDef> table = {
        {"U", {Op::U, C_BIT | C_TIMER | C_COUNTER}},
        {"UN", {Op::UN, C_BIT | C_TIMER | C_COUNTER}},
        {"O", {Op::O, C_BIT | C_TIMER | C_COUNTER | C_NONE}},
        {"ON", {Op::ON, C_BIT | C_TIMER | C_COUNTER}},
        {"=", {Op::Assign, C_BIT}},
        {"S", {Op::S, C_BIT | C_COUNTER}},
        {"R", {Op::R, C_BIT | C_TIMER | C_COUNTER}},
        {"SU", {Op::SU, C_BIT}},
        {"RU", {Op::RU, C_BIT}},
        {"P", {Op::P, C_BIT}},
        {"PN", {Op::PN, C_BIT}},
        {"L", {Op::L, C_BYTEWORD | C_CONST | C_TIMER | C_COUNTER}},
        {"LC", {Op::LC, C_TIMER | C_COUNTER}},
        {"T", {Op::T, C_BYTEWORD}},
        {"SI", {Op::SI, C_TIMER}},
        {"SV", {Op::SV, C_TIMER}},
        {"SE", {Op::SE, C_TIMER}},
        {"SS", {Op::SS, C_TIMER}},
        {"SA", {Op::SA, C_TIMER}},
        {"ZV", {Op::ZV, C_COUNTER}},
        {"ZR", {Op::ZR, C_COUNTER}},
        {"SPA", {Op::SPA, C_CODEBLOCK | C_LABEL}},
        {"SPB", {Op::SPB, C_CODEBLOCK | C_LABEL}},
        {"SPZ", {Op::SPZ, C_LABEL}},
        {"SPN", {Op::SPN, C_LABEL}},
        {"SPP", {Op::SPP, C_LABEL}},
        {"SPM", {Op::SPM, C_LABEL}},
        {"SPO", {Op::SPO, C_LABEL}},
        {"BE", {Op::BE, C_NONE}},
        {"BEA", {Op::BEA, C_NONE}},
        {"BEB", {Op::BEB, C_NONE}},
        {"A", {Op::ADb, C_DB}},
        {"UW", {Op::UW, C_NONE}},
        {"OW", {Op::OW, C_NONE}},
        {"XOW", {Op::XOW, C_NONE}},
        {"KEW", {Op::KEW, C_NONE}},
        {"KZW", {Op::KZW, C_NONE}},
        {"SLW", {Op::SLW, C_NUMBER}},
        {"SRW", {Op::SRW, C_NUMBER}},
        {"I", {Op::Inc, C_NUMBER}},
        {"D", {Op::Dec, C_NUMBER}},
        {"TAK", {Op::TAK, C_NONE}},
        {"NOP", {Op::NOP, C_NUMBER | C_NONE}},
        {"BLD", {Op::BLD, C_NUMBER}},
        {"STP", {Op::STP, C_NONE}},
        {"B", {Op::B, C_DB | C_CODEBLOCK}},
    };
    return table;
}

// Operationen ohne Operand, die auch ohne Leerzeichen geschrieben werden
const std::map<std::string, Op>& symbolOps()
{
    static const std::map<std::string, Op> table = {
        {"U(", Op::UOpen}, {"O(", Op::OOpen}, {")", Op::Close},
        {"+F", Op::AddF}, {"-F", Op::SubF}, {"XF", Op::MulF}, {"*F", Op::MulF},
        {":F", Op::DivF}, {"/F", Op::DivF},
        {"!=F", Op::CmpEq}, {"==F", Op::CmpEq}, {"><F", Op::CmpNe}, {"<>F", Op::CmpNe},
        {">F", Op::CmpGt}, {">=F", Op::CmpGe}, {"<F", Op::CmpLt}, {"<=F", Op::CmpLe},
    };
    return table;
}

// Operandenklasse, die ein Formaloperand innerhalb des FB vertritt
unsigned paramClass(const ParamDecl& d)
{
    switch (d.kind) {
    case ParamKind::E:
    case ParamKind::A: return d.type == "BI" ? C_BIT : C_BYTEWORD;
    case ParamKind::D: return C_CONST;
    case ParamKind::B: return C_DB | C_CODEBLOCK;
    case ParamKind::T: return C_TIMER;
    case ParamKind::Z: return C_COUNTER;
    }
    return 0;
}

// Prüft, ob ein Aktualoperand zum deklarierten Parameter passt
bool actualMatches(const ParamDecl& d, const Operand& a, std::string& why)
{
    switch (d.kind) {
    case ParamKind::E:
    case ParamKind::A:
        if (d.type == "BI") {
            why = "Bitoperand erwartet (z.B. E 0.0, M 1.0)";
            return a.isBit();
        }
        if (d.type == "BY") {
            why = "Byteoperand erwartet (EB, AB, MB, DL, DR)";
            return a.kind == OpKind::InputByte || a.kind == OpKind::OutputByte || a.kind == OpKind::FlagByte
                || a.kind == OpKind::DataLeft || a.kind == OpKind::DataRight;
        }
        why = "Wortoperand erwartet (EW, AW, MW, DW)";
        return a.kind == OpKind::InputWord || a.kind == OpKind::OutputWord || a.kind == OpKind::FlagWord
            || a.kind == OpKind::DataWord;
    case ParamKind::D:
        why = "Konstante im Format " + d.type + " erwartet";
        return a.kind == OpKind::Constant && a.label == d.type;
    case ParamKind::B:
        why = "Baustein erwartet (DB, PB, FB, SB)";
        return a.kind == OpKind::Block && a.blockType != BlockType::OB;
    case ParamKind::T:
        why = "Zeit erwartet (T n)";
        return a.kind == OpKind::Timer;
    case ParamKind::Z:
        why = "Zähler erwartet (Z n)";
        return a.kind == OpKind::Counter;
    }
    return false;
}

// Beim Durchreichen eines Formaloperanden müssen Art und Typ zusammenpassen (E/A sind austauschbar)
std::string paramCompatKey(const ParamDecl& d)
{
    switch (d.kind) {
    case ParamKind::E:
    case ParamKind::A: return "EA" + d.type;
    default: return d.describe();
    }
}

bool isValidName(const std::string& n)
{
    return !n.empty() && n.size() <= 8 && isIdentStart(n[0]) && std::all_of(n.begin(), n.end(), isIdentChar);
}

// BEZ :EIN  E/A/D/B/T/Z: E  BI/BY/W/K: BI   (Kurzform: BEZ :EIN E BI)
ParamDecl parseDeclaration(const std::string& text, const Block& fb)
{
    static const std::regex headings(R"(E/A/D/B/T/Z\s*:|BI/BY/W/K\s*:|KM/KH/KY/KC/KF/KT/KZ/KG\s*:)");
    std::istringstream ts(std::regex_replace(upper(text), headings, " "));
    std::vector<std::string> tok;
    for (std::string t; ts >> t;) tok.push_back(t);
    if (tok.size() < 2) fail("BEZ erwartet Name und Art, z.B. BEZ :EIN  E/A/D/B/T/Z: E  BI/BY/W/K: BI");
    if (tok.size() > 3) fail("Zu viele Angaben in der Parameterdeklaration");

    ParamDecl d;
    d.name = tok[0];
    if (d.name.size() > 4 || !std::isalpha(static_cast<unsigned char>(d.name[0]))
        || !std::all_of(d.name.begin(), d.name.end(), isIdentChar))
        fail("Ungültiger Parametername '" + d.name + "' (max. 4 Zeichen, beginnt mit Buchstabe)");
    if (fb.paramIndex(d.name) >= 0) fail("Parameter '" + d.name + "' ist doppelt deklariert");
    static const std::map<std::string, ParamKind> kinds = {
        {"E", ParamKind::E}, {"A", ParamKind::A}, {"D", ParamKind::D},
        {"B", ParamKind::B}, {"T", ParamKind::T}, {"Z", ParamKind::Z}};
    auto k = kinds.find(tok[1]);
    if (k == kinds.end()) fail("Parameterart '" + tok[1] + "' unbekannt (E, A, D, B, T oder Z)");
    d.kind = k->second;
    d.type = tok.size() > 2 ? tok[2] : "";

    switch (d.kind) {
    case ParamKind::E:
    case ParamKind::A:
        if (d.type == "K") fail("Typ K bei Art E/A wird nicht unterstützt – Konstanten über Art D übergeben");
        if (d.type != "BI" && d.type != "BY" && d.type != "W")
            fail("Parameter '" + d.name + "': Typ BI, BY oder W erwartet");
        break;
    case ParamKind::D: {
        static const std::set<std::string> types = {"KM", "KH", "KY", "KC", "KF", "KT", "KZ"};
        if (d.type == "KG") fail("Gleitpunktzahlen (KG) werden nicht unterstützt");
        if (!types.count(d.type)) fail("Parameter '" + d.name + "': Typ KM, KH, KY, KC, KF, KT oder KZ erwartet");
        break;
    }
    default:
        if (!d.type.empty()) fail("Parameterart " + tok[1] + " hat keinen Typ");
        break;
    }
    return d;
}

Instruction parseInstruction(const std::string& text, const Block* block)
{
    Instruction ins;
    std::string compact = upper(removeSpaces(text));
    if (auto it = symbolOps().find(compact); it != symbolOps().end()) {
        ins.op = it->second;
        return ins;
    }

    std::string mnemonic, operandText;
    if (text[0] == '=') {
        mnemonic = "=";
        operandText = text.substr(1);
    } else {
        size_t i = 0;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        mnemonic = upper(text.substr(0, i));
        operandText = text.substr(i);
    }

    auto it = mnemonics().find(mnemonic);
    if (it == mnemonics().end()) fail("Unbekannte Operation '" + mnemonic + "'");
    Operand opd = parseOperand(operandText);
    unsigned cls = classOf(opd);
    if (opd.kind == OpKind::Label && block) {
        int idx = block->paramIndex(opd.label);
        if (idx >= 0) {
            opd.kind = OpKind::Param;
            opd.address = static_cast<uint16_t>(idx);
            cls = paramClass(block->params[idx]);
            if (!(it->second.allowed & cls))
                fail("Formaloperand '=" + opd.label + "' (" + block->params[idx].describe() + ") ist bei '"
                     + mnemonic + "' nicht erlaubt");
        }
    }
    if (opd.kind == OpKind::Label && !(it->second.allowed & C_LABEL))
        fail("Formaloperand '=" + opd.label + "' ist "
             + (block ? std::string("in diesem FB nicht deklariert (BEZ fehlt)")
                      : std::string("nur in Funktionsbausteinen (FB) möglich")));
    if (!(it->second.allowed & cls)) {
        if (opd.kind == OpKind::None) fail("Operation '" + mnemonic + "' erwartet einen Operanden");
        if (it->second.allowed == C_NONE) fail("Operation '" + mnemonic + "' hat keinen Operanden");
        fail("Operand '" + trim(operandText) + "' ist bei '" + mnemonic + "' nicht erlaubt");
    }
    ins.op = it->second.op;
    if (ins.op == Op::B && opd.kind != OpKind::Param)
        fail("B ist nur mit einem Bausteinparameter möglich (B =X); B MW / B DW wird nicht unterstützt");
    if (mnemonic == "O" && opd.kind == OpKind::None) ins.op = Op::OAlone;
    if ((ins.op == Op::SLW || ins.op == Op::SRW) && opd.value > 15) fail("Schiebezahl 0..15 erwartet");
    if ((ins.op == Op::Inc || ins.op == Op::Dec) && opd.value > 255) fail("Wert 0..255 erwartet");
    ins.operand = opd;
    return ins;
}

// Ergänzende Operationen (STEP 5): nur in Funktionsbausteinen programmierbar
bool isSupplementary(const Instruction& ins)
{
    switch (ins.op) {
    case Op::P: case Op::PN: case Op::SU: case Op::RU:
    case Op::UW: case Op::OW: case Op::XOW: case Op::KEW: case Op::KZW:
    case Op::SLW: case Op::SRW: case Op::Inc: case Op::Dec: case Op::TAK:
    case Op::SPZ: case Op::SPN: case Op::SPP: case Op::SPM: case Op::SPO:
    case Op::B:
        return true;
    case Op::SPA:
    case Op::SPB:
        return ins.operand.kind == OpKind::Label;
    default:
        return false;
    }
}

// Zerlegt "NAME :rest"; liefert false, wenn die Zeile nicht mit einem Bezeichner und ':' beginnt
bool splitIdentColon(const std::string& line, std::string& ident, std::string& rest)
{
    if (line.empty() || !isIdentStart(line[0])) return false;
    size_t i = 0;
    while (i < line.size() && isIdentChar(line[i])) ++i;
    size_t j = i;
    while (j < line.size() && std::isspace(static_cast<unsigned char>(line[j]))) ++j;
    if (j >= line.size() || line[j] != ':') return false;
    ident = upper(line.substr(0, i));
    rest = trim(line.substr(j + 1));
    return true;
}

bool isActualOperand(const Operand& o)
{
    return o.kind != OpKind::None && o.kind != OpKind::Number && o.kind != OpKind::Param;
}

struct ArgLine {
    std::string param;
    Operand actual;
    int line;
    bool invalid = false;   // Aktualoperand fehlerhaft, Fehler wurde schon gemeldet
};

struct PendingCall {
    std::pair<int, int> caller;
    size_t index;
    int line;
    std::string name;
    int nameLine = 0;
    std::vector<ArgLine> args;
};

void resolveCalls(ParseResult& result, const std::vector<PendingCall>& calls)
{
    auto err = [&](int line, const std::string& m) { result.errors.push_back({line, m}); };
    for (const PendingCall& c : calls) {
        Block* caller = result.program.find(static_cast<BlockType>(c.caller.first), c.caller.second);
        if (!caller || c.index >= caller->code.size()) continue;
        Instruction& ins = caller->code[c.index];
        const Block* fb = result.program.find(BlockType::FB, ins.operand.address);
        const std::string fbName = "FB " + std::to_string(ins.operand.address);
        if (!fb) {
            if (!c.args.empty() || !c.name.empty()) err(c.line, fbName + " ist nicht vorhanden");
            continue;
        }
        if (!c.name.empty() && !fb->fbName.empty() && c.name != fb->fbName)
            err(c.nameLine, "NAME '" + c.name + "' passt nicht zu " + fbName + " (NAME :" + fb->fbName + ")");

        ins.args.assign(fb->params.size(), Operand{});
        std::vector<bool> given(fb->params.size(), false);
        for (const ArgLine& a : c.args) {
            int idx = fb->paramIndex(a.param);
            if (idx < 0) {
                err(a.line, fbName + " hat keinen Parameter '" + a.param + "'");
                continue;
            }
            if (given[idx]) {
                err(a.line, "Parameter '" + a.param + "' ist doppelt versorgt");
                continue;
            }
            given[idx] = true;
            if (a.invalid) continue;
            const ParamDecl& d = fb->params[idx];
            Operand actual = a.actual;
            if (actual.kind == OpKind::Label) {     // Formaloperand des aufrufenden FB durchreichen
                int cidx = caller->type == BlockType::FB ? caller->paramIndex(actual.label) : -1;
                if (cidx < 0) {
                    err(a.line, "'=" + actual.label + "' ist kein Parameter von " + caller->name());
                    continue;
                }
                const ParamDecl& cd = caller->params[cidx];
                if (paramCompatKey(cd) != paramCompatKey(d)) {
                    err(a.line, "Formaloperand '=" + cd.name + "' (" + cd.describe() + ") passt nicht zu Parameter '"
                                    + d.name + "' (" + d.describe() + ")");
                    continue;
                }
                actual.kind = OpKind::Param;
                actual.address = static_cast<uint16_t>(cidx);
            } else if (std::string why; !actualMatches(d, actual, why)) {
                err(a.line, "Parameter '" + d.name + "' (" + d.describe() + "): " + why);
                continue;
            }
            ins.args[idx] = actual;
        }
        std::string missing;
        for (size_t i = 0; i < given.size(); ++i)
            if (!given[i]) missing += (missing.empty() ? "" : ", ") + fb->params[i].name;
        if (!missing.empty()) err(c.line, fbName + ": Parameter nicht versorgt: " + missing);
    }
}

bool parseBlockHeader(const std::string& line, BlockType& type, int& number)
{
    std::string u = upper(removeSpaces(line));
    static const std::map<std::string, BlockType> heads = {
        {"OB", BlockType::OB}, {"PB", BlockType::PB}, {"FB", BlockType::FB},
        {"SB", BlockType::SB}, {"DB", BlockType::DB}};
    if (u.size() < 3) return false;
    auto it = heads.find(u.substr(0, 2));
    if (it == heads.end()) return false;
    std::string num = u.substr(2);
    if (num.empty() || !std::all_of(num.begin(), num.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
        return false;
    type = it->second;
    number = parseNumber(num, 0, 255, "Bausteinnummer");
    return true;
}

}  // namespace

ParseResult parse(const std::string& source)
{
    ParseResult result;
    std::istringstream in(source);
    std::string rawLine;
    int lineNo = 0;
    Block* current = nullptr;
    std::pair<int, int> currentKey;
    std::vector<std::string> pendingLabels;
    std::vector<PendingCall> calls;
    bool collectingArgs = false;    // Zeilen nach "SPA FB n" sind Parameterversorgung

    auto finishBlock = [&]() {
        if (!current) return;
        if (!pendingLabels.empty()) {
            // Marke am Bausteinende zeigt hinter die letzte Anweisung (wirkt wie BE)
            for (const auto& l : pendingLabels) current->labels[l] = current->code.size();
            pendingLabels.clear();
        }
        for (const auto& ins : current->code) {
            if (ins.operand.kind == OpKind::Label && !current->labels.count(ins.operand.label))
                result.errors.push_back({ins.line, "Sprungmarke '" + ins.operand.label + "' ist in "
                                                       + current->name() + " nicht definiert"});
        }
        current = nullptr;
        collectingArgs = false;
    };

    while (std::getline(in, rawLine)) {
        ++lineNo;
        if (!rawLine.empty() && rawLine.back() == '\r') rawLine.pop_back();
        std::string line = trim(stripComment(rawLine));
        if (line.empty()) continue;
        if (line == "***") {
            // Netzwerkgrenze: im MC5-Code als BLD 255 abgelegt
            if (current && current->type != BlockType::DB && !current->code.empty()) {
                Instruction sep;
                sep.op = Op::BLD;
                sep.operand.kind = OpKind::Number;
                sep.operand.value = 255;
                sep.line = lineNo;
                for (const auto& l : pendingLabels) current->labels[l] = current->code.size();
                pendingLabels.clear();
                current->code.push_back(sep);
            }
            collectingArgs = false;
            continue;
        }

        try {
            BlockType type;
            int number;
            if (parseBlockHeader(line, type, number)) {
                finishBlock();
                auto key = std::make_pair(static_cast<int>(type), number);
                if (result.program.blocks.count(key)) {
                    result.errors.push_back({lineNo, std::string("Baustein ") + blockTypeName(type) + " "
                                                         + std::to_string(number) + " ist doppelt vorhanden"});
                }
                Block& b = result.program.blocks[key];
                b = Block{};
                b.type = type;
                b.number = number;
                b.line = lineNo;
                current = &b;
                currentKey = key;
                continue;
            }

            if (!current) fail("Anweisung außerhalb eines Bausteins (Bausteinkopf wie 'OB 1' fehlt)");

            if (current->type == BlockType::DB) {
                // optionale Adresse "0:" am Zeilenanfang
                std::string s = line;
                size_t i = 0;
                while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
                if (i > 0) {
                    size_t j = i;
                    while (j < s.size() && std::isspace(static_cast<unsigned char>(s[j]))) ++j;
                    if (j < s.size() && s[j] == ':') {
                        int addr = parseNumber(s.substr(0, i), 0, 2047, "Datenwortadresse");
                        if (addr != static_cast<int>(current->data.size()))
                            fail("Datenwort " + std::to_string(addr) + " erwartet, nächstes freies ist DW "
                                 + std::to_string(current->data.size()));
                        s = trim(s.substr(j + 1));
                    }
                }
                std::string u = upper(s);
                if (u.size() < 2 || u[0] != 'K') fail("Im Datenbaustein wird eine Konstante erwartet, z.B. KF +100");
                std::string fmt = u.substr(0, 2);
                Operand c = parseOperand(s);
                if (c.kind != OpKind::Constant) fail("Unbekanntes Konstantenformat '" + fmt + "'");
                current->data.push_back(c.value);
                continue;
            }

            std::string ident, afterColon;
            const bool identColon = splitIdentColon(line, ident, afterColon);

            // Parameterversorgung nach "SPA FB n":  NAME :MOTOR / EIN :E 0.0
            if (collectingArgs) {
                PendingCall& call = calls.back();
                if (identColon && ident == "NAME") {
                    if (!call.name.empty()) fail("NAME ist beim Aufruf doppelt angegeben");
                    call.name = upper(afterColon);
                    call.nameLine = lineNo;
                    continue;
                }
                if (identColon) {
                    try {
                        Operand a = parseOperand(afterColon);
                        if (isActualOperand(a)) {
                            call.args.push_back({ident, a, lineNo});
                            continue;
                        }
                    } catch (const ParseFail& f) {
                        // Marke mit gültiger Anweisung beendet die Versorgung, sonst fehlerhafter Parameter
                        bool isInstruction = true;
                        try {
                            parseInstruction(afterColon, current->type == BlockType::FB ? current : nullptr);
                        } catch (const ParseFail&) {
                            isInstruction = false;
                        }
                        if (!isInstruction) {
                            call.args.push_back({ident, {}, lineNo, true});
                            fail("Aktualoperand für '" + ident + "': " + f.message);
                        }
                    }
                }
                collectingArgs = false;
            }

            // Kopf eines FB:  NAME :MOTOR  /  BEZ :EIN E/A/D/B/T/Z: E BI/BY/W/K: BI
            if (identColon && (ident == "NAME" || ident == "BEZ")) {
                if (current->type != BlockType::FB) fail(ident + " ist nur im Funktionsbaustein (FB) erlaubt");
                if (!current->code.empty() || !pendingLabels.empty())
                    fail(ident + " muss vor der ersten Anweisung des FB stehen");
                if (ident == "NAME") {
                    std::string n = upper(afterColon);
                    if (!current->fbName.empty()) fail("NAME ist doppelt angegeben");
                    if (!isValidName(n)) fail("Ungültiger Bausteinname '" + n + "' (max. 8 Zeichen)");
                    current->fbName = n;
                } else {
                    ParamDecl d = parseDeclaration(afterColon, *current);
                    d.line = lineNo;
                    current->params.push_back(d);
                }
                continue;
            }

            // Sprungmarke "M001 :" bzw. Spaltenzeichen ":" am Zeilenanfang
            std::string rest = line;
            if (rest == ":F" || rest == "::F") {
                rest = ":F";
            } else {
                size_t i = 0;
                if (isIdentStart(rest[0])) {
                    while (i < rest.size() && isIdentChar(rest[i])) ++i;
                }
                size_t j = i;
                while (j < rest.size() && std::isspace(static_cast<unsigned char>(rest[j]))) ++j;
                if (j < rest.size() && rest[j] == ':') {
                    if (i > 0) {
                        std::string label = upper(rest.substr(0, i));
                        if (label.size() > 8) fail("Sprungmarke '" + label + "' ist länger als 8 Zeichen");
                        bool dup = current->labels.count(label)
                                || std::find(pendingLabels.begin(), pendingLabels.end(), label) != pendingLabels.end();
                        if (dup) fail("Sprungmarke '" + label + "' ist doppelt vorhanden");
                        if (current->paramIndex(label) >= 0)
                            fail("Sprungmarke '" + label + "' heißt wie ein Bausteinparameter");
                        pendingLabels.push_back(label);
                    }
                    rest = trim(rest.substr(j + 1));
                }
            }
            if (rest.empty()) continue;

            Instruction ins = parseInstruction(rest, current->type == BlockType::FB ? current : nullptr);
            ins.line = lineNo;
            if (current->type != BlockType::FB && isSupplementary(ins))
                fail("'" + trim(rest) + "' ist eine ergänzende Operation und nur im Funktionsbaustein (FB) erlaubt");
            for (const auto& l : pendingLabels) current->labels[l] = current->code.size();
            pendingLabels.clear();
            current->code.push_back(ins);
            if ((ins.op == Op::SPA || ins.op == Op::SPB) && ins.operand.kind == OpKind::Block
                && ins.operand.blockType == BlockType::FB) {
                calls.push_back({currentKey, current->code.size() - 1, lineNo, {}, 0, {}});
                collectingArgs = true;
            }
        } catch (const ParseFail& f) {
            result.errors.push_back({lineNo, f.message});
        }
    }
    finishBlock();
    resolveCalls(result, calls);

    if (!result.program.find(BlockType::OB, 1) && result.errors.empty())
        result.errors.push_back({1, "Organisationsbaustein OB 1 fehlt (zyklisches Programm)"});

    std::sort(result.errors.begin(), result.errors.end(),
              [](const Diagnostic& a, const Diagnostic& b) { return a.line < b.line; });
    return result;
}

}  // namespace s5
