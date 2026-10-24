#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// include/chronon3d/verification/render_receipt.hpp
//
// Canonical render receipt (schema "chronon3d.render-receipt.v1").
//
// Emitted after a successful render as `<output>.receipt.json`.  It carries
// the deterministic identity of the render (content/request/asset-manifest
// digests — reused verbatim from the prepared-plan fingerprint, never
// recomputed), the media contract, the granular output-verification results
// and a `copy_eligible` verdict.
//
// The output verifier runs the full contract:
//   ffprobe → decode → frame count → codec → pixel_format → resolution → fps
//   → audio
//
// Verification cost is policy-controlled (env `CHRONON_RECEIPT_VERIFY`):
//   fast    — container/stream metadata only, NO re-decode of the output
//             (production hot path; the encoder+muxer already succeeded
//             in-process). Default.
//   normal  — one full multithreaded `ffmpeg -f null -` decode pass as the
//             decodability gate; frame count from the container metadata.
//   certify — historical strict gate: full `ffprobe -count_frames` decode
//             plus an independent full `ffmpeg -f null -` decode smoke
//             (CI/golden determinism runs).
// `fast` performs no decode at all; `normal` decodes the output exactly once.
//
// `copy_eligible` is NOT "render finished".  It is true only when the output
// file exists, its SHA-256 was computed (canonical assets::sha256_file), and
// every applicable verification check passed.  Any single FAIL makes it false.
//
// This header is dependency-light on purpose: `RenderReceiptInput` carries
// plain strings/ints so callers map their prepared-plan identity into it at
// the boundary (the render-plan compiler types are intentionally NOT part of
// the public SDK surface).  The builder never recomputes identity.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <filesystem>
#include <chronon3d/color/hdr_mastering_metadata.hpp>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d::verification {

/// Media contract observed from the rendered output by ffprobe.
struct MediaContract {
    std::string container;
    std::string codec;
    std::string pixel_format;
    int width{0};
    int height{0};
    int fps_num{0};
    int fps_den{1};
    // r_frame_rate from ffprobe: the CFR nominal rate derived from a
    // constant frame-duration pattern.  0/0-shaped (num=0) when the container
    // exposes no constant rate; the fps verdict then relies on avg_frame_rate
    // alone (fps_num/fps_den above).
    int r_fps_num{0};
    int r_fps_den{1};
    std::int64_t frame_count{-1};
    double duration_ms{-1.0};
    bool has_audio{false};
    bool probed{false};

    // Canonical media facts for streaming / probe-less verification
    std::string codec_profile;
    std::string video_level;
    bool has_video{false};
    int video_streams{0};
    int video_time_base_num{0};
    int video_time_base_den{1};
    int sar_num{0};
    int sar_den{1};
    std::string color_range;
    std::string color_space;
    std::string color_transfer;
    std::string color_primaries;
    std::optional<color::HdrMasteringMetadata> hdr_mastering_metadata;
    std::string field_order;
    std::int64_t start_pts{0};
    bool first_frame_keyframe{true};
    bool closed_gop{true};
    int keyframe_interval{0};
    int audio_streams{0};
    std::string audio_codec;
    std::string audio_profile;
    int sample_rate{0};
    int channels{0};
    std::string channel_layout;
    std::string audio_bitrate;
    int audio_time_base_num{0};
    int audio_time_base_den{1};
};

/// Granular output-verification results.  Each field is one of
/// "pass" / "fail" / "skip" ("skip" = not applicable to this output).
struct ReceiptVerification {
    std::string ffprobe{"skip"};
    std::string decode{"skip"};
    std::string frame_count{"skip"};
    std::string codec{"skip"};
    std::string pixel_format{"skip"};
    std::string resolution{"skip"};
    std::string fps{"skip"};
    std::string audio{"skip"};
    std::string hdr_mastering_metadata{"skip"};
    // SHA-256 coverage of the rendered still image or image sequence.
    std::string frame_images{"skip"};
    // Verification policy recorded on the receipt so consumers never infer
    // the executed policy from the presence/absence of decode timings.
    // requested_policy is the raw CHRONON_RECEIPT_VERIFY value ("" when
    // unset); resolved_policy is the level that actually ran
    // ("fast" | "normal" | "certify").
    std::string requested_policy;
    std::string resolved_policy;
};

/// Content identity for one rendered still frame. The frame index is the
/// RenderPlan frame number; SHA-256 covers the encoded image file bytes.
struct ReceiptFrameImage {
    std::int64_t frame_index{0};
    std::int64_t bytes{-1};
    std::string sha256;
};

