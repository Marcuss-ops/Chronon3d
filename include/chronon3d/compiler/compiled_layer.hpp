#pragma once

#include <chronon3d/compiler/compiled_node.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace chronon3d::compiler {

/// Static authoring payload for one lowered layer.  It contains only
/// immutable CPU data; resource handles and frame-varying values are resolved
/// by the materializer/sampler after the compiler boundary.
enum class LayerFitMode : std::uint8_t { Cover, Contain, Stretch, None };

enum class LayerTrackMatteType : std::uint8_t {
    None, Alpha, AlphaInverted, Luma, LumaInverted
};

struct StaticLayerProgram {
    static constexpr std::uint32_t kInvalidProgramIndex =
        std::numeric_limits<std::uint32_t>::max();
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
    std::optional<render_plan::ShapeContentPlan> shape;
    std::optional<render_plan::InlineMeshPlan> inline_mesh;
    bool loop{false};
    // Resolved once by compiler lowering. Runtime sampling must not scan the
    // program's text/animation tables by layer id on every frame.
    std::uint32_t text_program_index{kInvalidProgramIndex};
    std::uint32_t animation_program_index{kInvalidProgramIndex};
    std::uint32_t parent_index{kInvalidProgramIndex};
    std::uint32_t hierarchy_depth{0};
    std::uint32_t matte_source_index{kInvalidProgramIndex};
    LayerTrackMatteType matte_type{LayerTrackMatteType::None};
    float time_remap_speed{1.0f};
    std::int64_t time_remap_freeze_frame{-1};
    std::uint32_t time_remap_track_index{kInvalidProgramIndex};
};

}  // namespace chronon3d::compiler
