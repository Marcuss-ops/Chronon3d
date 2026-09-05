#pragma once

// =============================================================================
// encoder_config_resolution.hpp — Single authority for encoder configuration
// resolution and validation (C-1/C-2 closeout).
//
// The encoder pipeline used to push raw CLI/config strings
// (rate_control_mode="crf", preset="medium", tune=...) straight into
// NativeAvEncoder::open(), which then interpreted them per backend:
//
//   - an unknown rate-control string silently fell through to the NVENC
//     driver default ("driver-default") or was treated as CRF,
//   - CRF was silently ignored on NVENC (no rc option applied),
//   - `tune` silently disappeared on NVENC,
//   - an invalid preset was only rejected by avcodec_open2, long after the
//     encoder had been configured.
//
// This module is the single resolution/validation authority. It separates:
//
//   USER INTENT (what the caller explicitly asked for)
//     from
//   CHRONON DEFAULT (what the engine applies when nothing was requested)
//
// and turns that into an immutable ResolvedEncoderConfig that encoders only
// APPLY — they never reinterpret encoder policy themselves.
//
// The module is intentionally pure C++: no FFmpeg, no CUDA, no GPU, so the
// whole validation matrix can be unit-tested without hardware or libavcodec.
// =============================================================================

#include <chronon3d/core/types/result.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace chronon3d::cli {

struct FfmpegPipeOptions;

// ── Backend semantics ───────────────────────────────────────────────────────

enum class EncoderBackend : std::uint8_t {
    Software,   // libx264 / libx264rgb / libx265 in-process or pipe
    Nvenc,      // h264_nvenc / hevc_nvenc (native path only today)
};

[[nodiscard]] const char* to_string(EncoderBackend backend) noexcept;

// ── Rate-control intent (backend-neutral user vocabulary) ──────────────────
//
// Unspecified means the caller did not request any rate control; the resolver
// applies the deterministic engine default for the selected backend.

enum class EncoderRateControlRequest : std::uint8_t {
    Unspecified,
    ConstantQuality,  // "crf" semantics (x264 CRF)
    ConstantQp,       // constant quantizer
    Bitrate,          // "bitrate": target/ABR bitrate (NVENC → vbr)
    Vbr,              // "vbr"
    Cbr,              // "cbr"
};

[[nodiscard]] const char* to_string(EncoderRateControlRequest rc) noexcept;

/// Parse a rate-control mode string into the request vocabulary.
/// Returns false for unknown strings (e.g. "quality", "banana") so an
/// unknown rate-control mode can never reach an encoder silently.
[[nodiscard]] bool parse_rate_control_request(
    std::string_view mode, EncoderRateControlRequest& out) noexcept;

// ── Resolved rate control (what is actually applied) ────────────────────────

enum class ResolvedEncoderRateControl : std::uint8_t {
    ConstantQuality,
    ConstantQp,
    Vbr,
    Cbr,
    /// NVENC driver default: no rc option is applied because the caller did
    /// not request an explicit rate control. Deterministic in the sense that
    /// it is a deliberate engine decision, but its encoder behaviour is
    /// driver-defined — official benchmarks must reject it (see
    /// validate_for_benchmark).
    DriverDefault,
};

[[nodiscard]] const char* to_string(ResolvedEncoderRateControl rc) noexcept;

/// Human/FFmpeg-facing label for the resolved control (used by telemetry and
/// logs): "crf", "constqp", "vbr", "cbr", "driver-default".
[[nodiscard]] const char* resolved_rate_control_label(
    ResolvedEncoderRateControl rc) noexcept;

// ── Vocabulary helpers (single source of truth for backend presets/tunes) ──

[[nodiscard]] bool is_x264_preset(std::string_view preset) noexcept;
[[nodiscard]] bool is_nvenc_preset(std::string_view preset) noexcept;
[[nodiscard]] bool is_x264_tune(std::string_view tune) noexcept;

// ── Request ─────────────────────────────────────────────────────────────────
//
// The request distinguishes what the user/config asked for (explicit_*) from
// what is only an engine placeholder default. A default must never be
// indistinguishable from an explicit user request.

struct EncoderConfigRequest {
    /// Codec string as configured ("auto", "libx264", "libx265", "h264",
    /// "hevc", "h264_nvenc", "hevc_nvenc", ...).
    std::string codec{"auto"};
    /// Hardware encoder ("none" or "nvenc"; other backends are unsupported
    /// by the native encoder).
    std::string hardware_encoder{"none"};

    /// Raw configured rate-control mode string. Empty or an engine-default
    /// placeholder (when not explicit) means "not requested".
    std::string rate_control_mode;
    /// True only when the caller/config file explicitly requested the mode.
    bool rate_control_explicit{false};

    bool crf_explicit{false};
    int crf{-1};                  // -1 = not carried
    bool qp_explicit{false};
    int qp{-1};                   // -1 = not carried
    bool bitrate_explicit{false};
    std::int64_t bitrate{0};

    /// Empty = codec/encoder default.
    std::string preset;
    bool preset_explicit{false};

    std::string tune;
    bool tune_explicit{false};

    /// Requested NVENC in-flight depth (0 = engine default).
    int async_depth{0};
};

// ── Failure ─────────────────────────────────────────────────────────────────

struct EncoderConfigFailure {
    std::string message;
};

// Forward declarations so the Result alias (and the resolver friend) can be
// declared before the class body below.
class ResolvedEncoderConfig;

using EncoderResolution =
    chronon3d::Result<ResolvedEncoderConfig, EncoderConfigFailure>;

