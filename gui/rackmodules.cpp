#include "rackmodules.h"

#include "plccontroller.h"

#include <QContextMenuEvent>
#include <QDateTime>
#include <QHelpEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QToolTip>

namespace rack {

namespace {

// Farben der Hardware (unabhängig vom hellen/dunklen Systemdesign)
const QColor kFrontTop(0x50, 0x54, 0x59);
const QColor kFrontBottom(0x36, 0x39, 0x3d);
const QColor kFrontEdge(0x1c, 0x1e, 0x20);
const QColor kPaper(0xec, 0xe6, 0xd3);
const QColor kPaperText(0x2a, 0x2a, 0x2a);
const QColor kPrint(0xe8, 0xe8, 0xe8);
const QColor kPrintDim(0xa8, 0xad, 0xb3);
const QColor kPetrol(0x00, 0x99, 0x99);     // SIMATIC-Akzent

constexpr int kColumnWidth = 84;
constexpr int kRowsTop = 96;
constexpr int kRowHeight = 40;

void drawLed(QPainter& p, const QPointF& c, qreal r, bool on, const QColor& color)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0x10, 0x10, 0x10), 1.2));
    if (on) {
        // Lichthof
        QRadialGradient halo(c, r * 2.6);
        QColor h = color;
        h.setAlpha(110);
        halo.setColorAt(0, h);
        halo.setColorAt(1, Qt::transparent);
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(c, r * 2.6, r * 2.6);
        QRadialGradient g(c - QPointF(r * 0.35, r * 0.35), r * 1.3);
        g.setColorAt(0, color.lighter(185));
        g.setColorAt(0.45, color);
        g.setColorAt(1, color.darker(160));
        p.setBrush(g);
        p.setPen(QPen(QColor(0x10, 0x10, 0x10), 1.2));
    } else {
        QRadialGradient g(c - QPointF(r * 0.35, r * 0.35), r * 1.3);
        g.setColorAt(0, color.darker(260));
        g.setColorAt(1, color.darker(520));
        p.setBrush(g);
    }
    p.drawEllipse(c, r, r);
    p.restore();
}

// Kippschalter: up = Hebel oben
void drawToggle(QPainter& p, const QRectF& r, bool up)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    // Sockel
    QLinearGradient base(r.topLeft(), r.bottomLeft());
    base.setColorAt(0, QColor(0x2a, 0x2c, 0x2e));
    base.setColorAt(1, QColor(0x14, 0x15, 0x16));
    p.setPen(QPen(QColor(0x08, 0x08, 0x08), 1));
    p.setBrush(base);
    p.drawRoundedRect(r, 3, 3);
    // Mutter
    const QPointF c = r.center();
    const qreal nut = std::min(r.width(), r.height()) * 0.24;
    QRadialGradient ng(c - QPointF(1, 1), nut);
    ng.setColorAt(0, QColor(0xe0, 0xe2, 0xe4));
    ng.setColorAt(1, QColor(0x70, 0x74, 0x78));
    p.setBrush(ng);
    p.drawEllipse(c, nut, nut);
    // Hebel
    const qreal len = r.height() * 0.40;
    const QPointF tip = c + QPointF(0, up ? -len : len);
    QPen lever(QColor(0xb8, 0xbc, 0xc0), std::max<qreal>(3.0, r.width() * 0.16), Qt::SolidLine, Qt::RoundCap);
    p.setPen(lever);
    p.drawLine(c, tip);
    const qreal knob = std::max<qreal>(3.0, r.width() * 0.15);
    QRadialGradient kg(tip - QPointF(1, 1), knob * 1.2);
    kg.setColorAt(0, Qt::white);
    kg.setColorAt(1, QColor(0x90, 0x94, 0x98));
    p.setPen(QPen(QColor(0x40, 0x40, 0x40), 0.8));
    p.setBrush(kg);
    p.drawEllipse(tip, knob, knob);
    p.restore();
}