/// Wall-clock cost of the post-render verification, in milliseconds.
///
/// TIMING LEXICON (the one convention shared by every Chronon3d timing
/// telemetry surface — receipt `timing_ms`, `startup_breakdown`,
/// `prepare_breakdown`, `exclusive_wall_timeline`):
///   * every timing value is milliseconds as a double under a `*_ms` key;
///   * an absent value (JSON `null`) means "not measured / did not run" —
///     NEVER a fabricated 0 and NEVER a negative sentinel (the historical
///     -1 sentinel is gone; readers still accept it on legacy receipts);
///   * `0` always means "measured, took ~0".
/// Additive telemetry: written as `timing_ms` in the receipt JSON and never
/// part of the verification verdicts.
struct ReceiptTiming {
    std::optional<double> sha256_ms;
    std::optional<double> ffprobe_ms;      // ffprobe metadata-only probe
    std::optional<double> count_frames_ms; // certify-only -count_frames decode
    std::optional<double> decode_ms;       // full-decode smoke (normal+certify)
    std::optional<double> total_ms;        // whole build_render_receipt wall
};

/// Deterministic identity + render contract for one output.  Callers map
/// their prepared plan into this value; the receipt builder never recomputes
/// the content/request/asset-manifest identity from scratch.
struct RenderReceiptInput {
    // identity
    std::string job_id;
    std::string chronon_version{"unknown"};
    int chronon_abi{2};
    std::string git_sha{"unknown"};
    std::string render_plan_schema{"chronon.render-plan.v2"};
    std::string content_digest;
    std::string request_digest;
    std::string asset_manifest_digest;

    // render
    std::string backend{"software"};
    std::string render_lane;        // NEW: resolved lane id ("direct_yuv_cuda", "full_graph_vulkan", ...)
    std::string lane_reason;        // NEW: structured, not free text (see LaneReason below)
    int width{0};
    int height{0};
    int fps_num{0};
    int fps_den{1};
    std::int64_t first_frame{0};
    std::int64_t frames{0};
    // Final resolver receipt. A non-zero count is mandatory evidence for a
    // planned downgrade; an executor must never create an unrecorded fallback.
    std::string execution_downgrade_reason;
    std::uint32_t execution_downgrade_count{0};

    // output contract
    std::string requested_codec{"auto"};  // "auto" = any codec accepted
    std::string output_color_space{"srgb"};
    float reference_white_nits{100.0f};
    std::string ocio_output_display;
    std::string ocio_output_view;
    std::optional<color::HdrMasteringMetadata> hdr_mastering_metadata;
    bool has_audio_tracks{false};

    // Explicit per-job verification policy ("fast" | "normal" | "certify")
    // forwarded from the request boundary (daemon RENDER_JOB payload / IPC).
    // Empty falls back to the process environment (CHRONON_RECEIPT_VERIFY),
    // preserving the CLI-subprocess contract where RenderingGen sets the env
    // on the child process.
    std::string receipt_verify_policy;

    // Precomputed output facts (TICKET-OVERLAY-THROUGHPUT fix 2, dedup). The
    // caller that already inspected the freshly muxed output passes the
    // observed MediaContract and SHA-256 here so build_render_receipt never
    // re-probes or re-hashes the same bytes: one probe, one digest per
    // artifact. When absent (or when HDR mastering metadata must be compared)
    // the builder runs its own probe exactly as before. The *_ms fields carry
    // the caller-measured costs so the receipt keeps honest timings (absent
    // when the caller did not measure them — never a fabricated 0).
    std::optional<MediaContract> precomputed_media;
    std::optional<std::string> precomputed_sha256;
    std::optional<double> precomputed_probe_ms;
    std::optional<double> precomputed_sha256_ms;
};

/// The receipt payload written as `<output>.receipt.json`.
struct RenderReceipt {
    std::string schema{"chronon3d.render-receipt.v1"};

    // identity
    std::string job_id;
    std::string chronon_version{"unknown"};
    int chronon_abi{2};
    std::string git_sha{"unknown"};
    std::string render_plan_schema{"chronon.render-plan.v2"};
    std::string content_digest;
    std::string request_digest;
    std::string asset_manifest_digest;

    // render
    std::string backend{"software"};
    std::string render_lane;
    std::string lane_reason;
    int width{0};
    int height{0};
    int fps_num{0};
    int fps_den{1};
    std::int64_t first_frame{0};
    std::int64_t frames{0};
    std::string execution_downgrade_reason;
    std::uint32_t execution_downgrade_count{0};

    // media + verification
    MediaContract media;
    ReceiptVerification verification;
    ReceiptTiming timing;

    // output
    std::int64_t bytes{0};
    std::string sha256;
    std::vector<ReceiptFrameImage> frame_images;
    std::string output_color_space{"srgb"};
    float reference_white_nits{100.0f};
    std::string ocio_output_display;
    std::string ocio_output_view;
    std::optional<color::HdrMasteringMetadata> hdr_mastering_metadata;

    bool copy_eligible{false};
};

/// Build the receipt for a freshly rendered output.  `is_video` selects the
/// ffprobe media-verification path (still-image outputs skip media checks).
[[nodiscard]] RenderReceipt build_render_receipt(
    const RenderReceiptInput& input,
    const std::filesystem::path& output_path,
    bool is_video);

/// Serialize + write `<output>.receipt.json`; returns the path written.
[[nodiscard]] std::filesystem::path write_render_receipt(
    const RenderReceipt& receipt,
    const std::filesystem::path& output_path);

} // namespace chronon3d::verification
