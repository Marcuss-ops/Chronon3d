#pragma once

#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/color/hdr_mastering_metadata.hpp>
#include <chronon3d/animation/easing/spring.hpp>
#include <chronon3d/scene/model/core/transition.hpp>
#include <chronon3d/graphics/gradient.hpp>
#include <chronon3d/graphics/pattern_tile.hpp>
#include <chronon3d/graphics/field2d.hpp>
#include <chronon3d/graphics/gradient_mesh.hpp>
#include <chronon3d/graphics/stroke/brush_profile.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/result.hpp>
#include <chronon3d/core/types/time.hpp>
#include <chronon3d/render_plan/render_budget.hpp>
#include <chronon3d/render_plan/inline_mesh.hpp>
#include <chronon3d/scene/model/shape/particles.hpp>
#include <chronon3d/text/text_span_override.hpp>
#include <chronon3d/effects/backdrop_filter.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace chronon3d::render_plan {

using Field2DPlan = graphics::Field2D;

using SpringPlan = chronon3d::SpringConfig;

inline constexpr const char* kRenderPlanSchemaV2 = "chronon.render-plan.v2";
inline constexpr const char* kRenderPlanSchemaV3 = "chronon.render-plan.v3";

enum class LayerType : std::uint8_t { Image, Video, Text, Color, Shape, Particles, Light, Adjustment, Mesh };
enum class ShapeContentTypePlan : std::uint8_t {
    Rect, RoundedRect, Ellipse, Line, Path,
    Star, Polygon, Arrow, Arc, Grid, DotGrid, DeviceFrame
};
enum class DeviceFrameKindPlan : std::uint8_t { Phone, Browser };
enum class ShapeFillRulePlan : std::uint8_t { EvenOdd, NonZero };
enum class ShapePathCommandTypePlan : std::uint8_t {
    MoveTo, LineTo, QuadraticTo, CubicTo, Close
};

struct ShapePathCommandPlan {
    ShapePathCommandTypePlan type{ShapePathCommandTypePlan::MoveTo};
    std::array<float, 2> point{0.0f, 0.0f};
    std::array<float, 2> control1{0.0f, 0.0f};
    std::array<float, 2> control2{0.0f, 0.0f};
};

struct StrokeStyle {
    std::string color;
    std::optional<float> width;
    std::optional<float> trim_start;
    std::optional<float> trim_end;
    std::optional<std::array<float, 4>> solid_color;
    std::optional<graphics::GradientDefinition> gradient;
    std::optional<graphics::BrushProfile> brush;
    std::vector<float> dash_array;
    float dash_offset{0.0f};
    std::string cap{"butt"};
    std::string join{"miter"};
    float miter_limit{10.0f};
    float taper_start{1.0f};
    float taper_end{1.0f};
    std::optional<float> opacity;
};

struct AnimationKeyframePlan {
    Frame frame{0};
    std::vector<float> value;
    std::optional<std::string> interpolation;
    std::optional<std::string> easing;
    std::optional<std::array<std::string, 2>> easing_xy;
    std::optional<std::array<float, 4>> bezier_curve;
    bool roving{false};
    std::optional<float> temporal_in_dx;
    std::optional<float> temporal_in_dy;
    std::optional<float> temporal_out_dx;
    std::optional<float> temporal_out_dy;
    std::optional<SpringPlan> spring;
};

struct AnimationTrackPlan {
    std::string property;
    std::vector<AnimationKeyframePlan> keyframes;
    std::string easing{"linear"};
    std::optional<std::array<float, 4>> bezier_curve;
    std::optional<SpringPlan> spring;
    // Re-map evaluation time after the authored keyframe range. "none" holds
    // the end value; cycle and pingpong repeat the segment; offset adds the
    // authored end-to-start delta for each completed cycle.
    std::string loop_mode{"none"};
    struct WiggleDriverPlan {
        float frequency{2.0f};
        float amplitude{30.0f};
        std::uint32_t seed{0};
    };
    std::optional<WiggleDriverPlan> wiggle;
    struct OscillateDriverPlan {
        float frequency_hz{1.0f};
        float amplitude{1.0f};
        float phase_deg{0.0f};
        std::string waveform{"sine"};
        float offset{0.0f};
    };
    struct RandomHoldDriverPlan {
        std::uint32_t interval_frames{1};
        float min{0.0f};
        float max{1.0f};
        std::uint32_t seed{0};
    };
    struct PosterizeTimeDriverPlan { float fps{8.0f}; };
    struct DataFeedDriverPlan {
        std::vector<float> values;
        std::string asset;
        std::string sha256;
        std::uint32_t item_count{0};
        std::int32_t index_offset{0};
        std::string interpolation{"step"};
    };
    struct AudioReactDriverPlan {
        std::string source;
        std::string feature{"rms"};
        std::optional<std::array<float, 2>> band_hz;
        std::uint32_t smoothing_frames{1};
        float gain{1.0f};
        std::string map{"linear"};
        std::string source_sha256;
        std::vector<float> baked_values;
    };
    std::optional<OscillateDriverPlan> oscillate;
    std::optional<RandomHoldDriverPlan> random_hold;
    std::optional<PosterizeTimeDriverPlan> posterize_time;
    std::optional<DataFeedDriverPlan> data_feed;
    std::optional<AudioReactDriverPlan> audio_react;
};

