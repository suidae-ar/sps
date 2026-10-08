#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class AwlHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit AwlHighlighter(QTextDocument* doc, bool dark);

protected:
    void highlightBlock(const QString& text) override;

private:
    struct Rule {
        QRegularExpression re;
        QTextCharFormat format;
        int group = 0;
    };
    QList<Rule> rules_;
    QRegularExpression comment_;
    QTextCharFormat commentFormat_;
};
