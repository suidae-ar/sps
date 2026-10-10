#include "mainwindow.h"

#include "awlhighlighter.h"
#include "codeeditor.h"
#include "plccontroller.h"
#include "mc5view.h"
#include "parser.h"
#include "watchpanel.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QSaveFile>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

namespace {

struct Example {
    const char* title;
    const char* resource;
};

const Example kExamples[] = {
    {"Grundverknüpfungen", ":/examples/grundlagen.awl"},
    {"Zeiten (Blinker, Treppenlicht …)", ":/examples/zeiten.awl"},
    {"Zähler und Vergleicher", ":/examples/zaehler.awl"},
    {"Fußgängerampel (Schrittkette)", ":/examples/ampel.awl"},
    {"Bausteine, DB und Schleife", ":/examples/bausteine.awl"},
    {"Funktionsbausteine mit Parametern", ":/examples/fb_parameter.awl"},
};

QString readResource(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll());
}

}  // namespace

MainWindow::MainWindow(PlcController* plc)
    : plc_(plc)
{
    editor_ = new CodeEditor(this);
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    new AwlHighlighter(editor_->document(), dark);
    setCentralWidget(editor_);

    uiTimer_ = new QTimer(this);
    uiTimer_->setInterval(50);
    connect(uiTimer_, &QTimer::timeout, this, &MainWindow::refreshUi);
    uiTimer_->start();

    createDocks();
    createActions();

    // Geändertes Programm vor dem Neuanlauf übersetzen (auch beim Start über den CPU-Schalter)
    plc_->setPrepareHook([this] { return !needsCompile() || compile(); });
    connect(plc_, &PlcController::started, this, [this](bool halted) {
        messages_->addItem(halted ? tr("Neuanlauf im Einzelschritt") : tr("Neuanlauf – CPU in RUN"));
        messages_->scrollToBottom();
    });
    connect(plc_, &PlcController::resetDone, this, [this] {
        editor_->clearLineStatus();
        messages_->addItem(tr("Urlöschen ausgeführt"));
    });
    connect(plc_, &PlcController::runtimeError, this, &MainWindow::onRuntimeError);
    connect(plc_, &PlcController::familyChanged, this, [this] {
        for (QAction* a : std::as_const(familyActions_)) a->setChecked(a->data().toInt() == static_cast<int>(plc_->family()));
        // Programm gegen die neue CPU prüfen und neu laden
        compile();
        messages_->addItem(tr("CPU getauscht: %1").arg(QString::fromLatin1(s5::mc5::familyName(plc_->family()))));
    });
    connect(plc_, &PlcController::stateChanged, this, [this] {
        const bool halted = plc_->cpu().state() == s5::CpuState::Halt;
        editor_->setExecutionLine(halted ? plc_->cpu().currentLine() : -1);
        refreshUi();
    });

    cycleLabel_ = new QLabel;
    statusBar()->addPermanentWidget(cycleLabel_);

    connect(editor_->document(), &QTextDocument::modificationChanged, this, &MainWindow::updateTitle);
    connect(editor_->document(), &QTextDocument::contentsChanged, this, [this] {
        if (plc_->cpu().state() != s5::CpuState::Stop && needsCompile())
            statusBar()->showMessage(tr("Programm geändert – mit „Übersetzen“ (⌘B) in die CPU laden"));
    });

    QSettings s;
    restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(s.value(QStringLiteral("window/state")).toByteArray());
    // ältere gespeicherte Layouts kennen den Maschinencode-Reiter noch nicht
    if (!s.value(QStringLiteral("window/mc5Placed")).toBool()) {
        auto* mc5Dock = qobject_cast<QDockWidget*>(mc5View_->parentWidget());
        if (mc5Dock && !tabifiedDockWidgets(msgDock_).contains(mc5Dock)) tabifyDockWidget(msgDock_, mc5Dock);
        msgDock_->raise();
        s.setValue(QStringLiteral("window/mc5Placed"), true);
    }
    const QString last = s.value(QStringLiteral("lastFile")).toString();
    if (last.isEmpty() || !loadFile(last))
        loadText(readResource(QString::fromLatin1(kExamples[0].resource)), {});

    refreshUi();
}

