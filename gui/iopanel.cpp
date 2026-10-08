#include "iopanel.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

Led::Led(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(sizeHint());
}

void Led::setOn(bool on)
{
    if (on == on_) return;
    on_ = on;
    update();
}

void Led::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r = QRectF(rect()).adjusted(4, 4, -4, -4);
    if (on_) {
        QRadialGradient g(r.center() - QPointF(3, 3), r.width() * 0.7);
        g.setColorAt(0, QColor(255, 190, 170));
        g.setColorAt(0.5, QColor(240, 60, 40));
        g.setColorAt(1, QColor(170, 20, 10));
        p.setBrush(g);
        p.setPen(QPen(QColor(120, 10, 0), 1));
    } else {
        p.setBrush(QColor(70, 30, 28));
        p.setPen(QPen(QColor(40, 20, 20), 1));
    }
    p.drawEllipse(r);
}

namespace {

QSpinBox* makeAddressBox(int value)
{
    auto* sb = new QSpinBox;
    sb->setRange(0, s5::Cpu::kIoBytes - 1);
    sb->setValue(value);
    sb->setFixedWidth(64);
    return sb;
}

QToolButton* makeRemoveButton()
{
    auto* b = new QToolButton;
    b->setText(QStringLiteral("×"));
    b->setAutoRaise(true);
    b->setToolTip(QObject::tr("Zeile entfernen"));
    return b;
}

}  // namespace

IoPanel::IoPanel(s5::Cpu* cpu, QWidget* parent)
    : QWidget(parent)
    , cpu_(cpu)
{
    auto* layout = new QVBoxLayout(this);

    auto* inBox = new QGroupBox(tr("Eingänge (Schalter / Taster)"));
    auto* inLayout = new QVBoxLayout(inBox);
    inputGrid_ = new QGridLayout;
    inputGrid_->setHorizontalSpacing(4);
    inLayout->addLayout(inputGrid_);
    auto* addIn = new QPushButton(tr("+ Eingangsbyte"));
    inLayout->addWidget(addIn, 0, Qt::AlignLeft);
    layout->addWidget(inBox);

    auto* outBox = new QGroupBox(tr("Ausgänge"));
    auto* outLayout = new QVBoxLayout(outBox);
    outputGrid_ = new QGridLayout;
    outputGrid_->setHorizontalSpacing(4);
    outLayout->addLayout(outputGrid_);
    auto* addOut = new QPushButton(tr("+ Ausgangsbyte"));
    outLayout->addWidget(addOut, 0, Qt::AlignLeft);
    layout->addWidget(outBox);

    auto* hint = new QLabel(tr("Klick schaltet einen Eingang um. Mit „Taster“ ist er nur "
                               "gedrückt, solange die Maustaste gehalten wird."));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    layout->addWidget(hint);
    layout->addStretch();

    // Kopfzeilen mit Bitnummern
    for (QGridLayout* grid : {inputGrid_, outputGrid_}) {
        for (int b = 0; b < 8; ++b) {
            auto* l = new QLabel(QStringLiteral(".%1").arg(b));
            l->setAlignment(Qt::AlignCenter);
            grid->addWidget(l, 0, 2 + b);
        }
    }

    connect(addIn, &QPushButton::clicked, this, [this] {
        addInputRow(inputs_.empty() ? 0 : inputs_.back().address->value() + 1);
        saveSettings();
    });
    connect(addOut, &QPushButton::clicked, this, [this] {
        addOutputRow(outputs_.empty() ? 0 : outputs_.back().address->value() + 1);
        saveSettings();
    });

    QSettings s;
    const QStringList ins = s.value(QStringLiteral("io/inputs"), QStringList{"0", "1"}).toStringList();
    const QStringList taster = s.value(QStringLiteral("io/momentary")).toStringList();
    const QStringList outs = s.value(QStringLiteral("io/outputs"), QStringList{"0", "1"}).toStringList();
    for (int i = 0; i < ins.size(); ++i) addInputRow(ins[i].toInt(), taster.value(i) == QLatin1String("1"));
    for (const QString& o : outs) addOutputRow(o.toInt());
}

