// Baugruppen im Stil der S5-115U: CPU mit Betriebsartenschalter, Digitalein- und -ausgabe.
#pragma once

#include <QWidget>

class PlcController;

namespace rack {

enum class ModuleKind { DigitalIn, DigitalOut };

struct ModuleConfig {
    ModuleKind kind = ModuleKind::DigitalIn;
    int bytes = 2;          // 1, 2 oder 4 Byte (8/16/32 Bit)
    int address = 0;        // Anfangsadresse (EB/AB)
    bool momentary = false; // Eingänge als Taster statt Schalter
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

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRect rowRect(int byteIndex, int bit) const;
    int hitTest(const QPoint& pos) const;   // byteIndex*8+bit oder -1

    PlcController* plc_;
    ModuleConfig cfg_;
    int pressed_ = -1;
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