void MainWindow::createDocks()
{
    watch_ = new WatchPanel;
    auto* watchDock = new QDockWidget(tr("Variablen"), this);
    watchDock->setObjectName(QStringLiteral("watchDock"));
    watchDock->setWidget(watch_);
    addDockWidget(Qt::RightDockWidgetArea, watchDock);

    messages_ = new QListWidget;
    auto* msgDock = new QDockWidget(tr("Meldungen"), this);
    msgDock->setObjectName(QStringLiteral("msgDock"));
    msgDock->setWidget(messages_);
    addDockWidget(Qt::BottomDockWidgetArea, msgDock);
    msgDock_ = msgDock;

    mc5View_ = new Mc5View;
    auto* mc5Dock = new QDockWidget(tr("Maschinencode"), this);
    mc5Dock->setObjectName(QStringLiteral("mc5Dock"));
    mc5Dock->setWidget(mc5View_);
    addDockWidget(Qt::BottomDockWidgetArea, mc5Dock);
    tabifyDockWidget(msgDock, mc5Dock);
    msgDock->raise();
    connect(messages_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        int line = item->data(Qt::UserRole).toInt();
        if (line > 0) editor_->gotoLine(line);
    });

    resize(1300, 860);
    resizeDocks({watchDock}, {420}, Qt::Horizontal);
    resizeDocks({msgDock}, {160}, Qt::Vertical);
}

void MainWindow::createActions()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("&Datei"));
    QToolBar* tb = addToolBar(tr("Werkzeuge"));
    tb->setObjectName(QStringLiteral("toolbar"));
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);

    QAction* a = fileMenu->addAction(tr("&Neu"), QKeySequence::New, this, &MainWindow::newFile);
    a = fileMenu->addAction(tr("&Öffnen …"), QKeySequence::Open, this, &MainWindow::openFile);
    fileMenu->addAction(tr("&Speichern"), QKeySequence::Save, this, &MainWindow::saveFile);
    fileMenu->addAction(tr("Speichern &unter …"), QKeySequence::SaveAs, this, &MainWindow::saveFileAs);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("MC5 &exportieren …"), this, &MainWindow::exportMc5);
    fileMenu->addAction(tr("MC5 &importieren …"), this, &MainWindow::importMc5);
    fileMenu->addSeparator();
    QMenu* ex = fileMenu->addMenu(tr("&Beispiele"));
    for (int i = 0; i < static_cast<int>(std::size(kExamples)); ++i) {
        ex->addAction(QString::fromUtf8(kExamples[i].title), this, [this, i] {
            if (maybeSave()) loadExample(i);
        });
    }
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Beenden"), QKeySequence::Quit, qApp, &QApplication::closeAllWindows);

    QMenu* cpuMenu = menuBar()->addMenu(tr("&CPU"));
    a = cpuMenu->addAction(tr("Ü&bersetzen und laden"), QKeySequence(Qt::CTRL | Qt::Key_B), this, [this] { compile(); });
    tb->addAction(a);
    tb->addSeparator();
    runAct_ = cpuMenu->addAction(tr("&RUN"), QKeySequence(Qt::Key_F5), plc_, &PlcController::run);
    stopAct_ = cpuMenu->addAction(tr("&STOP"), QKeySequence(Qt::SHIFT | Qt::Key_F5), plc_, &PlcController::stop);
    stepAct_ = cpuMenu->addAction(tr("&Einzelschritt"), QKeySequence(Qt::Key_F10), plc_, &PlcController::step);
    runAct_->setToolTip(tr("CPU starten (F5)"));
    stopAct_->setToolTip(tr("CPU anhalten (⇧F5)"));
    stepAct_->setToolTip(tr("Eine Anweisung ausführen (F10)"));
    tb->addAction(runAct_);
    tb->addAction(stopAct_);
    tb->addAction(stepAct_);
    cpuMenu->addSeparator();
    QMenu* famMenu = cpuMenu->addMenu(tr("CPU-&Typ"));
    auto* famGroup = new QActionGroup(this);
    for (auto f : PlcController::families()) {
        QAction* fa = famMenu->addAction(QString::fromLatin1(s5::mc5::familyName(f)));
        fa->setCheckable(true);
        fa->setChecked(f == plc_->family());
        fa->setData(static_cast<int>(f));
        famGroup->addAction(fa);
        familyActions_ << fa;
        connect(fa, &QAction::triggered, plc_, [this, f] { plc_->setFamily(f); });
    }
    cpuMenu->addSeparator();
    a = cpuMenu->addAction(tr("&Urlöschen"), plc_, &PlcController::reset);
    a->setToolTip(tr("Merker, Zeiten, Zähler und Datenbausteine löschen"));
    tb->addSeparator();
    tb->addAction(a);

    QMenu* viewMenu = menuBar()->addMenu(tr("&Ansicht"));
    statusAct_ = viewMenu->addAction(tr("Status &Baustein"));
    statusAct_->setCheckable(true);
    statusAct_->setChecked(true);
    statusAct_->setShortcut(QKeySequence(Qt::Key_F7));
    statusAct_->setToolTip(tr("VKE, Statusbit und AKKU je Zeile anzeigen (F7)"));
    connect(statusAct_, &QAction::toggled, editor_, &CodeEditor::setStatusVisible);
    tb->addSeparator();
    tb->addAction(statusAct_);
    QAction* hexAct = viewMenu->addAction(tr("AKKU als &Hex (KH)"));
    hexAct->setCheckable(true);
    connect(hexAct, &QAction::toggled, editor_, &CodeEditor::setAkkuHex);
    viewMenu->addSeparator();
    for (QDockWidget* d : findChildren<QDockWidget*>()) viewMenu->addAction(d->toggleViewAction());
    viewMenu->addSeparator();
    viewMenu->addAction(tr("Baugruppen&träger"), QKeySequence(Qt::CTRL | Qt::Key_1), this, &MainWindow::showRackRequested);

    QMenu* helpMenu = menuBar()->addMenu(tr("&Hilfe"));
    helpMenu->addAction(tr("&Befehlsübersicht"), QKeySequence::HelpContents, this, &MainWindow::showHelp);
    helpMenu->addAction(tr("Ü&ber S5Sim"), this, &MainWindow::showAbout);

    stateLabel_ = new QLabel;
    stateLabel_->setAlignment(Qt::AlignCenter);
    stateLabel_->setMinimumWidth(90);
    auto* spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);
    tb->addWidget(stateLabel_);
}

