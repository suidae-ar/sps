#include "watchpanel.h"

#include "format.h"

#include <QFontDatabase>
#include <QHeaderView>

#include <set>

using s5::OpKind;

namespace {

int sortRank(OpKind k)
{
    switch (k) {
    case OpKind::InputBit: return 0;
    case OpKind::InputByte: return 1;
    case OpKind::InputWord: return 2;
    case OpKind::OutputBit: return 3;
    case OpKind::OutputByte: return 4;
    case OpKind::OutputWord: return 5;
    case OpKind::FlagBit: return 6;
    case OpKind::FlagByte: return 7;
    case OpKind::FlagWord: return 8;
    case OpKind::Timer: return 9;
    case OpKind::Counter: return 10;
    default: return 11;
    }
}

bool watchable(OpKind k)
{
    switch (k) {
    case OpKind::InputBit:
    case OpKind::OutputBit:
    case OpKind::FlagBit:
    case OpKind::InputByte:
    case OpKind::OutputByte:
    case OpKind::FlagByte:
    case OpKind::InputWord:
    case OpKind::OutputWord:
    case OpKind::FlagWord:
    case OpKind::Timer:
    case OpKind::Counter: return true;
    default: return false;
    }
}

}  // namespace

WatchPanel::WatchPanel(QWidget* parent)
    : QTableWidget(parent)
{
    setColumnCount(4);
    setHorizontalHeaderLabels({tr("Operand"), tr("Wert"), tr("Hex"), tr("Info")});
    verticalHeader()->hide();
    horizontalHeader()->setStretchLastSection(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setAlternatingRowColors(true);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setColumnWidth(0, 90);
    setColumnWidth(1, 110);
    setColumnWidth(2, 70);
}

void WatchPanel::setProgram(const s5::Program& program)
{
    std::set<std::pair<int, s5::Operand>> seen;
    for (const auto& [key, block] : program.blocks) {
        for (const auto& ins : block.code) {
            if (watchable(ins.operand.kind)) seen.insert({sortRank(ins.operand.kind), ins.operand});
            for (const auto& a : ins.args)      // Aktualoperanden von FB-Aufrufen
                if (watchable(a.kind)) seen.insert({sortRank(a.kind), a});
        }
    }
    entries_.clear();
    for (const auto& [rank, op] : seen) entries_.push_back({op, -1});

    for (const auto& [key, block] : program.blocks) {
        if (block.type != s5::BlockType::DB) continue;
        const int n = std::min<int>(static_cast<int>(block.data.size()), 64);
        for (int i = 0; i < n; ++i) {
            s5::Operand o;
            o.kind = OpKind::DataWord;
            o.address = static_cast<uint16_t>(i);
            entries_.push_back({o, block.number});
        }
    }

    setRowCount(static_cast<int>(entries_.size()));
    for (int r = 0; r < rowCount(); ++r) {
        const Entry& e = entries_[r];
        QString name = QString::fromStdString(s5::toString(e.operand));
        if (e.db >= 0) name = QStringLiteral("DB%1 %2").arg(e.db).arg(name);
        setItem(r, 0, new QTableWidgetItem(name));
        for (int c = 1; c < 4; ++c) setItem(r, c, new QTableWidgetItem);
    }
}

void WatchPanel::refresh(const s5::Cpu& cpu)
{
    const bool dark = palette().color(QPalette::Base).lightness() < 128;
    const QBrush onBrush(dark ? QColor(30, 110, 50) : QColor(190, 235, 195));

    for (int r = 0; r < rowCount() && r < static_cast<int>(entries_.size()); ++r) {
        const Entry& e = entries_[r];
        QString value, hex, info;
        bool highlight = false;

        if (e.db >= 0) {
            const auto* data = cpu.dataBlock(e.db);
            if (data && e.operand.address < data->size()) {
                uint16_t v = (*data)[e.operand.address];
                value = fmt::kf(v);
                hex = fmt::kh(v);
                info = fmt::km(v);
            }
        } else if (e.operand.kind == OpKind::Timer) {
            const s5::TimerState& t = cpu.timer(e.operand.address);
            value = QStringLiteral("KT ") + fmt::kt(t);
            info = QStringLiteral("Q=%1  %2").arg(t.q ? 1 : 0).arg(t.running ? tr("läuft") : QString());
            highlight = t.q;
        } else if (e.operand.kind == OpKind::Counter) {
            const s5::CounterState& c = cpu.counter(e.operand.address);
            value = QStringLiteral("KZ ") + fmt::kz(c.value);
            info = QStringLiteral("Q=%1").arg(c.value != 0 ? 1 : 0);
            highlight = c.value != 0;
        } else if (e.operand.isBit()) {
            bool b = cpu.peekBit(e.operand).value_or(false);
            value = b ? QStringLiteral("1") : QStringLiteral("0");
            highlight = b;
        } else if (auto v = cpu.peekValue(e.operand)) {
            const bool isByte = e.operand.kind == OpKind::InputByte || e.operand.kind == OpKind::OutputByte
                             || e.operand.kind == OpKind::FlagByte;
            value = isByte ? QString::number(*v) : fmt::kf(*v);
            hex = fmt::kh(*v, isByte ? 2 : 4);
            info = fmt::km(*v, isByte ? 8 : 16);
        }

        item(r, 1)->setText(value);
        item(r, 2)->setText(hex);
        item(r, 3)->setText(info);
        item(r, 1)->setBackground(highlight ? onBrush : QBrush());
    }
}
