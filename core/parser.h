// Übersetzer: AWL-Quelltext (STEP 5) -> Program.
#pragma once

#include "program.h"

#include <string>
#include <vector>

namespace s5 {

struct Diagnostic {
    int line = 0;
    std::string message;
};

struct ParseResult {
    Program program;
    std::vector<Diagnostic> errors;

    bool ok() const { return errors.empty(); }
};

ParseResult parse(const std::string& source);

// Hilfsfunktionen für BCD-Darstellung (KZ, KT)
bool isValidBcd(uint16_t bcd, int digits);
uint16_t toBcd(int value);          // 0..9999
int fromBcd(uint16_t bcd);          // ohne Prüfung

}  // namespace s5
