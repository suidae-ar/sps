// Darstellung von Werten in STEP-5-Formaten (KF, KH, KM, KT, KZ).
#pragma once

#include "cpu.h"

#include <QString>

namespace fmt {

QString kf(uint16_t v);                     // +00100
QString kh(uint16_t v, int digits = 4);     // 0064
QString km(uint16_t v, int bits = 16);      // 0000 0000 0110 0100
QString kt(const s5::TimerState& t);        // 045.1
QString kz(uint16_t v);                     // 012

}  // namespace fmt
