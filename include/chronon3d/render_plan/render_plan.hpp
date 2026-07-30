#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/result.hpp>

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
    std::string assets_root;
    CanvasSpec canvas;
    std::vector<LayerPlan> layers;
    std::vector<AudioTrackPlan> audio_tracks;
    OutputSpec output;
};

struct PlanDecodeError {
    std::string path;
    std::string message;
};

Result<RenderPlan, PlanDecodeError> decode_render_plan(const nlohmann::json& root);

}  // namespace chronon3d::render_plan
