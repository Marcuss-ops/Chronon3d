#pragma once

#include <cstdint>

namespace chronon3d::compiler {

using NodeId = std::uint32_t;

enum class NodeKind : std::uint16_t { Unknown, Image, Video, Text, Color, Shape, Mesh };

struct CompiledNode {
    NodeId id{0};
    NodeKind kind{NodeKind::Unknown};
    std::uint32_t input_begin{0};
    std::uint32_t input_count{0};
    std::uint32_t data_offset{0};
};

}  // namespace chronon3d::compiler