void drawPushButton(QPainter& p, const QRectF& r, bool down, const QColor& cap)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0x08, 0x08, 0x08), 1));
    p.setBrush(QColor(0x1a, 0x1b, 0x1c));
    p.drawEllipse(r);
    const QRectF in = r.adjusted(3, 3, -3, -3);
    QRadialGradient g(in.center() - QPointF(in.width() * 0.2, in.height() * 0.2), in.width() * 0.8);
    g.setColorAt(0, down ? cap.darker(130) : cap.lighter(140));
    g.setColorAt(1, down ? cap.darker(200) : cap.darker(120));
    p.setBrush(g);
    p.drawEllipse(down ? in.adjusted(1, 1, -1, -1) : in);
    p.restore();
}

void drawScrew(QPainter& p, const QPointF& c)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QRadialGradient g(c - QPointF(1.5, 1.5), 6);
    g.setColorAt(0, QColor(0xd8, 0xda, 0xdc));
    g.setColorAt(1, QColor(0x6a, 0x6e, 0x72));
    p.setPen(QPen(QColor(0x20, 0x20, 0x20), 0.8));
    p.setBrush(g);
    p.drawEllipse(c, 5, 5);
    p.setPen(QPen(QColor(0x3a, 0x3a, 0x3a), 1.4));
    p.drawLine(c + QPointF(-3, -1), c + QPointF(3, 1));
    p.restore();
}

}  // namespace

// ------------------------------------------------------------------ ModuleWidget

ModuleWidget::ModuleWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void ModuleWidget::setSlot(int slot)
{
    slot_ = slot;
    update();
}

void ModuleWidget::contextMenuEvent(QContextMenuEvent* e)
{
    emit contextRequested(e->globalPos());
}

void ModuleWidget::paintFrame(QPainter& p, const QString& title, const QString& subtitle) const
{
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    g.setColorAt(0, kFrontTop);
    g.setColorAt(1, kFrontBottom);
    p.setPen(QPen(kFrontEdge, 1.5));
    p.setBrush(g);
    p.drawRoundedRect(r, 4, 4);
    // Lichtkante links
    p.setPen(QPen(QColor(255, 255, 255, 40), 1));
    p.drawLine(r.topLeft() + QPointF(2, 6), r.bottomLeft() + QPointF(2, -6));

    drawScrew(p, QPointF(r.center().x(), 12));
    drawScrew(p, QPointF(r.center().x(), r.bottom() - 11));

    QFont f = font();
    f.setBold(true);
    f.setPointSizeF(13);
    p.setFont(f);
    p.setPen(kPrint);
    p.drawText(QRectF(0, 22, width(), 22), Qt::AlignCenter, title);
    f.setBold(false);
    f.setPointSizeF(9.5);
    p.setFont(f);
    p.setPen(kPrintDim);
    p.drawText(QRectF(0, 43, width(), 16), Qt::AlignCenter, subtitle);

    // Steckplatz
    p.setPen(kPrintDim);
    p.drawText(QRectF(0, height() - 40, width(), 14), Qt::AlignCenter, tr("Steckplatz %1").arg(slot_));
}

// ------------------------------------------------------------------ CPU

CpuModule::CpuModule(PlcController* plc, QWidget* parent)
    : ModuleWidget(parent)
    , plc_(plc)
{
    setFixedSize(200, kHeight);
    setToolTip(tr("Betriebsartenschalter RUN/STOP, Urlöschen (nur im STOP), PG-Taster öffnet den Editor"));
}

void CpuModule::setFamilyName(const QString& name)
{
    family_ = name;
    update();
}

QRect CpuModule::modeSwitchRect() const { return QRect(40, 222, 34, 62); }
QRect CpuModule::resetSwitchRect() const { return QRect(126, 222, 34, 62); }
QRect CpuModule::pgButtonRect() const { return QRect(118, 352, 44, 44); }

