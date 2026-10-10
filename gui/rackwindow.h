// Hauptfenster: Baugruppenträger mit CPU und steckbaren E/A-Baugruppen.
#pragma once

#include "program.h"
#include "rackmodules.h"

#include <QMainWindow>
#include <QStringList>

#include <vector>

class MainWindow;
class PlcController;
class QHBoxLayout;
class QMenu;

class RackWindow : public QMainWindow {
    Q_OBJECT
public:
    RackWindow(PlcController* plc, MainWindow* editor);

    void showEditor();

    // Raspberry-Pi-Baugruppen abmelden (s5io schaltet die Ausgänge ab). Vor dem Beenden aufrufen:
    // unter macOS endet die App über exit(), ohne dass die Fenster zerstört werden.
    void shutdownPeripherals();

    // Warnungen zu Ein-/Ausgängen, die nicht an einem Raspberry Pi anliegen (leer ohne Pi-Baugruppe)
    QStringList hardwareWarnings(const s5::Program& program) const;

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void addDigital(const rack::ModuleConfig& cfg);
    void addPi(const PiConfig& cfg);
    void registerModule(rack::ModuleWidget* m);
    void addModuleOfKind(rack::ModuleKind kind, int bytes);
    void addPiInteractive();
    void showAddMenu(const QPoint& globalPos);
    void addModuleActions(QMenu* menu);
    void addCpuActions(QMenu* menu);
    void showModuleMenu(rack::ModuleWidget* m, const QPoint& globalPos);
    void fillDigitalMenu(rack::DigitalModule* m, QMenu& menu, const QPoint& globalPos);
    void fillPiMenu(rack::PiModule* m, QMenu& menu);
    void editPi(rack::PiModule* m);
    void removeModule(rack::ModuleWidget* m);
    void setMomentaryMask(rack::DigitalModule* m, uint32_t mask);
    void moveModule(rack::ModuleWidget* m, int delta);
    void relayout();
    void saveModules() const;
    void restoreModules();
    void refresh();
    void reportHardware();
    int nextFreeAddress(bool input) const;

    PlcController* plc_;
    MainWindow* editor_;
    rack::RackWidget* rack_;
    QHBoxLayout* layout_;
    rack::CpuModule* cpuModule_;
    rack::EmptySlot* emptySlot_;
    std::vector<rack::ModuleWidget*> modules_;
};
