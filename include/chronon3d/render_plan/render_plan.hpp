#pragma once

#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/graphics/gradient.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/result.hpp>
#include <chronon3d/core/types/time.hpp>
#include <chronon3d/render_plan/render_budget.hpp>
#include <chronon3d/scene/model/shape/particles.hpp>
#include <chronon3d/text/text_span_override.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace chronon3d::render_plan {

inline constexpr const char* kRenderPlanSchemaV2 = "chronon.render-plan.v2";
inline constexpr const char* kRenderPlanSchemaV3 = "chronon.render-plan.v3";

enum class LayerType : std::uint8_t { Image, Video, Text, Color, Shape, Particles, Light, Adjustment };
enum class ShapeContentTypePlan : std::uint8_t { Rect, RoundedRect, Ellipse, Line, Path };
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
    std::vector<float> dash_array;
    float dash_offset{0.0f};
};

struct AnimationKeyframePlan {
    Frame frame{0};
    std::vector<float> value;
};

struct AnimationTrackPlan {
    std::string property;
    std::vector<AnimationKeyframePlan> keyframes;
    std::string easing{"linear"};
    std::optional<std::array<float, 4>> bezier_curve;
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
    // Trim Paths: visible arc-length window [trim_start, trim_end] shifted by
    // trim_offset (all fractions of the subpath length; the window wraps).
    float trim_start{0.0f};
    float trim_end{1.0f};
    float trim_offset{0.0f};
};

struct ShapeContentPlan {
    ShapeContentTypePlan type{ShapeContentTypePlan::Rect};
    float radius{0.0f};
    std::optional<std::array<float, 4>> corner_radius;
    std::optional<std::array<float, 4>> fill_color;
    std::optional<graphics::GradientDefinition> gradient;
    std::optional<StrokeStyle> stroke;
    std::vector<ShapePathCommandPlan> path;
    std::vector<PathOperatorPlan> operators;
};
enum class FitMode : std::uint8_t { Cover, Contain, Stretch, None };
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

// Generic concrete property track. `property` names engine properties only;
// editorial ids (preset/unit/role/profile) are intentionally not representable.
struct AnimationPlan {
    std::vector<AnimationTrackPlan> tracks;
};

struct MotionBlurPlan {
    float shutter_angle{180.0f};
    std::uint32_t samples{12};
};

struct LightLayerPlan {
    float radius{256.0f};
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    float intensity{1.0f};
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
};

enum class CameraPropertyPlan : std::uint8_t {
    PositionX, PositionY, PositionZ,
    RotationX, RotationY, RotationZ,
    FovDeg, Zoom
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
    std::optional<std::string> semantic_id;
};

struct TextAnimatorPlan {
    std::string id;
    std::vector<TextSelectorPlan> selectors;
    std::vector<AnimationTrackPlan> properties;
};

struct LayerPlan {
    std::string id;
    LayerType type{LayerType::Color};
    std::optional<std::string> parent;
    std::optional<TrackMattePlan> track_matte;
    std::optional<TimeRemapPlan> time_remap;
    std::string asset;
    std::string source;
    std::string text;
    std::vector<TextSpanOverride> text_spans;

    // Normalized concrete font asset path copied from style.font by decoder.
    // Kept as an execution field so asset preflight remains simple.
    std::string font;
    std::optional<LayerStylePlan> style;
    std::optional<MotionBlurPlan> motion_blur;
    std::optional<ShapeContentPlan> shape;
    std::optional<ParticleEmitterParams> particle_emitter;
    std::optional<LightLayerPlan> light;
    struct PathMaskPlan {
        std::vector<ShapePathCommandPlan> path;
        float feather{0.0f};
        float expansion{0.0f};
        bool inverted{false};
    };
    std::optional<PathMaskPlan> path_mask;
    enum class EffectKindPlan : std::uint8_t { Lut3D, Echo, Keying, Threshold, Mosaic, GaussianBlur, DropShadow, Bloom, Glow, Noise, FractalNoise, CornerPin, Twirl, Bulge, MeshWarp, Sharpen, TurbulentDisplace, WaveWarp, Ripple, DisplacementMap };
    enum class KeyingModePlan : std::uint8_t { Luma, Chroma };
    struct EffectPlan {
        EffectKindPlan kind{EffectKindPlan::Lut3D};
        std::string asset;
        float amount{1.0f};
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
        float radius{0.0f};
        std::array<float, 2> offset{0.0f, 8.0f};
        std::array<float, 4> effect_color{0.0f, 0.0f, 0.0f, 0.35f};
        float bloom_threshold{0.80f};
        float bloom_intensity{0.60f};
        float glow_intensity{0.25f};
        std::uint32_t seed{0};
        bool animated{false};
        bool rgb_noise{false};
        float noise_size{1.0f};
        float grain_shadow_response{1.0f};
        float grain_midtone_response{1.0f};
        float grain_highlight_response{1.0f};
        std::uint32_t octaves{4};
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

    };
    std::vector<EffectPlan> effects;

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
    // Route this layer through Chronon's camera-backed 2.5D/3D projection.
    // The flag is explicit so a plan cannot accidentally carry X/Y rotation
    // while still being treated as a flat screen-space overlay.
    bool enable_3d{false};
    // Keep a layer pinned to the output canvas while a scene camera is active.
    // Render-plan backgrounds commonly need this so transparent projected
    // layers reveal the authored background instead of the camera clear color.
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
    std::optional<FitMode> fit;
    std::optional<AnimationPlan> animation;
    std::vector<TextAnimatorPlan> text_animators;

    std::optional<chronon3d::BlendMode> blend_mode;
    std::optional<float> opacity;
    bool loop{false};
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
    OutputFormat format{OutputFormat::Png};
    VideoCodec codec{VideoCodec::Auto};
    RateControlMode rate_control{RateControlMode::Crf};
    std::int64_t bitrate{0};
    int crf{0};
    int qp{-1};
    std::string profile_id;
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
    std::optional<std::vector<CameraTrackPlan>> camera_animation;
    std::vector<LayerPlan> layers;
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
