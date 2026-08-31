#pragma once

#include <chronon3d/core/gpu_hot_path_mode.hpp>
#include <cstdint>
#include <optional>
#include <string_view>

namespace chronon3d::media {

enum class VideoExecutionPath : unsigned char {
    BitstreamCopy,   ///< whole-clip packet copy, zero GPU
    SmartGopCopy,    ///< hybrid: some GOPs copied, others DirectYUV/NVENC
    DirectYuv,       ///< NVDEC → CUDA → NVENC for every frame
    RenderGraph,     ///< render-graph composition; encode/interop are separate plan axes
};

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
    // Authoritative plan. `path` and `render_fallback` remain as a migration
    // view for old packet/export callers and must not gain new semantics.
    VideoExecutionPlan plan{};
    VideoExecutionPath path{VideoExecutionPath::RenderGraph};
    // Path used when the selected packet-copy path cannot be safely executed.
    // The executor must honor this resolver-owned fallback rather than infer
    // FullGraph from any path other than DirectYuv.
    VideoExecutionPath render_fallback{VideoExecutionPath::RenderGraph};
    std::string_view reason{"render_graph_default"};
    bool valid{true};
    // Populated when path == SmartGopCopy or BitstreamCopy. Null otherwise.
    std::optional<GopExecutionPlan> gop_plan{};
};

[[nodiscard]] VideoExecutionDecision resolve_video_execution(
    const ExecutionRequirements& requirements,
    const OutputSpec& output,
    const VideoCapabilities& capabilities,
    bool has_gop_source = false,
    bool gop_copy_only = false,
    bool allow_hybrid_gop = false) noexcept;

} // namespace chronon3d::media
