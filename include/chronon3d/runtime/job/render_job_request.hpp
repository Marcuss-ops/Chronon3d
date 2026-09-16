#pragma once

#include <chronon3d/core/types/result.hpp>
#include <chronon3d/media/video/video_execution_resolver.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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
    /// Requested OUTPUT audio sample rate in Hz. 0 (the default) keeps the
    /// legacy copy semantics: the source audio stream is muxed verbatim and
    /// the artifact inherits the source rate. A positive value that differs
    /// from the source makes the native mux transcode the audio so the
    /// published artifact satisfies the declared rate. It is part of the
    /// output contract, so it participates in the normalized job fingerprint.
    int audio_target_sample_rate{0};
    bool gop_copy_only{false};
    /// Source-time content/trim intervals supplied by the authoring layer.
    /// Empty preserves the legacy whole-export content-change semantics.
    std::vector<media::ChangedInterval> changed_intervals{};
    std::uint32_t fps_num{0};
    std::uint32_t fps_den{1};
    /// Requested CUDA/pipe surface format (e.g. nv12 or p010). Empty means
    /// the normalizer/executor default applies. Parsed by the canonical
    /// authority in <chronon3d/runtime/frame_format.hpp>; an unknown value is
    /// rejected by `normalize_render_job` with InvalidPipePixelFormat.
    std::string pipe_pixfmt;
    /// Requested output sink: "ffmpeg" (render → encode → mux) or "raw"
    /// (render → pre-encode host frames, no encoder). Empty means "not
    /// requested", and normalization back-fills "ffmpeg" so a plan-file
    /// request and a daemon/IPC request resolve the same sink. The accepted
    /// vocabulary is owned by the CLI sink layer
    /// (apps/chronon3d_cli/commands/video/common/sink_options.hpp); an unknown
    /// name is refused by the CLI job validator before execution.
    std::string sink_type;
};

struct RenderJobRequest {
    std::filesystem::path plan_path;
    /// Optional immutable overlay preparation sidecar produced by the
    /// authoring pipeline. When present, the CLI validates it at the same
    /// preparation boundary as the render plan.
    std::filesystem::path prepared_package_path;
    std::filesystem::path output_path;
    std::filesystem::path assets_root;

    std::optional<FrameRange> frame_range;
    media::ExecutionRequirements requirements{};
    OutputSpec output{};
    TraceOptions trace{};
    std::string receipt_verification;
    std::string backend{std::string{media::kDefaultBackend}};
    bool report{false};
};

struct RuntimeCapabilities {
    /// Host/daemon backend policy. The request may use "auto" to inherit it.
    std::string backend{std::string{media::kDefaultBackend}};
};

enum class RenderJobRequestErrorCode : std::uint8_t {
    MissingPlan,
    InvalidBackend,
    InvalidFrameRange,
    /// `output.pipe_pixfmt` is outside the canonical vocabulary. Validated at
    /// this boundary so a plan file or an IPC spec cannot smuggle an unknown
    /// format past the CLI validator into the encoder.
    InvalidPipePixelFormat,
    InvalidChangedInterval,
};

struct RenderJobRequestError {
    RenderJobRequestErrorCode code{RenderJobRequestErrorCode::MissingPlan};
    std::string message;
};

// Compatibility boundary for the legacy request error payload. New request
// validation code must enter through this adapter so the legacy enum remains
// a detail payload while its construction surface is reduced to one place.
[[nodiscard]] inline RenderJobRequestError make_render_job_request_error(
    RenderJobRequestErrorCode code, std::string message) {
    return RenderJobRequestError{code, std::move(message)};
}

struct NormalizedRenderJob {
    std::filesystem::path plan_path;
    std::filesystem::path prepared_package_path;
    std::filesystem::path output_path;
    std::filesystem::path assets_root;
    std::optional<FrameRange> frame_range;
    media::ExecutionRequirements requirements{};
    OutputSpec output{};
    TraceOptions trace{};
    std::string receipt_verification;
    std::string backend{std::string{media::kDefaultBackend}};
    bool report{false};

    /// Final policy decision consumed by the execution adapter. No transport
    /// layer is allowed to resolve these fields again.
    media::ResolvedExecutionParameters execution{};
};

[[nodiscard]] Result<NormalizedRenderJob, RenderJobRequestError>
normalize_render_job(
    const RenderJobRequest& request,
    const RuntimeCapabilities& capabilities = {});

/// Deterministic single-line fingerprint of a normalized render job. The
/// CLI↔IPC parity gate compares fingerprints of jobs produced by the two
/// canonical entry points: equal fingerprint ⇔ identical render semantics
/// (plan/output/range/output spec/trace/receipt/report/backend — the surface
/// the executor consumes after normalization back-fills the resolved codec /
/// hardware / encoder / hot-path decision). Raw requirement flags are not
/// part of the key: transports may spell them differently, but they must
/// agree on the resolved job. No transport layer may re-resolve `execution`
/// after normalization. Deterministic per build; intended for equality
/// checks, not durable storage.
[[nodiscard]] std::string render_job_fingerprint(
    const NormalizedRenderJob& job);

} // namespace chronon3d::runtime::job
