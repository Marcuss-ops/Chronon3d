#pragma once

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
};

struct LayerPlan {
    std::string id;
    LayerType type{LayerType::Color};
    std::string asset;
    std::string source;
    std::string text;
    std::string font;
    std::string preset;
    std::optional<float> font_size;
    std::optional<float> box_width;
    std::optional<float> box_height;
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::size_t position_dimensions{0};
    std::optional<Frame> start_frame;
    std::optional<Frame> duration_frames;
    std::optional<FitMode> fit;
    std::optional<SubtitleFormat> subtitle_format;
    std::optional<AnimationTiming> animation;
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
};

struct RenderPlan {
    std::string job_id{"chronon_plan"};
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
