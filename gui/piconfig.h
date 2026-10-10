// Einstellungen der Baugruppe "Raspberry-Pi-E/A": Verbindung, Adressen und Pinbelegung je Bit.
#pragma once

#include "protocol.h"

#include <QString>

#include <array>
#include <vector>

struct PiConfig {
    static constexpr int kMaxBytes = 2;                 // je Richtung höchstens 2 Byte (16 Bit)
    static constexpr int kMaxChannels = kMaxBytes * 8;

    QString host = QStringLiteral("raspberrypi.local");
    int port = s5io::kDefaultPort;
    int inAddress = 0;      // erstes Eingangsbyte (EB)
    int inBytes = 1;
    int outAddress = 0;     // erstes Ausgangsbyte (AB)
    int outBytes = 1;
    std::array<int, kMaxChannels> inGpio;       // -1 = nicht belegt; Kanal = Byte*8 + Bit
    std::array<int, kMaxChannels> outGpio;
    std::array<s5io::Bias, kMaxChannels> inBias;
    uint32_t inInvert = 0;
    int debounceMs = 10;

    PiConfig();
    void applyDefaultPins();    // Belegung aus docs/raspi/pinbelegung.md

    std::vector<s5io::InputConfig> inputs() const;
    std::vector<s5io::OutputConfig> outputs() const;

    QString encode() const;
    static bool decode(const QString& text, PiConfig& cfg);

    QString inputName(int channel) const;   // "E 0.3"
    QString outputName(int channel) const;  // "A 1.0"
};
