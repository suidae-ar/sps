#include "rackwindow.h"

#include "mainwindow.h"
#include "plccontroller.h"

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QScrollArea>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>

using rack::DigitalModule;
using rack::ModuleConfig;
using rack::ModuleKind;

RackWindow::RackWindow(PlcController* plc, MainWindow* editor)
    : plc_(plc)
    , editor_(editor)
{
    setWindowTitle(tr("S5Sim – Baugruppenträger"));

    rack_ = new rack::RackWidget;
    layout_ = new QHBoxLayout(rack_);
    layout_->setContentsMargins(16, 22, 16, 22);
    layout_->setSpacing(4);

    cpuModule_ = new rack::CpuModule(plc_);
    cpuModule_->setFamilyName(QString::fromLatin1(s5::mc5::familyName(editor_->family())));
    emptySlot_ = new rack::EmptySlot;
    layout_->addWidget(cpuModule_);
    layout_->addWidget(emptySlot_);
    layout_->addStretch();

    auto* scroll = new QScrollArea;
    scroll->setWidget(rack_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setCentralWidget(scroll);

    connect(cpuModule_, &rack::CpuModule::editorRequested, this, &RackWindow::showEditor);
    connect(cpuModule_, &rack::CpuModule::message, this, [this](const QString& m) { statusBar()->showMessage(m, 5000); });
    connect(emptySlot_, &rack::EmptySlot::clicked, this, &RackWindow::showAddMenu);
    connect(editor_, &MainWindow::familyChanged, this, [this] {
        cpuModule_->setFamilyName(QString::fromLatin1(s5::mc5::familyName(editor_->family())));
    });
    connect(editor_, &MainWindow::showRackRequested, this, [this] {
        show();
        raise();
        activateWindow();
    });
    connect(plc_, &PlcController::runtimeError, this, [this](const QString& msg, int line) {
        if (!msg.isEmpty())
            statusBar()->showMessage(line > 0 ? tr("STOP – Zeile %1: %2").arg(line).arg(msg) : tr("STOP – %1").arg(msg));
    });

    // Menüs
    QMenu* rackMenu = menuBar()->addMenu(tr("&Baugruppe"));
    for (int bytes : {1, 2, 4})
        rackMenu->addAction(tr("Digitaleingabe DE %1 stecken").arg(bytes * 8), this,
                            [this, bytes] { addModuleOfKind(ModuleKind::DigitalIn, bytes); });
    rackMenu->addSeparator();
    for (int bytes : {1, 2, 4})
        rackMenu->addAction(tr("Digitalausgabe DA %1 stecken").arg(bytes * 8), this,
                            [this, bytes] { addModuleOfKind(ModuleKind::DigitalOut, bytes); });
    rackMenu->addSeparator();
    rackMenu->addAction(tr("&Beenden"), QKeySequence::Quit, this, &QWidget::close);

    QMenu* winMenu = menuBar()->addMenu(tr("&Fenster"));
    winMenu->addAction(tr("&Editor (PG)"), QKeySequence(Qt::CTRL | Qt::Key_E), this, &RackWindow::showEditor);

    cycleLabel_ = new QLabel;
    statusBar()->addPermanentWidget(cycleLabel_);
    statusBar()->showMessage(tr("Rechtsklick auf eine Baugruppe: Adresse, Taster, verschieben oder ziehen"), 8000);

    restoreModules();

    auto* timer = new QTimer(this);
    timer->setInterval(50);
    connect(timer, &QTimer::timeout, this, &RackWindow::refresh);
    timer->start();

    QSettings s;
    if (!restoreGeometry(s.value(QStringLiteral("rack/geometry")).toByteArray())) resize(980, 600);
}

void RackWindow::showEditor()
{
    editor_->show();
    editor_->raise();
    editor_->activateWindow();
}

void RackWindow::addModule(const ModuleConfig& cfg)
{
    auto* m = new DigitalModule(plc_, cfg);
    connect(m, &DigitalModule::contextRequested, this, [this, m](const QPoint& pos) { showModuleMenu(m, pos); });
    modules_.push_back(m);
    relayout();
}

int RackWindow::nextFreeAddress(ModuleKind kind) const
{
    int next = 0;
    for (const DigitalModule* m : modules_)
        if (m->config().kind == kind) next = std::max(next, m->config().address + m->config().bytes);
    return next;
}

void RackWindow::addModuleOfKind(ModuleKind kind, int bytes)
{
    ModuleConfig cfg;
    cfg.kind = kind;
    cfg.bytes = bytes;
    cfg.address = std::min(nextFreeAddress(kind), s5::Cpu::kIoBytes - bytes);
    addModule(cfg);
    saveModules();
}

void RackWindow::showAddMenu(const QPoint& globalPos)
{
    QMenu menu;
    for (int bytes : {1, 2, 4})
        menu.addAction(tr("Digitaleingabe DE %1").arg(bytes * 8), this,
                       [this, bytes] { addModuleOfKind(ModuleKind::DigitalIn, bytes); });
    menu.addSeparator();
    for (int bytes : {1, 2, 4})
        menu.addAction(tr("Digitalausgabe DA %1").arg(bytes * 8), this,
                       [this, bytes] { addModuleOfKind(ModuleKind::DigitalOut, bytes); });
    menu.exec(globalPos);
}

void RackWindow::showModuleMenu(DigitalModule* m, const QPoint& globalPos)
{
    const ModuleConfig cfg = m->config();
    const bool in = cfg.kind == ModuleKind::DigitalIn;
    QMenu menu;
    menu.addAction(tr("Anfangsadresse ändern …"), this, [this, m, in] {
        ModuleConfig c = m->config();
        bool ok = false;
        const int a = QInputDialog::getInt(this, tr("Anfangsadresse"),
                                           in ? tr("Erstes Eingangsbyte (EB):") : tr("Erstes Ausgangsbyte (AB):"),
                                           c.address, 0, s5::Cpu::kIoBytes - c.bytes, 1, &ok);
        if (!ok) return;
        c.address = a;
        m->setConfig(c);
        saveModules();
    });
    if (in) {
        QAction* t = menu.addAction(tr("Taster statt Schalter"));
        t->setCheckable(true);
        t->setChecked(cfg.momentary);
        connect(t, &QAction::toggled, this, [this, m](bool on) {
            ModuleConfig c = m->config();
            if (on)     // eingeschaltete Schalter lösen
                for (int b = 0; b < c.bytes; ++b)
                    for (int bit = 0; bit < 8; ++bit) plc_->cpu().setInput(c.address + b, bit, false);
            c.momentary = on;
            m->setConfig(c);
            saveModules();
        });
    }
    menu.addSeparator();
    menu.addAction(tr("Nach links"), this, [this, m] { moveModule(m, -1); });
    menu.addAction(tr("Nach rechts"), this, [this, m] { moveModule(m, +1); });
    menu.addSeparator();
    menu.addAction(tr("Baugruppe ziehen"), this, [this, m] { removeModule(m); });
    menu.exec(globalPos);
}

void RackWindow::removeModule(DigitalModule* m)
{
    const ModuleConfig c = m->config();
    if (c.kind == ModuleKind::DigitalIn)    // gezogene Eingabe liefert 0-Signal
        for (int b = 0; b < c.bytes; ++b)
            for (int bit = 0; bit < 8; ++bit) plc_->cpu().setInput(c.address + b, bit, false);
    modules_.erase(std::find(modules_.begin(), modules_.end(), m));
    m->deleteLater();
    relayout();
    saveModules();
}

void RackWindow::moveModule(DigitalModule* m, int delta)
{
    auto it = std::find(modules_.begin(), modules_.end(), m);
    const long idx = it - modules_.begin();
    const long target = idx + delta;
    if (target < 0 || target >= static_cast<long>(modules_.size())) return;
    std::swap(modules_[idx], modules_[target]);
    relayout();
    saveModules();
}

void RackWindow::relayout()
{
    // CPU auf Steckplatz 1, dann die Baugruppen, dann der freie Platz
    while (layout_->count() > 0) layout_->takeAt(0);
    layout_->addWidget(cpuModule_);
    cpuModule_->setSlot(1);
    int slot = 2;
    for (DigitalModule* m : modules_) {
        layout_->addWidget(m);
        m->setSlot(slot++);
    }
    layout_->addWidget(emptySlot_);
    layout_->addStretch();
}

void RackWindow::saveModules() const
{
    QStringList list;
    for (const DigitalModule* m : modules_) {
        const ModuleConfig& c = m->config();
        list << QStringLiteral("%1,%2,%3,%4")
                    .arg(c.kind == ModuleKind::DigitalIn ? QStringLiteral("DE") : QStringLiteral("DA"))
                    .arg(c.bytes)
                    .arg(c.address)
                    .arg(c.momentary ? 1 : 0);
    }
    QSettings().setValue(QStringLiteral("rack/modules"), list);
}

void RackWindow::restoreModules()
{
    QSettings s;
    QStringList list = s.value(QStringLiteral("rack/modules")).toStringList();
    if (!s.contains(QStringLiteral("rack/modules"))) list = {QStringLiteral("DE,2,0,0"), QStringLiteral("DA,2,0,0")};
    for (const QString& entry : list) {
        const QStringList f = entry.split(QLatin1Char(','));
        if (f.size() != 4) continue;
        ModuleConfig c;
        c.kind = f[0] == QLatin1String("DA") ? ModuleKind::DigitalOut : ModuleKind::DigitalIn;
        c.bytes = std::clamp(f[1].toInt(), 1, 4);
        if (c.bytes == 3) c.bytes = 4;
        c.address = std::clamp(f[2].toInt(), 0, s5::Cpu::kIoBytes - c.bytes);
        c.momentary = f[3] == QLatin1String("1");
        addModule(c);
    }
    relayout();
}

void RackWindow::refresh()
{
    cpuModule_->update();
    for (DigitalModule* m : modules_) m->update();
    const s5::Cpu& cpu = plc_->cpu();
    if (cpu.state() == s5::CpuState::Run)
        cycleLabel_->setText(tr("Zyklus %1   Bearbeitungszeit %2 ms").arg(cpu.cycleCount()).arg(plc_->cycleTimeMs(), 0, 'f', 3));
    else
        cycleLabel_->setText(cpu.state() == s5::CpuState::Halt ? tr("Einzelschritt") : QString());
}

void RackWindow::closeEvent(QCloseEvent* e)
{
    if (!editor_->maybeSave()) {
        e->ignore();
        return;
    }
    QSettings().setValue(QStringLiteral("rack/geometry"), saveGeometry());
    if (editor_->isVisible()) editor_->saveWindowState();
    saveModules();
    e->accept();
    QApplication::quit();
}
