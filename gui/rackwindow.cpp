#include "rackwindow.h"

#include "mainwindow.h"
#include "piconfigdialog.h"
#include "pilink.h"
#include "plccontroller.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QScrollArea>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>

#include <set>

using rack::DigitalModule;
using rack::ModuleConfig;
using rack::ModuleKind;
using rack::ModuleWidget;
using rack::PiModule;

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
    cpuModule_->setFamilyName(QString::fromLatin1(s5::mc5::familyName(plc_->family())));
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
    connect(cpuModule_, &rack::CpuModule::contextRequested, this, [this](const QPoint& pos) {
        QMenu menu;
        addCpuActions(&menu);
        menu.exec(pos);
    });
    connect(plc_, &PlcController::familyChanged, this, [this] {
        const QString name = QString::fromLatin1(s5::mc5::familyName(plc_->family()));
        cpuModule_->setFamilyName(name);
        const auto bad = s5::mc5::checkCpu(plc_->cpu().program(), plc_->family());
        statusBar()->showMessage(bad.empty()
                                     ? tr("CPU getauscht: %1 – CPU im STOP").arg(name)
                                     : tr("CPU getauscht: %1 – das Programm enthält %n Operation(en), die diese CPU "
                                          "nicht kennt (siehe Editor)", nullptr, static_cast<int>(bad.size())).arg(name),
                                 8000);
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

    connect(qApp, &QCoreApplication::aboutToQuit, this, &RackWindow::shutdownPeripherals);

    // Prüfung beim Übersetzen: Ein-/Ausgänge, die nicht am Raspberry Pi anliegen
    editor_->setHardwareCheck([this](const s5::Program& p) { return hardwareWarnings(p); });

    // Menüs
    QMenu* rackMenu = menuBar()->addMenu(tr("&Baugruppe"));
    addModuleActions(rackMenu);
    rackMenu->addSeparator();
    addCpuActions(rackMenu->addMenu(tr("&CPU tauschen")));
    rackMenu->addSeparator();
    rackMenu->addAction(tr("&Beenden"), QKeySequence::Quit, this, &QWidget::close);

    QMenu* winMenu = menuBar()->addMenu(tr("&Fenster"));
    winMenu->addAction(tr("&Editor (PG)"), QKeySequence(Qt::CTRL | Qt::Key_E), this, &RackWindow::showEditor);

    statusBar()->showMessage(tr("Rechtsklick auf die CPU: CPU tauschen · auf eine E/A-Baugruppe: Einstellungen, "
                                "verschieben, ziehen"), 8000);

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

// ---------------------------------------------------------------- Baugruppen stecken

void RackWindow::registerModule(ModuleWidget* m)
{
    connect(m, &ModuleWidget::contextRequested, this, [this, m](const QPoint& pos) { showModuleMenu(m, pos); });
    modules_.push_back(m);
    relayout();
}

void RackWindow::addDigital(const ModuleConfig& cfg)
{
    registerModule(new DigitalModule(plc_, cfg));
}

void RackWindow::addPi(const PiConfig& cfg)
{
    auto* m = new PiModule(plc_, cfg);
    connect(m, &PiModule::statusChanged, this, [this, m] {
        const PiLink* l = m->link();
        if (l->state() == PiLink::State::Online || l->state() == PiLink::State::Failed)
            statusBar()->showMessage(tr("Raspberry Pi %1: %2").arg(m->config().host, l->statusText()), 6000);
    });
    registerModule(m);
}

int RackWindow::nextFreeAddress(bool input) const
{
    int next = 0;
    for (const ModuleWidget* w : modules_) {
        if (auto* d = qobject_cast<const DigitalModule*>(w)) {
            if ((d->config().kind == ModuleKind::DigitalIn) == input)
                next = std::max(next, d->config().address + d->config().bytes);
        } else if (auto* p = qobject_cast<const PiModule*>(w)) {
            const PiConfig& c = p->config();
            if (input && c.inBytes) next = std::max(next, c.inAddress + c.inBytes);
            if (!input && c.outBytes) next = std::max(next, c.outAddress + c.outBytes);
        }
    }
    return next;
}

void RackWindow::addModuleOfKind(ModuleKind kind, int bytes)
{
    ModuleConfig cfg;
    cfg.kind = kind;
    cfg.bytes = bytes;
    cfg.address = std::min(nextFreeAddress(kind == ModuleKind::DigitalIn), s5::Cpu::kIoBytes - bytes);
    addDigital(cfg);
    saveModules();
    reportHardware();
}

void RackWindow::addPiInteractive()
{
    PiConfig cfg;
    cfg.inAddress = std::min(nextFreeAddress(true), s5::Cpu::kIoBytes - 1);
    cfg.outAddress = std::min(nextFreeAddress(false), s5::Cpu::kIoBytes - 1);
    PiConfigDialog dlg(cfg, this);
    if (dlg.exec() != QDialog::Accepted) return;
    addPi(dlg.config());
    saveModules();
    reportHardware();
}

void RackWindow::addModuleActions(QMenu* menu)
{
    for (int bytes : {1, 2, 4})
        menu->addAction(tr("Digitaleingabe DE %1 stecken").arg(bytes * 8), this,
                        [this, bytes] { addModuleOfKind(ModuleKind::DigitalIn, bytes); });
    menu->addSeparator();
    for (int bytes : {1, 2, 4})
        menu->addAction(tr("Digitalausgabe DA %1 stecken").arg(bytes * 8), this,
                        [this, bytes] { addModuleOfKind(ModuleKind::DigitalOut, bytes); });
    menu->addSeparator();
    menu->addAction(tr("Raspberry-Pi-E/A stecken …"), this, &RackWindow::addPiInteractive);
}

void RackWindow::showAddMenu(const QPoint& globalPos)
{
    QMenu menu;
    addModuleActions(&menu);
    menu.exec(globalPos);
}

void RackWindow::addCpuActions(QMenu* menu)
{
    // Menüeinträge werden bei jedem Öffnen neu gesetzt, damit das Häkchen stimmt
    auto fill = [this, menu] {
        menu->clear();
        auto* group = new QActionGroup(menu);
        for (auto f : PlcController::families()) {
            QAction* a = menu->addAction(tr("CPU %1").arg(QString::fromLatin1(s5::mc5::familyName(f))), this,
                                         [this, f] { plc_->setFamily(f); });
            a->setCheckable(true);
            a->setChecked(f == plc_->family());
            group->addAction(a);
        }
    };
    fill();
    connect(menu, &QMenu::aboutToShow, this, fill);
}

// ---------------------------------------------------------------- Kontextmenüs

void RackWindow::showModuleMenu(ModuleWidget* m, const QPoint& globalPos)
{
    QMenu menu;
    if (auto* d = qobject_cast<DigitalModule*>(m))
        fillDigitalMenu(d, menu, globalPos);
    else if (auto* p = qobject_cast<PiModule*>(m))
        fillPiMenu(p, menu);
    menu.addSeparator();
    menu.addAction(tr("Nach links"), this, [this, m] { moveModule(m, -1); });
    menu.addAction(tr("Nach rechts"), this, [this, m] { moveModule(m, +1); });
    menu.addSeparator();
    menu.addAction(tr("Baugruppe ziehen"), this, [this, m] { removeModule(m); });
    menu.exec(globalPos);
}

void RackWindow::fillDigitalMenu(DigitalModule* m, QMenu& menu, const QPoint& globalPos)
{
    const ModuleConfig cfg = m->config();
    const bool in = cfg.kind == ModuleKind::DigitalIn;
    if (in) {
        // Einzelner Eingang unter dem Mauszeiger
        const int idx = m->bitAt(m->mapFromGlobal(globalPos));
        if (idx >= 0) {
            const QString name = QStringLiteral("E %1.%2").arg(cfg.address + idx / 8).arg(idx % 8);
            const uint32_t bitMask = 1u << idx;
            auto* group = new QActionGroup(&menu);
            QAction* sw = menu.addAction(tr("%1 als Schalter").arg(name), this,
                                         [this, m, bitMask] { setMomentaryMask(m, m->config().momentaryMask & ~bitMask); });
            QAction* ta = menu.addAction(tr("%1 als Taster").arg(name), this,
                                         [this, m, bitMask] { setMomentaryMask(m, m->config().momentaryMask | bitMask); });
            for (QAction* a : {sw, ta}) {
                a->setCheckable(true);
                group->addAction(a);
            }
            (cfg.isMomentary(idx) ? ta : sw)->setChecked(true);
            menu.addSeparator();
        }
        menu.addAction(tr("Alle Eingänge als Schalter"), this, [this, m] { setMomentaryMask(m, 0); });
        menu.addAction(tr("Alle Eingänge als Taster"), this, [this, m] { setMomentaryMask(m, m->config().allBits()); });
        menu.addSeparator();
    }
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
        reportHardware();
    });
}

