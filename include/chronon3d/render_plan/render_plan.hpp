#pragma once

#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/color/hdr_mastering_metadata.hpp>
#include <chronon3d/animation/easing/spring.hpp>
#include <chronon3d/scene/model/core/transition.hpp>
#include <chronon3d/graphics/gradient.hpp>
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

enum class LayerType : std::uint8_t { Image, Video, Text, Color, Shape, Particles, Light, Adjustment, Mesh, Null };
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
    float taper_start{1.0f};
    float taper_end{1.0f};
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
enum class PathOperatorKindPlan : std::uint8_t { Repeater, RoundCorners, ZigZag, OffsetPath, PuckerBloat, Twist, WigglePath, Merge, Trim, Morph };
struct PathOperatorPlan {
    PathOperatorKindPlan kind{PathOperatorKindPlan::Repeater};
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
    std::vector<ShapePathCommandPlan> morph_target;
    std::optional<AnimationTrackPlan> morph_animation;
    std::vector<std::vector<ShapePathCommandPlan>> morph_keyframe_targets;
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
enum class FitMode : std::uint8_t { Cover, Contain, Stretch, None };
struct ImageCropPlan {
    bool enabled{false};
    std::array<float, 2> origin{0.0f, 0.0f};
    std::array<float, 2> size{1.0f, 1.0f};
    bool flip_x{false};
};
enum class OutputFormat : std::uint8_t { Png, Mp4, Mkv, WebM, PngSequence, Gif };
enum class VideoCodec : std::uint8_t { Auto, H264, H265, VP9, AV1 };
enum class RateControlMode : std::uint8_t { Crf, ConstantQp, Bitrate };

struct CanvasSpec {
    int width{0};
    int height{0};
    FrameRate fps{30, 1};
    Frame duration{0};
};

struct ShadowStyle {
    std::string color;
    std::optional<float> opacity;
    std::optional<float> blur;
    std::array<float, 2> offset{0.0f, 0.0f};
    std::size_t offset_dimensions{0};
};

struct BackdropBlurStyle {
    float blur{16.0f};
    float brightness{1.0f};
    float saturation{1.0f};
    float rim_light{0.0f};
    float refraction{0.0f};
    float surface_noise{0.0f};
};

// Per-text halo parameters. This is distinct from ShadowStyle: shadow is a
// translated blurred silhouette, while glow is lowered to GlowParams.
struct GlowStyle {
    float radius{24.0f};
    float intensity{0.25f};
    std::string color{"#FFFFFF"};
};

struct BackgroundStyle {
    std::string color;
    std::optional<float> opacity;
    std::optional<float> radius;
    std::array<float, 2> padding{0.0f, 0.0f};
    std::size_t padding_dimensions{0};
};

// Concrete text paint supplied by the caller. There is deliberately no
// family/weight lookup or preset/profile fallback at the render-plan boundary.
struct LayerStylePlan {
    std::string font;
    std::optional<float> font_size;
    std::string fill;
    std::string text_fit_mode{"none"};
    std::optional<float> min_font_size;
    std::optional<float> max_font_size;
    std::optional<StrokeStyle> stroke;
    std::optional<ShadowStyle> shadow;
    std::vector<ShadowStyle> shadows;
    std::optional<BackdropBlurStyle> backdrop;
    std::optional<GlowStyle> glow;
    std::optional<BackgroundStyle> background;
};

struct AnimationPlan {
    std::vector<AnimationTrackPlan> tracks;
};

struct MotionBlurPlan {
    float shutter_angle{180.0f};
    std::uint32_t samples{12};
};

/// Light kinds. `Radial` is the historical screen-space glow (default, output
/// unchanged). `Point`, `Spot` and `Directional` are real light sources; their
/// shading and cast shadows are implemented by the lighting slices of
/// TICKET-LIGHTS-SHADOWS-PARTICLES. The compiler rejects unsupported light
/// behavior explicitly rather than drawing the glow in its place.
enum class LightKindPlan : std::uint8_t { Radial, Point, Spot, Directional };

struct LightLayerPlan {
    float radius{256.0f};
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    float intensity{1.0f};
    LightKindPlan kind{LightKindPlan::Radial};
    /// Direction the light travels (Spot, Directional). Need not be unit length;
    /// the decoder rejects the zero vector.
    std::array<float, 3> direction{0.0f, 0.0f, -1.0f};
    /// Distance at which Point/Spot contribution reaches zero. 0 = unbounded.
    float range{0.0f};
    /// Falloff exponent: contribution ~ 1 / (1 + (d / radius)^decay).
    float decay{1.0f};
    /// Spot half-angle in radians, (0, pi].
    float angle{0.78539816339744830962f};
    /// Spot edge softness in [0, 1].
    float penumbra{0.0f};
    bool cast_shadow{false};
    /// Shadow blur radius in pixels, >= 0.
    float shadow_radius{1.0f};
    /// Depth bias that prevents self-shadowing, >= 0.
    float shadow_bias{0.0f};
};

enum class TrackMatteTypePlan : std::uint8_t {
    Alpha, AlphaInverted, Luma, LumaInverted
};

struct TrackMattePlan {
    std::string source;
    TrackMatteTypePlan type{TrackMatteTypePlan::Alpha};
};

struct TimeRemapPlan {
    std::optional<float> speed;
    bool reverse{false};
    std::optional<Frame> freeze_frame;
    std::optional<AnimationTrackPlan> animation;
};

// Camera values are expressed in world units and degrees. rotation_deg uses
// the canonical [tilt_x, pan_y, roll_z] order consumed by Camera2_5D.
struct CameraPlan {
    std::string type{"perspective"};
    std::array<float, 3> position{0.0f, 0.0f, -1000.0f};
    std::array<float, 3> rotation_deg{0.0f, 0.0f, 0.0f};
    float fov_deg{50.0f};
    float near{1.0f};
    float far{10000.0f};
    float zoom{1.0f};
    bool dof_enabled{false};
    float dof_focus_distance{1000.0f};
    float dof_aperture{0.015f};
    float dof_max_blur{24.0f};
};

struct CameraShakePlan {
    float amplitude_px{20.0f};
    float frequency_hz{8.0f};
    float decay{0.0f};
    std::uint32_t seed{0};
    float rotation_amplitude_deg{0.0f};
    Frame start_frame{0};
    Frame duration_frames{0}; // zero means no duration limit
};

enum class CameraPropertyPlan : std::uint8_t {
    PositionX, PositionY, PositionZ,
    RotationX, RotationY, RotationZ,
    FovDeg, Zoom, FocusDistance, Aperture, MaxBlur
};

struct CameraTrackPlan {
    CameraPropertyPlan property{CameraPropertyPlan::PositionZ};
    std::vector<AnimationKeyframePlan> keyframes;
    std::string easing{"linear"};
    std::optional<std::array<float, 4>> bezier_curve;
};

// Generic AE-style text animator contract. Editorial motion ids are lowered
// into these primitives before Chronon sees the plan.
struct TextSelectorPlan {
    std::string id;
    std::string unit{"glyph"};
    std::string shape{"smooth"};
    std::string order{"forward"};
    std::string combine{"replace"};
    AnimationTrackPlan start;
    AnimationTrackPlan end;
    AnimationTrackPlan offset;
    AnimationTrackPlan amount;
    bool exclude_spaces{true};
    bool randomize_order{false};
    std::uint64_t random_seed{0};
    float wiggly_frequency{1.0f};
    float wiggly_amplitude{0.25f};
    std::uint64_t wiggly_seed{0};
    float noise_frequency{1.0f};
    float noise_amplitude{0.25f};
    std::uint64_t noise_seed{0};
    std::uint32_t index_step{1};
    std::optional<std::string> semantic_id;
};

struct TextAnimatorPlan {
    std::string id;
    std::vector<TextSelectorPlan> selectors;
    std::vector<AnimationTrackPlan> properties;
};

struct TransitionPlan {
    std::string id;
    std::string kind{"none"};
    Frame duration_frames{1};
    Frame delay_frames{0};
    std::string easing{"linear"};
    std::string direction;
    float distance{1.0f};
    float zoom_amount{0.2f};
    float feather{0.1f};
    std::array<float, 2> center{0.5f, 0.5f};
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    float seed{1.2f};
    std::array<float, 4> inner_color{1.0f, 0.45f, 0.0f, 1.0f};
    std::array<float, 4> mid_color{1.0f, 0.45f, 0.0f, 1.0f};
    std::array<float, 4> outer_color{1.0f, 1.0f, 1.0f, 1.0f};
    float speed{1.35f};
    float remotion_direction{3.0f};
    float angle{0.0f};
    float dip_px{60.0f};
};

struct LayerPlan {
    struct StaggerPlan {
        std::string group_id;
        std::uint32_t per_item_frames{0};
        std::string order{"forward"};
        std::uint32_t seed{0};
        std::string track_property{"opacity"};
        std::string easing;
    };
    struct FollowPathDriverPlan {
        std::string path_id;
        std::vector<ShapePathCommandPlan> path_commands;
        AnimationTrackPlan progress;
        bool auto_orient{false};
        float offset_deg{0.0f};
        bool clamp_end{true};
    };
    std::optional<FollowPathDriverPlan> follow_path;
    struct FlowLayoutPlan { std::string group_id; std::string axis{"row"}; std::string align{"start"}; std::string justify{"start"}; float gap{0}; bool wrap{false}; std::array<float,2> cell_size{0,0}; std::optional<StaggerPlan> stagger; };
    struct GridLayoutPlan { std::string group_id; std::string fit{"auto"}; int columns{1}; int rows{1}; std::array<float,2> gap{0,0}; std::array<float,2> cell_size{0,0}; std::optional<StaggerPlan> stagger; };
    std::optional<FlowLayoutPlan> flow_layout;
    std::optional<GridLayoutPlan> grid_layout;
    std::optional<StaggerPlan> stagger_group;
    std::string id;
    LayerType type{LayerType::Color};
    std::optional<std::string> parent;
    std::optional<TrackMattePlan> track_matte;
    std::optional<TimeRemapPlan> time_remap;
    std::string asset;
    // Transfer encoding for byte-backed image layers; tagged EXR and video
    // sources use their own metadata and remain handled by their decoders.
    std::string input_color_space{"srgb"};
    // Optional named OCIO source space. Requires the plan-level OCIO contract.
    std::string ocio_input_space;
    std::string source;
    std::string text;
    std::vector<TextSpanOverride> text_spans;
    std::optional<TextCounterPlan> text_counter;

