// MC5-Maschinencode der S5: Assembler (Program -> MC5) und Disassembler (MC5 -> AWL-Text).
//
// Quellen der Codierung: Siemens-Operationslisten S5-115U (CPU 941-944, "Auflistung des
// Maschinencodes"), S5-90U/95U, S5-100U und S5-135U/155U (CPU 928/928B/948).
// Bausteinkopf und FB-Parameterblock nach S5-115U-Handbuch Kap. 7 und S5-POWER-BIOS-Handbuch.
#pragma once

#include "parser.h"
#include "program.h"

#include <cstdint>
#include <string>
#include <vector>

namespace s5::mc5 {

enum class CpuFamily { S5_90_95U, S5_100U, S5_115U, S5_135U };

const char* familyName(CpuFamily f);

struct Mc5Block {
    BlockType type = BlockType::OB;
    int number = 0;
    std::vector<uint16_t> words;    // vollständig, einschließlich 5-Wort-Bausteinkopf
    std::vector<int> lines;         // je Wort die Quellzeile (0 = Kopf / Parameterblock)
};

struct AssembleResult {
    std::vector<Mc5Block> blocks;
    std::vector<Diagnostic> errors;
    std::vector<Diagnostic> cpuErrors;  // Teil von errors: Operation gibt es auf dieser CPU nicht
    std::vector<Diagnostic> warnings;

    bool ok() const { return errors.empty(); }
};

AssembleResult assemble(const Program& program, CpuFamily family);

// Operationen, die die gewählte CPU nicht kennt (z.B. XF auf S5-115U)
std::vector<Diagnostic> checkCpu(const Program& program, CpuFamily family);

struct DisassembleResult {
    std::string source;                 // AWL-Text, wieder übersetzbar
    std::vector<std::string> warnings;
};

DisassembleResult disassemble(const std::vector<Mc5Block>& blocks);

// Speicherabbild: Bausteine hintereinander, Wörter big-endian
std::vector<uint8_t> toBytes(const std::vector<Mc5Block>& blocks);
std::vector<Mc5Block> fromBytes(const std::vector<uint8_t>& bytes, std::vector<std::string>& warnings);

// Typkennung im Bausteinkopf (Wort 2, Bits 13..8)
uint8_t blockTypeCode(BlockType t);
bool blockTypeFromCode(uint8_t code, BlockType& t);

}  // namespace s5::mc5