void CpuModule::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    paintFrame(p, QStringLiteral("CPU"), family_);

    const s5::Cpu& cpu = plc_->cpu();
    const s5::CpuState st = cpu.state();
    const bool blink = (QDateTime::currentMSecsSinceEpoch() / 400) % 2 == 0;
    const bool fault = st == s5::CpuState::Stop && !cpu.errorMessage().empty();

    // SIMATIC-Schriftzug
    QFont f = font();
    f.setBold(true);
    f.setPointSizeF(10);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
    p.setFont(f);
    p.setPen(kPetrol);
    p.drawText(QRectF(0, 64, width(), 16), Qt::AlignCenter, QStringLiteral("SIMATIC S5"));

    // Anzeigen
    struct Lamp {
        const char* text;
        bool on;
        QColor color;
    };
    const Lamp lamps[] = {
        {"RUN", st == s5::CpuState::Run || (st == s5::CpuState::Halt && blink), QColor(0x3c, 0xd0, 0x3c)},
        {"STOP", st == s5::CpuState::Stop, QColor(0xf0, 0x3a, 0x2a)},
        {"STÖRUNG", fault && blink, QColor(0xf0, 0x3a, 0x2a)},
    };
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    f.setPointSizeF(10);
    p.setFont(f);
    for (int i = 0; i < 3; ++i) {
        const int y = 104 + i * 30;
        drawLed(p, QPointF(48, y), 7, lamps[i].on, lamps[i].color);
        p.setPen(kPrint);
        p.drawText(QRectF(66, y - 9, 120, 18), Qt::AlignVCenter | Qt::AlignLeft, QString::fromUtf8(lamps[i].text));
    }

    // Betriebsartenschalter
    const QRect ms = modeSwitchRect();
    p.setPen(kPrint);
    f.setPointSizeF(9.5);
    p.setFont(f);
    p.drawText(QRect(ms.left() - 20, ms.top() - 22, ms.width() + 40, 16), Qt::AlignCenter, QStringLiteral("RUN"));
    p.drawText(QRect(ms.left() - 20, ms.bottom() + 6, ms.width() + 40, 16), Qt::AlignCenter, QStringLiteral("STOP"));
    drawToggle(p, ms, st != s5::CpuState::Stop);

    // Urlöschen (Taster-Schalter, federt zurück)
    const QRect rs = resetSwitchRect();
    p.setPen(kPrint);
    p.drawText(QRect(rs.left() - 24, rs.bottom() + 6, rs.width() + 48, 16), Qt::AlignCenter, QStringLiteral("RESET"));
    drawToggle(p, rs, !resetDown_);
    f.setBold(false);
    f.setPointSizeF(8.5);
    p.setFont(f);
    p.setPen(kPrintDim);
    p.drawText(QRect(rs.left() - 24, rs.bottom() + 22, rs.width() + 48, 14), Qt::AlignCenter, tr("Urlöschen"));

    // PG-Schnittstelle (15-polig) und Taster zum Editor
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF dsub(30, 356, 62, 34);
    QPainterPath shell;
    shell.moveTo(dsub.left(), dsub.top());
    shell.lineTo(dsub.right(), dsub.top());
    shell.lineTo(dsub.right() - 6, dsub.bottom());
    shell.lineTo(dsub.left() + 6, dsub.bottom());
    shell.closeSubpath();
    p.setPen(QPen(QColor(0x9a, 0x9e, 0xa2), 2));
    p.setBrush(QColor(0x16, 0x17, 0x18));
    p.drawPath(shell);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xc8, 0xa8, 0x50));
    for (int i = 0; i < 8; ++i) p.drawEllipse(QPointF(dsub.left() + 9 + i * 6.3, dsub.top() + 11), 1.6, 1.6);
    for (int i = 0; i < 7; ++i) p.drawEllipse(QPointF(dsub.left() + 12 + i * 6.3, dsub.top() + 23), 1.6, 1.6);
    f.setBold(true);
    f.setPointSizeF(9.5);
    p.setFont(f);
    p.setPen(kPrint);
    p.drawText(QRectF(dsub.left(), dsub.top() - 20, dsub.width(), 16), Qt::AlignCenter, QStringLiteral("PG"));

    const QRect pg = pgButtonRect();
    drawPushButton(p, pg, pgDown_, kPetrol);
    p.drawText(QRect(pg.left() - 20, pg.top() - 20, pg.width() + 40, 16), Qt::AlignCenter, tr("Editor"));
}

