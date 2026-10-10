// Editorfenster ("Programmiergerät"): AWL-Editor, Status, Variablen, Maschinencode.
#pragma once

#include "cpu.h"
#include "mc5.h"

#include <QMainWindow>
#include <QStringList>

#include <functional>

class CodeEditor;
class Mc5View;
class PlcController;
class QDockWidget;
class QAction;
class QLabel;
class QListWidget;
class QTimer;
class WatchPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(PlcController* plc);

    void loadExample(int index);
    void showMc5View();
    bool maybeSave();
    void saveWindowState() const;

    // Zusätzliche Prüfung beim Übersetzen (z.B. Ein-/Ausgänge ohne echten Raspberry-Pi-Pin)
    void setHardwareCheck(std::function<QStringList(const s5::Program&)> check) { hardwareCheck_ = std::move(check); }

signals:
    void showRackRequested();

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void createActions();
    void createDocks();

    bool compile();
    bool needsCompile() const;
    void refreshUi();
    void onRuntimeError(const QString& message, int line);

    void newFile();
    void openFile();
    bool saveFile();
    bool saveFileAs();
    void loadText(const QString& text, const QString& path);
    bool loadFile(const QString& path);
    void updateTitle();
    void updateMc5View();
    void exportMc5();
    void importMc5();
    void showHelp();
    void showAbout();

    PlcController* plc_;
    CodeEditor* editor_;
    WatchPanel* watch_;
    QListWidget* messages_;
    Mc5View* mc5View_;
    QDockWidget* msgDock_;
    QList<QAction*> familyActions_;
    QLabel* stateLabel_;
    QLabel* cycleLabel_;

    QAction* runAct_;
    QAction* stopAct_;
    QAction* stepAct_;
    QAction* statusAct_;

    QTimer* uiTimer_;

    std::function<QStringList(const s5::Program&)> hardwareCheck_;
    QString currentFile_;
    int compiledRevision_ = -1;
};