// ---------------------------------------------------------------- CPU

bool MainWindow::needsCompile() const
{
    return editor_->document()->revision() != compiledRevision_;
}

bool MainWindow::compile()
{
    const s5::ParseResult r = s5::parse(editor_->toPlainText().toStdString());
    messages_->clear();
    QList<int> errLines;
    for (const auto& e : r.errors) {
        auto* item = new QListWidgetItem(tr("Zeile %1: %2").arg(e.line).arg(QString::fromStdString(e.message)));
        item->setData(Qt::UserRole, e.line);
        item->setForeground(QColor(0xd0, 0x30, 0x30));
        messages_->addItem(item);
        errLines << e.line;
    }
    editor_->setErrorLines(errLines);
    if (!r.ok()) {
        compiledRevision_ = -1;     // beim nächsten Start erneut übersetzen
        statusBar()->showMessage(tr("Übersetzen fehlgeschlagen: %n Fehler", nullptr, static_cast<int>(r.errors.size())));
        if (!errLines.isEmpty()) editor_->gotoLine(errLines.first());
        return false;
    }

    // Kennt die gesteckte CPU alle Operationen?
    const QString cpuName = QString::fromLatin1(s5::mc5::familyName(plc_->family()));
    const auto cpuErrors = s5::mc5::checkCpu(r.program, plc_->family());
    for (const auto& e : cpuErrors) {
        auto* item = new QListWidgetItem(
            tr("Zeile %1: %2 (gesteckte CPU: %3)").arg(e.line).arg(QString::fromStdString(e.message), cpuName));
        item->setData(Qt::UserRole, e.line);
        item->setForeground(QColor(0xd0, 0x30, 0x30));
        messages_->addItem(item);
        errLines << e.line;
    }
    if (!cpuErrors.empty()) {
        compiledRevision_ = -1;
        editor_->setErrorLines(errLines);
        editor_->gotoLine(errLines.first());
        updateMc5View();
        statusBar()->showMessage(tr("Das Programm passt nicht zur CPU %1: %n Fehler", nullptr,
                                    static_cast<int>(cpuErrors.size())).arg(cpuName));
        return false;
    }

    int blocks = 0, instr = 0;
    for (const auto& [key, b] : r.program.blocks) {
        ++blocks;
        instr += static_cast<int>(b.code.size());
    }
    plc_->cpu().load(r.program);
    watch_->setProgram(r.program);
    updateMc5View();
    editor_->clearLineStatus();
    compiledRevision_ = editor_->document()->revision();
    const QString msg = tr("Übersetzt und geladen: %1 Bausteine, %2 Anweisungen").arg(blocks).arg(instr);
    messages_->addItem(msg);
    statusBar()->showMessage(msg, 5000);
    if (hardwareCheck_) {
        for (const QString& w : hardwareCheck_(r.program)) {
            auto* item = new QListWidgetItem(tr("Hinweis: %1").arg(w));
            item->setForeground(QColor(0xd0, 0x90, 0x10));
            messages_->addItem(item);
        }
    }
    return true;
}

