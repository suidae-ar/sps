#include "awlhighlighter.h"

namespace {

QTextCharFormat makeFormat(const QColor& color, bool bold = false, bool italic = false)
{
    QTextCharFormat f;
    f.setForeground(color);
    if (bold) f.setFontWeight(QFont::Bold);
    f.setFontItalic(italic);
    return f;
}

}  // namespace

AwlHighlighter::AwlHighlighter(QTextDocument* doc, bool dark)
    : QSyntaxHighlighter(doc)
{
    const QColor blockColor = dark ? QColor(0x82, 0xaa, 0xff) : QColor(0x1a, 0x4f, 0xa0);
    const QColor opColor = dark ? QColor(0xe0, 0xe0, 0xe0) : QColor(0x20, 0x20, 0x20);
    const QColor operandColor = dark ? QColor(0x4e, 0xc9, 0xb0) : QColor(0x00, 0x7a, 0x6e);
    const QColor constColor = dark ? QColor(0xd1, 0x9a, 0x66) : QColor(0xa0, 0x52, 0x00);
    const QColor labelColor = dark ? QColor(0xc6, 0x78, 0xdd) : QColor(0x8a, 0x2b, 0xb0);
    const QColor commentColor = dark ? QColor(0x7f, 0x84, 0x8e) : QColor(0x70, 0x70, 0x70);

    const auto ci = QRegularExpression::CaseInsensitiveOption;

    // Operation (erstes Wort nach optionaler Marke)
    rules_.append({QRegularExpression(QStringLiteral(R"(^\s*(?:[A-Za-z_]\w*\s*:|:)?\s*([^\s;]+))")),
                   makeFormat(opColor, true), 1});
    // Operanden
    rules_.append({QRegularExpression(QStringLiteral(R"(\b(?:E|A|M|D)\s*\d+\s*\.\s*\d+\b)"), ci),
                   makeFormat(operandColor), 0});
    rules_.append({QRegularExpression(QStringLiteral(R"(\b(?:EB|EW|AB|AW|MB|MW|DW|DL|DR|T|Z)\s*\d+\b)"), ci),
                   makeFormat(operandColor), 0});
    rules_.append({QRegularExpression(QStringLiteral(R"(\b(?:PB|FB|SB|DB|OB)\s*\d+\b)"), ci),
                   makeFormat(blockColor), 0});
    // Konstanten
    rules_.append({QRegularExpression(QStringLiteral(R"(\bK[FHMYTZCB]\s*=?\s*('[^']*'|[^\s;]+))"), ci),
                   makeFormat(constColor), 0});
    // Sprungmarken
    rules_.append({QRegularExpression(QStringLiteral(R"(^\s*[A-Za-z_]\w*\s*:)")), makeFormat(labelColor, true), 0});
    rules_.append({QRegularExpression(QStringLiteral(R"(=\s*[A-Za-z_]\w*)")), makeFormat(labelColor), 0});
    // FB-Kopf
    rules_.append({QRegularExpression(QStringLiteral(R"(^\s*(?:NAME|BEZ)\s*:)"), ci), makeFormat(blockColor, true), 0});
    rules_.append({QRegularExpression(QStringLiteral(R"((?:E/A/D/B/T/Z|BI/BY/W/K|KM/KH/KY/KC/KF/KT/KZ/KG)\s*:)"), ci),
                   makeFormat(commentColor), 0});
    // Bausteinkopf
    rules_.append({QRegularExpression(QStringLiteral(R"(^\s*(?:OB|PB|FB|SB|DB)\s*\d+\s*$)"), ci),
                   makeFormat(blockColor, true), 0});
    // Netzwerktrenner
    rules_.append({QRegularExpression(QStringLiteral(R"(^\s*\*\*\*\s*$)")), makeFormat(commentColor, true), 0});

    comment_ = QRegularExpression(QStringLiteral(R"((;|//).*$)"));
    commentFormat_ = makeFormat(commentColor, false, true);
}

void AwlHighlighter::highlightBlock(const QString& text)
{
    for (const Rule& rule : std::as_const(rules_)) {
        auto it = rule.re.globalMatch(text);
        while (it.hasNext()) {
            auto m = it.next();
            if (m.capturedLength(rule.group) > 0)
                setFormat(m.capturedStart(rule.group), m.capturedLength(rule.group), rule.format);
        }
    }
    auto m = comment_.match(text);
    if (m.hasMatch()) setFormat(m.capturedStart(), m.capturedLength(), commentFormat_);
}