void IoPanel::addInputRow(int byte, bool momentary)
{
    byte = std::clamp(byte, 0, s5::Cpu::kIoBytes - 1);
    InputRow row;
    row.address = makeAddressBox(byte);
    row.address->setPrefix(QStringLiteral("EB "));
    row.address->setFixedWidth(84);
    const int r = inputGrid_->rowCount();
    inputGrid_->addWidget(row.address, r, 0, 1, 2);

    for (int b = 0; b < 8; ++b) {
        auto* btn = new QToolButton;
        btn->setFixedSize(28, 28);
        btn->setCheckable(true);
        btn->setText(QString::number(b));
        btn->setStyleSheet(QStringLiteral(
            "QToolButton { border: 1px solid palette(mid); border-radius: 4px; background: palette(button); }"
            "QToolButton:checked, QToolButton:pressed { background: #2e7d32; color: white; border-color: #1b5e20; }"));
        inputGrid_->addWidget(btn, r, 2 + b);
        row.bits[b] = btn;

        QSpinBox* addr = row.address;
        connect(btn, &QToolButton::toggled, this, [this, addr, b](bool on) {
            cpu_->setInput(addr->value(), b, on);
        });
        connect(btn, &QToolButton::pressed, this, [this, addr, b, btn] {
            if (!btn->isCheckable()) cpu_->setInput(addr->value(), b, true);
        });
        connect(btn, &QToolButton::released, this, [this, addr, b, btn] {
            if (!btn->isCheckable()) cpu_->setInput(addr->value(), b, false);
        });
    }

    row.momentary = new QCheckBox(tr("Taster"));
    inputGrid_->addWidget(row.momentary, r, 10);
    auto* rm = makeRemoveButton();
    inputGrid_->addWidget(rm, r, 11);

    QSpinBox* key = row.address;
    connect(rm, &QToolButton::clicked, this, [this, key] { removeInputRow(key); });
    connect(row.momentary, &QCheckBox::toggled, this, [this, key](bool on) {
        for (auto& ir : inputs_)
            if (ir.address == key) setMomentary(ir, on);
        saveSettings();
    });
    connect(row.address, &QSpinBox::valueChanged, this, [this] {
        refresh();
        saveSettings();
    });

    inputs_.push_back(row);
    if (momentary) row.momentary->setChecked(true);
    refresh();
}

void IoPanel::setMomentary(InputRow& row, bool on)
{
    for (QToolButton* b : row.bits) {
        if (b->isChecked()) b->setChecked(false);   // setzt den Eingang zurück
        b->setCheckable(!on);
    }
}

void IoPanel::addOutputRow(int byte)
{
    byte = std::clamp(byte, 0, s5::Cpu::kIoBytes - 1);
    OutputRow row;
    row.address = makeAddressBox(byte);
    row.address->setPrefix(QStringLiteral("AB "));
    row.address->setFixedWidth(84);
    const int r = outputGrid_->rowCount();
    outputGrid_->addWidget(row.address, r, 0, 1, 2);
    for (int b = 0; b < 8; ++b) {
        row.leds[b] = new Led;
        outputGrid_->addWidget(row.leds[b], r, 2 + b, Qt::AlignCenter);
    }
    auto* rm = makeRemoveButton();
    outputGrid_->addWidget(rm, r, 10);
    QSpinBox* key = row.address;
    connect(rm, &QToolButton::clicked, this, [this, key] { removeOutputRow(key); });
    connect(row.address, &QSpinBox::valueChanged, this, [this] {
        refresh();
        saveSettings();
    });
    outputs_.push_back(row);
    refresh();
}

namespace {

// Entfernt alle Widgets einer Grid-Zeile, in der 'anchor' steht
void removeGridRow(QGridLayout* grid, QWidget* anchor)
{
    int idx = grid->indexOf(anchor);
    if (idx < 0) return;
    int row, col, rs, cs;
    grid->getItemPosition(idx, &row, &col, &rs, &cs);
    for (int c = 0; c < grid->columnCount(); ++c) {
        if (QLayoutItem* item = grid->itemAtPosition(row, c)) {
            if (QWidget* w = item->widget()) {
                grid->removeWidget(w);
                w->deleteLater();
            }
        }
    }
}

}  // namespace

void IoPanel::removeInputRow(QSpinBox* key)
{
    for (auto it = inputs_.begin(); it != inputs_.end(); ++it) {
        if (it->address != key) continue;
        for (QToolButton* b : it->bits)
            if (b->isChecked()) b->setChecked(false);
        removeGridRow(inputGrid_, key);
        inputs_.erase(it);
        break;
    }
    saveSettings();
}

void IoPanel::removeOutputRow(QSpinBox* key)
{
    for (auto it = outputs_.begin(); it != outputs_.end(); ++it) {
        if (it->address != key) continue;
        removeGridRow(outputGrid_, key);
        outputs_.erase(it);
        break;
    }
    saveSettings();
}

void IoPanel::refresh()
{
    for (auto& row : inputs_) {
        const int byte = row.address->value();
        for (int b = 0; b < 8; ++b) {
            QToolButton* btn = row.bits[b];
            btn->setToolTip(QStringLiteral("E %1.%2").arg(byte).arg(b));
            if (btn->isCheckable() && btn->isChecked() != cpu_->input(byte, b)) {
                QSignalBlocker block(btn);
                btn->setChecked(cpu_->input(byte, b));
            }
        }
    }
    for (auto& row : outputs_) {
        const int byte = row.address->value();
        const uint8_t v = cpu_->outputByte(byte);
        for (int b = 0; b < 8; ++b) {
            row.leds[b]->setOn((v >> b) & 1);
            row.leds[b]->setToolTip(QStringLiteral("A %1.%2").arg(byte).arg(b));
        }
    }
}

void IoPanel::saveSettings() const
{
    QStringList ins, taster, outs;
    for (const auto& r : inputs_) {
        ins << QString::number(r.address->value());
        taster << (r.momentary->isChecked() ? QStringLiteral("1") : QStringLiteral("0"));
    }
    for (const auto& r : outputs_) outs << QString::number(r.address->value());
    QSettings s;
    s.setValue(QStringLiteral("io/inputs"), ins);
    s.setValue(QStringLiteral("io/momentary"), taster);
    s.setValue(QStringLiteral("io/outputs"), outs);
}