void MainWindow::onRuntimeError(const QString& msg, int line)
{
    editor_->setExecutionLine(-1);
    if (msg.isEmpty()) return;
    auto* item = new QListWidgetItem(line > 0 ? tr("STOP in Zeile %1: %2").arg(line).arg(msg) : tr("STOP: %1").arg(msg));
    item->setData(Qt::UserRole, line);
    item->setForeground(QColor(0xd0, 0x30, 0x30));
    messages_->addItem(item);
    messages_->scrollToBottom();
    if (line > 0) {
        editor_->setErrorLines({line});
        editor_->gotoLine(line);
    }
    statusBar()->showMessage(tr("CPU in STOP: %1").arg(msg));
}

void MainWindow::refreshUi()
{
    const s5::Cpu& cpu = plc_->cpu();
    const s5::CpuState st = cpu.state();
    QString text, color;
    switch (st) {
    case s5::CpuState::Stop: text = QStringLiteral("STOP"); color = QStringLiteral("#c62828"); break;
    case s5::CpuState::Run: text = QStringLiteral("RUN"); color = QStringLiteral("#2e7d32"); break;
    case s5::CpuState::Halt: text = QStringLiteral("HALT"); color = QStringLiteral("#f9a825"); break;
    }
    stateLabel_->setText(text);
    stateLabel_->setStyleSheet(QStringLiteral("QLabel { background: %1; color: white; font-weight: bold;"
                                              " border-radius: 4px; padding: 3px 12px; }").arg(color));

    runAct_->setEnabled(st != s5::CpuState::Run);
    stopAct_->setEnabled(st != s5::CpuState::Stop);

    if (!isVisible()) return;
    watch_->refresh(cpu);
    if (editor_->statusVisible() && st != s5::CpuState::Stop) editor_->setLineStatus(cpu.lineStatus());

    if (st == s5::CpuState::Run)
        cycleLabel_->setText(tr("Zyklus %1   Bearbeitungszeit %2 ms")
                                 .arg(cpu.cycleCount())
                                 .arg(plc_->cycleTimeMs(), 0, 'f', 3));
    else
        cycleLabel_->setText(tr("Zyklus %1").arg(cpu.cycleCount()));
}

// ---------------------------------------------------------------- MC5

void MainWindow::updateMc5View()
{
    const s5::ParseResult r = s5::parse(editor_->toPlainText().toStdString());
    if (!r.ok()) {
        mc5View_->setPlainText(tr("Programm enthält Syntaxfehler – siehe Meldungen."));
        return;
    }
    mc5View_->showResult(s5::mc5::assemble(r.program, plc_->family()), plc_->family(), editor_->toPlainText().split(QLatin1Char('\n')));
}

void MainWindow::showMc5View()
{
    if (auto* dock = mc5View_->parentWidget() ? qobject_cast<QDockWidget*>(mc5View_->parentWidget()) : nullptr) {
        dock->show();
        dock->raise();
    }
}

