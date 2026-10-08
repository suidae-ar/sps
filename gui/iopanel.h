// E/A-Panel: Eingangsbytes als Schalter/Taster, Ausgangsbytes als LEDs.
#pragma once

#include "cpu.h"

#include <QWidget>

#include <vector>

class QCheckBox;
class QGridLayout;
class QSpinBox;
class QToolButton;

class Led : public QWidget {
    Q_OBJECT
public:
    explicit Led(QWidget* parent = nullptr);
    void setOn(bool on);
    QSize sizeHint() const override { return {28, 28}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    bool on_ = false;
};

class IoPanel : public QWidget {
    Q_OBJECT
public:
    explicit IoPanel(s5::Cpu* cpu, QWidget* parent = nullptr);
    void refresh();

private:
    struct InputRow {
        QSpinBox* address;      // dient zugleich als Schlüssel der Zeile
        QToolButton* bits[8];
        QCheckBox* momentary;
    };
    struct OutputRow {
        QSpinBox* address;
        Led* leds[8];
    };

    void addInputRow(int byte, bool momentary = false);
    void addOutputRow(int byte);
    void removeInputRow(QSpinBox* key);
    void removeOutputRow(QSpinBox* key);
    void setMomentary(InputRow& row, bool on);
    void saveSettings() const;

    s5::Cpu* cpu_;
    QGridLayout* inputGrid_;
    QGridLayout* outputGrid_;
    std::vector<InputRow> inputs_;
    std::vector<OutputRow> outputs_;
};
