// Datenmodell eines übersetzten STEP-5-Programms (AWL).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace s5 {

enum class BlockType : uint8_t { OB, PB, FB, SB, DB };

const char* blockTypeName(BlockType t);

enum class OpKind : uint8_t {
    None,
    InputBit, OutputBit, FlagBit, DataBit,   // E n.m, A n.m, M n.m, D n.m
    InputByte, OutputByte, FlagByte,         // EB n, AB n, MB n
    InputWord, OutputWord, FlagWord,         // EW n, AW n, MW n
    DataWord, DataLeft, DataRight,           // DW n, DL n, DR n
    Timer, Counter,                          // T n, Z n
    Constant,                                // KF, KH, KM, KY, KT, KZ, KC, KB
    Block,                                   // OB/PB/FB/SB/DB n
    Label,                                   // =M001
    Param,                                   // Formaloperand =EIN im FB (address = Parameterindex)
    Number,                                  // Parameter für I, D, SLW, SRW, NOP, BLD
};

struct Operand {
    OpKind kind = OpKind::None;
    uint16_t address = 0;   // Byte-, Wort-, Zeit-, Zähler- oder Bausteinnummer
    uint8_t bit = 0;
    uint16_t value = 0;     // Konstante bzw. Zahl
    BlockType blockType = BlockType::OB;
    std::string label;      // Sprungziel, Parametername bzw. Konstantenformat ("KF", "KT", ...)

    bool isBit() const;
    bool operator<(const Operand& o) const;
    bool operator==(const Operand& o) const;
};

std::string toString(const Operand& o);

enum class Op : uint8_t {
    // Verknüpfungen
    U, UN, O, ON, OAlone, UOpen, OOpen, Close,
    Assign, S, R, SU, RU, P, PN,
    // Laden / Transferieren
    L, LC, T,
    // Zeiten und Zähler
    SI, SV, SE, SS, SA, ZV, ZR,
    // Arithmetik und Vergleich (Festpunkt 16 Bit)
    AddF, SubF, MulF, DivF,
    CmpEq, CmpNe, CmpGt, CmpGe, CmpLt, CmpLe,
    // Digitalverknüpfungen / Schieben / Sonstiges
    UW, OW, XOW, KEW, KZW, SLW, SRW, Inc, Dec, TAK,
    // Sprünge und Bausteine
    SPA, SPB, SPZ, SPN, SPP, SPM, SPO,
    BE, BEA, BEB, ADb,
    NOP, BLD, STP,
    B,      // B =X: Bausteinparameter bearbeiten (A DB bzw. SPA PB/FB/SB)
};

struct Instruction {
    Op op = Op::NOP;
    Operand operand;
    int line = 0;           // 1-basierte Zeilennummer im Quelltext
    std::vector<Operand> args;  // FB-Aufruf: Aktualoperanden in Deklarationsreihenfolge
};

// Bausteinparameter eines FB (BEZ :EIN E/A/D/B/T/Z: E BI/BY/W/K: BI)
enum class ParamKind : uint8_t { E, A, D, B, T, Z };

struct ParamDecl {
    std::string name;
    ParamKind kind = ParamKind::E;
    std::string type;       // BI, BY, W bzw. KM, KH, KY, KC, KF, KT, KZ; leer bei B, T, Z
    int line = 0;

    std::string describe() const;   // z.B. "E BI", "D KT", "T"
};

struct Block {
    BlockType type = BlockType::OB;
    int number = 0;
    int line = 0;
    std::vector<Instruction> code;
    std::map<std::string, size_t> labels;   // Sprungmarke -> Index in code
    std::vector<uint16_t> data;              // nur DB: Datenwörter
    std::string fbName;                      // nur FB: NAME
    std::vector<ParamDecl> params;           // nur FB: Bausteinparameter

    std::string name() const;
    int paramIndex(const std::string& name) const;  // -1 wenn unbekannt
};

struct Program {
    std::map<std::pair<int, int>, Block> blocks;

    const Block* find(BlockType t, int number) const;
    Block* find(BlockType t, int number);
};

}  // namespace s5
