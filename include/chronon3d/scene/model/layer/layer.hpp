#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/animation/effects/animated_transform.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/scene/model/layer/mask.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/scene/model/core/depth_role.hpp>
#include <chronon3d/scene/model/layer/track_matte.hpp>
#include <chronon3d/scene/model/core/transition.hpp>
#include <chronon3d/scene/model/shape/material_2_5d.hpp>
#include <chronon3d/scene/model/core/card3d_material.hpp>
#include <chronon3d/layout/layout_rules.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/compositor/composite_operator.hpp>
#include <string>
#include <vector>
#include <memory>
#include <memory_resource>

namespace chronon3d {

namespace video { struct VideoSource; }

// ── Time Remap (AE-4) ────────────────────────────────────────────────────────
// Per-layer time control: speed, reverse, freeze frame, and animated time remap.
// When active (speed != 1.0 || reverse || time_remap is animated || freeze_frame >= 0),
// Layer::local_frame() remaps the composition frame to a source frame.

struct TimeRemap {
    f32  speed{1.0f};          // playback speed multiplier (0.5 = half-speed, 2.0 = double, -1.0 = reverse)
    Frame freeze_frame{-1};    // if >= 0, hold this source frame forever
    AnimatedValue<f32> time_remap;  // maps comp frame → source frame (in frames). When animated, overrides speed.

    [[nodiscard]] bool active() const {
        return speed != 1.0f || freeze_frame >= 0 || time_remap.is_time_dependent();
    }
};

enum class LayerKind {
    Normal,       // standard layer: draws its own content, then effects applied
    Adjustment,   // no own content: effects applied to everything rendered below it
    Null,         // no rendering at all; useful as a parent for transform hierarchy
    Precomp,      // references a nested composition by name
    Video,        // plays a video file via VideoNode
    Glass         // frosted glass: blurs background within shapes, then draws content
};

struct Layer {
    std::pmr::string name;
    std::pmr::string parent_name;
    LayerKind kind{LayerKind::Normal};
    Transform transform{};
    AnimatedTransform anim_transform{};
    Frame from{0};
    Frame duration{-1};
    Frame time_offset{0};
    TimeRemap time_remap{};
    bool      visible{true};
    bool      uses_2_5d_projection{false};
    bool      hierarchy_resolved{false};
    bool      cache_static{false};
    Mask      mask{};
    EffectStack effects;     // ordered effect stack
    BlendMode blend_mode{BlendMode::Normal};
    CompositeOperator composite_operator{CompositeOperator::SourceOver};
    DepthRole   depth_role{DepthRole::None};
    f32         depth_offset{0.0f};
    LayoutRules layout{};
    TrackMatte  track_matte{};
    Material2_5D material{};
    Card3DMaterial card3d_material{};
    LayerTransitionSpec transition_in{};
    LayerTransitionSpec transition_out{};
    std::pmr::vector<RenderNode> nodes;
    std::pmr::string precomp_composition_name; // for LayerKind::Precomp

    // Video source parameters (isolated from FFmpeg/Backend headers)
    std::unique_ptr<video::VideoSource> video_source;

    // FontEngine pointer for precise text shaping / glyph metrics.
    // Inherited from LayerBuilder when the layer is built.
    FontEngine* font_engine{nullptr};

    // Cache for incremental scene fingerprinting
    mutable uint64_t m_static_hash{0};
    mutable bool m_static_hash_computed{false};

    [[nodiscard]] uint64_t get_static_hash() const;

    explicit Layer(std::pmr::memory_resource* res = std::pmr::get_default_resource());
    ~Layer();
    
    // Custom copy/move for unique_ptr
    Layer(const Layer& other);
    Layer& operator=(const Layer& other);
    Layer(Layer&& other) noexcept;
    Layer& operator=(Layer&& other) noexcept;

    [[nodiscard]] bool active_at(Frame frame) const {
        if (!visible) return false;
        if (frame < from) return false;
        if (duration < 0) return true;
        return frame < from + duration;
    }

    /// Legacy integer-frame local time (backward compatible).
    [[nodiscard]] Frame local_frame(Frame global_frame) const {
        const Frame raw = global_frame - from + time_offset;

        if (!time_remap.active()) {
            return raw;
        }

        // Freeze frame: always return the frozen source frame
        if (time_remap.freeze_frame >= 0) {
            return time_remap.freeze_frame;
        }

        // Animated time_remap curve: maps comp frame → source frame directly
        if (time_remap.time_remap.is_time_dependent()) {
            return Frame{static_cast<i64>(time_remap.time_remap.evaluate(raw))};
        }

        // Speed control: scale the local frame by the speed multiplier.
        f32 scaled;
        if (time_remap.speed < 0.0f && duration > 0) {
            scaled = static_cast<f32>(duration) + time_remap.speed * static_cast<f32>(raw);
        } else {
            scaled = static_cast<f32>(raw) * time_remap.speed;
        }
        return Frame{static_cast<i64>(std::round(scaled))};
    }

    /// Sub-frame aware local time — preserves fractional precision through
    /// offset, time remap, speed, and freeze-frame so that animated transforms
    /// evaluated in LayerBuilder::build() see true sub-frame differences.
    [[nodiscard]] SampleTime local_time(SampleTime global_time) const {
        const double raw_frame = global_time.frame
            - static_cast<double>(from) + static_cast<double>(time_offset);

        if (!time_remap.active()) {
            return SampleTime::from_frame(raw_frame, global_time.frame_rate);
        }

        // Freeze frame
        if (time_remap.freeze_frame >= 0) {
            return SampleTime::from_frame_int(time_remap.freeze_frame, global_time.frame_rate);
        }

        // Animated time_remap curve — evaluate at sub-frame precision
        if (time_remap.time_remap.is_time_dependent()) {
            const double mapped = static_cast<double>(
                time_remap.time_remap.evaluate(
                    SampleTime::from_frame(raw_frame, global_time.frame_rate)));
            return SampleTime::from_frame(mapped, global_time.frame_rate);
        }

        // Speed control — preserves fractional portion
        double scaled;
        if (time_remap.speed < 0.0 && static_cast<double>(duration) > 0.0) {
            scaled = static_cast<double>(duration) + time_remap.speed * raw_frame;
        } else {
            scaled = raw_frame * static_cast<double>(time_remap.speed);
        }
        return SampleTime::from_frame(scaled, global_time.frame_rate);
    }
};

} // namespace chronon3d
