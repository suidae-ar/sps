// Steuert die simulierte CPU: Zyklustakt, RUN/STOP, Einzelschritt, Urlöschen.
// Wird vom Baugruppenträger (CPU-Schalter) und vom Editor (PG) gemeinsam benutzt.
#pragma once

#include "cpu.h"
#include "mc5.h"

#include <QElapsedTimer>
#include <QObject>

#include <functional>

class QTimer;

class PlcController : public QObject {
    Q_OBJECT
public:
    explicit PlcController(QObject* parent = nullptr);

    s5::Cpu& cpu() { return cpu_; }
    const s5::Cpu& cpu() const { return cpu_; }
    double cycleTimeMs() const { return cycleTimeMs_; }

    // Gesteckte CPU (S5-Familie); ein Tausch versetzt die CPU in STOP
    s5::mc5::CpuFamily family() const { return family_; }
    void setFamily(s5::mc5::CpuFamily family);
    static QList<s5::mc5::CpuFamily> families();

    // Wird vor einem Neuanlauf aufgerufen (z.B. geändertes Programm übersetzen); false bricht ab
    void setPrepareHook(std::function<bool()> hook) { prepare_ = std::move(hook); }

    bool run();     // STOP: Neuanlauf, HALT: weiter
    void stop();
    void step();    // STOP: Neuanlauf im Einzelschritt, RUN: anhalten, HALT: eine Anweisung
    void reset();   // Urlöschen

signals:
    void stateChanged();
    void started(bool halted);
    void stepped();
    void resetDone();
    void runtimeError(const QString& message, int line);
    void familyChanged();

private:
    void onCycle();
    void fail();

    s5::Cpu cpu_;
    QTimer* cycleTimer_;
    QElapsedTimer clock_;
    qint64 lastTickMs_ = 0;
    double cycleTimeMs_ = 0;
    std::function<bool()> prepare_;
    s5::mc5::CpuFamily family_ = s5::mc5::CpuFamily::S5_115U;
};
