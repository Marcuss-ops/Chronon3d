#pragma once

#include <chronon3d/compiler/compiled_node.hpp>

#include <array>
#include <cstdint>
#include <string>

namespace chronon3d::compiler {

/// Static authoring payload for one lowered layer.  It contains only
/// immutable CPU data; resource handles and frame-varying values are resolved
/// by the materializer/sampler after the compiler boundary.
enum class LayerFitMode : std::uint8_t { Cover, Contain, Stretch, None };

struct StaticLayerProgram {
    NodeId id{0};
    NodeKind kind{NodeKind::Unknown};
    std::string name;
    std::string asset;
    std::string source;
    std::string text;
    std::string font;

    std::array<float, 2> size{0.0f, 0.0f};
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    std::array<float, 3> rotation{0.0f, 0.0f, 0.0f};
    std::uint8_t size_dimensions{0};
    std::uint8_t position_dimensions{0};
    std::uint8_t scale_dimensions{0};
    std::uint8_t rotation_dimensions{0};

    LayerFitMode fit{LayerFitMode::Cover};
    std::int64_t start_frame{0};
    std::int64_t duration_frames{0};
    float opacity{1.0f};
    float radius{0.0f};
    bool loop{false};
};

}  // namespace chronon3d::compiler
