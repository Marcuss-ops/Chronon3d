#pragma once

#include <chronon3d/animation/animated_value.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/model/depth_role.hpp>
#include <chronon3d/scene/model/layer.hpp>  // for LayerKind
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/description/visual_desc.hpp>
#include <chronon3d/description/effect_desc.hpp>
#include <chronon3d/scene/model/transition.hpp>
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
    LayerKind  kind{LayerKind::Normal};  // Normal, Adjustment, Null, etc.

    // ── Time Remap (AE-4) ──
    f32  time_remap_speed{1.0f};             // playback speed multiplier
    Frame time_remap_freeze_frame{-1};        // if >= 0, hold this source frame
    AnimatedValue<f32> time_remap_curve;      // animated comp-frame → source-frame

    // ── Precomp (AE-6) ──
    std::string precomp_composition_name;  // for LayerKind::Precomp: name of the nested composition

    std::vector<VisualDesc> visuals;
    std::vector<EffectDesc> effects;

    std::optional<LayerId> parent;

    LayerTransitionSpec transition_in{};
    LayerTransitionSpec transition_out{};
};

} // namespace chronon3d
