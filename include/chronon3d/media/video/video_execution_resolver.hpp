#pragma once

#include <chronon3d/core/gpu_hot_path_mode.hpp>
#include <cstdint>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::media {

/// A source-time interval whose semantics are known before compressed-source
/// inspection. Keeping this on the canonical job contract prevents the GOP
/// analyzer from treating an entire export as changed when only a short cue
/// contains authored pixels.
enum class ChangedIntervalKind : std::uint8_t {
    Content,
    Trim,
};

struct ChangedInterval {
    double start_seconds{0.0};
    double end_seconds{0.0};
    ChangedIntervalKind kind{ChangedIntervalKind::Content};

    [[nodiscard]] bool valid() const noexcept {
        return std::isfinite(start_seconds) && std::isfinite(end_seconds) &&
            start_seconds >= 0.0 && end_seconds > start_seconds;
    }
};

// ── Canonical defaults (single source of truth) ──────────────────────────
//
// Every generic default backend/codec fact lives here: the daemon option
// default, the resolver's fallback chain and the CLI option defaults all read
// these constants, so re-policing a default is a one-line change instead of a
// three-file hunt (audit N8).
//   kDefaultBackend   — "auto": let Chronon pick software/vulkan.
//   kAutoCodec        — "auto": let the encoder pick a codec.
//   kDefaultCodec     — the concrete codec used when a strict GPU/native lane
//                       was requested without an explicit codec.
//   kDefaultGpuBackend— the backend a strict GPU request falls back to when
//                       neither the request nor the daemon names one.
inline constexpr std::string_view kDefaultBackend{"auto"};
inline constexpr std::string_view kAutoCodec{"auto"};
inline constexpr std::string_view kDefaultCodec{"h264"};
inline constexpr std::string_view kDefaultGpuBackend{"vulkan"};

// The `encoder_backend` selector vocabulary: which encoder authority executes
// the job. It lives here, with the other canonical defaults, because the SDK job
// contract (include/chronon3d/timeline/render_job.hpp), this resolver and the
// CLI validator/resolver (apps/chronon3d_cli/utils/video/
// encoder_config_resolution.hpp) must agree on it, and src/ may not depend on
// apps/. AUDIT F4: this was previously spelled as a raw "native"/"pipe" literal
// at ~20 sites, where one forgotten comparison silently downgrades a job to the
// subprocess encoder. The CLI header aliases these two names; do not respell
// the literals.
//   kEncoderTransportNative — "native": in-process libavcodec.
//   kEncoderTransportPipe   — "pipe": external ffmpeg subprocess.
inline constexpr std::string_view kEncoderTransportNative{"native"};
inline constexpr std::string_view kEncoderTransportPipe{"pipe"};

// The `hardware_encoder` field vocabulary: which hardware encoder the job asks
// for. It lives here for the same reason as the transport tokens above — the
// daemon admission, this resolver and the CLI classification must agree on it,
// and src/ may not depend on apps/. AUDIT P1-2: the field was classified by a
// literal `== "nvenc"` at ~10 CLI/IPC sites while the canonical parser matched
// case-insensitively, so a payload spelling `NVENC` asked for hardware encoding
// and still reached the requirement builder with `gpu_required = false`.
// Use hardware_encoder_requests_nvenc(); do not respell the literal.
//   kNvencHardwareEncoder — "nvenc": the canonical spelling of the field.
inline constexpr std::string_view kNvencHardwareEncoder{"nvenc"};

/// True when the hardware-encoder field requests NVENC (case-insensitive).
[[nodiscard]] bool hardware_encoder_requests_nvenc(std::string_view field) noexcept;

/// Canonical spelling of the hardware-encoder field: a recognized backend is
/// written back in its canonical token, anything else is returned unchanged so
/// the encoder resolution keeps producing its own explicit error instead of a
/// silent rewrite of an unknown value.
[[nodiscard]] std::string normalize_hardware_encoder_field(std::string_view field);

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
    std::string_view codec{kDefaultCodec};
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

/// A closed timestamp envelope in the source stream time base. The four
/// schedule ranges deliberately do not share one overloaded pair of
/// timestamps: decode may need a dependency prefix while render/encode only
/// cover the pixels that become part of the output.
struct TimestampRange {
    std::int64_t first{0};
    std::int64_t last{0};
    bool present{false};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return present && first <= last;
    }
};

/// Backend-neutral bitstream facts used by the compiler-owned schedule. The
/// concrete FFmpeg codec ids stay outside this public media contract.
struct BitstreamCompatibility {
    bool codec_match{false};
    bool profile_match{false};
    bool level_compatible{false};
    bool dimensions_match{false};
    bool pixel_format_match{false};
    bool parameter_sets_compatible{false};
    bool color_params_match{false};
    bool random_access_safe{false};

    [[nodiscard]] constexpr bool safe_to_splice() const noexcept {
        return codec_match && profile_match && level_compatible &&
            dimensions_match && pixel_format_match &&
            parameter_sets_compatible && color_params_match &&
            random_access_safe;
    }
};

