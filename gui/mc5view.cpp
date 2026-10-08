#include "mc5view.h"

#include <QFontDatabase>

Mc5View::Mc5View(QWidget* parent)
    : QPlainTextEdit(parent)
{
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
}

void Mc5View::showResult(const s5::mc5::AssembleResult& result, s5::mc5::CpuFamily family,
                         const QStringList& sourceLines)
{
    QString out;
    out += tr("Maschinencode MC5 für %1\n").arg(QString::fromLatin1(s5::mc5::familyName(family)));

    if (!result.ok()) {
        out += tr("\nNicht übersetzbar (%n Fehler):\n", nullptr, static_cast<int>(result.errors.size()));
        for (const auto& e : result.errors)
            out += tr("  Zeile %1: %2\n").arg(e.line).arg(QString::fromStdString(e.message));
    }
    for (const auto& w : result.warnings)
        out += tr("Hinweis Zeile %1: %2\n").arg(w.line).arg(QString::fromStdString(w.message));

    int total = 0;
    for (const auto& b : result.blocks) {
        total += static_cast<int>(b.words.size());
        out += QStringLiteral("\n%1 %2   (%3 Wörter)\n")
                   .arg(QString::fromLatin1(s5::blockTypeName(b.type)))
                   .arg(b.number)
                   .arg(b.words.size());

        auto row = [&](size_t from, size_t to, const QString& text) {
            // höchstens 6 Wörter je Zeile
            for (size_t i = from; i < to; i += 6) {
                QString hex;
                for (size_t k = i; k < std::min(to, i + 6); ++k)
                    hex += QStringLiteral("%1 ").arg(b.words[k], 4, 16, QLatin1Char('0')).toUpper();
                out += QStringLiteral("  %1  %2  %3\n")
                           .arg(i, 4, 16, QLatin1Char('0'))
                           .arg(hex, -30)
                           .arg(i == from ? text : QString());
            }
        };

        row(0, 5, tr("Bausteinkopf"));
        size_t i = 5;
        if (b.type == s5::BlockType::DB) {
            for (; i < b.words.size(); ++i)
                row(i, i + 1, tr("DW %1").arg(i - 5));
            continue;
        }
        if (b.type == s5::BlockType::FB) {
            const size_t end = std::min(b.words.size(), 5 + static_cast<size_t>(b.words[5] & 0xFF));
            row(5, end, tr("Name und Bausteinparameter"));
            i = end;
        }
        while (i < b.words.size()) {
            size_t j = i + 1;
            while (j < b.words.size() && b.lines[j] == b.lines[i]) ++j;
            const int line = b.lines[i];
            QString text = line > 0 && line <= sourceLines.size() ? sourceLines[line - 1].trimmed() : QString();
            const int sc = text.indexOf(QLatin1Char(';'));
            if (sc >= 0) text = text.left(sc).trimmed();
            row(i, j, text);
            i = j;
        }
    }
    out += tr("\nGesamt: %1 Wörter (%2 Byte)\n").arg(total).arg(total * 2);
    setPlainText(out);
}
