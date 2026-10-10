#include "piconfigdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <map>

namespace {

enum InColumn { InOperand, InGpio, InBias, InInvert };
enum OutColumn { OutOperand, OutGpio };

QComboBox* bytesCombo(int value)
{
    auto* c = new QComboBox;
    c->addItem(QObject::tr("keine"), 0);
    c->addItem(QObject::tr("1 Byte (8 Bit)"), 1);
    c->addItem(QObject::tr("2 Byte (16 Bit)"), 2);
    c->setCurrentIndex(value);
    return c;
}

QTableWidget* makeTable(const QStringList& headers)
{
    auto* t = new QTableWidget;
    t->setColumnCount(headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->hide();
    t->horizontalHeader()->setStretchLastSection(true);
    t->setSelectionMode(QAbstractItemView::NoSelection);
    t->setMinimumHeight(220);
    return t;
}

}  // namespace

PiConfigDialog::PiConfigDialog(const PiConfig& cfg, QWidget* parent)
    : QDialog(parent)
    , cfg_(cfg)
{
    setWindowTitle(tr("Raspberry-Pi-E/A – Einstellungen"));

    // Verbindung
    host_ = new QLineEdit(cfg_.host);
    host_->setPlaceholderText(QStringLiteral("raspberrypi.local, 192.168.1.20 oder 127.0.0.1"));
    port_ = new QSpinBox;
    port_->setRange(1, 65535);
    port_->setValue(cfg_.port);
    auto* conn = new QGroupBox(tr("Verbindung zu s5io"));
    auto* connForm = new QFormLayout(conn);
    connForm->addRow(tr("Rechner:"), host_);
    connForm->addRow(tr("Port:"), port_);

    // Adressen
    inAddress_ = new QSpinBox;
    inAddress_->setRange(0, 127);
    inAddress_->setPrefix(QStringLiteral("EB "));
    inAddress_->setValue(cfg_.inAddress);
    inBytes_ = bytesCombo(cfg_.inBytes);
    outAddress_ = new QSpinBox;
    outAddress_->setRange(0, 127);
    outAddress_->setPrefix(QStringLiteral("AB "));
    outAddress_->setValue(cfg_.outAddress);
    outBytes_ = bytesCombo(cfg_.outBytes);
    debounce_ = new QSpinBox;
    debounce_->setRange(0, 1000);
    debounce_->setSuffix(QStringLiteral(" ms"));
    debounce_->setValue(cfg_.debounceMs);
    auto* addr = new QGroupBox(tr("Adressen"));
    auto* addrForm = new QFormLayout(addr);
    auto* inRow = new QHBoxLayout;
    inRow->addWidget(inAddress_);
    inRow->addWidget(inBytes_);
    auto* outRow = new QHBoxLayout;
    outRow->addWidget(outAddress_);
    outRow->addWidget(outBytes_);
    addrForm->addRow(tr("Eingänge ab:"), inRow);
    addrForm->addRow(tr("Ausgänge ab:"), outRow);
    addrForm->addRow(tr("Entprellzeit Eingänge:"), debounce_);

    // Pinbelegung
    inTable_ = makeTable({tr("Eingang"), tr("GPIO"), tr("Pull-Widerstand"), tr("invertiert")});
    outTable_ = makeTable({tr("Ausgang"), tr("GPIO")});
    auto* tables = new QHBoxLayout;
    tables->addWidget(inTable_, 3);
    tables->addWidget(outTable_, 2);
    auto* defaults = new QPushButton(tr("Standardbelegung"));
    defaults->setToolTip(tr("Eingänge GPIO 5, 6, 12, 13, 16, 19, 20, 21 – Ausgänge GPIO 17, 18, 27, 22, 23, 24, 25, 26"));

    message_ = new QLabel;
    message_->setWordWrap(true);
    auto* hint = new QLabel(tr("Die GPIO-Pins vertragen nur 3,3 V. 24-V-Signale nur über Optokoppler anschließen "
                               "(siehe docs/raspi). Taster gegen Masse: Pull-up wählen und „invertiert“ ankreuzen."));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* top = new QHBoxLayout;
    top->addWidget(conn);
    top->addWidget(addr);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addLayout(tables);
    layout->addWidget(defaults, 0, Qt::AlignLeft);
    layout->addWidget(message_);
    layout->addWidget(hint);
    layout->addWidget(buttons_);
    resize(820, 640);

    auto changed = [this] {
        if (updating_) return;
        readWidgets();
        rebuildTables();
    };
    connect(inAddress_, &QSpinBox::valueChanged, this, changed);
    connect(outAddress_, &QSpinBox::valueChanged, this, changed);
    connect(inBytes_, &QComboBox::currentIndexChanged, this, changed);
    connect(outBytes_, &QComboBox::currentIndexChanged, this, changed);
    connect(host_, &QLineEdit::textChanged, this, [this] { readWidgets(); validate(); });
    connect(port_, &QSpinBox::valueChanged, this, [this] { readWidgets(); validate(); });
    connect(debounce_, &QSpinBox::valueChanged, this, [this] { readWidgets(); });
    connect(defaults, &QPushButton::clicked, this, [this] {
        readWidgets();
        cfg_.applyDefaultPins();
        rebuildTables();
    });

    rebuildTables();
}

QComboBox* PiConfigDialog::gpioCombo(int gpio)
{
    auto* c = new QComboBox;
    c->addItem(tr("— nicht belegt"), -1);
    for (int g : s5io::headerGpios()) {
        QString text = tr("GPIO %1 · Pin %2").arg(g).arg(s5io::physicalPin(g));
        if (const char* f = s5io::specialFunction(g)) text += QStringLiteral(" · ") + QString::fromUtf8(f);
        c->addItem(text, g);
    }
    c->setCurrentIndex(std::max(0, c->findData(gpio)));
    connect(c, &QComboBox::currentIndexChanged, this, [this] {
        if (updating_) return;
        readWidgets();
        validate();
    });
    return c;
}

void PiConfigDialog::rebuildTables()
{
    updating_ = true;
    inTable_->setRowCount(cfg_.inBytes * 8);
    for (int ch = 0; ch < cfg_.inBytes * 8; ++ch) {
        auto* name = new QTableWidgetItem(cfg_.inputName(ch));
        name->setFlags(Qt::ItemIsEnabled);
        inTable_->setItem(ch, InOperand, name);
        inTable_->setCellWidget(ch, InGpio, gpioCombo(cfg_.inGpio[ch]));
        auto* bias = new QComboBox;
        bias->addItem(tr("keiner"), static_cast<int>(s5io::Bias::None));
        bias->addItem(tr("Pull-up"), static_cast<int>(s5io::Bias::PullUp));
        bias->addItem(tr("Pull-down"), static_cast<int>(s5io::Bias::PullDown));
        bias->setCurrentIndex(bias->findData(static_cast<int>(cfg_.inBias[ch])));
        connect(bias, &QComboBox::currentIndexChanged, this, [this] {
            if (!updating_) readWidgets();
        });
        inTable_->setCellWidget(ch, InBias, bias);
        auto* holder = new QWidget;
        auto* box = new QCheckBox;
        box->setChecked((cfg_.inInvert >> ch) & 1u);
        auto* hl = new QHBoxLayout(holder);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->addWidget(box, 0, Qt::AlignCenter);
        connect(box, &QCheckBox::toggled, this, [this] {
            if (!updating_) readWidgets();
        });
        inTable_->setCellWidget(ch, InInvert, holder);
    }
    outTable_->setRowCount(cfg_.outBytes * 8);
    for (int ch = 0; ch < cfg_.outBytes * 8; ++ch) {
        auto* name = new QTableWidgetItem(cfg_.outputName(ch));
        name->setFlags(Qt::ItemIsEnabled);
        outTable_->setItem(ch, OutOperand, name);
        outTable_->setCellWidget(ch, OutGpio, gpioCombo(cfg_.outGpio[ch]));
    }
    inTable_->resizeColumnsToContents();
    outTable_->resizeColumnsToContents();
    updating_ = false;
    validate();
}

void PiConfigDialog::readWidgets()
{
    cfg_.host = host_->text().trimmed();
    cfg_.port = port_->value();
    cfg_.inAddress = inAddress_->value();
    cfg_.inBytes = inBytes_->currentData().toInt();
    cfg_.outAddress = outAddress_->value();
    cfg_.outBytes = outBytes_->currentData().toInt();
    cfg_.debounceMs = debounce_->value();
    for (int ch = 0; ch < inTable_->rowCount() && ch < PiConfig::kMaxChannels; ++ch) {
        if (auto* c = qobject_cast<QComboBox*>(inTable_->cellWidget(ch, InGpio))) cfg_.inGpio[ch] = c->currentData().toInt();
        if (auto* c = qobject_cast<QComboBox*>(inTable_->cellWidget(ch, InBias)))
            cfg_.inBias[ch] = static_cast<s5io::Bias>(c->currentData().toInt());
        if (QWidget* holder = inTable_->cellWidget(ch, InInvert)) {
            if (auto* box = holder->findChild<QCheckBox*>()) {
                if (box->isChecked())
                    cfg_.inInvert |= 1u << ch;
                else
                    cfg_.inInvert &= ~(1u << ch);
            }
        }
    }
    for (int ch = 0; ch < outTable_->rowCount() && ch < PiConfig::kMaxChannels; ++ch)
        if (auto* c = qobject_cast<QComboBox*>(outTable_->cellWidget(ch, OutGpio))) cfg_.outGpio[ch] = c->currentData().toInt();
}

void PiConfigDialog::validate()
{
    QStringList errors, warnings;
    std::map<int, QString> used;
    auto check = [&](int gpio, const QString& name) {
        if (gpio < 0) return;
        if (auto it = used.find(gpio); it != used.end())
            errors << tr("GPIO %1 ist doppelt belegt (%2 und %3).").arg(gpio).arg(it->second, name);
        else
            used[gpio] = name;
        if (const char* f = s5io::specialFunction(gpio))
            warnings << tr("GPIO %1 hat die Sonderfunktion %2 (%3).").arg(gpio).arg(QString::fromUtf8(f), name);
    };
    for (int ch = 0; ch < cfg_.inBytes * 8; ++ch) check(cfg_.inGpio[ch], cfg_.inputName(ch));
    for (int ch = 0; ch < cfg_.outBytes * 8; ++ch) check(cfg_.outGpio[ch], cfg_.outputName(ch));
    if (cfg_.host.isEmpty()) errors << tr("Bitte den Rechner angeben, auf dem s5io läuft.");
    if (used.empty()) warnings << tr("Es ist noch kein Pin belegt.");

    QString html;
    for (const QString& e : std::as_const(errors)) html += QStringLiteral("<div style='color:#d03030'>%1</div>").arg(e.toHtmlEscaped());
    for (const QString& w : std::as_const(warnings)) html += QStringLiteral("<div style='color:#c08000'>%1</div>").arg(w.toHtmlEscaped());
    message_->setText(html);
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(errors.isEmpty());
}