void RackWindow::fillPiMenu(PiModule* m, QMenu& menu)
{
    menu.addAction(tr("Einstellungen …"), this, [this, m] { editPi(m); });
    menu.addAction(tr("Verbindung neu aufbauen"), this, [m] { m->link()->reconnect(); });
}

void RackWindow::editPi(PiModule* m)
{
    PiConfigDialog dlg(m->config(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    m->setConfig(dlg.config());
    relayout();     // Breite kann sich geändert haben
    saveModules();
    reportHardware();
}

void RackWindow::setMomentaryMask(DigitalModule* m, uint32_t mask)
{
    ModuleConfig c = m->config();
    mask &= c.allBits();
    // Eingänge, die zum Taster werden, liefern 0, bis der Taster gedrückt wird
    const uint32_t becomeMomentary = mask & ~c.momentaryMask;
    for (int i = 0; i < c.bitCount(); ++i)
        if ((becomeMomentary >> i) & 1u) plc_->cpu().setInput(c.address + i / 8, i % 8, false);
    c.momentaryMask = mask;
    m->setConfig(c);
    saveModules();
}

void RackWindow::removeModule(ModuleWidget* m)
{
    if (auto* d = qobject_cast<DigitalModule*>(m)) {
        const ModuleConfig c = d->config();
        if (c.kind == ModuleKind::DigitalIn)    // gezogene Eingabe liefert 0-Signal
            for (int b = 0; b < c.bytes; ++b)
                for (int bit = 0; bit < 8; ++bit) plc_->cpu().setInput(c.address + b, bit, false);
    }
    // PiModule: der Destruktor meldet sich bei s5io ab (Ausgänge aus) und setzt die Eingänge auf 0
    modules_.erase(std::find(modules_.begin(), modules_.end(), m));
    m->deleteLater();
    relayout();
    saveModules();
    reportHardware();
}

void RackWindow::moveModule(ModuleWidget* m, int delta)
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
    for (ModuleWidget* m : modules_) {
        layout_->addWidget(m);
        m->setSlot(slot++);
    }
    layout_->addWidget(emptySlot_);
    layout_->addStretch();
}

// ---------------------------------------------------------------- Speichern

void RackWindow::saveModules() const
{
    QStringList list;
    for (const ModuleWidget* w : modules_) {
        if (auto* d = qobject_cast<const DigitalModule*>(w)) {
            const ModuleConfig& c = d->config();
            list << QStringLiteral("%1,%2,%3,m:%4")
                        .arg(c.kind == ModuleKind::DigitalIn ? QStringLiteral("DE") : QStringLiteral("DA"))
                        .arg(c.bytes)
                        .arg(c.address)
                        .arg(c.momentaryMask & c.allBits(), 8, 16, QLatin1Char('0'));
        } else if (auto* p = qobject_cast<const PiModule*>(w)) {
            list << p->config().encode();
        }
    }
    QSettings().setValue(QStringLiteral("rack/modules"), list);
}

void RackWindow::restoreModules()
{
    QSettings s;
    QStringList list = s.value(QStringLiteral("rack/modules")).toStringList();
    if (!s.contains(QStringLiteral("rack/modules"))) list = {QStringLiteral("DE,2,0,0"), QStringLiteral("DA,2,0,0")};
    for (const QString& entry : list) {
        if (entry.startsWith(QLatin1String("PI;"))) {
            PiConfig pc;
            if (PiConfig::decode(entry, pc)) addPi(pc);
            continue;
        }
        const QStringList f = entry.split(QLatin1Char(','));
        if (f.size() != 4) continue;
        ModuleConfig c;
        c.kind = f[0] == QLatin1String("DA") ? ModuleKind::DigitalOut : ModuleKind::DigitalIn;
        c.bytes = std::clamp(f[1].toInt(), 1, 4);
        if (c.bytes == 3) c.bytes = 4;
        c.address = std::clamp(f[2].toInt(), 0, s5::Cpu::kIoBytes - c.bytes);
        // 4. Feld: "m:<Hex>" = Taster je Eingang; ältere Einstellungen: "1" = alle Taster
        if (f[3].startsWith(QLatin1String("m:")))
            c.momentaryMask = f[3].mid(2).toUInt(nullptr, 16) & c.allBits();
        else
            c.momentaryMask = f[3] == QLatin1String("1") ? c.allBits() : 0;
        if (c.kind == ModuleKind::DigitalOut) c.momentaryMask = 0;
        addDigital(c);
    }
    relayout();
}

// ---------------------------------------------------------------- Zustand, Prüfung

void RackWindow::refresh()
{
    bool fault = false;
    for (ModuleWidget* m : modules_) {
        m->update();
        if (auto* p = qobject_cast<PiModule*>(m); p && !p->online()) fault = true;
    }
    cpuModule_->setPeripheryFault(fault);
    cpuModule_->update();
}

QStringList RackWindow::hardwareWarnings(const s5::Program& program) const
{
    std::vector<const PiModule*> pis;
    for (const ModuleWidget* w : modules_)
        if (auto* p = qobject_cast<const PiModule*>(w)) pis.push_back(p);
    if (pis.empty()) return {};

    // 0 = kein Pi zuständig, 1 = Pi zuständig, Bit nicht belegt, 2 = am Pi-Pin
    auto bitState = [&](bool input, int byte, int bit) {
        int best = 0;
        for (const PiModule* p : pis) {
            const PiConfig& c = p->config();
            const int start = input ? c.inAddress : c.outAddress;
            const int bytes = input ? c.inBytes : c.outBytes;
            if (byte < start || byte >= start + bytes) continue;
            const int ch = (byte - start) * 8 + bit;
            best = std::max(best, (input ? c.inGpio[ch] : c.outGpio[ch]) >= 0 ? 2 : 1);
        }
        return best;
    };

    std::set<s5::Operand> operands;
    for (const auto& [key, block] : program.blocks) {
        for (const auto& ins : block.code) {
            operands.insert(ins.operand);
            for (const auto& a : ins.args) operands.insert(a);
        }
    }
    QStringList screenOnly, unassigned;
    for (const s5::Operand& o : operands) {
        const QString name = QString::fromStdString(s5::toString(o));
        switch (o.kind) {
        case s5::OpKind::InputBit:
        case s5::OpKind::OutputBit: {
            const int st = bitState(o.kind == s5::OpKind::InputBit, o.address, o.bit);
            if (st == 0) screenOnly << name;
            if (st == 1) unassigned << name;
            break;
        }
        case s5::OpKind::InputByte:
        case s5::OpKind::OutputByte:
        case s5::OpKind::InputWord:
        case s5::OpKind::OutputWord: {
            const bool input = o.kind == s5::OpKind::InputByte || o.kind == s5::OpKind::InputWord;
            const bool word = o.kind == s5::OpKind::InputWord || o.kind == s5::OpKind::OutputWord;
            int best = 0;
            for (int b = 0; b < (word ? 2 : 1); ++b)
                for (int bit = 0; bit < 8; ++bit) best = std::max(best, bitState(input, o.address + b, bit));
            if (best == 0) screenOnly << name;
            break;
        }
        default: break;
        }
    }

    auto shortList = [](const QStringList& l) {
        return l.size() <= 12 ? l.join(QStringLiteral(", ")) : l.mid(0, 12).join(QStringLiteral(", ")) + QStringLiteral(", …");
    };
    QStringList warnings;
    if (!screenOnly.isEmpty())
        warnings << tr("Nur auf dem Bildschirm, nicht am Raspberry Pi: %1").arg(shortList(screenOnly));
    if (!unassigned.isEmpty())
        warnings << tr("Auf der Raspberry-Pi-Baugruppe nicht belegt: %1").arg(shortList(unassigned));
    return warnings;
}

void RackWindow::reportHardware()
{
    const QStringList w = hardwareWarnings(plc_->cpu().program());
    if (!w.isEmpty()) statusBar()->showMessage(w.join(QStringLiteral(" · ")), 10000);
}

void RackWindow::shutdownPeripherals()
{
    for (ModuleWidget* m : modules_)
        if (auto* p = qobject_cast<PiModule*>(m)) p->link()->stop();
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
    shutdownPeripherals();
    e->accept();
    QApplication::quit();
}