/// One unit in the compiler-owned minimum-work schedule. The timestamps are
/// source-stream timestamps; the executor uses the ordinal and mode to keep
/// copied and re-encoded GOPs in presentation order.
enum class GopWorkKind : std::uint8_t {
    CopyPackets,
    Reencode,
};

struct GopWorkUnit {
    std::size_t ordinal{0};
    std::int64_t first_pts{0};
    std::int64_t last_pts{0};
    std::int64_t first_dts{0};
    std::int64_t last_dts{0};
    GopWorkKind kind{GopWorkKind::Reencode};

    /// Packet envelope consumed by the unit. For a re-encode this may be
    /// narrower than decode_range once dependency closure is implemented.
    TimestampRange source_packets{};
    /// Decode-only dependency window; it is allowed to precede render_range.
    TimestampRange decode_range{};
    /// Pixels that must be rendered for this unit.
    TimestampRange render_range{};
    /// Encoded output envelope emitted into the shared MuxSession.
    TimestampRange encode_range{};
    bool force_idr_at_start{false};
    bool closes_dependencies_at_end{false};
    BitstreamCompatibility compatibility{};

    [[nodiscard]] constexpr bool copies_packets() const noexcept {
        return kind == GopWorkKind::CopyPackets;
    }
};

/// GOP-level execution plan produced by the resolver when the path is
/// SmartGopCopy or BitstreamCopy. This is the first concrete form of the
/// compiler-owned MinimumWorkSchedule: no executor is allowed to infer a
/// copied/re-encoded GOP from the scene again.
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
    /// Ordered work units. Empty is valid for legacy intent-only decisions;
    /// source preflight fills it before a final decision is dispatched.
    std::vector<GopWorkUnit> work_units{};
    /// Time base for all PTS/DTS values in work_units. A zero numerator means
    /// that the plan is intent-only and has not been populated by preflight.
    std::int32_t source_time_base_num{0};
    std::int32_t source_time_base_den{1};
    /// True when this is an unchanged narrowed time window. Such a plan uses
    /// packet trimming semantics, not whole-clip CopyPackets semantics.
    bool trim_only{false};
};

/// The work units a requested window [window_first, window_last) actually
/// needs, in schedule order, as indices into `plan.work_units`.
///
/// A preflight plan covers the WHOLE source: nothing about a GOP's identity
/// depends on the export window, so the window selects units rather than
/// changing them. A unit is selected when its tick span intersects the window
/// — a GOP that merely straddles an edge still participates, because both ends
/// are resolved by the caller (a copy is all-or-nothing and therefore demands
/// an aligned edge, while a re-encode can be rendered for exactly the
/// requested frames).
[[nodiscard]] inline std::vector<std::size_t> gop_units_in_window(
    const GopExecutionPlan& plan, std::int64_t window_first,
    std::int64_t window_last) noexcept {
    std::vector<std::size_t> selected;
    if (window_last <= window_first) return selected;
    for (std::size_t index = 0; index < plan.work_units.size(); ++index) {
        const auto& unit = plan.work_units[index];
        const std::int64_t unit_start = unit.render_range.present
            ? unit.render_range.first : unit.first_pts;
        // Right edge: the next unit's start is this GOP's right edge, and the
        // window is authoritative for the final unit of the plan.
        const std::int64_t next_start = index + 1 < plan.work_units.size()
            ? (plan.work_units[index + 1].render_range.present
                   ? plan.work_units[index + 1].render_range.first
                   : plan.work_units[index + 1].first_pts)
            : window_last;
        const std::int64_t unit_end =
            std::max<std::int64_t>(next_start, unit_start + 1);
        if (unit_end > window_first && unit_start < window_last) {
            selected.push_back(index);
        }
    }
    return selected;
}

// ── Codec-boundary decision ─────────────────────────────────────────────
//
// The video analogue of AudioExecutionPath (packet_assembler.hpp): the same
// four-state vocabulary, decided from FACTS before execution, so the executor
// never re-decides and cannot invent a silent fallback. Audio and video codec
// boundaries therefore speak one decision language.

/// How the source bitstream is reused at this codec boundary.
///
///   CopyPackets      — the whole segment is copied verbatim: no decode, no
///                      encode. Legal only while the composition leaves the
///                      source pixels and codec parameters untouched.
///   TrimPackets      — no pixel change; only the leading/trailing GOP(s) are
///                      dropped to honour the requested time range.
///   BoundaryReencode — only the leading/trailing GOP(s) are re-encoded; the
///                      interior bitstream is copied. Requires a
///                      codec-compatible output and a splice-safe boundary.
///   FullReencode     — every frame is re-encoded (interior change, codec
///                      mismatch, or an unsafe splice).
enum class VideoExecutionPath : std::uint8_t {
    CopyPackets,
    TrimPackets,
    BoundaryReencode,
    FullReencode,
};

