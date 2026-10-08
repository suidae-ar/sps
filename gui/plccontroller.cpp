#include "plccontroller.h"

#include <QTimer>

PlcController::PlcController(QObject* parent)
    : QObject(parent)
    , cycleTimer_(new QTimer(this))
{
    cycleTimer_->setTimerType(Qt::PreciseTimer);
    cycleTimer_->setInterval(10);
    connect(cycleTimer_, &QTimer::timeout, this, &PlcController::onCycle);
    clock_.start();
}

bool PlcController::run()
{
    if (cpu_.state() == s5::CpuState::Stop) {
        if (prepare_ && !prepare_()) return false;
        if (!cpu_.start()) {
            fail();
            return false;
        }
        emit started(false);
    } else {
        cpu_.setHalted(false);
    }
    lastTickMs_ = clock_.elapsed();
    cycleTimer_->start();
    emit stateChanged();
    return true;
}

void PlcController::stop()
{
    cycleTimer_->stop();
    const bool wasRunning = cpu_.state() != s5::CpuState::Stop;
    cpu_.stop();
    if (wasRunning) emit stateChanged();
}

void PlcController::step()
{
    switch (cpu_.state()) {
    case s5::CpuState::Stop:
        if (prepare_ && !prepare_()) return;
        if (!cpu_.start(true)) {
            fail();
            return;
        }
        lastTickMs_ = clock_.elapsed();
        emit started(true);
        break;
    case s5::CpuState::Run:
        cycleTimer_->stop();
        cpu_.setHalted(true);
        break;
    case s5::CpuState::Halt: {
        const qint64 now = clock_.elapsed();
        cpu_.tick(static_cast<int>(now - lastTickMs_));
        lastTickMs_ = now;
        cpu_.step();
        if (cpu_.state() == s5::CpuState::Stop) {
            fail();
            return;
        }
        break;
    }
    }
    emit stepped();
    emit stateChanged();
}

void PlcController::reset()
{
    cycleTimer_->stop();
    cpu_.resetMemory();
    emit resetDone();
    emit stateChanged();
}

void PlcController::onCycle()
{
    const qint64 now = clock_.elapsed();
    cpu_.tick(static_cast<int>(now - lastTickMs_));
    lastTickMs_ = now;

    QElapsedTimer t;
    t.start();
    const bool ok = cpu_.runCycle();
    cycleTimeMs_ = t.nsecsElapsed() / 1e6;
    if (!ok) fail();
}

void PlcController::fail()
{
    cycleTimer_->stop();
    emit runtimeError(QString::fromStdString(cpu_.errorMessage()), cpu_.errorLine());
    emit stateChanged();
}