struct PathRepeaterPlan {
    std::uint32_t copies{1};
    std::array<float, 2> position{0.0f, 0.0f};
    std::array<float, 2> scale{1.0f, 1.0f};
    float rotation_degrees{0.0f};
    std::array<float, 2> anchor{0.0f, 0.0f};
    float opacity_decay{1.0f};
};
enum class PathOperatorKindPlan : std::uint8_t { Repeater, RoundCorners, ZigZag, OffsetPath, PuckerBloat, Twist, WigglePath, Merge, Trim, Morph, BooleanUnion };
struct PathOperatorPlan {
    PathOperatorKindPlan kind{PathOperatorKindPlan::Repeater};
    /// Optional SVG element/group target; empty applies to all authored paths.
    std::string target_id;
    PathRepeaterPlan repeater{};
    float radius{0.0f};
    float size{0.0f};
    float points_per_unit{0.1f};
    bool smooth{false};
    float amount{0.0f};
    float miter_limit{4.0f};
    std::array<float, 2> anchor{0.0f, 0.0f};
    float degrees{0.0f};
    std::array<float, 2> center{0.0f, 0.0f};
    float frequency{1.0f};
    std::uint32_t seed{0};
    std::vector<std::vector<ShapePathCommandPlan>> paths;
    std::vector<ShapePathCommandPlan> morph_target;
    float morph_progress{0.0f};
    std::optional<AnimationTrackPlan> morph_animation;
    std::vector<std::vector<ShapePathCommandPlan>> morph_keyframe_targets;
    // Trim Paths: visible arc-length window [trim_start, trim_end] shifted by
    // trim_offset (all fractions of the subpath length; the window wraps).
    float trim_start{0.0f};
    float trim_end{1.0f};
    float trim_offset{0.0f};
    std::optional<AnimationTrackPlan> trim_animation;
    /// Repeater keyframes: position x/y, scale x/y, rotation degrees.
    std::optional<AnimationTrackPlan> repeater_animation;
};

struct GradientKeyframePlan {
    Frame frame{0};
    graphics::GradientDefinition value;
};

struct GradientAnimationPlan {
    std::vector<GradientKeyframePlan> keyframes;
    std::string easing{"linear"};
    std::optional<std::array<float, 4>> bezier_curve;
};

struct ImportedSubpathPlan {
    std::vector<ShapePathCommandPlan> path;
    std::optional<std::array<float, 4>> fill_color;
    std::optional<AnimationTrackPlan> fill_color_animation;
    std::optional<AnimationTrackPlan> fill_opacity_animation;
    std::optional<graphics::GradientDefinition> fill_gradient;
    std::optional<GradientAnimationPlan> fill_gradient_animation;
    std::optional<StrokeStyle> stroke;
    std::optional<AnimationTrackPlan> stroke_color_animation;
    std::optional<AnimationTrackPlan> stroke_opacity_animation;
    std::optional<AnimationTrackPlan> stroke_width_animation;
    std::optional<AnimationTrackPlan> svg_opacity_animation;
    std::optional<AnimationTrackPlan> svg_translation_animation;
    std::optional<AnimationTrackPlan> svg_scale_animation;
    std::optional<AnimationTrackPlan> svg_rotation_animation;
    struct SvgTransformAnimationPlan {
        std::string operation; // translate | scale | rotate
        AnimationTrackPlan animation;
    };
    /// Ordered SVG animateTransform operations, outermost first.
    std::vector<SvgTransformAnimationPlan> svg_transform_animations;
    std::vector<ShapePathCommandPlan> morph_target;
    std::optional<AnimationTrackPlan> morph_animation;
    std::vector<std::vector<ShapePathCommandPlan>> morph_keyframe_targets;
    /// Source SVG identity retained through RenderPlan lowering for addressing.
    std::string source_id;
    std::vector<std::string> source_group_ids;
    ShapeFillRulePlan fill_rule{ShapeFillRulePlan::NonZero};
    /// ADR-035 isolated group (innermost enclosing opacity < 1 group):
    /// children paint at full alpha and the surface composites once.
    struct SvgGroupIsolationPlan {
        std::uint32_t id{0};
        float opacity{1.0f};
        std::optional<AnimationTrackPlan> opacity_animation;
    };
    std::optional<SvgGroupIsolationPlan> svg_group;
    /// Nested isolated SVG groups, outermost first. `svg_group` is kept as
    /// the innermost compatibility view.
    std::vector<SvgGroupIsolationPlan> svg_groups;
    /// Per-element clip/mask geometries, outermost scope first.
    std::vector<std::vector<ShapePathCommandPlan>> svg_local_masks;
    struct GroupTransformPlan {
        std::array<float, 2> position{0.0f, 0.0f};
        std::array<float, 2> anchor{0.0f, 0.0f};
        std::array<float, 2> scale{1.0f, 1.0f};
        float rotation_degrees{0.0f};
        float opacity{1.0f};
        std::optional<AnimationTrackPlan> position_animation;
        std::optional<AnimationTrackPlan> scale_animation;
        std::optional<AnimationTrackPlan> rotation_animation;
        std::optional<AnimationTrackPlan> opacity_animation;
    };
    std::optional<GroupTransformPlan> lottie_group_transform;
};

