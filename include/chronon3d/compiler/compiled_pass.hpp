#pragma once

#include <cstdint>

namespace chronon3d::compiler {

enum class PassKind : std::uint16_t { Composite, Encode };

struct CompiledPass {
    std::uint32_t id{0};
    PassKind kind{PassKind::Composite};
    std::uint32_t node_begin{0};
    std::uint32_t node_count{0};
    std::uint32_t data_offset{0};
};

}  // namespace chronon3d::compiler
