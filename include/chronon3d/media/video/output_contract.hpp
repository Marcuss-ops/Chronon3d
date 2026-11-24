#pragma once

// ---------------------------------------------------------------------------
// output_contract.hpp — Canonical output contract for rendered artifacts.
//
// An `OutputContract` is the single source of truth for what a rendered
// artifact must be before it is considered distributable. Both Chronon's
// post-render verifier and any external consumer read the SAME contract, so
// `copy_eligible` is never decided by scattered one-off checks.
//
// The verification is split into two verdicts:
//   - `passed`        — structural integrity: the file exists, libavformat can
//                       inspect it, there is exactly one video stream, and the
//                       geometry / frame rate / duration are coherent.
//   - `copy_eligible` — `passed` AND the media contract (codec, pixel format,
//                       audio policy, frame count) matches AND a SHA-256 was
//                       computed (and, when `expected_sha256` is set, matches).
//
// A decodable but contract-violating artifact (e.g. yuv444p where yuv420p
// was required) is `passed` but NOT `copy_eligible`.
// ---------------------------------------------------------------------------

#include <chronon3d/core/types/result.hpp>
#include <chronon3d/core/types/time.hpp>
#include <chronon3d/media/video/media_probe.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace chronon3d::media::video {

/// Canonical output contract for a rendered artifact.
struct OutputContract {
    int width{1920};
    int height{1080};
    chronon3d::FrameRate fps{30, 1};

    /// Expected FFmpeg codec name (e.g. "h264"), NOT the encoder name
    /// (which may be "libx264" / "h264_nvenc" / ...).
    std::string video_codec{"h264"};

    /// Expected FFmpeg pixel format name (e.g. "yuv420p").
    std::string pixel_format{"yuv420p"};

    /// Audio policy: must the artifact carry exactly `audio_streams` audio
    /// streams? Video-only exports consumed by the canonical external audio
    /// assembler set this to false.
    bool audio_required{true};
    std::size_t audio_streams{1};

    /// Expected frame count; -1 = derive from duration × fps (report only).
    std::int64_t frame_count{-1};

    /// Expected SHA-256 hex digest; empty = compute + report only (the digest
    /// is still required for `copy_eligible`).
    std::string expected_sha256;
};

/// Observation + verdict from verifying an artifact against an OutputContract.
struct OutputVerificationResult {
    /// Structural integrity + geometry/fps/duration passed (see file header).
    bool passed{false};

    /// True ONLY when every contract field verified (incl. SHA-256).
    bool copy_eligible{false};

    /// Legacy ABI field: true when the in-process libavformat probe backend is
    /// unavailable (currently a build without CHRONON3D_ENABLE_NATIVE_FFMPEG).
    bool ffprobe_missing{false};

    // ── Observed facts (populated even on mismatch, best-effort) ──────────
    int width{0};
    int height{0};
    double fps{0.0};
    std::string video_codec;
    std::string pixel_format;
    double duration_seconds{0.0};
    std::int64_t frame_count{0};
    std::size_t audio_streams{0};

    /// Observed SHA-256 hex digest; empty = not computed.
    std::string sha256;

    /// Legacy telemetry field name. Wall time of the in-process libavformat
    /// probe in ms; measured with steady_clock. Absent = probe did not run
    /// (timing lexicon: never a fabricated 0, never a negative sentinel).
    std::optional<double> ffprobe_ms;

    /// Wall time of the SHA-256 digest computation, in ms.
    /// Absent = not computed.
    std::optional<double> sha256_ms;

    /// The exact probe observation this verification ran
    /// (TICKET-OVERLAY-THROUGHPUT fix 2, dedup): a downstream receipt builder
    /// reuses these facts instead of probing the same bytes a second time.
    /// Empty when the probe did not succeed.
    std::optional<MediaProbeInfo> probe_facts;

    /// Human-readable reason when !passed.
    std::string failure;

    // ── Provenance (TICKET-PERF-RECEIPT-CONTRACT-FASTPATH) ──────────────
    /// True when the verdict was produced from in-process muxer facts
    /// (`OutputContractFacts`) instead of reopening the artifact.
    bool contract_fast_path{false};
    /// True when the caller supplied facts but the verifier had to fall back
    /// to the canonical libavformat probe (missing field, size mismatch,
    /// HDR, unfinalized trailer, ...).
    bool contract_probe_fallback{false};
    /// Where the observed media facts came from:
    /// "libavformat" | "in_process_mux" | "fallback" | "none".
    std::string probe_source{"none"};
};

/// In-process facts about an artifact just produced and finalized by this
/// process. They are accepted only when every precondition holds; otherwise
/// verification falls back to `probe_media` — absence of a fact is never a
/// pass.
struct OutputContractFacts {
    /// The caller is the process that wrote and closed the artifact.
    bool trusted_in_process{false};
    /// The muxer wrote its trailer successfully (`av_write_trailer`).
    bool trailer_finalized{false};
    /// HDR mastering metadata requires the libavformat probe for side data.
    bool hdr_mastering{false};
    /// Final file size committed by the muxer (0 = unknown). A mismatch means
    /// the artifact changed after close (for example a truncated file).
    std::uint64_t expected_bytes{0};
    /// Facts observed from the muxer/encoder, in the canonical probe shape so
    /// downstream receipt builders can reuse them without a second probe.
    MediaProbeInfo media;
};

/// Resolve the single canonical contract for a named output profile.
/// Unknown profile ids fail loud (no silent default).
[[nodiscard]] Result<OutputContract, std::string> resolve_output_contract(
    std::string_view profile_id);

/// Verify an artifact file against a contract using in-process libavformat
/// stream/container inspection followed by a SHA-256 content digest.
/// `copy_eligible` is true only when every field matches and the digest was
/// computed. Lean builds without the native FFmpeg libraries fail closed.
[[nodiscard]] OutputVerificationResult verify_output_contract(
    const std::filesystem::path& artifact,
    const OutputContract& contract);

/// Verify an artifact from in-process facts. The fast path is taken only when
/// `facts.trusted_in_process` and `facts.trailer_finalized` are true, the file
/// still has `facts.expected_bytes`, the output is not HDR mastering, and the
/// facts carry every field the contract verdict needs (including audio
/// duration when audio is required). Any missing or inconsistent fact falls
/// back to the canonical libavformat probe.
[[nodiscard]] OutputVerificationResult verify_output_contract(
    const std::filesystem::path& artifact,
    const OutputContract& contract,
    const OutputContractFacts& facts);

} // namespace chronon3d::media::video