void CpuModule::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    const QPoint pos = e->position().toPoint();
    if (modeSwitchRect().adjusted(-10, -24, 10, 24).contains(pos)) {
        if (plc_->cpu().state() == s5::CpuState::Stop) {
            if (!plc_->run() && plc_->cpu().errorMessage().empty())
                emit message(tr("Kein Neuanlauf – das Programm enthält Fehler (PG-Taster öffnet den Editor)"));
        } else
            plc_->stop();
    } else if (resetSwitchRect().adjusted(-10, -24, 10, 24).contains(pos)) {
        resetDown_ = true;
        if (plc_->cpu().state() == s5::CpuState::Stop) {
            plc_->reset();
            emit message(tr("Urlöschen: Merker, Zeiten, Zähler und Datenbausteine gelöscht"));
        } else {
            emit message(tr("Urlöschen ist nur im STOP möglich – erst den Betriebsartenschalter auf STOP"));
        }
    } else if (pgButtonRect().contains(pos)) {
        pgDown_ = true;
    }
    update();
}

void CpuModule::mouseReleaseEvent(QMouseEvent* e)
{
    if (pgDown_ && pgButtonRect().contains(e->position().toPoint())) emit editorRequested();
    resetDown_ = false;
    pgDown_ = false;
    update();
}

// ------------------------------------------------------------------ Digitalbaugruppen

DigitalModule::DigitalModule(PlcController* plc, const ModuleConfig& cfg, QWidget* parent)
    : ModuleWidget(parent)
    , plc_(plc)
{
    setConfig(cfg);
}

int DigitalModule::widthFor(int bytes) { return 24 + bytes * kColumnWidth; }

void DigitalModule::setConfig(const ModuleConfig& cfg)
{
    cfg_ = cfg;
    setFixedSize(widthFor(cfg_.bytes), kHeight);
    update();
}

QRect DigitalModule::rowRect(int byteIndex, int bit) const
{
    return QRect(12 + byteIndex * kColumnWidth, kRowsTop + bit * kRowHeight, kColumnWidth - 8, kRowHeight);
}

int DigitalModule::bitAt(const QPoint& pos) const
{
    for (int b = 0; b < cfg_.bytes; ++b)
        for (int bit = 0; bit < 8; ++bit)
            if (rowRect(b, bit).contains(pos)) return b * 8 + bit;
    return -1;
}

void DigitalModule::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const bool in = cfg_.kind == ModuleKind::DigitalIn;
    const QString title = QStringLiteral("%1 %2").arg(in ? QStringLiteral("DE") : QStringLiteral("DA")).arg(cfg_.bytes * 8);
    QString subtitle = tr("DC 24 V / 0,5 A");
    if (in) {
        const uint32_t mask = cfg_.momentaryMask & cfg_.allBits();
        subtitle = mask == 0 ? tr("DC 24 V · Schalter")
                 : mask == cfg_.allBits() ? tr("DC 24 V · Taster")
                                          : tr("DC 24 V · Schalter/Taster");
    }
    paintFrame(p, title, subtitle);

    const s5::Cpu& cpu = plc_->cpu();
    const QColor ledColor = in ? QColor(0x3c, 0xd0, 0x3c) : QColor(0xf0, 0x3a, 0x2a);  // Eingänge grün, Ausgänge rot
    QFont f = font();
    for (int b = 0; b < cfg_.bytes; ++b) {
        const int byte = cfg_.address + b;
        if (byte >= s5::Cpu::kIoBytes) break;
        const QRect col(12 + b * kColumnWidth, kRowsTop - 26, kColumnWidth - 8, 8 * kRowHeight + 32);

        // Beschriftungsstreifen
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(0x9a, 0x94, 0x80), 1));
        p.setBrush(kPaper);
        p.drawRoundedRect(QRectF(col).adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);

        f.setBold(true);
        f.setPointSizeF(10.5);
        p.setFont(f);
        p.setPen(kPaperText);
        p.drawText(QRect(col.left(), col.top() + 3, col.width(), 20), Qt::AlignCenter,
                   QStringLiteral("%1 %2").arg(in ? QStringLiteral("EB") : QStringLiteral("AB")).arg(byte));
        p.setPen(QColor(0x9a, 0x94, 0x80));
        p.drawLine(col.left() + 6, col.top() + 24, col.right() - 6, col.top() + 24);

        const uint8_t value = in ? cpu.inputByte(byte) : cpu.outputByte(byte);
        f.setBold(false);
        f.setPointSizeF(10);
        p.setFont(f);
        for (int bit = 0; bit < 8; ++bit) {
            const QRect r = rowRect(b, bit);
            const bool on = (value >> bit) & 1;
            drawLed(p, QPointF(r.left() + 13, r.center().y()), 6.5, on, ledColor);
            p.setPen(kPaperText);
            p.drawText(QRect(r.left() + 25, r.top(), 32, r.height()), Qt::AlignVCenter | Qt::AlignLeft,
                       QStringLiteral("%1.%2").arg(byte).arg(bit));
            if (in) {
                const QRectF sw(r.right() - 22, r.top() + 5, 18, r.height() - 10);
                if (cfg_.isMomentary(b * 8 + bit))
                    drawPushButton(p, QRectF(r.right() - 24, r.center().y() - 11, 22, 22), pressed_ == b * 8 + bit,
                                   QColor(0x9a, 0x9e, 0xa2));     // neutrale Kappe, nicht mit LED verwechselbar
                else
                    drawToggle(p, sw, on);
            }
        }
    }
}

