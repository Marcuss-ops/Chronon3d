// =============================================================================
// encoder_config_resolution.cpp — Single resolution/validation authority for
// encoder configuration. See encoder_config_resolution.hpp for the rationale.
//
// The matrix implemented here is the ONLY place where rate-control intent,
// presets and tunes are interpreted for the FFmpeg backends Chronon drives:
//
//   | intent                | software (libx264 family)   | NVENC                 |
//   |-----------------------|-----------------------------|-----------------------|
//   | ConstantQuality (crf) | ✅ crf level resolved       | ❌ explicit → error   |
//   | ConstantQp            | ❌ error (no certified      | ✅ constqp + qp       |
//   |                       |    x264 constant-QP path)   |                       |
//   | Bitrate / vbr         | ✅ ABR bitrate              | ✅ rc=vbr + bitrate   |
//   | cbr                   | ❌ error                    | ✅ rc=cbr + bitrate   |
//   | unspecified           | ✅ crf engine default       | ✅ DriverDefault      |
//   |                       |                             |    (marked, not       |
//   |                       |                             |    benchmark-safe)    |
//   | tune                  | ✅ x264 vocab (explicit)    | ❌ explicit → error   |
//   | preset                | ✅ x264 vocab               | ✅ NVENC vocab        |
//
// Everything below is pure C++ (no FFmpeg, CUDA or GPU), so the whole matrix
// is unit-testable without hardware.
// =============================================================================

#include "encoder_config_resolution.hpp"

#include "ffmpeg_pipe_encoder.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace chronon3d::cli {

namespace {

[[nodiscard]] std::string ascii_lower(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

[[nodiscard]] bool equals_ignore_case(std::string_view a, std::string_view b) noexcept {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](char x, char y) {
                          return std::tolower(static_cast<unsigned char>(x)) ==
                                 std::tolower(static_cast<unsigned char>(y));
                      });
}

const char* kX264Presets[] = {
    "ultrafast", "superfast", "veryfast", "faster", "fast",
    "medium",    "slow",      "slower",   "veryslow", "placebo",
};

const char* kNvencPresets[] = {
    "default", "slow",   "medium",     "fast",    "hp",      "hq",
    "bd",      "ll",     "llhq",       "llhp",    "lossless", "losslesshp",
    "p1",      "p2",     "p3",         "p4",      "p5",      "p6",   "p7",
};

const char* kX264Tunes[] = {
    "film", "animation", "grain", "stillimage",
    "fastdecode", "zerolatency", "psnr", "ssim",
};

[[nodiscard]] bool contains_ci(std::string_view value,
                               const char* const* list, std::size_t count) noexcept {
    for (std::size_t i = 0; i < count; ++i) {
        if (equals_ignore_case(value, list[i])) return true;
    }
    return false;
}

[[nodiscard]] std::string join_list(const char* const* list, std::size_t count) {
    std::string out;
    for (std::size_t i = 0; i < count; ++i) {
        if (!out.empty()) out += ", ";
        out += list[i];
    }
    return out;
}

[[nodiscard]] EncoderResolution failure(std::string message) {
    return EncoderConfigFailure{std::move(message)};
}

[[nodiscard]] bool software_x264_family(std::string_view encoder_name) noexcept {
    return encoder_name == "libx264" || encoder_name == "libx264rgb";
}

} // namespace

// ── Enum strings ────────────────────────────────────────────────────────────

const char* to_string(EncoderBackend backend) noexcept {
    switch (backend) {
        case EncoderBackend::Software: return "software";
        case EncoderBackend::Nvenc: return "nvenc";
    }
    return "unknown";
}

const char* to_string(EncoderRateControlRequest rc) noexcept {
    switch (rc) {
        case EncoderRateControlRequest::Unspecified: return "unspecified";
        case EncoderRateControlRequest::ConstantQuality: return "constant-quality";
        case EncoderRateControlRequest::ConstantQp: return "constant-qp";
        case EncoderRateControlRequest::Bitrate: return "bitrate";
        case EncoderRateControlRequest::Vbr: return "vbr";
        case EncoderRateControlRequest::Cbr: return "cbr";
    }
    return "unknown";
}

