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

#include <cstdint>
#include <filesystem>
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
    /// probe in ms; measured with steady_clock. 0.0 = probe did not run.
    double ffprobe_ms{0.0};

    /// Wall time of the SHA-256 digest computation, in ms. 0.0 = not computed.
    double sha256_ms{0.0};

    /// Human-readable reason when !passed.
    std::string failure;
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

} // namespace chronon3d::media::video
