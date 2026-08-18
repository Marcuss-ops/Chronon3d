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
#include <string>

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
    std::int64_t frame_count{-1};
    double duration_ms{-1.0};
    bool has_audio{false};
    bool probed{false};
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
    std::string render_plan_schema{"chronon.render-plan.v1"};
    std::string content_digest;
    std::string request_digest;
    std::string asset_manifest_digest;

    // render
    std::string backend{"software"};
    int width{0};
    int height{0};
    int fps_num{0};
    int fps_den{1};
    std::int64_t frames{0};

    // output contract
    std::string requested_codec{"auto"};  // "auto" = any codec accepted
    bool has_audio_tracks{false};
};

/// The receipt payload written as `<output>.receipt.json`.
struct RenderReceipt {
    std::string schema{"chronon3d.render-receipt.v1"};

    // identity
    std::string job_id;
    std::string chronon_version{"unknown"};
    int chronon_abi{2};
    std::string git_sha{"unknown"};
    std::string render_plan_schema{"chronon.render-plan.v1"};
    std::string content_digest;
    std::string request_digest;
    std::string asset_manifest_digest;

    // render
    std::string backend{"software"};
    int width{0};
    int height{0};
    int fps_num{0};
    int fps_den{1};
    std::int64_t frames{0};

    // media + verification
    MediaContract media;
    ReceiptVerification verification;

    // output
    std::int64_t bytes{0};
    std::string sha256;

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