const char* to_string(ResolvedEncoderRateControl rc) noexcept {
    switch (rc) {
        case ResolvedEncoderRateControl::ConstantQuality: return "constant-quality";
        case ResolvedEncoderRateControl::ConstantQp: return "constant-qp";
        case ResolvedEncoderRateControl::Vbr: return "vbr";
        case ResolvedEncoderRateControl::Cbr: return "cbr";
        case ResolvedEncoderRateControl::DriverDefault: return "driver-default";
    }
    return "unknown";
}

const char* resolved_rate_control_label(ResolvedEncoderRateControl rc) noexcept {
    switch (rc) {
        case ResolvedEncoderRateControl::ConstantQuality: return "crf";
        case ResolvedEncoderRateControl::ConstantQp: return "constqp";
        case ResolvedEncoderRateControl::Vbr: return "vbr";
        case ResolvedEncoderRateControl::Cbr: return "cbr";
        case ResolvedEncoderRateControl::DriverDefault: return "driver-default";
    }
    return "unknown";
}

bool parse_rate_control_request(
    std::string_view mode, EncoderRateControlRequest& out) noexcept {
    const std::string normalized = ascii_lower(mode);
    if (normalized == "crf") {
        out = EncoderRateControlRequest::ConstantQuality;
        return true;
    }
    if (normalized == "qp") {
        out = EncoderRateControlRequest::ConstantQp;
        return true;
    }
    if (normalized == "bitrate") {
        out = EncoderRateControlRequest::Bitrate;
        return true;
    }
    if (normalized == "vbr") {
        out = EncoderRateControlRequest::Vbr;
        return true;
    }
    if (normalized == "cbr") {
        out = EncoderRateControlRequest::Cbr;
        return true;
    }
    return false;
}

// ── Vocabulary ──────────────────────────────────────────────────────────────

bool is_x264_preset(std::string_view preset) noexcept {
    return contains_ci(preset, kX264Presets, std::size(kX264Presets));
}

bool is_nvenc_preset(std::string_view preset) noexcept {
    return contains_ci(preset, kNvencPresets, std::size(kNvencPresets));
}

bool is_x264_tune(std::string_view tune) noexcept {
    return contains_ci(tune, kX264Tunes, std::size(kX264Tunes));
}

// ── Backend resolution ──────────────────────────────────────────────────────

EncoderBackend resolve_encoder_backend(
    const std::string& codec, const std::string& hardware_encoder) noexcept {
    if (equals_ignore_case(hardware_encoder, "nvenc")) return EncoderBackend::Nvenc;
    if (equals_ignore_case(codec, "h264_nvenc") ||
        equals_ignore_case(codec, "hevc_nvenc") ||
        equals_ignore_case(codec, "av1_nvenc")) {
        return EncoderBackend::Nvenc;
    }
    return EncoderBackend::Software;
}

std::string resolve_ffmpeg_encoder_name(
    const std::string& codec, const std::string& hardware_encoder) {
    const std::string normalized_codec = ascii_lower(codec);
    const std::string normalized_hw = ascii_lower(hardware_encoder);
    const bool nvenc = normalized_hw == "nvenc" ||
                       normalized_codec == "h264_nvenc" ||
                       normalized_codec == "hevc_nvenc" ||
                       normalized_codec == "av1_nvenc";
    if (nvenc) {
        if (normalized_codec == "hevc_nvenc" || normalized_codec == "hevc" ||
            normalized_codec == "libx265" || normalized_codec == "h265") {
            return "hevc_nvenc";
        }
        return "h264_nvenc";
    }
    // NOTE: encoder-name selection intentionally mirrors the historical
    // native selection so this resolution does not change which codec runs
    // for existing configurations; the C-1/C-2 policy scope is rate control,
    // preset and tune.
    if (normalized_codec == "libx264rgb") return "libx264rgb";
    return "libx264";
}

