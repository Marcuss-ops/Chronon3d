#pragma once

#include <cstdint>

namespace chronon3d::compiler {

enum class ResourceKind : std::uint16_t { Asset, Font, Source };

struct ResourceBindingSpec {
    std::uint32_t id{0};
    ResourceKind kind{ResourceKind::Asset};
    std::uint32_t data_offset{0};
    std::uint32_t data_size{0};
};

struct AnimationProgram {
    std::uint32_t id{0};
    std::uint32_t first_track{0};
    std::uint32_t track_count{0};
    std::uint32_t data_offset{0};
};

/// Static parameter topology. Values are sampled per frame by the runtime;
/// this object contains no frame-owned state.
struct ParameterProgram {
    std::uint32_t id{0};
    std::uint32_t first_parameter{0};
    std::uint32_t parameter_count{0};
};

}  // namespace chronon3d::compiler
