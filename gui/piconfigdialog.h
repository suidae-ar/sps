// Einstellungsdialog der Baugruppe "Raspberry-Pi-E/A".
#pragma once

#include "piconfig.h"

#include <QDialog>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;

class PiConfigDialog : public QDialog {
    Q_OBJECT
public:
    PiConfigDialog(const PiConfig& cfg, QWidget* parent = nullptr);
    PiConfig config() const { return cfg_; }

private:
    void rebuildTables();
    void readWidgets();
    void validate();
    QComboBox* gpioCombo(int gpio);

    PiConfig cfg_;
    QLineEdit* host_;
    QSpinBox* port_;
    QSpinBox* inAddress_;
    QComboBox* inBytes_;
    QSpinBox* outAddress_;
    QComboBox* outBytes_;
    QSpinBox* debounce_;
    QTableWidget* inTable_;
    QTableWidget* outTable_;
    QLabel* message_;
    QDialogButtonBox* buttons_;
    bool updating_ = false;
};