void MainWindow::exportMc5()
{
    const s5::ParseResult r = s5::parse(editor_->toPlainText().toStdString());
    if (!r.ok()) {
        compile();
        return;
    }
    const s5::mc5::AssembleResult a = s5::mc5::assemble(r.program, plc_->family());
    updateMc5View();
    if (!a.ok()) {
        QString msg;
        for (const auto& e : a.errors) msg += tr("Zeile %1: %2\n").arg(e.line).arg(QString::fromStdString(e.message));
        QMessageBox::warning(this, tr("MC5 exportieren"),
                             tr("Das Programm lässt sich nicht für %1 übersetzen:\n\n%2")
                                 .arg(QString::fromLatin1(s5::mc5::familyName(plc_->family())), msg));
        return;
    }
    QString suggestion = currentFile_.isEmpty() ? QStringLiteral("programm.mc5")
                                                : QFileInfo(currentFile_).path() + QLatin1Char('/')
                                                      + QFileInfo(currentFile_).completeBaseName() + QStringLiteral(".mc5");
    QString path = QFileDialog::getSaveFileName(this, tr("MC5-Code exportieren"), suggestion,
                                                tr("MC5-Speicherabbild (*.mc5);;Alle Dateien (*)"));
    if (path.isEmpty()) return;
    const std::vector<uint8_t> bytes = s5::mc5::toBytes(a.blocks);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)
        || f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size())
        || !f.commit()) {
        QMessageBox::warning(this, tr("MC5 exportieren"), tr("Datei konnte nicht geschrieben werden:\n%1").arg(path));
        return;
    }
    for (const auto& w : a.warnings)
        messages_->addItem(tr("MC5 Zeile %1: %2").arg(w.line).arg(QString::fromStdString(w.message)));
    const QString msg = tr("MC5 exportiert (%1): %2 Bausteine, %3 Byte → %4")
                            .arg(QString::fromLatin1(s5::mc5::familyName(plc_->family())))
                            .arg(a.blocks.size())
                            .arg(bytes.size())
                            .arg(QFileInfo(path).fileName());
    messages_->addItem(msg);
    statusBar()->showMessage(msg, 5000);
}

void MainWindow::importMc5()
{
    if (!maybeSave()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("MC5-Code importieren"), QFileInfo(currentFile_).path(),
                                                      tr("MC5-Speicherabbild (*.mc5 *.bin);;Alle Dateien (*)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("MC5 importieren"), tr("Datei konnte nicht gelesen werden:\n%1").arg(path));
        return;
    }
    const QByteArray data = f.readAll();
    std::vector<std::string> warnings;
    const auto blocks = s5::mc5::fromBytes(std::vector<uint8_t>(data.begin(), data.end()), warnings);
    if (blocks.empty()) {
        QMessageBox::warning(this, tr("MC5 importieren"), tr("In der Datei wurden keine S5-Bausteine gefunden."));
        return;
    }
    const s5::mc5::DisassembleResult d = s5::mc5::disassemble(blocks);
    plc_->stop();
    loadText(QString::fromStdString(d.source), {});
    for (const auto& w : warnings) messages_->addItem(tr("Import: %1").arg(QString::fromStdString(w)));
    for (const auto& w : d.warnings) messages_->addItem(tr("Rückübersetzung: %1").arg(QString::fromStdString(w)));
    messages_->addItem(tr("MC5 importiert: %1 Bausteine aus %2").arg(blocks.size()).arg(QFileInfo(path).fileName()));
}

// ---------------------------------------------------------------- Dateien

void MainWindow::loadExample(int index)
{
    if (index < 0 || index >= static_cast<int>(std::size(kExamples))) return;
    plc_->stop();
    loadText(readResource(QString::fromLatin1(kExamples[index].resource)), {});
}

void MainWindow::loadText(const QString& text, const QString& path)
{
    editor_->setPlainText(text);
    editor_->document()->setModified(false);
    editor_->setErrorLines({});
    editor_->clearLineStatus();
    currentFile_ = path;
    compiledRevision_ = -1;
    messages_->clear();
    updateTitle();
    compile();
}

bool MainWindow::loadFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    loadText(QString::fromUtf8(f.readAll()), path);
    QSettings().setValue(QStringLiteral("lastFile"), path);
    return true;
}

void MainWindow::newFile()
{
    if (!maybeSave()) return;
    plc_->stop();
    loadText(QStringLiteral("; Neues Programm\n\nOB 1\n:\n:BE\n"), {});
}

