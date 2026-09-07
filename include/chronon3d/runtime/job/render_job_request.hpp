#pragma once

#include <chronon3d/core/types/result.hpp>
#include <chronon3d/media/video/video_execution_resolver.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace chronon3d::runtime::job {

struct FrameRange {
    std::int64_t first{0};
    std::int64_t last{0};
};

struct TraceOptions {
    std::filesystem::path output;
    std::string level{"pipeline"};
};

/// Semantic output intent. It owns strings and contains no CLI, JSON, IPC,
/// renderer, or backend object.
struct OutputSpec {
    std::string codec;
    std::string hardware_encoder;
    std::string encoder_backend;
    std::string ffmpeg_mode;
    std::string gpu_hot_path_mode;
    std::string encode_preset;
    std::string rate_control_mode;
    int crf{-1};
    int qp{-1};
    std::int64_t bitrate{0};
    std::string audio_source_path;
    bool gop_copy_only{false};
    std::uint32_t fps_num{0};
    std::uint32_t fps_den{1};
};

struct RenderJobRequest {
    std::filesystem::path plan_path;
    std::filesystem::path output_path;
    std::filesystem::path assets_root;

    std::optional<FrameRange> frame_range;
    media::ExecutionRequirements requirements{};
    OutputSpec output{};
    TraceOptions trace{};
    std::string receipt_verification;
    std::string backend{"auto"};
    bool report{false};
};

struct RuntimeCapabilities {
    /// Host/daemon backend policy. The request may use "auto" to inherit it.
    std::string backend{"auto"};
};

enum class RenderJobRequestErrorCode : std::uint8_t {
    MissingPlan,
    InvalidBackend,
    InvalidFrameRange,
};

struct RenderJobRequestError {
    RenderJobRequestErrorCode code{RenderJobRequestErrorCode::MissingPlan};
    std::string message;
};

struct NormalizedRenderJob {
    std::filesystem::path plan_path;
    std::filesystem::path output_path;
    std::filesystem::path assets_root;
    std::optional<FrameRange> frame_range;
    media::ExecutionRequirements requirements{};
    OutputSpec output{};
    TraceOptions trace{};
    std::string receipt_verification;
    std::string backend{"auto"};
    bool report{false};

    /// Final policy decision consumed by the execution adapter. No transport
    /// layer is allowed to resolve these fields again.
    media::ResolvedExecutionParameters execution{};
};

[[nodiscard]] Result<NormalizedRenderJob, RenderJobRequestError>
normalize_render_job(
    const RenderJobRequest& request,
    const RuntimeCapabilities& capabilities = {});

} // namespace chronon3d::runtime::job
