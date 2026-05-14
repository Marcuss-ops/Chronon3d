#pragma once

#include <chronon3d/animation/animated_value.hpp>
#include <chronon3d/math/vec3.hpp>
#include <chronon3d/core/types.hpp>
#include <chronon3d/core/frame.hpp>
#include <chronon3d/scene/layer/depth_role.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/description/visual_desc.hpp>
#include <chronon3d/description/effect_desc.hpp>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d {

// Half-open time range with infinite-duration sentinel (-1).
// Semantics match the legacy Layer: from=0, duration=-1 means always active.
struct LayerTimeRange {
    Frame from{0};
    Frame duration{-1};  // -1 = infinite

    [[nodiscard]] constexpr bool contains(Frame f) const noexcept {
        if (f < from) return false;
        if (duration < 0) return true;
        return f < from + duration;
    }
};

using LayerId = std::string;

// Declarative layer: properties stored as AnimatedValue<T>, evaluated later
// by TimelineEvaluator. Visuals and effects are static in v1.
struct LayerDesc {
    LayerId     id;
    std::string name;

    LayerTimeRange time_range;

    AnimatedValue<Vec3> position{Vec3{0.0f, 0.0f, 0.0f}};
    AnimatedValue<Vec3> rotation{Vec3{0.0f, 0.0f, 0.0f}};  // euler degrees
    AnimatedValue<Vec3> scale{Vec3{1.0f, 1.0f, 1.0f}};
    AnimatedValue<f32>  opacity{1.0f};

    bool      is_3d{false};
    DepthRole depth_role{DepthRole::None};
    f32       depth_offset{0.0f};
    BlendMode blend_mode{BlendMode::Normal};

    std::vector<VisualDesc> visuals;
    std::vector<EffectDesc> effects;

    std::optional<LayerId> parent;
};

} // namespace chronon3d
