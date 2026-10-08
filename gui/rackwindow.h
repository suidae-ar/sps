// Hauptfenster: Baugruppenträger mit CPU und steckbaren E/A-Baugruppen.
#pragma once

#include "rackmodules.h"

#include <QMainWindow>

#include <vector>

class MainWindow;
class PlcController;
class QHBoxLayout;
class QLabel;

class RackWindow : public QMainWindow {
    Q_OBJECT
public:
    RackWindow(PlcController* plc, MainWindow* editor);

    void showEditor();

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void addModule(const rack::ModuleConfig& cfg);
    void addModuleOfKind(rack::ModuleKind kind, int bytes);
    void showAddMenu(const QPoint& globalPos);
    void showModuleMenu(rack::DigitalModule* m, const QPoint& globalPos);
    void removeModule(rack::DigitalModule* m);
    void moveModule(rack::DigitalModule* m, int delta);
    void relayout();
    void saveModules() const;
    void restoreModules();
    void refresh();
    int nextFreeAddress(rack::ModuleKind kind) const;

    PlcController* plc_;
    MainWindow* editor_;
    rack::RackWidget* rack_;
    QHBoxLayout* layout_;
    rack::CpuModule* cpuModule_;
    rack::EmptySlot* emptySlot_;
    std::vector<rack::DigitalModule*> modules_;
    QLabel* cycleLabel_;
};