struct SvgElementAnimationPlan {
    std::string target_id;
    std::string property;
    AnimationTrackPlan animation;
};

struct DeviceFramePlan {
    std::string frame_type{"browser"}; // browser | phone | window
    float radius{12.0f}; // corner radius px
    float border_width{1.0f};
    std::string border_color{"#2A2A2A"};
    bool show_chrome{true}; // traffic lights / notch
};

struct CounterPlan {
    float from{0.0f};
    float to{100.0f};
    Frame start_frame{0};
    Frame duration_frames{30};
    std::string easing{"out_expo"};
    std::string format{"plain"}; // plain | currency | percent | compact
    std::string currency{"USD"};
    int decimals{0}; // 0..6
    std::string prefix{};
    std::string suffix{};
};

struct TextCounterPlan {
    CounterPlan counter;
    std::string token{"{{value}}"};
};

struct ShapeContentPlan {
    ShapeContentTypePlan type{ShapeContentTypePlan::Rect};
    // Resolver-backed SVG reference. Expanded into path commands before the
    // composition compiler sees the plan.
    std::optional<std::string> svg_asset;
    // Bounded Bodymovin source, lowered before plan compilation.
    std::optional<std::string> lottie_asset;
    float radius{0.0f};
    std::optional<std::array<float, 4>> corner_radius;
    std::optional<std::array<float, 4>> fill_color;
    ShapeFillRulePlan fill_rule{ShapeFillRulePlan::EvenOdd};
    std::optional<graphics::GradientDefinition> gradient;
    std::optional<Field2DPlan> field;
    // Maps the scalar Field2D sample to color; gradient geometry is ignored.
    std::optional<graphics::GradientDefinition> field_ramp;
    // Sample scalar fields at 1/2/4x fewer pixels per axis, then interpolate.
    std::uint32_t field_render_scale{1};
    // Normalized coordinate drift per second; sampled from frame / fps.
    std::optional<Vec2> field_drift;
    std::optional<graphics::GradientMesh> gradient_mesh;
    std::optional<StrokeStyle> stroke;
    std::vector<ShapePathCommandPlan> path;
    // Resolver-backed SVG/Lottie elements retain authored path/style order as
    // deterministic sibling path nodes without widening the path rasterizer.
    std::vector<ImportedSubpathPlan> imported_subpaths;
    /// Timeline bindings resolved against SVG source ids during import lowering.
    std::vector<SvgElementAnimationPlan> svg_element_animations;
    std::vector<PathOperatorPlan> operators;
    // ── Star / Polygon ──────────────────────────────────────────────
    std::optional<int> points; // 3..64
    float inner_radius_ratio{0.4f}; // star inner/outer [0.05,0.95]
    float shape_rotation_deg{0.0f};
    // ── Arrow ────────────────────────────────────────────────────────
    std::optional<float> arrow_head_length; // px, default 0.3*width
    std::optional<float> arrow_head_width;  // px, default 0.6*width
    float arrow_shaft_width{4.0f};
    std::string arrow_direction{"right"}; // right | left | up | down | both
    // ── Grid / DotGrid ───────────────────────────────────────────────
    float grid_spacing{24.0f}; // px [4,512]
    float grid_line_width{1.0f}; // px [0.25,32]
    float dot_radius{3.0f}; // px [0.5,64]
    // ── Arc ──────────────────────────────────────────────────────────
    float arc_start_deg{0.0f}; // degrees [0,360)
    float arc_sweep_deg{270.0f}; // degrees (1..359.9)
    float arc_thickness{8.0f}; // px, 0 = stroked arc
    // ── DeviceFrame (additive payload) ─
    DeviceFrameKindPlan device_frame_kind{DeviceFrameKindPlan::Phone};
    bool device_frame_chrome{true};
    std::optional<DeviceFramePlan> device_frame;
};

}  // namespace chronon3d::render_plan
