#pragma once

#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/result.hpp>
#include <chronon3d/render_plan/render_budget.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace chronon3d::render_plan {

enum class LayerType : std::uint8_t { Image, Video, Text, Color, SubtitleTrack };
enum class FitMode : std::uint8_t { Cover, Contain, Stretch, None };
enum class SubtitleFormat : std::uint8_t { Srt, Vtt, Json };
enum class OutputFormat : std::uint8_t { Png, Mp4, Mkv, WebM };
enum class VideoCodec : std::uint8_t { Auto, H264, H265, VP9, AV1 };

struct CanvasSpec {
    int width{0};
    int height{0};
    int fps{0};
    Frame duration{0};
};

struct AnimationTiming {
    std::optional<Frame> start_frame;
    std::optional<Frame> duration_frames;
    std::string preset;
    // Motion-intent extension (render-plan.v1): selector unit + enter/exit
    // frame durations. Absent fields keep the preset defaults.
    std::string unit;                        // "word" | "glyph" | "line"
    std::optional<Frame> enter_duration_frames;
    std::optional<Frame> exit_duration_frames;
};

// ── Visual contract extension (ADR-029) ────────────────────────────────────
//
// `anchor` is a layout INTENT, not absolute coordinates: the anchor resolver
// maps it to final x/y through canvas → safe area → content bounds. Numeric
// coordinates stay available as the legacy `position` override.
struct AnchorPlan {
    std::string type;                   // "center", "safe_area", "lower_third", ...
    float safe_margin{0.06f};           // fraction of the canvas reserved per side
    std::string alignment{"left"};      // "left" | "center" | "right"
};

// `style` carries per-job style OVERRIDES; the preset's VisualStyle defaults
// (from VisualPresetRegistry) fill any absent field:
//   preset defaults + job overrides = ResolvedVisualStyle.
struct StrokeStyle {
    std::string color;
    std::optional<float> width;
};

struct ShadowStyle {
    std::string color;
    std::optional<float> opacity;
    std::optional<float> blur;
    std::array<float, 2> offset{0.0f, 0.0f};
    std::size_t offset_dimensions{0};
};

struct BackgroundStyle {
    std::string color;
    std::optional<float> opacity;
    std::optional<float> radius;
    std::array<float, 2> padding{0.0f, 0.0f};
    std::size_t padding_dimensions{0};
};

struct LayerStylePlan {
    std::string font_family;
    std::optional<int> font_weight;
    std::optional<float> font_size;
    std::string fill;
    std::optional<StrokeStyle> stroke;
    std::optional<ShadowStyle> shadow;
    std::optional<BackgroundStyle> background;
};

// `font_asset` is the canonical font reference: the logical asset path plus
// its family/weight metadata (deterministic across workers). The legacy
// string `font` field remains the path-only spelling.
struct FontAssetPlan {
    std::string asset;                  // logical path, e.g. fonts/Poppins-Bold.ttf
    std::string family;                 // "Poppins"
    std::optional<int> weight;          // 700
};

struct LayerPlan {
    std::string id;
    LayerType type{LayerType::Color};
    std::string asset;
    std::string source;
    std::string text;
    std::string font;
    std::optional<FontAssetPlan> font_asset;
    std::string preset;
    std::string semantic_role;
    std::optional<float> font_size;
    std::optional<float> box_width;
    std::optional<float> box_height;
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::size_t position_dimensions{0};
    // Numeric override applied ON TOP of the anchor-resolved position (NOT
    // absolute placement). `position` above is the legacy absolute spelling.
    std::array<float, 2> offset{0.0f, 0.0f};
    std::size_t offset_dimensions{0};
    std::optional<Frame> start_frame;
    std::optional<Frame> duration_frames;
    std::optional<FitMode> fit;
    std::optional<SubtitleFormat> subtitle_format;
    std::optional<AnimationTiming> animation;
    std::optional<AnchorPlan> anchor;
    std::optional<LayerStylePlan> style;
    // Compositing hints exposed by the render-plan contract for effect
    // layers (e.g. a light leak blended with BlendMode::Screen). Absent
    // fields keep the renderer defaults (Normal blend, full opacity, no loop).
    std::optional<chronon3d::BlendMode> blend_mode;
    std::optional<float> opacity;
    bool loop{false};
};

struct AudioTrackPlan {
    std::string source;
    double volume{1.0};
    double start_time_offset{0.0};
    double duration_seconds{0.0};
    std::string role;

    // Rendering hints set by the hybrid.v1 compiler for background_music.
    // The audio muxer reads these to generate the corresponding FFmpeg
    // filter chain (-stream_loop, afade, sidechain compression).
    bool loop{false};
    double fade_in_seconds{0.0};
    double fade_out_seconds{0.0};
    bool ducking_enabled{false};
};

struct OutputSpec {
    std::string path;
    OutputFormat format{OutputFormat::Png};
    VideoCodec codec{VideoCodec::Auto};
    std::int64_t bitrate{0};
    int crf{0};
    std::string profile_id;
};

struct RenderPlan {
    std::string job_id{"chronon_plan"};
    // Editorial visual profile.  Chronon owns the profile registry; callers
    // select only one of the three supported channel styles.
    std::string style_profile{"discovery"};
    // Deterministic identity of all decoded plan values. Asset bytes are not
    // implied here; the asset-manifest preflight will add content hashes.
    std::uint64_t content_fingerprint{0};
    CanvasSpec canvas;
    std::vector<LayerPlan> layers;
    std::vector<AudioTrackPlan> audio_tracks;
    OutputSpec output;
    /// Canonical job-level resource policy. Runtime renderers resolve the
    /// temporal portion through TemporalBudgetResolver.
    RenderBudget budget{};
};

struct PlanDecodeError {
    std::string path;
    std::string message;
};

/// Explicit fail-loud budget phase executed before render-plan compilation.
/// It validates dimensions, frame/duration bounds, layer/audio counts and
/// timing, text/reference bytes, memory, and estimated output size. Temporal
/// sample-pixel limits are resolved at runtime by TemporalBudgetResolver.
[[nodiscard]] std::optional<PlanDecodeError> validate_render_budget(
    const RenderPlan& plan,
    const RenderBudget& budget = {});

/// Compatibility spelling for callers that used the original budget helper.
[[nodiscard]] std::optional<PlanDecodeError> validate_render_plan_budget(
    const RenderPlan& plan,
    const RenderBudget& budget = {});

/// Compute the deterministic identity of the decoded plan values.
/// Asset bytes are deliberately not included until asset-manifest preflight
/// supplies their content hashes.
[[nodiscard]] std::uint64_t compute_render_plan_content_fingerprint(
    const RenderPlan& plan);

Result<RenderPlan, PlanDecodeError> decode_render_plan(const nlohmann::json& root);

}  // namespace chronon3d::render_plan