void MainWindow::openFile()
{
    if (!maybeSave()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("AWL-Programm öffnen"), QFileInfo(currentFile_).path(),
                                                      tr("AWL-Quelltext (*.awl *.txt);;Alle Dateien (*)"));
    if (path.isEmpty()) return;
    plc_->stop();
    if (!loadFile(path)) QMessageBox::warning(this, tr("Öffnen"), tr("Datei konnte nicht gelesen werden:\n%1").arg(path));
}

bool MainWindow::saveFile()
{
    if (currentFile_.isEmpty()) return saveFileAs();
    QFile f(currentFile_);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Speichern"), tr("Datei konnte nicht geschrieben werden:\n%1").arg(currentFile_));
        return false;
    }
    f.write(editor_->toPlainText().toUtf8());
    editor_->document()->setModified(false);
    QSettings().setValue(QStringLiteral("lastFile"), currentFile_);
    statusBar()->showMessage(tr("Gespeichert: %1").arg(currentFile_), 3000);
    return true;
}

bool MainWindow::saveFileAs()
{
    QString path = QFileDialog::getSaveFileName(this, tr("AWL-Programm speichern"),
                                                currentFile_.isEmpty() ? QStringLiteral("programm.awl") : currentFile_,
                                                tr("AWL-Quelltext (*.awl)"));
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".awl");
    currentFile_ = path;
    updateTitle();
    return saveFile();
}