// ── Immutable resolved configuration ────────────────────────────────────────

ResolvedEncoderConfig::ResolvedEncoderConfig(
    EncoderBackend backend, std::string encoder_name,
    ResolvedEncoderRateControl rate_control, bool rate_control_explicit,
    std::optional<int> crf, std::optional<int> qp,
    std::optional<std::int64_t> bitrate,
    std::string preset, bool preset_explicit,
    std::optional<std::string> tune, int async_depth)
    : backend_(backend),
      encoder_name_(std::move(encoder_name)),
      rate_control_(rate_control),
      rate_control_explicit_(rate_control_explicit),
      crf_(std::move(crf)),
      qp_(std::move(qp)),
      bitrate_(std::move(bitrate)),
      preset_(std::move(preset)),
      preset_explicit_(preset_explicit),
      tune_(std::move(tune)),
      async_depth_(async_depth) {}

std::string ResolvedEncoderConfig::preset_label() const {
    if (!preset_.empty()) return preset_;
    return backend_ == EncoderBackend::Nvenc ? "ffmpeg-nvenc-default"
                                             : "x264-default";
}

// ── Resolution ──────────────────────────────────────────────────────────────

EncoderResolution resolve_encoder_config(const EncoderConfigRequest& request) {
    const EncoderBackend backend =
        resolve_encoder_backend(request.codec, request.hardware_encoder);
    const std::string encoder_name =
        resolve_ffmpeg_encoder_name(request.codec, request.hardware_encoder);
    const bool nvenc = backend == EncoderBackend::Nvenc;

    // ── 1. Determine rate-control intent ────────────────────────────────
    // An unknown rate-control string must never reach an encoder: when the
    // caller explicitly requested a mode, reject anything outside the
    // canonical vocabulary right here (no silent fallback onto CRF or onto
    // the NVENC driver default).
    EncoderRateControlRequest intent = EncoderRateControlRequest::Unspecified;
    const bool rc_explicit = request.rate_control_explicit;
    if (request.rate_control_explicit) {
        if (!parse_rate_control_request(request.rate_control_mode, intent)) {
            return failure(
                "unknown rate-control mode \"" + request.rate_control_mode +
                "\"; allowed modes: crf, qp, bitrate" +
                (nvenc ? ", vbr, cbr" : "") +
                " (vbr/cbr are NVENC-only)");
        }
    } else if (!request.rate_control_mode.empty() &&
               !equals_ignore_case(request.rate_control_mode, "crf")) {
        // A carried mode that is not the engine-default placeholder: the
        // caller is requesting something but forgot to mark it explicit.
        // Reject it rather than guessing (default != explicit guarantee).
        return failure(
            "rate-control mode \"" + request.rate_control_mode +
            "\" was configured without an explicit request marker; internal "
            "configuration error (default must never be indistinguishable "
            "from an explicit request)");
    }

    // A single explicit quality knob with no explicit mode expresses intent
    // (e.g. --crf 20 alone means constant quality).
    const int explicit_knobs =
        static_cast<int>(request.crf_explicit) +
        static_cast<int>(request.qp_explicit) +
        static_cast<int>(request.bitrate_explicit);
    if (intent == EncoderRateControlRequest::Unspecified && explicit_knobs > 0) {
        if (explicit_knobs > 1) {
            return failure(
                "conflicting explicit quality knobs: crf, qp and bitrate are "
                "mutually exclusive; select one rate-control mode");
        }
        if (request.crf_explicit) intent = EncoderRateControlRequest::ConstantQuality;
        else if (request.qp_explicit) intent = EncoderRateControlRequest::ConstantQp;
        else intent = EncoderRateControlRequest::Bitrate;
    }

    // Reject quality knobs that contradict the explicitly requested mode.
    if (request.rate_control_explicit) {
        const bool has_foreign_knob =
            (intent != EncoderRateControlRequest::ConstantQuality && request.crf_explicit) ||
            (intent != EncoderRateControlRequest::ConstantQp && request.qp_explicit) ||
            (intent != EncoderRateControlRequest::Bitrate &&
             intent != EncoderRateControlRequest::Vbr &&
             intent != EncoderRateControlRequest::Cbr &&
             request.bitrate_explicit);
        if (has_foreign_knob) {
            return failure(
                "rate-control mode '" + request.rate_control_mode +
                "' cannot be combined with an explicit crf/qp/bitrate value "
                "of another mode; select one mode and its own quality knob");
        }
    }

    // ── 2. Resolve rate control per backend ─────────────────────────────
    ResolvedEncoderRateControl resolved_rc =
        ResolvedEncoderRateControl::DriverDefault;
    std::optional<int> resolved_crf;
    std::optional<int> resolved_qp;
    std::optional<std::int64_t> resolved_bitrate;

    switch (intent) {
        case EncoderRateControlRequest::Unspecified:
            // Engine default — not a user request.
            if (nvenc) {
                resolved_rc = ResolvedEncoderRateControl::DriverDefault;
            } else {
                resolved_rc = ResolvedEncoderRateControl::ConstantQuality;
                const int carried_crf =
                    (request.crf >= 0 && request.crf <= 51) ? request.crf : 23;
                resolved_crf = carried_crf;
            }
            break;

        case EncoderRateControlRequest::ConstantQuality:
            if (nvenc) {
                return failure(
                    "rate control 'crf' (constant quality) is not supported on "
                    "the NVENC backend in this release; use qp, bitrate/vbr or "
                    "cbr, or omit rate control to keep the NVENC driver default "
                    "(constant-quality NVENC needs a certified profile)");
            }
            resolved_rc = ResolvedEncoderRateControl::ConstantQuality;
            if (request.crf > 51) {
                return failure("crf value " + std::to_string(request.crf) +
                               " is out of range; CRF must be in [0, 51]");
            }
            resolved_crf = (request.crf >= 0) ? request.crf : 23;
            break;

        case EncoderRateControlRequest::ConstantQp:
            if (!nvenc) {
                return failure(
                    "rate control 'qp' (constant quantizer) is not supported on "
                    "the software encoder; use crf or bitrate");
            }
            if (request.qp < 0 || request.qp > 63) {
                return failure("qp rate control requires a valid qp value in "
                               "[0, 63]; got " + std::to_string(request.qp));
            }
            resolved_rc = ResolvedEncoderRateControl::ConstantQp;
            resolved_qp = request.qp;
            break;

        case EncoderRateControlRequest::Bitrate:
        case EncoderRateControlRequest::Vbr:
            if (request.bitrate <= 0) {
                return failure(
                    std::string("bitrate rate control requires a positive "
                                "bitrate in bits/second; got ") +
                    std::to_string(static_cast<long long>(request.bitrate)));
            }
            resolved_rc = ResolvedEncoderRateControl::Vbr;
            resolved_bitrate = request.bitrate;
            break;

        case EncoderRateControlRequest::Cbr:
            if (!nvenc) {
                return failure("rate control 'cbr' is not available on the "
                               "software encoder; use bitrate or crf");
            }
            if (request.bitrate <= 0) {
                return failure("cbr rate control requires a positive bitrate "
                               "in bits/second; got " +
                               std::to_string(static_cast<long long>(request.bitrate)));
            }
            resolved_rc = ResolvedEncoderRateControl::Cbr;
            resolved_bitrate = request.bitrate;
            break;
    }

    // ── 3. Preset validation per backend ────────────────────────────────
    std::string resolved_preset;
    if (request.preset_explicit) {
        if (!request.preset.empty()) {
            if (nvenc) {
                if (!is_nvenc_preset(request.preset)) {
                    return failure(
                        "preset \"" + request.preset +
                        "\" is not valid for the selected NVENC backend; "
                        "allowed presets: " +
                        join_list(kNvencPresets, std::size(kNvencPresets)));
                }
            } else if (software_x264_family(encoder_name)) {
                if (!is_x264_preset(request.preset)) {
                    return failure(
                        "preset \"" + request.preset +
                        "\" is not valid for the software (libx264) encoder; "
                        "allowed presets: " +
                        join_list(kX264Presets, std::size(kX264Presets)));
                }
            } else if (!is_x264_preset(request.preset)) {
                return failure(
                    "preset \"" + request.preset +
                    "\" is not valid for software codec \"" + encoder_name +
                    "\"; allowed presets: " +
                    join_list(kX264Presets, std::size(kX264Presets)));
            }
            resolved_preset = request.preset;
        }
    } else {
        // Engine placeholder default: apply it only when it is valid for the
        // selected backend; an x264-oriented placeholder (e.g. "superfast")
        // must never be forwarded to NVENC, where it would be rejected deep
        // inside avcodec_open2.
        const bool valid = nvenc ? is_nvenc_preset(request.preset)
                                 : is_x264_preset(request.preset);
        if (valid) resolved_preset = request.preset;
    }

    // ── 4. Tune per backend ─────────────────────────────────────────────
    std::optional<std::string> resolved_tune;
    if (request.tune_explicit) {
        if (!request.tune.empty()) {
            if (nvenc) {
                return failure(
                    "tune=\"" + request.tune +
                    "\" is not supported on the NVENC backend; NVENC has no "
                    "x264 tune concept — remove the tune option");
            }
            if (!is_x264_tune(request.tune)) {
                return failure(
                    "tune \"" + request.tune +
                    "\" is not valid for the software (libx264) encoder; "
                    "allowed tunes: " +
                    join_list(kX264Tunes, std::size(kX264Tunes)));
            }
            resolved_tune = request.tune;
        }
    } else if (!nvenc && !request.tune.empty()) {
        // Engine default tune (e.g. the CLI's zerolatency for libx264):
        // applied only when it is a valid x264 tune.
        if (is_x264_tune(request.tune)) resolved_tune = request.tune;
    }

    // ── 5. NVENC async depth (engine default 4) ─────────────────────────
    int resolved_async_depth = 0;
    if (nvenc) {
        resolved_async_depth = request.async_depth > 0 ? request.async_depth : 4;
    }

    return ResolvedEncoderConfig{
        backend,
        encoder_name,
        resolved_rc,
        rc_explicit && intent != EncoderRateControlRequest::Unspecified,
        std::move(resolved_crf),
        std::move(resolved_qp),
        std::move(resolved_bitrate),
        std::move(resolved_preset),
        request.preset_explicit,
        std::move(resolved_tune),
        resolved_async_depth,
    };
}

