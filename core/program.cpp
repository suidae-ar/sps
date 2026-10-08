#include "program.h"

#include <cstdio>
#include <tuple>

namespace s5 {

const char* blockTypeName(BlockType t)
{
    switch (t) {
    case BlockType::OB: return "OB";
    case BlockType::PB: return "PB";
    case BlockType::FB: return "FB";
    case BlockType::SB: return "SB";
    case BlockType::DB: return "DB";
    }
    return "?";
}

bool Operand::isBit() const
{
    return kind == OpKind::InputBit || kind == OpKind::OutputBit || kind == OpKind::FlagBit
        || kind == OpKind::DataBit;
}

bool Operand::operator<(const Operand& o) const
{
    return std::tie(kind, address, bit, value, blockType, label)
         < std::tie(o.kind, o.address, o.bit, o.value, o.blockType, o.label);
}

bool Operand::operator==(const Operand& o) const
{
    return std::tie(kind, address, bit, value, blockType, label)
        == std::tie(o.kind, o.address, o.bit, o.value, o.blockType, o.label);
}

std::string toString(const Operand& o)
{
    char buf[64];
    switch (o.kind) {
    case OpKind::None: return {};
    case OpKind::InputBit:  std::snprintf(buf, sizeof buf, "E %u.%u", o.address, o.bit); break;
    case OpKind::OutputBit: std::snprintf(buf, sizeof buf, "A %u.%u", o.address, o.bit); break;
    case OpKind::FlagBit:   std::snprintf(buf, sizeof buf, "M %u.%u", o.address, o.bit); break;
    case OpKind::DataBit:   std::snprintf(buf, sizeof buf, "D %u.%u", o.address, o.bit); break;
    case OpKind::InputByte:  std::snprintf(buf, sizeof buf, "EB %u", o.address); break;
    case OpKind::OutputByte: std::snprintf(buf, sizeof buf, "AB %u", o.address); break;
    case OpKind::FlagByte:   std::snprintf(buf, sizeof buf, "MB %u", o.address); break;
    case OpKind::InputWord:  std::snprintf(buf, sizeof buf, "EW %u", o.address); break;
    case OpKind::OutputWord: std::snprintf(buf, sizeof buf, "AW %u", o.address); break;
    case OpKind::FlagWord:   std::snprintf(buf, sizeof buf, "MW %u", o.address); break;
    case OpKind::DataWord:  std::snprintf(buf, sizeof buf, "DW %u", o.address); break;
    case OpKind::DataLeft:  std::snprintf(buf, sizeof buf, "DL %u", o.address); break;
    case OpKind::DataRight: std::snprintf(buf, sizeof buf, "DR %u", o.address); break;
    case OpKind::Timer:   std::snprintf(buf, sizeof buf, "T %u", o.address); break;
    case OpKind::Counter: std::snprintf(buf, sizeof buf, "Z %u", o.address); break;
    case OpKind::Constant: std::snprintf(buf, sizeof buf, "%s %04X", o.label.c_str(), o.value); break;
    case OpKind::Block: std::snprintf(buf, sizeof buf, "%s %u", blockTypeName(o.blockType), o.address); break;
    case OpKind::Label:
    case OpKind::Param: return "=" + o.label;
    case OpKind::Number: std::snprintf(buf, sizeof buf, "%u", o.value); break;
    }
    return buf;
}

std::string Block::name() const
{
    return std::string(blockTypeName(type)) + " " + std::to_string(number);
}

int Block::paramIndex(const std::string& n) const
{
    for (size_t i = 0; i < params.size(); ++i)
        if (params[i].name == n) return static_cast<int>(i);
    return -1;
}

std::string ParamDecl::describe() const
{
    static const char* kinds[] = {"E", "A", "D", "B", "T", "Z"};
    std::string s = kinds[static_cast<int>(kind)];
    if (!type.empty()) s += " " + type;
    return s;
}

const Block* Program::find(BlockType t, int number) const
{
    auto it = blocks.find({static_cast<int>(t), number});
    return it == blocks.end() ? nullptr : &it->second;
}

Block* Program::find(BlockType t, int number)
{
    auto it = blocks.find({static_cast<int>(t), number});
    return it == blocks.end() ? nullptr : &it->second;
}

}  // namespace s5