/// Pure decision table, the video analogue of `resolve_audio_execution()`.
///
/// All four inputs are facts resolved before execution, never capabilities:
///   content_changed  — the composition changes the source content at all
///                      (overlay, subtitle, effect). An interior change
///                      (`content_changed && !boundary_change`) always forces a
///                      full re-encode: the copied interior would reference
///                      rebuilt pixels.
///   boundary_change  — the change (if any) is confined to the leading/trailing
///                      GOP(s); it can be true with `content_changed == false`,
///                      which is a pure time-range trim.
///   codec_compatible — the output codec/parameters match the source, so the
///                      encoder can emit packets into the copied stream.
///   splice_safe      — concatenating copied and freshly-encoded packets stays
///                      decodable (GOP/reference structure permits it).
[[nodiscard]] constexpr VideoExecutionPath resolve_video_execution_path(
    bool content_changed,
    bool boundary_change,
    bool codec_compatible,
    bool splice_safe) noexcept {
    if (!content_changed && !boundary_change && codec_compatible) {
        return VideoExecutionPath::CopyPackets;
    }
    if (!content_changed && boundary_change && codec_compatible) {
        return VideoExecutionPath::TrimPackets;
    }
    if (content_changed && boundary_change && codec_compatible && splice_safe) {
        return VideoExecutionPath::BoundaryReencode;
    }
    return VideoExecutionPath::FullReencode;
}

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
    // Codec-boundary decision for this plan. CopyPackets / TrimPackets /
    // BoundaryReencode mean the source bitstream is (partly) preserved;
    // FullReencode means the encoder emits every frame. A plan that never
    // reuses the source bitstream reports FullReencode.
    //
    // The analysis-aware entry point (resolve_video_execution_with_gop_analysis)
    // always reports a path consistent with the FINAL plan. The intent-only
    // overload is the preflight layer: with a hybrid intent and no analysis yet
    // it reports the boundary IDEAL (BoundaryReencode) while the declared plan
    // still needs the compressed-source preflight; the final entry point then
    // replaces that ideal with the plan-consistent path.
    VideoExecutionPath codec_path{VideoExecutionPath::FullReencode};
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
    /// Ordered schedule emitted by compressed-source preflight.
    std::vector<GopWorkUnit> work_units{};
    /// Time base for the PTS/DTS values in work_units.
    std::int32_t source_time_base_num{0};
    std::int32_t source_time_base_den{1};
    /// True when the source is unchanged and only leading/trailing GOPs are
    /// removed for the requested output window.
    bool trim_only{false};

    /// Structural validation for the compiler-owned schedule. Empty
    /// work_units remains valid for legacy intent-only callers; once a
    /// schedule is present, ordinals and aggregate counters must agree.
    [[nodiscard]] bool valid_schedule() const noexcept {
        if (copy_gops + reencode_gops != total_gops && total_gops != 0) {
            return false;
        }
        if (work_units.empty()) return true;
        // Trim plans may intentionally carry only the selected GOP subset;
        // non-trim schedules must describe the complete source envelope.
        if (total_gops != 0 && work_units.size() != total_gops && !trim_only) {
            return false;
        }
        std::size_t copies = 0;
        std::size_t reencodes = 0;
        for (std::size_t index = 0; index < work_units.size(); ++index) {
            const auto& unit = work_units[index];
            if ((!trim_only && unit.ordinal != index) ||
                (index > 0 && unit.ordinal <= work_units[index - 1].ordinal) ||
                unit.first_pts > unit.last_pts ||
                unit.first_dts > unit.last_dts) {
                return false;
            }
            if (unit.copies_packets()) ++copies;
            else ++reencodes;
        }
        if (trim_only) {
            return reencodes == 0 && copies == work_units.size();
        }
        return copies == copy_gops && reencodes == reencode_gops &&
            (total_gops == 0 || total_gops == work_units.size()) &&
            all_copy == (copy_gops == total_gops) &&
            hybrid == (copy_gops > 0 && reencode_gops > 0);
    }
};

/// Reduce GOP pre-analysis + authoring intent to the boundary decision.
///
/// `intent_gop_copy_only` asserts the whole clip is untouched;
/// `intent_allow_hybrid_gop` allows a boundary-only change. When the source
/// was not inspected the splice safety is unknown and therefore unsafe (fail
/// closed), and an explicit copy intent that the preflight cannot prove
/// collapses to FullReencode rather than an unverified copy.
[[nodiscard]] VideoExecutionPath resolve_video_execution_path_from_gop_analysis(
    bool intent_gop_copy_only,
    bool intent_allow_hybrid_gop,
    const GopAnalysisSummary& analysis) noexcept;

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
    std::string backend{std::string{kDefaultBackend}};
    std::string hardware_encoder{"none"};
    std::string encoder_backend{"pipe"};
    std::string gpu_hot_path_mode{"auto"};
    std::string codec{std::string{kAutoCodec}};
    bool native_nvenc{false};
    bool direct_yuv{false};
    bool cpu_fallback_forbidden{false};
};

[[nodiscard]] ResolvedExecutionParameters resolve_canonical_execution_parameters(
    const ExecutionRequirements& requirements,
    std::string_view requested_codec = "",
    std::string_view requested_backend = kDefaultBackend,
    std::string_view daemon_backend = kDefaultBackend) noexcept;

} // namespace chronon3d::media
