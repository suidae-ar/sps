// Baugruppen im Stil der S5-115U: CPU mit Betriebsartenschalter, Digitalein- und -ausgabe.
#pragma once

#include "piconfig.h"

#include <QWidget>

#include <cstdint>

class PiLink;
class PlcController;
class QTimer;

namespace rack {

enum class ModuleKind { DigitalIn, DigitalOut };

struct ModuleConfig {
    ModuleKind kind = ModuleKind::DigitalIn;
    int bytes = 2;              // 1, 2 oder 4 Byte (8/16/32 Bit)
    int address = 0;            // Anfangsadresse (EB/AB)
    uint32_t momentaryMask = 0; // je Eingang (Byte*8 + Bit): gesetzt = Taster, sonst Schalter

    int bitCount() const { return bytes * 8; }
    uint32_t allBits() const { return bytes >= 4 ? 0xFFFFFFFFu : (1u << (bytes * 8)) - 1u; }
    bool isMomentary(int index) const { return (momentaryMask >> index) & 1u; }
};

// Gemeinsame Optik: Frontplatte, Beschriftung, Steckplatznummer
class ModuleWidget : public QWidget {
    Q_OBJECT
public:
    static constexpr int kHeight = 470;

    explicit ModuleWidget(QWidget* parent = nullptr);
    void setSlot(int slot);

signals:
    void contextRequested(const QPoint& globalPos);

protected:
    void contextMenuEvent(QContextMenuEvent* e) override;
    void paintFrame(class QPainter& p, const QString& title, const QString& subtitle) const;

    int slot_ = 0;
};

class CpuModule : public ModuleWidget {
    Q_OBJECT
public:
    CpuModule(PlcController* plc, QWidget* parent = nullptr);
    void setFamilyName(const QString& name);
    void setPeripheryFault(bool fault);     // z.B. Raspberry Pi nicht erreichbar → STÖRUNG leuchtet

signals:
    void editorRequested();
    void message(const QString& text);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRect modeSwitchRect() const;
    QRect resetSwitchRect() const;
    QRect pgButtonRect() const;

    PlcController* plc_;
    QString family_;
    bool peripheryFault_ = false;
    bool resetDown_ = false;
    bool pgDown_ = false;
};

class DigitalModule : public ModuleWidget {
    Q_OBJECT
public:
    DigitalModule(PlcController* plc, const ModuleConfig& cfg, QWidget* parent = nullptr);

    const ModuleConfig& config() const { return cfg_; }
    void setConfig(const ModuleConfig& cfg);
    static int widthFor(int bytes);
    int bitAt(const QPoint& pos) const;     // Byte*8 + Bit unter pos oder -1

protected:
    bool event(QEvent* e) override;
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRect rowRect(int byteIndex, int bit) const;

    PlcController* plc_;
    ModuleConfig cfg_;
    int pressed_ = -1;
};

// Raspberry-Pi-E/A: Ein- und Ausgänge an den GPIO-Pins eines Raspberry Pi (über den Dienst s5io)
class PiModule : public ModuleWidget {
    Q_OBJECT
public:
    PiModule(PlcController* plc, const PiConfig& cfg, QWidget* parent = nullptr);
    ~PiModule() override;

    const PiConfig& config() const { return cfg_; }
    void setConfig(const PiConfig& cfg);
    PiLink* link() const { return link_; }
    bool online() const;
    static int widthFor(const PiConfig& cfg);

signals:
    void statusChanged();

protected:
    bool event(QEvent* e) override;
    void paintEvent(QPaintEvent*) override;

private:
    int columns() const { return cfg_.inBytes + cfg_.outBytes; }
    QRect rowRect(int column, int bit) const;
    void applyInputs(uint32_t mask);
    void clearInputs();
    void updateOutputs();

    PlcController* plc_;
    PiConfig cfg_;
    PiLink* link_;
    QTimer* outTimer_;
};

// Leerer Steckplatz mit "+" zum Stecken einer Baugruppe
class EmptySlot : public QWidget {
    Q_OBJECT
public:
    explicit EmptySlot(QWidget* parent = nullptr);

signals:
    void clicked(const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
};

// Baugruppenträger: Hintergrund mit Profilschienen
class RackWidget : public QWidget {
    Q_OBJECT
public:
    explicit RackWidget(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;
};

}  // namespace rack
