#include "codeeditor.h"

#include "format.h"

#include <QFontDatabase>
#include <QPainter>
#include <QTextBlock>

class SideArea : public QWidget {
public:
    SideArea(CodeEditor* editor, CodeEditor::Side side)
        : QWidget(editor), editor_(editor), side_(side)
    {
    }

protected:
    void paintEvent(QPaintEvent* e) override { editor_->paintSide(side_, e); }

private:
    CodeEditor* editor_;
    CodeEditor::Side side_;
};

CodeEditor::CodeEditor(QWidget* parent)
    : QPlainTextEdit(parent)
    , lineArea_(new SideArea(this, Side::LineNumbers))
    , statusArea_(new SideArea(this, Side::Status))
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(13);
    setFont(font);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);

    connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::updateMargins);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::onUpdateRequest);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::updateExtraSelections);
    updateMargins();
    updateExtraSelections();
}

int CodeEditor::lineNumberWidth() const
{
    int digits = QString::number(std::max(1, blockCount())).size();
    return 16 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * std::max(3, digits);
}

int CodeEditor::statusWidth() const
{
    return statusVisible_ ? fontMetrics().horizontalAdvance(QLatin1Char('M')) * 24 : 0;
}

void CodeEditor::updateMargins()
{
    setViewportMargins(lineNumberWidth(), 0, statusWidth(), 0);
    layoutSideAreas();
}

void CodeEditor::layoutSideAreas()
{
    QRect vr = viewport()->geometry();
    lineArea_->setGeometry(vr.left() - lineNumberWidth(), vr.top(), lineNumberWidth(), vr.height());
    statusArea_->setGeometry(vr.right() + 1, vr.top(), statusWidth(), vr.height());
    statusArea_->setVisible(statusVisible_);
}

void CodeEditor::resizeEvent(QResizeEvent* e)
{
    QPlainTextEdit::resizeEvent(e);
    layoutSideAreas();
}

void CodeEditor::onUpdateRequest(const QRect& rect, int dy)
{
    if (dy) {
        lineArea_->scroll(0, dy);
        statusArea_->scroll(0, dy);
    } else {
        lineArea_->update(0, rect.y(), lineArea_->width(), rect.height());
        statusArea_->update(0, rect.y(), statusArea_->width(), rect.height());
    }
    if (rect.contains(viewport()->rect())) updateMargins();
}

void CodeEditor::setLineStatus(const std::map<int, s5::LineStatus>& status)
{
    status_ = status;
    statusArea_->update();
}

void CodeEditor::clearLineStatus()
{
    status_.clear();
    statusArea_->update();
}

void CodeEditor::setStatusVisible(bool on)
{
    statusVisible_ = on;
    updateMargins();
}

void CodeEditor::setAkkuHex(bool hex)
{
    akkuHex_ = hex;
    statusArea_->update();
}

void CodeEditor::setExecutionLine(int line)
{
    if (line == execLine_) return;
    execLine_ = line;
    updateExtraSelections();
    if (line > 0) {
        QTextBlock block = document()->findBlockByNumber(line - 1);
        if (block.isValid()) {
            QRectF r = blockBoundingGeometry(block).translated(contentOffset());
            if (r.top() < 0 || r.bottom() > viewport()->height()) {
                QTextCursor c(block);
                setTextCursor(c);
                centerCursor();
            }
        }
    }
}

void CodeEditor::setErrorLines(const QList<int>& lines)
{
    errorLines_ = lines;
    updateExtraSelections();
}

void CodeEditor::gotoLine(int line)
{
    QTextBlock block = document()->findBlockByNumber(line - 1);
    if (!block.isValid()) return;
    QTextCursor c(block);
    setTextCursor(c);
    centerCursor();
    setFocus();
}

