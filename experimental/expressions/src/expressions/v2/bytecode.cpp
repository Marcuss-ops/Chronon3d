// bytecode.cpp — Path B bytecode helpers.

#include <chronon3d_experimental/expressions/v2/bytecode.hpp>

namespace chronon3d::expressions::v2 {

const char* op_kind_name(OpKind k) noexcept {
    switch (k) {
        case OpKind::LOAD_CONST:    return "LOAD_CONST";
        case OpKind::LOAD_VAR:      return "LOAD_VAR";
        case OpKind::STORE_VAR:     return "STORE_VAR";
        case OpKind::ADD:           return "ADD";
        case OpKind::SUB:           return "SUB";
        case OpKind::MUL:           return "MUL";
        case OpKind::DIV:           return "DIV";
        case OpKind::MOD:           return "MOD";
        case OpKind::NEG:           return "NEG";
        case OpKind::EQ:            return "EQ";
        case OpKind::NE:            return "NE";
        case OpKind::LT:            return "LT";
        case OpKind::LE:            return "LE";
        case OpKind::GT:            return "GT";
        case OpKind::GE:            return "GE";
        case OpKind::AND:           return "AND";
        case OpKind::OR:            return "OR";
        case OpKind::NOT:           return "NOT";
        case OpKind::MEMBER:        return "MEMBER";
        case OpKind::INDEX:         return "INDEX";
        case OpKind::CALL:          return "CALL";
        case OpKind::JMP:           return "JMP";
        case OpKind::JMP_IF_FALSE:  return "JMP_IF_FALSE";
        case OpKind::JMP_IF_TRUE:   return "JMP_IF_TRUE";
        case OpKind::RETURN:        return "RETURN";
    }
    return "<unknown OpKind>";
}

} // namespace chronon3d::expressions::v2