bool MainWindow::maybeSave()
{
    if (!editor_->document()->isModified()) return true;
    auto ret = QMessageBox::warning(this, tr("S5Sim"), tr("Das Programm wurde geändert. Änderungen speichern?"),
                                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (ret == QMessageBox::Save) return saveFile();
    return ret == QMessageBox::Discard;
}

void MainWindow::updateTitle()
{
    const QString name = currentFile_.isEmpty() ? tr("unbenannt.awl") : QFileInfo(currentFile_).fileName();
    setWindowTitle(QStringLiteral("%1[*] – S5Sim").arg(name));
    setWindowModified(editor_->document()->isModified());
}

void MainWindow::saveWindowState() const
{
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/state"), saveState());
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    // Das Editorfenster wird nur ausgeblendet; beendet wird über den Baugruppenträger
    saveWindowState();
    e->accept();
}

// ---------------------------------------------------------------- Hilfe

void MainWindow::showHelp()
{
    static const char* html = R"(
<h2>S5Sim – Befehlsübersicht (STEP 5 AWL)</h2>
<p>Ein Programm besteht aus Bausteinen. Jeder Baustein beginnt mit einer Kopfzeile
(<code>OB 1</code>, <code>PB 3</code>, <code>FB 10</code>, <code>SB 2</code>, <code>DB 5</code>)
und endet mit <code>BE</code>. <b>OB 1</b> wird zyklisch bearbeitet, <b>OB 21</b> (falls vorhanden)
einmal beim Neuanlauf. Ein führender Doppelpunkt (<code>:U E 0.0</code>) ist erlaubt, Sprungmarken stehen
davor (<code>M001: L MW 10</code>). Kommentare beginnen mit <code>;</code> oder <code>//</code>, <code>***</code>
trennt Netzwerke.</p>

<h3>Verknüpfungen</h3>
<table cellpadding="3">
<tr><td><code>U / UN</code></td><td>UND / UND NICHT</td></tr>
<tr><td><code>O / ON</code></td><td>ODER / ODER NICHT</td></tr>
<tr><td><code>O</code></td><td>ODER-Verknüpfung von UND-Gruppen (UND vor ODER)</td></tr>
<tr><td><code>U( O( )</code></td><td>Klammern (max. 7 Ebenen)</td></tr>
<tr><td><code>= S R</code></td><td>Zuweisen, Setzen, Rücksetzen</td></tr>
<tr><td><code>P PN SU RU</code></td><td>Bit abfragen, Bit unbedingt setzen/rücksetzen</td></tr>
</table>
<p>Operanden: <code>E n.m</code>, <code>A n.m</code>, <code>M n.m</code>, <code>D n.m</code> (Datenbit),
<code>T n</code>, <code>Z n</code></p>

<h3>Laden und Transferieren</h3>
<table cellpadding="3">
<tr><td><code>L</code></td><td>Laden: EB, EW, AB, AW, MB, MW, DW, DL, DR, T, Z, Konstanten</td></tr>
<tr><td><code>LC</code></td><td>Zeit bzw. Zähler BCD-codiert laden</td></tr>
<tr><td><code>T</code></td><td>Transferieren in Byte/Wort</td></tr>
</table>
<p>Konstanten: <code>KF +100</code>, <code>KH 00FF</code>, <code>KM 0000111100001111</code>, <code>KY 10,20</code>,
<code>KB 12</code>, <code>KT 050.1</code>, <code>KZ 010</code>, <code>KC AB</code></p>

<h3>Zeiten und Zähler</h3>
<table cellpadding="3">
<tr><td><code>SI SV SE SS SA</code></td><td>Impuls, verlängerter Impuls, Einschalt-, speichernde Einschalt-,
Ausschaltverzögerung. Zeitwert vorher laden, z.B. <code>L KT 020.1</code>
(Zeitbasis .0=10 ms, .1=100 ms, .2=1 s, .3=10 s)</td></tr>
<tr><td><code>R T</code></td><td>Zeit rücksetzen</td></tr>
<tr><td><code>ZV ZR</code></td><td>Vorwärts- und Rückwärtszählen (positive Flanke)</td></tr>
<tr><td><code>S Z / R Z</code></td><td>Zähler setzen (Wert aus AKKU 1, z.B. <code>L KZ 010</code>) bzw. rücksetzen</td></tr>
</table>

<h3>Arithmetik und Vergleich (16-Bit-Festpunkt)</h3>
<p><code>+F -F XF :F</code> rechnen mit AKKU 2 und AKKU 1 und schreiben das Ergebnis in AKKU 1.<br>
<code>!=F &gt;&lt;F &gt;F &gt;=F &lt;F &lt;=F</code> vergleichen AKKU 2 mit AKKU 1, das Ergebnis steht im VKE.</p>

<h3>Wortverknüpfungen, Schieben, Sonstiges</h3>
<p><code>UW OW XOW</code> (Wortverknüpfung), <code>KEW KZW</code> (Einer-/Zweierkomplement),
<code>SLW n SRW n</code> (Schieben), <code>I n D n</code> (Inkrement/Dekrement), <code>TAK</code> (Akkus tauschen),
<code>NOP 0</code>, <code>BLD n</code>, <code>STP</code></p>

<h3>Sprünge und Bausteine</h3>
<table cellpadding="3">
<tr><td><code>SPA / SPB =M001</code></td><td>Sprung absolut / bei VKE=1</td></tr>
<tr><td><code>SPZ SPN SPP SPM SPO</code></td><td>Sprung bei Ergebnis =0, ≠0, &gt;0, &lt;0, Überlauf</td></tr>
<tr><td><code>SPA / SPB PB n</code></td><td>Baustein aufrufen (PB, FB, SB)</td></tr>
<tr><td><code>BE BEA BEB</code></td><td>Bausteinende, absolut, bedingt</td></tr>
<tr><td><code>A DB n</code></td><td>Datenbaustein aufschlagen</td></tr>
</table>
<p>Datenbausteine enthalten eine Konstante je Zeile, optional mit Adresse: <code>0: KF +100</code></p>

<h3>Funktionsbausteine mit Parametern</h3>
<p>Am Anfang des FB stehen der Name und die Parameterdeklarationen, im Baustein werden die Parameter als
Formaloperanden <code>=NAME</code> angesprochen:</p>
<pre>FB 10
NAME :MOTOR
BEZ  :EIN   E/A/D/B/T/Z: E  BI/BY/W/K: BI
BEZ  :ZEIT  E/A/D/B/T/Z: D  KM/KH/KY/KC/KF/KT/KZ/KG: KT
BEZ  :MOT   E/A/D/B/T/Z: A  BI/BY/W/K: BI
:U  =EIN
:L  =ZEIT
...</pre>
<p>Kurzform: <code>BEZ :EIN E BI</code>. Beim Aufruf folgen die Aktualoperanden direkt auf
<code>SPA FB n</code> bzw. <code>SPB FB n</code> (Reihenfolge beliebig, <code>NAME</code> optional):</p>
<pre>:SPA FB 10
NAME :MOTOR
EIN  :E 0.0
ZEIT :KT 050.1
MOT  :A 0.0</pre>
<table cellpadding="3">
<tr><td><code>E, A</code> mit <code>BI / BY / W</code></td><td>Bit (E, A, M, D), Byte (EB, AB, MB, DL, DR), Wort (EW, AW, MW, DW)</td></tr>
<tr><td><code>D</code> mit <code>KM KH KY KC KF KT KZ</code></td><td>Konstante im angegebenen Format</td></tr>
<tr><td><code>B</code></td><td>Baustein: <code>A =DBX</code> schlägt einen DB auf, <code>SPA =BST</code> ruft PB/FB/SB auf</td></tr>
<tr><td><code>T, Z</code></td><td>Zeit bzw. Zähler</td></tr>
</table>
<p>Ein FB kann seine Formaloperanden an einen weiteren FB durchreichen (<code>X :=EIN</code>).
Anzahl, Namen und Typen der Parameter werden beim Übersetzen geprüft.</p>

<h3>Raspberry Pi</h3>
<p>Die Baugruppe <i>Raspberry-Pi-E/A</i> (Baugruppenträger, Menü <i>Baugruppe</i>) verbindet Ein- und Ausgänge mit
den GPIO-Pins eines Raspberry Pi, auf dem der Dienst <code>s5io</code> läuft. Pinbelegung, Pull-Widerstände und
Invertierung werden in den Einstellungen der Baugruppe festgelegt. Beim Übersetzen weist S5Sim auf Ein- und
Ausgänge hin, die nur auf dem Bildschirm existieren. Details: <code>docs/raspi</code>.</p>

<h3>Maschinencode (MC5)</h3>
<p>Die gesteckte CPU (S5-90U/95U, 100U, 115U, 135U/155U) wird am Baugruppenträger per Rechtsklick auf die CPU
oder unter <i>CPU → CPU-Typ</i> gewählt; sie gilt für das Übersetzen und den Maschinencode. Das Fenster <i>Maschinencode</i> zeigt je Baustein
die MC5-Wörter mit Adresse und AWL-Zeile. <i>Datei → MC5 exportieren</i> speichert die Bausteine als
Speicherabbild (Bausteine mit Kopf 7070 hintereinander), <i>MC5 importieren</i> übersetzt ein solches Abbild
zurück in AWL. Ergänzende Operationen (Sprünge auf Marken, Wortverknüpfungen, Schieben, I/D, TAK, P/SU/RU, B =)
sind wie bei STEP 5 nur im FB erlaubt; XF, :F und Datenbit-Verknüpfungen nur bei S5-135U/155U.</p>

<h3>Bedienung</h3>
<p>Das Hauptfenster ist der <b>Baugruppenträger</b>: CPU mit Betriebsartenschalter RUN/STOP, Urlöschen (nur im STOP)
und PG-Taster zum Öffnen des Editors. Über den freien Steckplatz oder das Menü <i>Baugruppe</i> werden Digitalein-
und -ausgaben (8/16/32 Bit) gesteckt; ein Rechtsklick ändert Anfangsadresse, Taster/Schalter oder Position.</p>
<p><b>F5</b> RUN (vorher automatisch übersetzen), <b>⇧F5</b> STOP, <b>F10</b> Einzelschritt,
<b>F7</b> Status Baustein ein/aus, <b>⌘B</b> übersetzen und laden. Die Statusspalte zeigt je Zeile das VKE
(Kästchen), den Signalzustand des Operanden (STA) und bei Lade-/Rechenoperationen AKKU 1.
Ausgänge werden – wie bei der echten CPU – erst am Zyklusende ausgegeben.</p>
)";
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(tr("Befehlsübersicht"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    auto* browser = new QTextBrowser(dlg);
    browser->setHtml(QString::fromUtf8(html));
    auto* layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(browser);
    dlg->resize(720, 760);
    dlg->show();
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("Über S5Sim"),
                       tr("<h3>S5Sim %1</h3><p>Simulator für SIMATIC-S5-Steuerungsprogramme in AWL.</p>"
                          "<p>C++20 und Qt %2</p>")
                           .arg(QStringLiteral("0.1"), QString::fromLatin1(qVersion())));
}
