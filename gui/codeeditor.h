// AWL-Editor mit Zeilennummern und Statusspalte ("Status Baustein").
#pragma once

#include "cpu.h"

#include <QPlainTextEdit>

#include <map>

class CodeEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget* parent = nullptr);

    void setLineStatus(const std::map<int, s5::LineStatus>& status);
    void clearLineStatus();
    void setStatusVisible(bool on);
    bool statusVisible() const { return statusVisible_; }
    void setAkkuHex(bool hex);
    void setExecutionLine(int line);    // 1-basiert, -1 = keine
    void setErrorLines(const QList<int>& lines);
    void gotoLine(int line);

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    friend class SideArea;
    enum class Side { LineNumbers, Status };

    int lineNumberWidth() const;
    int statusWidth() const;
    void updateMargins();
    void layoutSideAreas();
    void onUpdateRequest(const QRect& rect, int dy);
    void updateExtraSelections();
    void paintSide(Side side, QPaintEvent* e);

    QWidget* lineArea_;
    QWidget* statusArea_;
    std::map<int, s5::LineStatus> status_;
    bool statusVisible_ = true;
    bool akkuHex_ = false;
    int execLine_ = -1;
    QList<int> errorLines_;
};
