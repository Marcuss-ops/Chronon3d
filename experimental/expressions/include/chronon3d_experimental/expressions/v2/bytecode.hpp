// bytecode.hpp — Path B expression bytecode.
//
// Stack-machine opcodes. Each Op carries a single operand (slot index into
// the program-level constants/names table). The compiler emits this for the
// VM to interpret.
//
// Compactness note: payload<0> could be Operation::NEG which carries no slot.

#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace chronon3d::expressions::v2 {

enum class OpKind : std::uint8_t {
    LOAD_CONST = 0,  // operand: const-pool slot
    LOAD_VAR   = 1,  // operand: name-pool slot
    STORE_VAR  = 2,  // operand: name-pool slot

    ADD = 3,  SUB = 4,  MUL = 5,  DIV = 6,  MOD = 7,
    NEG = 8,
    EQ  = 9,  NE  = 10, LT  = 11, LE  = 12, GT  = 13, GE  = 14,
    AND = 15, OR  = 16, NOT = 17,

    MEMBER = 18,  // operand: name-pool slot; pops base
    INDEX  = 19,  // pops index then base; pushes base[index]
    CALL   = 20,  // operand: name-pool slot for function name; pops argc args; pushes result

    JMP         = 21, // unconditional
    JMP_IF_FALSE= 22,
    JMP_IF_TRUE = 23,
    RETURN      = 24,
};

[[nodiscard]] const char* op_kind_name(OpKind k) noexcept;

struct Op {
    OpKind                kind{OpKind::RETURN};
    std::uint32_t         slot{0};
};

struct Program {
    std::vector<Op>             code;
    // Pool tables; constants loaded by LOAD_CONST come from these. The
    // LOAD_CONST op packs its pool type into the high 8 bits of `slot`,
    // so any single index table is unambiguous at runtime.
    std::vector<double>         const_numbers;
    std::vector<std::string>    const_strings;
    std::vector<bool>           const_bools;
    // Intern table for variable / member / function-call names. LOAD_VAR,
    // STORE_VAR, CALL, MEMBER all reference slot indices into this table.
    std::vector<std::string>    names;

    [[nodiscard]] bool empty() const noexcept { return code.empty(); }
    [[nodiscard]] std::size_t size()  const noexcept { return code.size(); }
};

// ── Slot packing helpers for LOAD_CONST ─────────────────────────
// LOAD_CONST's slot packs a type tag in the high byte (0 = number,
// 1 = string, 2 = bool) and the pool index in the lower 24 bits.
[[nodiscard]] inline std::uint32_t pack_const_slot(std::uint8_t tag, std::uint32_t idx) noexcept {
    return (static_cast<std::uint32_t>(tag) << 24) | (idx & 0x00FFFFFFu);
}
[[nodiscard]] inline std::uint8_t const_slot_tag(std::uint32_t slot) noexcept {
    return static_cast<std::uint8_t>((slot >> 24) & 0xFFu);
}
[[nodiscard]] inline std::uint32_t const_slot_index(std::uint32_t slot) noexcept {
    return slot & 0x00FFFFFFu;
}

} // namespace chronon3d::expressions::v2
