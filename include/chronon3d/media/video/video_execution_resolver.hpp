#pragma once

#include <chronon3d/core/gpu_hot_path_mode.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace chronon3d::media {

// These dimensions are independent.  A composition can therefore select a
// Vulkan graph while the encoder remains NVENC, without overloading one enum
// with the meaning of all four decisions.
enum class DecodePath : unsigned char {
    PacketCopy,
    Nvdec,
    Software,
};

enum class CompositePath : unsigned char {
    None,
    DirectYuv,
    VulkanGraph,
    SoftwareGraph,
};

enum class EncodePath : unsigned char {
    PacketCopy,
    Nvenc,
    Pipe,
};

enum class InteropPath : unsigned char {
    None,
    CudaNative,
    VulkanCuda,
};

// Describes how the compositor's output reaches the encoder-owned surface.
// This is intentionally separate from InteropPath: Vulkan/CUDA interop can be
// active whether the handoff is direct or requires a device-side copy.
enum class SurfaceHandoffPath : unsigned char {
    None,
    Direct,
    VulkanCopy,
    HostUpload,
};

/// Backend-neutral execution contract.  Every field has exactly one meaning;
/// callers must not infer a decode, composition, or interop choice from
/// another field.
struct VideoExecutionPlan {
    DecodePath decode{DecodePath::Software};
    CompositePath composite{CompositePath::SoftwareGraph};
    EncodePath encode{EncodePath::Pipe};
    InteropPath interop{InteropPath::None};
    SurfaceHandoffPath handoff{SurfaceHandoffPath::HostUpload};

    [[nodiscard]] constexpr bool uses_gpu() const noexcept {
        return composite == CompositePath::DirectYuv ||
               composite == CompositePath::VulkanGraph ||
               encode == EncodePath::Nvenc || decode == DecodePath::Nvdec;
    }
};

/// Public, backend-neutral requirements supplied by an authoring/orchestration
/// layer. These types deliberately do not mention Vulkan, CUDA or NVENC.
struct ExecutionRequirements {
    bool gpu_required{false};
    bool cpu_fallback_allowed{true};
    bool composition_required{true};
    // Mirrors RenderingGen's ExecutionRequirements.VideoSourceRequired (wire
    // key execution_requirements.video_source_required). Distinguishes a
    // source-video composition (native NVENC handoff) from an authored
    // image/text-only composition that must fall back to the pipe encoder
    // even when a GPU is requested (Phase 9 CLI/IPC parity axis). Defaults to
    // true to preserve the legacy resolver behavior when the key is absent.
    bool video_source_required{true};
    bool packet_copy_allowed{true};
};

struct OutputSpec {
    std::string_view codec{"h264"};
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t fps_num{0};
    std::uint32_t fps_den{1};
};

/// Capabilities are discovered by Chronon and passed to the resolver; they
/// are not selected by RenderingGen.
struct VideoCapabilities {
    bool nvdec{false};
    bool nvenc{false};
    bool vulkan_graph{false};
    bool cuda_native{false};
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
    // Authoritative FINAL plan. The executor executes exactly these axes and
    // never re-decides policy; downgrades (e.g. a hybrid GOP plan that cannot
    // be executed) are already resolved here with a structured `reason`.
    VideoExecutionPlan plan{};
    std::string_view reason{"render_graph_default"};
    bool valid{true};
    // Non-zero only when the resolver intentionally selected a less capable
    // final lane. This is the machine-readable downgrade receipt: an
    // executor may not invent a fallback without incrementing it here.
    std::uint32_t downgrade_count{0};
    // Populated when the final plan copies packets (whole-clip or per-GOP).
    // Null otherwise.
    std::optional<GopExecutionPlan> gop_plan{};
};

/// GOP-level facts extracted by the caller's compressed-source preflight
/// (inspect_gop_source). Feeding these into the resolver lets the FINAL plan
/// (including any downgrade) be decided before execution starts, with a
/// structured reason; the executor never downgrades at runtime.
struct GopAnalysisSummary {
    bool available{false};        ///< source inspection succeeded
    std::size_t total_gops{0};
    std::size_t copy_gops{0};     ///< copy-eligible AND splice-safe
    std::size_t reencode_gops{0};
    bool all_copy{false};         ///< copy_gops == total_gops
    bool hybrid{false};           ///< copy_gops > 0 && reencode_gops > 0
    bool safe_to_splice{false};   ///< valid only when hybrid
};

/// Public, backend-neutral resolution. `has_gop_source` / `gop_copy_only` /
/// `allow_hybrid_gop` describe authoring intent; when a caller has already
/// inspected the source it should pass the summary via
/// resolve_video_execution_with_gop_analysis() so the plan is final.
[[nodiscard]] VideoExecutionDecision resolve_video_execution(
    const ExecutionRequirements& requirements,
    const OutputSpec& output,
    const VideoCapabilities& capabilities,
    bool has_gop_source = false,
    bool gop_copy_only = false,
    bool allow_hybrid_gop = false) noexcept;

/// Resolves with GOP pre-analysis folded in: the returned plan is FINAL and
/// the executor must not perform runtime downgrades. `intent_*` mirror the
/// parameters of resolve_video_execution(); when `analysis.available` is true
/// they are interpreted as: explicit whole-clip copy intent (gop_copy_only),
/// or hybrid eligibility (allow_hybrid_gop).
[[nodiscard]] VideoExecutionDecision resolve_video_execution_with_gop_analysis(
    const ExecutionRequirements& requirements,
    const OutputSpec& output,
    const VideoCapabilities& capabilities,
    bool intent_gop_copy_only,
    bool intent_allow_hybrid_gop,
    const GopAnalysisSummary& analysis) noexcept;

struct ResolvedExecutionParameters {
    std::string backend{"auto"};
    std::string hardware_encoder{"none"};
    std::string encoder_backend{"pipe"};
    std::string gpu_hot_path_mode{"auto"};
    std::string codec{"auto"};
    bool native_nvenc{false};
    bool direct_yuv{false};
    bool cpu_fallback_forbidden{false};
};

[[nodiscard]] ResolvedExecutionParameters resolve_canonical_execution_parameters(
    const ExecutionRequirements& requirements,
    std::string_view requested_codec = "",
    std::string_view requested_backend = "auto",
    std::string_view daemon_backend = "auto") noexcept;

} // namespace chronon3d::media