bool DigitalModule::event(QEvent* e)
{
    if (e->type() == QEvent::ToolTip) {
        auto* he = static_cast<QHelpEvent*>(e);
        const int hit = bitAt(he->pos());
        if (hit < 0) {
            QToolTip::hideText();
            e->ignore();
            return true;
        }
        const bool in = cfg_.kind == ModuleKind::DigitalIn;
        QString text = QStringLiteral("%1 %2.%3").arg(in ? QStringLiteral("E") : QStringLiteral("A"))
                           .arg(cfg_.address + hit / 8).arg(hit % 8);
        if (in) text += cfg_.isMomentary(hit) ? tr(" – Taster") : tr(" – Schalter");
        QToolTip::showText(he->globalPos(), text, this);
        return true;
    }
    return ModuleWidget::event(e);
}

void DigitalModule::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || cfg_.kind != ModuleKind::DigitalIn) return;
    const int hit = bitAt(e->position().toPoint());
    if (hit < 0) return;
    const int byte = cfg_.address + hit / 8, bit = hit % 8;
    s5::Cpu& cpu = plc_->cpu();
    if (cfg_.isMomentary(hit)) {
        pressed_ = hit;
        cpu.setInput(byte, bit, true);
    } else {
        cpu.setInput(byte, bit, !cpu.input(byte, bit));
    }
    update();
}

void DigitalModule::mouseReleaseEvent(QMouseEvent*)
{
    if (pressed_ >= 0) {
        plc_->cpu().setInput(cfg_.address + pressed_ / 8, pressed_ % 8, false);
        pressed_ = -1;
        update();
    }
}

// ------------------------------------------------------------------ Leerplatz, Träger

EmptySlot::EmptySlot(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(108, ModuleWidget::kHeight);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Baugruppe stecken"));
}

void EmptySlot::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0x80, 0x84, 0x88), 1.5, Qt::DashLine));
    p.setBrush(QColor(0, 0, 0, 40));
    p.drawRoundedRect(QRectF(rect()).adjusted(2, 2, -2, -2), 4, 4);
    QFont f = font();
    f.setPointSizeF(28);
    p.setFont(f);
    p.setPen(QColor(0xa0, 0xa4, 0xa8));
    p.drawText(QRect(0, 0, width(), height() - 30), Qt::AlignCenter, QStringLiteral("+"));
    f.setPointSizeF(10);
    p.setFont(f);
    p.drawText(QRect(0, height() / 2 + 10, width(), 40), Qt::AlignHCenter | Qt::AlignTop, tr("Baugruppe\nstecken"));
}

void EmptySlot::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) emit clicked(e->globalPosition().toPoint());
}

RackWidget::RackWidget(QWidget* parent)
    : QWidget(parent)
{
}

void RackWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x26, 0x28, 0x2b));
    // Profilschienen oben und unten
    for (int y : {14, height() - 30}) {
        QLinearGradient g(0, y, 0, y + 16);
        g.setColorAt(0, QColor(0xc8, 0xcc, 0xd0));
        g.setColorAt(0.5, QColor(0x8c, 0x90, 0x94));
        g.setColorAt(1, QColor(0x5a, 0x5e, 0x62));
        p.fillRect(QRect(0, y, width(), 16), g);
        p.setPen(QColor(0x40, 0x42, 0x44));
        for (int x = 20; x < width(); x += 60) p.drawRect(x, y + 5, 14, 6);
    }
}

}  // namespace rack