void CodeEditor::updateExtraSelections()
{
    const bool dark = palette().color(QPalette::Base).lightness() < 128;
    QList<QTextEdit::ExtraSelection> sels;

    auto lineSel = [&](int line, const QColor& color) {
        QTextBlock block = document()->findBlockByNumber(line - 1);
        if (!block.isValid()) return;
        QTextEdit::ExtraSelection s;
        s.format.setBackground(color);
        s.format.setProperty(QTextFormat::FullWidthSelection, true);
        s.cursor = QTextCursor(block);
        sels.append(s);
    };

    QTextEdit::ExtraSelection cur;
    cur.format.setBackground(dark ? QColor(255, 255, 255, 14) : QColor(0, 0, 0, 10));
    cur.format.setProperty(QTextFormat::FullWidthSelection, true);
    cur.cursor = textCursor();
    cur.cursor.clearSelection();
    sels.append(cur);

    for (int l : std::as_const(errorLines_)) lineSel(l, dark ? QColor(140, 30, 30, 140) : QColor(255, 205, 205));
    if (execLine_ > 0) lineSel(execLine_, dark ? QColor(150, 120, 0, 150) : QColor(255, 236, 140));

    setExtraSelections(sels);
}

void CodeEditor::paintSide(Side side, QPaintEvent* e)
{
    QWidget* area = side == Side::LineNumbers ? lineArea_ : statusArea_;
    QPainter p(area);
    const QPalette pal = palette();
    const bool dark = pal.color(QPalette::Base).lightness() < 128;
    const QColor bg = dark ? pal.color(QPalette::Base).lighter(118) : pal.color(QPalette::Base).darker(104);
    p.fillRect(e->rect(), bg);
    p.setPen(dark ? QColor(70, 70, 70) : QColor(215, 215, 215));
    if (side == Side::LineNumbers)
        p.drawLine(area->width() - 1, e->rect().top(), area->width() - 1, e->rect().bottom());
    else
        p.drawLine(0, e->rect().top(), 0, e->rect().bottom());

    const QColor dim = pal.color(QPalette::PlaceholderText);
    const QColor text = pal.color(QPalette::Text);
    const QColor on = QColor(0x2e, 0xa0, 0x43);
    const int lineH = fontMetrics().height();
    const int charW = fontMetrics().horizontalAdvance(QLatin1Char('M'));

    QFont small = font();
    small.setPointSizeF(font().pointSizeF() * 0.85);

    QTextBlock block = firstVisibleBlock();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid() && top <= e->rect().bottom()) {
        if (block.isVisible() && bottom >= e->rect().top()) {
            const int line = block.blockNumber() + 1;
            if (side == Side::LineNumbers) {
                p.setPen(line == execLine_ ? text : dim);
                p.setFont(font());
                p.drawText(0, top, area->width() - 8, lineH, Qt::AlignRight | Qt::AlignVCenter, QString::number(line));
            } else if (auto it = status_.find(line); it != status_.end()) {
                const s5::LineStatus& st = it->second;
                int x = 8;
                // VKE als Kästchen
                QRect box(x, top + 2, charW * 2, lineH - 4);
                p.setPen(Qt::NoPen);
                p.setBrush(st.vke ? on : (dark ? QColor(80, 80, 80) : QColor(200, 200, 200)));
                p.drawRoundedRect(box, 3, 3);
                p.setPen(st.vke ? Qt::white : text);
                p.setFont(small);
                p.drawText(box, Qt::AlignCenter, st.vke ? QStringLiteral("1") : QStringLiteral("0"));
                x += charW * 3;

                if (st.hasSta) {
                    p.setPen(dim);
                    p.drawText(x, top, charW * 6, lineH, Qt::AlignVCenter, QStringLiteral("STA"));
                    p.setPen(text);
                    p.drawText(x + charW * 3, top, charW * 2, lineH, Qt::AlignVCenter,
                               st.sta ? QStringLiteral("1") : QStringLiteral("0"));
                }
                x += charW * 6;

                if (st.akkuOp) {
                    p.setPen(dim);
                    p.drawText(x, top, charW * 5, lineH, Qt::AlignVCenter, QStringLiteral("AKKU1"));
                    p.setPen(text);
                    p.setFont(font());
                    QString v = akkuHex_ ? fmt::kh(st.akku1) : fmt::kf(st.akku1);
                    p.drawText(x + charW * 5 + 6, top, charW * 8, lineH, Qt::AlignVCenter, v);
                }
            }
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
    }
}
