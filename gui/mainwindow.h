#pragma once

#include "cpu.h"
#include "mc5.h"

#include <QElapsedTimer>
#include <QMainWindow>

class CodeEditor;
class IoPanel;
class Mc5View;
class QDockWidget;
class QAction;
class QLabel;
class QListWidget;
class QTimer;
class WatchPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

    // auch für Entwickleroptionen (--example, --run, --step) genutzt
    void loadExample(int index);
    void runCpu();
    void stepCpu();
    s5::Cpu& cpu() { return cpu_; }
    void showMc5View();

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void createActions();
    void createDocks();

    bool compile();
    bool needsCompile() const;
    void stopCpu();
    void resetCpu();
    void onCycleTimer();
    void refreshUi();
    void handleCpuError();

    void newFile();
    void openFile();
    bool saveFile();
    bool saveFileAs();
    bool maybeSave();
    void loadText(const QString& text, const QString& path);
    bool loadFile(const QString& path);
    void updateTitle();
    void updateMc5View();
    void exportMc5();
    void importMc5();
    void showHelp();
    void showAbout();

    s5::Cpu cpu_;
    CodeEditor* editor_;
    IoPanel* ioPanel_;
    WatchPanel* watch_;
    QListWidget* messages_;
    Mc5View* mc5View_;
    QDockWidget* msgDock_;
    s5::mc5::CpuFamily family_ = s5::mc5::CpuFamily::S5_115U;
    QLabel* stateLabel_;
    QLabel* cycleLabel_;

    QAction* runAct_;
    QAction* stopAct_;
    QAction* stepAct_;
    QAction* statusAct_;

    QTimer* cycleTimer_;
    QTimer* uiTimer_;
    QElapsedTimer clock_;
    qint64 lastTickMs_ = 0;
    double cycleTimeMs_ = 0;

    QString currentFile_;
    int compiledRevision_ = -1;
};