// ── Immutable resolved configuration ────────────────────────────────────────
//
// Constructed only by resolve_encoder_config(); exposes read-only getters.
// Encoders consume this value and apply it verbatim; they never decide
// encoder policy themselves.

class ResolvedEncoderConfig {
public:
    ResolvedEncoderConfig(const ResolvedEncoderConfig&) = default;
    ResolvedEncoderConfig& operator=(const ResolvedEncoderConfig&) = delete;
    ResolvedEncoderConfig(ResolvedEncoderConfig&&) = default;
    ResolvedEncoderConfig& operator=(ResolvedEncoderConfig&&) = delete;

    [[nodiscard]] EncoderBackend backend() const noexcept { return backend_; }
    [[nodiscard]] const std::string& encoder_name() const noexcept { return encoder_name_; }

    /// Resolved rate control (never Unspecified).
    [[nodiscard]] ResolvedEncoderRateControl rate_control() const noexcept { return rate_control_; }
    /// Whether the resolved rate control was explicitly requested by the
    /// caller (false = engine default for the backend).
    [[nodiscard]] bool rate_control_explicit() const noexcept { return rate_control_explicit_; }
    [[nodiscard]] const char* rate_control_label() const noexcept {
        return resolved_rate_control_label(rate_control_);
    }
    [[nodiscard]] bool is_driver_default() const noexcept {
        return rate_control_ == ResolvedEncoderRateControl::DriverDefault;
    }

    [[nodiscard]] const std::optional<int>& crf() const noexcept { return crf_; }
    [[nodiscard]] const std::optional<int>& qp() const noexcept { return qp_; }
    [[nodiscard]] const std::optional<std::int64_t>& bitrate() const noexcept { return bitrate_; }

    /// Preset actually applied. Empty means "codec default" (no option).
    [[nodiscard]] const std::string& preset() const noexcept { return preset_; }
    [[nodiscard]] bool preset_explicit() const noexcept { return preset_explicit_; }
    /// Human label for telemetry: the preset, or "ffmpeg-default"/"x264-default".
    [[nodiscard]] std::string preset_label() const;

    /// Tune actually applied on software encoders (never on NVENC).
    [[nodiscard]] const std::optional<std::string>& tune() const noexcept { return tune_; }

    /// Resolved NVENC in-flight depth (software keeps 0).
    [[nodiscard]] int async_depth() const noexcept { return async_depth_; }

private:
    friend EncoderResolution resolve_encoder_config(const EncoderConfigRequest& request);

    ResolvedEncoderConfig() = default;
    ResolvedEncoderConfig(EncoderBackend backend, std::string encoder_name,
                          ResolvedEncoderRateControl rate_control,
                          bool rate_control_explicit,
                          std::optional<int> crf, std::optional<int> qp,
                          std::optional<std::int64_t> bitrate,
                          std::string preset, bool preset_explicit,
                          std::optional<std::string> tune, int async_depth);

    EncoderBackend backend_{EncoderBackend::Software};
    std::string encoder_name_;
    ResolvedEncoderRateControl rate_control_{ResolvedEncoderRateControl::DriverDefault};
    bool rate_control_explicit_{false};
    std::optional<int> crf_;
    std::optional<int> qp_;
    std::optional<std::int64_t> bitrate_;
    std::string preset_;
    bool preset_explicit_{false};
    std::optional<std::string> tune_;
    int async_depth_{0};
};

// ── Resolution entry point ──────────────────────────────────────────────────

/// Resolve + validate a request against the canonical matrix.
///
/// The result is immutable; failures are explicit (never a silent fallback).
/// The matrix (see the .cpp for the full table):
///
///   software + CRF        → valid, crf level resolved
///   software + QP         → invalid (no certified constant-QP x264 profile)
///   software + bitrate    → valid, ABR bitrate resolved
///   software + vbr        → valid (alias of bitrate on x264)
///   software + cbr        → invalid
///   NVENC   + CRF         → invalid when explicitly requested (no certified
///                           constant-quality NVENC profile in this closeout)
///   NVENC   + QP          → valid, constqp + qp resolved
///   NVENC   + bitrate/vbr → valid, rc=vbr + bitrate resolved
///   NVENC   + cbr         → valid, rc=cbr + bitrate resolved
///   NVENC   + unspecified → valid, resolved DriverDefault (marked)
///   tune on NVENC         → invalid when explicitly requested
///   preset/tune vocabulary is validated per backend
[[nodiscard]] EncoderResolution resolve_encoder_config(
    const EncoderConfigRequest& request);

/// Certification/benchmark gate: official benchmarks must make every encoder
/// decision explicit — a resolved NVENC driver default makes the encoded
/// output depend on the installed driver and must not be benchmarked.
/// Returns a failure message when the resolution is not benchmark-safe.
[[nodiscard]] std::optional<std::string> validate_for_benchmark(
    const ResolvedEncoderConfig& resolved) noexcept;

// ── Backend helpers (single source of truth) ────────────────────────────────

/// Resolve the encoder backend from codec + hardware_encoder strings.
[[nodiscard]] EncoderBackend resolve_encoder_backend(
    const std::string& codec, const std::string& hardware_encoder) noexcept;

/// Resolve the FFmpeg encoder name (libx264/libx264rgb/libx265/h264_nvenc/
/// hevc_nvenc) from codec + hardware_encoder strings.
[[nodiscard]] std::string resolve_ffmpeg_encoder_name(
    const std::string& codec, const std::string& hardware_encoder);

/// Build an EncoderConfigRequest from the CLI pipe options, preserving the
/// caller's explicitness markers.
[[nodiscard]] EncoderConfigRequest make_encoder_config_request(
    const FfmpegPipeOptions& options);

} // namespace chronon3d::cli
