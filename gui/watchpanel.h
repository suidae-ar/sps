// Variablentabelle: alle im Programm verwendeten Operanden mit aktuellem Wert.
#pragma once

#include "cpu.h"

#include <QTableWidget>

#include <vector>

class WatchPanel : public QTableWidget {
    Q_OBJECT
public:
    explicit WatchPanel(QWidget* parent = nullptr);

    void setProgram(const s5::Program& program);
    void refresh(const s5::Cpu& cpu);

private:
    struct Entry {
        s5::Operand operand;
        int db = -1;    // für Datenwörter: DB-Nummer
    };
    std::vector<Entry> entries_;
};