    // Normalized concrete font asset path copied from style.font by decoder.
    // Kept as an execution field so asset preflight remains simple.
    std::string font;
    std::optional<LayerStylePlan> style;
    std::optional<MotionBlurPlan> motion_blur;
    std::optional<ShapeContentPlan> shape;
    std::optional<InlineMeshPlan> inline_mesh;
    // V3 inline meshes can carry baked per-frame position samples while keeping
    // immutable topology, UVs and material in InlineMeshPlan.
    std::optional<ParticleEmitterParams> particle_emitter;
    std::optional<LightLayerPlan> light;
    struct PathMaskPlan {
        std::string type{"path"};
        std::string mode{"add"};
        // Text mask source layer. The layer must be a prepared text layer;
        // graph compilation resolves its same-frame rendered alpha.
        std::string text_source;
        std::vector<ShapePathCommandPlan> path;
        std::vector<ShapePathCommandPlan> morph_target;
        std::optional<Field2DPlan> field;
        std::optional<AnimationTrackPlan> morph_animation;
        std::array<float, 2> position{0.0f, 0.0f};
        std::array<float, 2> size{0.0f, 0.0f};
        float radius{0.0f};
        float feather{0.0f};
        float expansion{0.0f};
        float opacity{1.0f};
        std::optional<AnimationTrackPlan> feather_track;
        std::optional<AnimationTrackPlan> expansion_track;
        std::optional<AnimationTrackPlan> opacity_track;
        bool inverted{false};
    };
    std::optional<PathMaskPlan> path_mask;
    std::vector<PathMaskPlan> masks;
    enum class EffectKindPlan : std::uint8_t { Lut3D, Echo, Keying, Threshold, Mosaic, GaussianBlur, DropShadow, Bloom, Glow, Noise, FractalNoise, CornerPin, Twirl, Bulge, MeshWarp, Sharpen, RoughenEdges, TurbulentDisplace, WaveWarp, Ripple, DisplacementMap, LightRays, ChromaticAberration, Halftone, DirectionalBlur, RadialBlur, Vignette, Catalog, LensDistortion, LuminanceResponse, FoldWarp, CreaseShading, NoiseSampler, BandedGrain };
    enum class KeyingModePlan : std::uint8_t { Luma, Chroma };
    struct EffectPlan {
        EffectKindPlan kind{EffectKindPlan::Lut3D};
        std::string canonical_effect_id;
        struct ParamValue { std::string param; float value{0.0f}; };
        std::vector<ParamValue> canonical_params;
        std::string asset;
        float amount{1.0f};
        float roughen_scale{8.0f};
        std::uint32_t roughen_complexity{1};
        std::uint32_t roughen_seed{0};
        bool roughen_is_animated{false};
        std::uint32_t echoes{3};
        float decay{0.5f};
        std::uint32_t time_offset_frames{1};
        KeyingModePlan keying_mode{KeyingModePlan::Chroma};
        std::array<float, 3> key_color{0.0f, 1.0f, 0.0f};
        float threshold{0.20f};
        float softness{0.08f};
        float spill{0.75f};
        // Screen-matte refinement + garbage matte (see KeyingParams).
        float clip_black{0.0f};
        float clip_white{1.0f};
        float choke{0.0f};
        std::array<float, 4> garbage_rect{0.0f, 0.0f, 0.0f, 0.0f};
        float garbage_feather{0.0f};
        float threshold_softness{0.0f};
        std::uint32_t block_size{8};
        float chromatic_red_offset{2.0f};
        float chromatic_blue_offset{-2.0f};
        std::uint8_t chromatic_offset_mode{0}; // 0 = Constant, 1 = Radial, 2 = Velocity
        std::array<float, 2> chromatic_velocity{0.0f, 0.0f};
        // translation x/y, scale x/y, rotation degrees, opacity
        std::array<float, 6> chromatic_red_transform{0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
        std::array<float, 6> chromatic_green_transform{0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
        std::array<float, 6> chromatic_blue_transform{0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
        std::uint32_t halftone_size{8};
        float halftone_angle{0.0f};
        float halftone_amount{1.0f};
        float directional_angle{0.0f};
        float directional_length{0.0f};
        int directional_samples{0};
        std::array<float, 2> radial_center{0.5f, 0.5f};
        float radial_amount{0.0f};
        int radial_render_samples{8};
        int radial_preview_samples{4};
        float radius{0.0f};
        std::array<float, 2> offset{0.0f, 8.0f};
        std::array<float, 4> effect_color{0.0f, 0.0f, 0.0f, 0.35f};
        float bloom_threshold{0.80f};
        float bloom_intensity{0.60f};
        float glow_intensity{0.25f};
        std::array<float, 2> light_rays_origin{0.5f, 0.5f};
        float light_rays_length{320.0f};
        float light_rays_density{0.75f};
        std::array<float, 4> light_rays_color{1.0f, 0.86f, 0.55f, 1.0f};
        float light_rays_decay{0.92f};
        std::uint32_t seed{0};
        bool animated{false};
        bool rgb_noise{false};
        float noise_size{1.0f};
        float grain_shadow_response{1.0f};
        float grain_midtone_response{1.0f};
        float grain_highlight_response{1.0f};
        std::uint32_t octaves{4};
        std::optional<Field2DPlan> noise_field;
        float frequency{0.015f};
        float amplitude{1.0f};
        float evolution{0.0f};
        float contrast{1.0f};
        float brightness{0.0f};
        float warp_size{0.1f}, warp_evolution{0.0f}, warp_speed{0.0f}, warp_wavelength{0.2f}, warp_phase{0.0f}, warp_width{0.1f}, warp_height{0.05f}, warp_damping{0.5f};
        std::uint32_t warp_complexity{2};
        std::array<float, 2> warp_direction{1.0f, 0.0f};
        std::uint8_t wave_type{0};
        std::optional<float> posterize_fps;
        std::optional<AnimationTrackPlan> time_curve;
        std::string map_layer;
        std::optional<Field2DPlan> map_field;
        float map_intensity_x{0.05f}, map_intensity_y{0.05f};
        std::uint8_t map_channel{0};
        // Warp family — quad corners (TL, TR, BR, BL as x,y pairs), radial
        // controls and the mesh-warp control grid.
        std::array<float, 8> quad{0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
        std::array<float, 2> warp_center{0.5f, 0.5f};
        float warp_radius{0.5f};
        float warp_falloff{1.0f};
        float warp_angle{90.0f};
        float warp_amount{0.5f};
        std::uint32_t mesh_cols{2};
        std::uint32_t mesh_rows{2};
        float mesh_smoothness{0.0f};
        std::vector<float> mesh_offsets{}; // cols*rows x,y pairs

        // Lens distortion
        float lens_curvature{0.15f};
        std::array<float, 2> lens_center{0.5f, 0.5f};
        float lens_scale{1.0f};

        // Fold warp & Crease shading
        std::uint8_t fold_axis{0};
        float fold_position{0.5f};
        float fold_amount{0.25f};
        float fold_radius{0.20f};
        float fold_falloff{1.0f};
        float fold_perspective{0.05f};

        std::uint8_t crease_axis{0};
        float crease_position{0.5f};
        float crease_width{24.0f};
        float crease_dark_side{0.35f};
        float crease_light_side{0.20f};
        float crease_softness{0.50f};

        // Luminance response
        float response_threshold{0.70f};
        float response_softness{0.20f};
        float response_radius{18.0f};
        std::array<float, 4> response_tint{1.0f, 0.35f, 0.15f, 1.0f};
        float response_intensity{1.0f};
        std::uint8_t response_blend{0}; // 0 = Screen, 1 = Add, 2 = Over

        // Noise sampler (2.0)
        std::uint8_t noise_sampler_type{0}; // 0 = Simplex, 1 = Worley, 2 = Ridged, 3 = Value, 4 = Blue
        float noise_lacunarity{2.0f};
        float noise_gain{0.5f};
        float noise_domain_warp{0.0f};
        float noise_remap_min{0.0f};
        float noise_remap_max{1.0f};
        std::uint32_t noise_quantize_steps{0};

        // Banded grain (2.0)
        struct GrainBandPlan {
            float scale{1.0f};
            float amplitude{0.05f};
            std::uint32_t octave{0};
        };
        std::vector<GrainBandPlan> grain_bands;
    };
    struct EffectParamTrackPlan {
        std::string effect_id;
        std::string param;
        AnimationTrackPlan track;
    };
    std::vector<EffectParamTrackPlan> effect_param_tracks;
    std::vector<EffectPlan> effects;
    std::optional<BackdropFilterPlan> backdrop_filter;

    std::array<float, 2> size{0.0f, 0.0f};
    std::size_t size_dimensions{0};
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::size_t position_dimensions{0};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    std::size_t scale_dimensions{0};
    // Euler angles in degrees, [x, y, z], for both static and tracked values.
    std::array<float, 3> rotation{0.0f, 0.0f, 0.0f};
    std::size_t rotation_dimensions{0};
    float skew_angle_deg{0.0f};
    float skew_axis_deg{0.0f};
    // Spatial contract: ordinary layers stay in the flat canvas plane. Set
    // this flag to route a planar image/shape through the scene camera, making
    // its Z position and X/Y rotations meaningful (2.5D). Mesh layers are
    // camera-backed by their kind and do not need this flag. This switch does
    // not mean “arbitrary 3D geometry”; that is represented by Mesh.
    bool enable_3d{false};
    // Mesh shadow participation is opt-in in the wire contract. Absence keeps
    // the historical compiler behavior and therefore old fingerprints stable.
    std::optional<bool> casts_shadows;
    std::optional<bool> accepts_shadows;
    // Keep a layer pinned to output-canvas coordinates while a scene camera
    // is active. This is independent of enable_3d and is for screen overlays
    // such as backgrounds or HUD elements.
    bool screen_space{false};
    // Pixel insets [top, right, bottom, left] for title/action-safe layout.
    std::optional<std::array<float, 4>> safe_insets;
    std::optional<std::string> depth_role;
    float depth_offset{0.0f};
    // Rounded image corner radius, evaluated by the image compositor's
    // analytic/SDF-style mask. Zero keeps the regular image fast path.
    std::optional<float> radius;

    std::optional<Frame> start_frame;
    std::optional<Frame> duration_frames;
    std::optional<TransitionPlan> transition_in;
    std::optional<TransitionPlan> transition_out;
    std::optional<FitMode> fit;
    std::optional<ImageCropPlan> crop;
    std::optional<AnimationPlan> animation;
    std::optional<GradientAnimationPlan> fill_gradient_animation;
    std::vector<TextAnimatorPlan> text_animators;

    std::optional<chronon3d::BlendMode> blend_mode;
    std::optional<float> opacity;
    bool loop{false};
    bool auto_orient{false};
};

// Immutable producer-side preparation metadata. The JSON sidecar is decoded
// and validated at the CLI boundary, then carried into the canonical compiler
// so equal text identities can share one PreparedTextRun. It contains no
// backend handles and is intentionally not a second render plan.
struct PreparedOverlayPackage {
    struct OverlayBinding {
        std::string id;
        std::string kind;
        std::string text_key;
        std::string asset_key;
    };
    struct TextBinding {
        std::string key;
        std::string text;
    };
    struct AssetBinding {
        std::string key;
        std::string logical_path;
        std::string content_hash;
    };

    std::string plan_id;
    std::string language;
    std::string content_hash;
    std::vector<OverlayBinding> overlays;
    std::vector<TextBinding> text_runs;
    std::vector<AssetBinding> assets;
};

struct OutputSpec {
    std::string path;
    std::string color_space{"srgb"};
    float reference_white_nits{100.0f};
    std::optional<chronon3d::color::HdrMasteringMetadata> hdr_mastering_metadata;
    OutputFormat format{OutputFormat::Png};
    VideoCodec codec{VideoCodec::Auto};
    RateControlMode rate_control{RateControlMode::Crf};
    std::int64_t bitrate{0};
    int crf{0};
    int qp{-1};
    std::string profile_id;
};

struct AudioClipPlan {
    std::string id;
    std::string asset;
    std::optional<Frame> clip_in;
    std::optional<Frame> clip_out;
    std::optional<Frame> start;
    float gain_db{0.0f};
    bool muted{false};
    std::optional<AnimationTrackPlan> gain;
    std::string role{"music"};
};

struct AudioMixPlan {
    std::vector<AudioClipPlan> clips;
    float master_gain_db{0.0f};
    float duck_amount_db{-9.0f};
    float duck_attack_ms{120.0f};
    float duck_release_ms{400.0f};
};

struct LightingRigPlan {
    std::array<float, 3> key_direction{-0.2857143f, 0.8571429f, -0.4285714f};
    std::array<float, 4> key_color{1.0f, 1.0f, 1.0f, 1.0f};
    float key_intensity{0.80f};
    std::array<float, 4> ambient_color{0.6f, 0.65f, 0.7f, 1.0f};
    float ambient_intensity{0.25f};
    bool rim_enabled{false};
    std::array<float, 4> rim_color{1.0f, 1.0f, 1.0f, 1.0f};
    float rim_intensity{0.30f};
    float rim_power{2.2f};
};

struct RenderPlan {
    std::string schema{kRenderPlanSchemaV2};
    std::string job_id{"chronon_plan"};
    std::uint64_t content_fingerprint{0};
    std::vector<std::string> warnings;
    CanvasSpec canvas;
    // Full-scene shutter accumulation; separate from the per-layer analytic blur.
    std::optional<MotionBlurPlan> temporal_motion_blur;
    std::optional<CameraPlan> camera;
    std::optional<CameraShakePlan> camera_shake;
    std::optional<std::vector<CameraTrackPlan>> camera_animation;
    std::optional<LightingRigPlan> lighting_rig;
    struct ColorManagementPlan {
        std::string ocio_config;
        std::string working_space;
        // Optional project default for video layers. A layer-level
        // ocio_input_space overrides this value.
        std::string video_input_space;
        // Optional display-referred output transform. Both names must be
        // present together; empty values preserve output.color_space behavior.
        std::string output_display;
        std::string output_view;
    };
    std::optional<ColorManagementPlan> color_management;
    std::vector<LayerPlan> layers;
    std::optional<AudioMixPlan> audio;
    std::shared_ptr<const PreparedOverlayPackage> prepared_overlay_package{};
    OutputSpec output;
    RenderBudget budget{};
};

struct PlanDecodeError {
    std::string path;
    std::string message;
    std::string code;
    std::string component;
};

[[nodiscard]] std::optional<PlanDecodeError> validate_render_budget(
    const RenderPlan& plan,
    const RenderBudget& budget = {});

[[nodiscard]] std::optional<PlanDecodeError> validate_render_plan_budget(
    const RenderPlan& plan,
    const RenderBudget& budget = {});

[[nodiscard]] std::uint64_t compute_render_plan_content_fingerprint(
    const RenderPlan& plan);

[[nodiscard]] std::optional<PlanDecodeError> validate_render_plan_layer_relations(
    const RenderPlan& plan);

Result<RenderPlan, PlanDecodeError> decode_render_plan(const nlohmann::json& root);

}  // namespace chronon3d::render_plan
