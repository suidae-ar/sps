// Ansicht "Maschinencode": MC5-Wörter je Baustein mit zugehöriger AWL-Zeile.
#pragma once

#include "mc5.h"

#include <QPlainTextEdit>

class Mc5View : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit Mc5View(QWidget* parent = nullptr);

    // sourceLines: Quelltext zeilenweise (für die AWL-Spalte)
    void showResult(const s5::mc5::AssembleResult& result, s5::mc5::CpuFamily family, const QStringList& sourceLines);
};