std::optional<std::string> validate_for_benchmark(
    const ResolvedEncoderConfig& resolved) noexcept {
    if (resolved.is_driver_default()) {
        return std::string(
            "benchmark/certification requires explicit encoder rate control; "
            "the resolved NVENC driver default depends on the installed driver "
            "and is not reproducible — request qp, bitrate/vbr or cbr");
    }
    return std::nullopt;
}

// ── Request builder from CLI pipe options ──────────────────────────────────

EncoderConfigRequest make_encoder_config_request(
    const FfmpegPipeOptions& options) {
    EncoderConfigRequest request;
    request.codec = options.codec;
    request.hardware_encoder = options.hardware_encoder;
    request.rate_control_mode = options.rate_control_mode;
    request.rate_control_explicit = options.rate_control_mode_explicit;
    request.crf_explicit = options.crf_explicit;
    request.crf = options.crf;
    request.qp_explicit = options.qp_explicit;
    request.qp = options.qp;
    request.bitrate_explicit = options.bitrate_explicit;
    request.bitrate = options.bitrate;
    request.preset = options.preset;
    request.preset_explicit = options.preset_explicit;
    request.tune = options.tune;
    request.tune_explicit = options.tune_explicit;
    request.async_depth = options.async_depth;
    return request;
}

} // namespace chronon3d::cli
