#pragma once

#include <chronon3d/core/gpu_hot_path_mode.hpp>
#include <optional>
#include <string_view>

namespace chronon3d::media {

enum class VideoExecutionPath : unsigned char {
    BitstreamCopy,   ///< whole-clip packet copy, zero GPU
    SmartGopCopy,    ///< hybrid: some GOPs copied, others DirectYUV/NVENC
    DirectYuv,       ///< NVDEC → CUDA → NVENC for every frame
    FullGraph,       ///< software render graph + pipe/subprocess encoder
};

struct VideoExecutionRequest {
    std::string_view encoder_backend;
    std::string_view hardware_encoder;
    std::string_view codec;
    GpuHotPathMode hot_path{GpuHotPathMode::Auto};
    bool has_gop_source{false};
    bool gop_copy_only{false};
    // When true the resolver considers SmartGopCopy (hybrid) in addition to
    // the whole-clip BitstreamCopy path. Requires has_gop_source.
    bool allow_hybrid_gop{false};
};

/// GOP-level execution plan produced by the resolver when the path is
/// SmartGopCopy or BitstreamCopy. The caller uses this to drive the
/// per-GOP demux/mux + selective NVDEC/NVENC pipeline.
struct GopExecutionPlan {
    /// Total GOPs in the source interval.
    std::size_t total_gops{0};
    /// GOPs eligible for packet copy (untouched, codec-compatible).
    std::size_t copy_gops{0};
    /// GOPs that require DirectYUV/NVENC re-encode (touched by overlay/subtitle).
    std::size_t reencode_gops{0};
    /// True when the plan is whole-clip copy (copy_gops == total_gops).
    bool all_copy{false};
    /// True when the plan is hybrid (copy_gops > 0 && reencode_gops > 0).
    bool hybrid{false};
    /// Bitstream compatibility gate result. When false the splice of
    /// source packets + NVENC packets would corrupt the decoder; the
    /// resolver must downgrade to DirectYuv for the entire clip.
    bool safe_to_splice{false};
};

struct VideoExecutionDecision {
    VideoExecutionPath path{VideoExecutionPath::FullGraph};
    // Path used when the selected packet-copy path cannot be safely executed.
    // The executor must honor this resolver-owned fallback rather than infer
    // FullGraph from any path other than DirectYuv.
    VideoExecutionPath render_fallback{VideoExecutionPath::FullGraph};
    std::string_view reason{"full_graph_default"};
    bool valid{true};
    // Populated when path == SmartGopCopy or BitstreamCopy. Null otherwise.
    std::optional<GopExecutionPlan> gop_plan{};
};

[[nodiscard]] VideoExecutionDecision resolve_video_execution(
    const VideoExecutionRequest& request) noexcept;

} // namespace chronon3d::media
