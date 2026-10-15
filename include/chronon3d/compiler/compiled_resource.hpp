#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <chronon3d/animation/animation_property_kind.hpp>

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

using AnimationProperty = AnimationPropertyKind;

/// Immutable, typed timeline entry. Frame values are authored-time data;
/// sampling converts them into frame parameters without rebuilding the plan.
struct TimelineProgram {
    std::uint32_t id{0};
    std::int64_t start_frame{0};
    std::int64_t duration_frames{0};
    std::uint32_t animation_index{0};
};

struct AnimationTrackProgram {
    std::uint32_t id{0};
    std::string property;
    std::uint32_t first_keyframe{0};
    std::uint32_t keyframe_count{0};
    AnimationProperty property_kind{AnimationProperty::Unknown};
    std::string easing{"linear"};
    // Cubic-bezier timing for easing="bezier", copied from the authored
    // track. Samplers build the identical EasingCurve from these fields so
    // scene-side and program-side evaluation cannot diverge.
    std::array<float, 4> bezier{0.25f, 0.1f, 0.25f, 1.0f};
    bool has_bezier{false};
};

struct CurveKeyframeProgram {
    std::int64_t frame{0};
    float value{0.0f};
};

/// Text payload and animator topology are kept typed and owned by the
/// immutable program. Runtime text layout receives only sampled parameters.
struct TextProgram {
    std::uint32_t id{0};
    std::uint32_t text_index{0};
    std::uint32_t animator_count{0};
};

/// Static parameter topology. Values are sampled per frame by the runtime;
/// this object contains no frame-owned state.
struct ParameterProgram {
    std::uint32_t id{0};
    std::uint32_t first_parameter{0};
    std::uint32_t parameter_count{0};
};

}  // namespace chronon3d::compiler
