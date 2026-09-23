// =============================================================================
// test_encoder_config_resolution.cpp — Pure table-driven tests for the
// encoder-configuration resolver (C-1/C-2 closeout).
//
// The resolver is deliberately dependency-free (no FFmpeg, CUDA, GPU or CLI
// process), so the whole validation matrix runs as fast unit tests here:
//
//   software + unspecified        → valid, CRF engine default resolved
//   NVENC   + unspecified         → valid, driver-default (marked, not
//                                   benchmark-safe)
//   NVENC   + explicit CRF        → invalid
//   NVENC   + QP=23               → valid, constqp + qp resolved
//   NVENC   + bitrate/vbr         → valid, vbr + bitrate resolved
//   NVENC   + cbr w/o bitrate     → invalid
//   NVENC   + tune                → invalid
//   unknown rate-control string   → invalid on every backend
//   NVENC preset on software / x264 preset on NVENC → invalid
//   default != explicit request   → enforced (never conflated)
// =============================================================================

#include <doctest/doctest.h>

#include <apps/chronon3d_cli/utils/video/encoder_config_resolution.hpp>
#include <apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.hpp>

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/media/video/video_config.hpp>

#include <optional>
#include <string>

using namespace chronon3d::cli;

namespace {

// ── Helpers ────────────────────────────────────────────────────────────────

[[nodiscard]] EncoderConfigRequest sw_request() {
    EncoderConfigRequest r;
    r.codec = "libx264";
    r.hardware_encoder = "none";
    return r;
}

[[nodiscard]] EncoderConfigRequest nv_request() {
    EncoderConfigRequest r;
    r.codec = "h264";
    r.hardware_encoder = "nvenc";
    return r;
}

/// Resolve and require success; returns the resolved config for checks.
[[nodiscard]] ResolvedEncoderConfig require_ok(const EncoderConfigRequest& r) {
    const auto resolution = resolve_encoder_config(r);
    REQUIRE(resolution.has_value());
    return std::move(resolution).value();
}

/// Resolve and require failure; returns the error message.
[[nodiscard]] std::string require_fail(const EncoderConfigRequest& r) {
    const auto resolution = resolve_encoder_config(r);
    REQUIRE_FALSE(resolution.has_value());
    return std::move(resolution).error().message;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Backend vocabulary & encoder-name resolution
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("resolve_encoder_backend: hardware_encoder and nvenc codec names") {
    CHECK(resolve_encoder_backend("libx264", "none") == EncoderBackend::Software);
    CHECK(resolve_encoder_backend("h264", "none") == EncoderBackend::Software);
    CHECK(resolve_encoder_backend("libx264", "nvenc") == EncoderBackend::Nvenc);
    CHECK(resolve_encoder_backend("h264", "nvenc") == EncoderBackend::Nvenc);
    CHECK(resolve_encoder_backend("h264_nvenc", "none") == EncoderBackend::Nvenc);
    CHECK(resolve_encoder_backend("hevc_nvenc", "none") == EncoderBackend::Nvenc);
}

TEST_CASE("resolve_ffmpeg_encoder_name: mirrors the native selection") {
    CHECK(resolve_ffmpeg_encoder_name("libx264", "none") == "libx264");
    CHECK(resolve_ffmpeg_encoder_name("libx264rgb", "none") == "libx264rgb");
    CHECK(resolve_ffmpeg_encoder_name("h264", "nvenc") == "h264_nvenc");
    CHECK(resolve_ffmpeg_encoder_name("hevc", "nvenc") == "hevc_nvenc");
    CHECK(resolve_ffmpeg_encoder_name("libx265", "nvenc") == "hevc_nvenc");
    CHECK(resolve_ffmpeg_encoder_name("h264_nvenc", "none") == "h264_nvenc");
    CHECK(resolve_ffmpeg_encoder_name("hevc_nvenc", "none") == "hevc_nvenc");
}

TEST_CASE("preset/tune vocabulary membership") {
    CHECK(is_x264_preset("ultrafast"));
    CHECK(is_x264_preset("placebo"));
    CHECK_FALSE(is_x264_preset("p4"));
    CHECK_FALSE(is_x264_preset("lossless"));
    CHECK(is_nvenc_preset("p4"));
    CHECK(is_nvenc_preset("lossless"));
    CHECK(is_nvenc_preset("slow"));
    CHECK_FALSE(is_nvenc_preset("superfast"));
    CHECK_FALSE(is_nvenc_preset("veryslow"));
    CHECK(is_x264_tune("zerolatency"));
    CHECK(is_x264_tune("film"));
    CHECK_FALSE(is_x264_tune("veryfast"));
}

TEST_CASE("parse_rate_control_request: canonical vocabulary and unknowns") {
    EncoderRateControlRequest out = EncoderRateControlRequest::Unspecified;
    CHECK(parse_rate_control_request("crf", out));
    CHECK(out == EncoderRateControlRequest::ConstantQuality);
    CHECK(parse_rate_control_request("qp", out));
    CHECK(out == EncoderRateControlRequest::ConstantQp);
    CHECK(parse_rate_control_request("bitrate", out));
    CHECK(out == EncoderRateControlRequest::Bitrate);
    CHECK(parse_rate_control_request("vbr", out));
    CHECK(out == EncoderRateControlRequest::Vbr);
    CHECK(parse_rate_control_request("cbr", out));
    CHECK(out == EncoderRateControlRequest::Cbr);
    CHECK_FALSE(parse_rate_control_request("quality", out));
    CHECK_FALSE(parse_rate_control_request("banana", out));
    CHECK_FALSE(parse_rate_control_request("", out));
}

// ═══════════════════════════════════════════════════════════════════════════
// Engine defaults (no explicit request)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("software + unspecified rate control → valid CRF engine default") {
    auto r = sw_request();
    r.crf = 18;   // carried engine-default quality level
    const auto resolved = require_ok(r);

    CHECK(resolved.backend() == EncoderBackend::Software);
    CHECK(resolved.encoder_name() == "libx264");
    CHECK(resolved.rate_control() == ResolvedEncoderRateControl::ConstantQuality);
    CHECK_FALSE(resolved.rate_control_explicit());
    CHECK(resolved.crf().has_value());
    CHECK(*resolved.crf() == 18);
    CHECK_FALSE(resolved.is_driver_default());
    CHECK(std::string(resolved.rate_control_label()) == "crf");
}

TEST_CASE("NVENC + unspecified rate control → valid, driver default marked") {
    const auto resolved = require_ok(nv_request());

    CHECK(resolved.backend() == EncoderBackend::Nvenc);
    CHECK(resolved.encoder_name() == "h264_nvenc");
    CHECK(resolved.rate_control() == ResolvedEncoderRateControl::DriverDefault);
    CHECK_FALSE(resolved.rate_control_explicit());
    CHECK(resolved.is_driver_default());
    CHECK_FALSE(resolved.crf().has_value());
    CHECK_FALSE(resolved.qp().has_value());
    // Engine NVENC async-depth default is deterministic (4 in-flight frames).
    CHECK(resolved.async_depth() == 4);
}

TEST_CASE("NVENC + HEVC codec resolves to hevc_nvenc with the same policy") {
    auto r = nv_request();
    r.codec = "hevc";
    const auto resolved = require_ok(r);
    CHECK(resolved.backend() == EncoderBackend::Nvenc);
    CHECK(resolved.encoder_name() == "hevc_nvenc");
    CHECK(resolved.is_driver_default());
}

TEST_CASE("NVENC + shared engine-default preset resolves to codec default") {
    auto r = nv_request();
    r.preset = std::string(kDefaultEncoderPreset);
    r.preset_explicit = false;
    const auto resolved = require_ok(r);
    CHECK(resolved.preset().empty());
    CHECK(resolved.preset_label() == "ffmpeg-nvenc-default");
}

TEST_CASE("software + shared engine-default preset resolves to ultrafast") {
    auto r = sw_request();
    r.preset = std::string(kDefaultEncoderPreset);
    r.preset_explicit = false;
    const auto resolved = require_ok(r);
    CHECK(resolved.preset() == "ultrafast");
    CHECK_FALSE(resolved.preset_explicit());
}

TEST_CASE("NVENC + engine-default valid NVENC preset is retained") {
    auto r = nv_request();
    r.preset = "slow";   // valid legacy NVENC name (also the CLI default)
    r.preset_explicit = false;
    const auto resolved = require_ok(r);
    CHECK(resolved.preset() == "slow");
}

// ═══════════════════════════════════════════════════════════════════════════
// Validation matrix — explicit requests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("NVENC + explicit CRF → invalid (no certified constant-quality profile)") {
    auto r = nv_request();
    r.rate_control_mode = "crf";
    r.rate_control_explicit = true;
    r.crf = 18;
    const auto message = require_fail(r);
    CHECK(message.find("crf") != std::string::npos);
    CHECK(message.find("NVENC") != std::string::npos);
}

TEST_CASE("NVENC + explicit crf quality knob (no mode) → invalid") {
    auto r = nv_request();
    r.crf_explicit = true;
    r.crf = 20;
    const auto message = require_fail(r);
    CHECK(message.find("crf") != std::string::npos);
}

TEST_CASE("software + explicit CRF → valid constant-quality") {
    auto r = sw_request();
    r.rate_control_mode = "crf";
    r.rate_control_explicit = true;
    r.crf_explicit = true;
    r.crf = 21;
    const auto resolved = require_ok(r);
    CHECK(resolved.rate_control() == ResolvedEncoderRateControl::ConstantQuality);
    CHECK(resolved.rate_control_explicit());
    CHECK(*resolved.crf() == 21);
}

TEST_CASE("NVENC + QP=23 → constqp resolved") {
    auto r = nv_request();
    r.rate_control_mode = "qp";
    r.rate_control_explicit = true;
    r.qp_explicit = true;
    r.qp = 23;
    const auto resolved = require_ok(r);
    CHECK(resolved.rate_control() == ResolvedEncoderRateControl::ConstantQp);
    CHECK(resolved.rate_control_explicit());
    CHECK(resolved.qp().has_value());
    CHECK(*resolved.qp() == 23);
    CHECK(std::string(resolved.rate_control_label()) == "constqp");
    CHECK_FALSE(resolved.is_driver_default());
}

TEST_CASE("NVENC + QP mode without a QP value → invalid") {
    auto r = nv_request();
    r.rate_control_mode = "qp";
    r.rate_control_explicit = true;
    r.qp = -1;   // no value carried
    const auto message = require_fail(r);
    CHECK(message.find("qp") != std::string::npos);
}

TEST_CASE("NVENC + VBR + bitrate=8M → valid") {
    auto r = nv_request();
    r.rate_control_mode = "vbr";
    r.rate_control_explicit = true;
    r.bitrate_explicit = true;
    r.bitrate = 8'000'000;
    const auto resolved = require_ok(r);
    CHECK(resolved.rate_control() == ResolvedEncoderRateControl::Vbr);
    CHECK(resolved.bitrate().has_value());
    CHECK(*resolved.bitrate() == 8'000'000);
}

TEST_CASE("NVENC + CBR + bitrate missing → invalid") {
    auto r = nv_request();
    r.rate_control_mode = "cbr";
    r.rate_control_explicit = true;
    r.bitrate = 0;
    const auto message = require_fail(r);
    CHECK(message.find("cbr") != std::string::npos);
    CHECK(message.find("bitrate") != std::string::npos);
}

TEST_CASE("NVENC + CBR + bitrate present → valid") {
    auto r = nv_request();
    r.rate_control_mode = "cbr";
    r.rate_control_explicit = true;
    r.bitrate_explicit = true;
    r.bitrate = 6'000'000;
    const auto resolved = require_ok(r);
    CHECK(resolved.rate_control() == ResolvedEncoderRateControl::Cbr);
    CHECK(*resolved.bitrate() == 6'000'000);
}

TEST_CASE("software + bitrate mode with no bitrate → invalid") {
    auto r = sw_request();
    r.rate_control_mode = "bitrate";
    r.rate_control_explicit = true;
    r.bitrate = 0;
    const auto message = require_fail(r);
    CHECK(message.find("bitrate") != std::string::npos);
}

TEST_CASE("software + bitrate mode with bitrate → valid ABR") {
    auto r = sw_request();
    r.rate_control_mode = "bitrate";
    r.rate_control_explicit = true;
    r.bitrate_explicit = true;
    r.bitrate = 4'000'000;
    const auto resolved = require_ok(r);
    CHECK(resolved.rate_control() == ResolvedEncoderRateControl::Vbr);
    CHECK(*resolved.bitrate() == 4'000'000);
    CHECK_FALSE(resolved.crf().has_value());
}

TEST_CASE("software + QP mode → invalid (no certified x264 constant-QP path)") {
    auto r = sw_request();
    r.rate_control_mode = "qp";
    r.rate_control_explicit = true;
    r.qp_explicit = true;
    r.qp = 23;
    const auto message = require_fail(r);
    CHECK(message.find("qp") != std::string::npos);
}

TEST_CASE("software + CBR → invalid") {
    auto r = sw_request();
    r.rate_control_mode = "cbr";
    r.rate_control_explicit = true;
    r.bitrate_explicit = true;
    r.bitrate = 6'000'000;
    const auto message = require_fail(r);
    CHECK(message.find("cbr") != std::string::npos);
}

TEST_CASE("unknown rate-control mode is rejected on every backend") {
    SUBCASE("software") {
        auto r = sw_request();
        r.rate_control_mode = "quality";
        r.rate_control_explicit = true;
        const auto message = require_fail(r);
        CHECK(message.find("quality") != std::string::npos);
        CHECK(message.find("rate-control") != std::string::npos);
    }
    SUBCASE("nvenc") {
        auto r = nv_request();
        r.rate_control_mode = "banana";
        r.rate_control_explicit = true;
        const auto message = require_fail(r);
        CHECK(message.find("banana") != std::string::npos);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Preset and tune vocabulary per backend
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("NVENC + x264-only preset → invalid") {
    auto r = nv_request();
    r.rate_control_mode = "qp";
    r.rate_control_explicit = true;
    r.qp_explicit = true;
    r.qp = 23;
    r.preset_explicit = true;
    r.preset = "superfast";
    const auto message = require_fail(r);
    CHECK(message.find("superfast") != std::string::npos);
    CHECK(message.find("NVENC") != std::string::npos);
}

TEST_CASE("software + NVENC-only preset → invalid") {
    auto r = sw_request();
    r.preset_explicit = true;
    r.preset = "p4";
    const auto message = require_fail(r);
    CHECK(message.find("p4") != std::string::npos);
}

TEST_CASE("NVENC + valid p-series preset → valid") {
    auto r = nv_request();
    r.preset_explicit = true;
    r.preset = "p4";
    const auto resolved = require_ok(r);
    CHECK(resolved.preset() == "p4");
    CHECK(resolved.preset_explicit());
}

TEST_CASE("software + x264 preset → valid") {
    auto r = sw_request();
    r.preset_explicit = true;
    r.preset = "medium";
    const auto resolved = require_ok(r);
    CHECK(resolved.preset() == "medium");
}

TEST_CASE("NVENC + explicit tune → invalid (no silent ignore)") {
    auto r = nv_request();
    r.rate_control_mode = "qp";
    r.rate_control_explicit = true;
    r.qp_explicit = true;
    r.qp = 23;
    r.tune_explicit = true;
    r.tune = "film";
    const auto message = require_fail(r);
    CHECK(message.find("tune") != std::string::npos);
    CHECK(message.find("NVENC") != std::string::npos);
}

TEST_CASE("NVENC + engine-default tune is dropped, not an error") {
    // finalize_video_settings can inject a default tune for libx264 only, but
    // a defensive caller may still carry a non-explicit tune: it must not be
    // silently forwarded to NVENC nor fail the job.
    auto r = nv_request();
    r.tune = "zerolatency";
    r.tune_explicit = false;
    const auto resolved = require_ok(r);
    CHECK_FALSE(resolved.tune().has_value());
}

TEST_CASE("software + valid x264 tune → valid") {
    auto r = sw_request();
    r.tune_explicit = true;
    r.tune = "zerolatency";
    const auto resolved = require_ok(r);
    REQUIRE(resolved.tune().has_value());
    CHECK(*resolved.tune() == "zerolatency");
}

TEST_CASE("software + invalid tune → invalid") {
    auto r = sw_request();
    r.tune_explicit = true;
    r.tune = "superfast";
    const auto message = require_fail(r);
    CHECK(message.find("tune") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════
// default != explicit request (never conflated)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("NVENC default CRF mode (not explicit) is NOT an explicit CRF request") {
    // This is the exact historical trap: FfmpegPipeOptions defaulted to
    // rate_control_mode="crf", so every NVENC job looked like an explicit CRF
    // request. With explicitness markers, the same value with
    // rate_control_explicit=false must resolve to the NVENC driver default.
    auto r = nv_request();
    r.rate_control_mode = "crf";   // engine placeholder default
    r.rate_control_explicit = false;
    r.crf = 18;                    // carried quality placeholder, not explicit
    const auto resolved = require_ok(r);
    CHECK(resolved.rate_control() == ResolvedEncoderRateControl::DriverDefault);
    CHECK_FALSE(resolved.rate_control_explicit());
}

TEST_CASE("non-default mode string without an explicit marker → internal config error") {
    auto r = sw_request();
    r.rate_control_mode = "bitrate";   // not the engine-default placeholder
    r.rate_control_explicit = false;
    const auto message = require_fail(r);
    CHECK(message.find("explicit") != std::string::npos);
}

TEST_CASE("conflicting explicit quality knobs → invalid") {
    auto r = sw_request();
    r.crf_explicit = true;
    r.crf = 18;
    r.bitrate_explicit = true;
    r.bitrate = 4'000'000;
    const auto message = require_fail(r);
    CHECK(message.find("conflicting") != std::string::npos);
}

TEST_CASE("explicit mode combined with a foreign knob → invalid") {
    auto r = sw_request();
    r.rate_control_mode = "bitrate";
    r.rate_control_explicit = true;
    r.bitrate_explicit = true;
    r.bitrate = 4'000'000;
    r.crf_explicit = true;
    r.crf = 18;
    const auto message = require_fail(r);
    CHECK(message.find("bitrate") != std::string::npos);
}

TEST_CASE("single explicit knob implies the mode (--crf alone means CRF)") {
    auto r = sw_request();
    r.crf_explicit = true;
    r.crf = 20;
    const auto resolved = require_ok(r);
    CHECK(resolved.rate_control() == ResolvedEncoderRateControl::ConstantQuality);
    CHECK(*resolved.crf() == 20);
}

TEST_CASE("explicit bitrate out of range crf on software") {
    auto r = sw_request();
    r.rate_control_mode = "crf";
    r.rate_control_explicit = true;
    r.crf_explicit = true;
    r.crf = 80;   // out of [0, 51]
    const auto message = require_fail(r);
    CHECK(message.find("out of range") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════
// Benchmark / certification gate
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("benchmark gate rejects the resolved NVENC driver default") {
    const auto resolved = require_ok(nv_request());
    const auto failure_message = validate_for_benchmark(resolved);
    REQUIRE(failure_message.has_value());
    CHECK(failure_message->find("driver default") != std::string::npos);
}

TEST_CASE("benchmark gate accepts explicit encoder decisions") {
    auto r = nv_request();
    r.rate_control_mode = "qp";
    r.rate_control_explicit = true;
    r.qp_explicit = true;
    r.qp = 23;
    const auto resolved = require_ok(r);
    CHECK_FALSE(validate_for_benchmark(resolved).has_value());
}

TEST_CASE("benchmark gate accepts software CRF engine default with explicit quality") {
    auto r = sw_request();
    r.rate_control_mode = "crf";
    r.rate_control_explicit = true;
    r.crf_explicit = true;
    r.crf = 18;
    const auto resolved = require_ok(r);
    CHECK_FALSE(validate_for_benchmark(resolved).has_value());
}

// ═══════════════════════════════════════════════════════════════════════════
// Resolved config is immutable / applied-only
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("resolved encoder config exposes read-only state") {
    // The resolved value has no setters — encoders can only read and apply it.
    // This is enforced at compile time (no mutating API) and pinned here by
    // exercising every accessor on a software resolution.
    auto r = sw_request();
    r.rate_control_mode = "crf";
    r.rate_control_explicit = true;
    r.crf_explicit = true;
    r.crf = 18;
    r.preset_explicit = true;
    r.preset = "medium";
    r.tune_explicit = true;
    r.tune = "zerolatency";
    const auto resolved = require_ok(r);

    CHECK(resolved.backend() == EncoderBackend::Software);
    CHECK(resolved.encoder_name() == "libx264");
    CHECK(resolved.rate_control_label() == std::string("crf"));
    CHECK_FALSE(resolved.is_driver_default());
    REQUIRE(resolved.crf().has_value());
    CHECK(*resolved.crf() == 18);
    CHECK_FALSE(resolved.qp().has_value());
    CHECK_FALSE(resolved.bitrate().has_value());
    CHECK(resolved.preset() == "medium");
    CHECK(resolved.preset_explicit());
    CHECK(resolved.preset_label() == "medium");
    REQUIRE(resolved.tune().has_value());
    CHECK(*resolved.tune() == "zerolatency");
    CHECK(resolved.async_depth() == 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// FfmpegPipeOptions → request builder keeps explicitness
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("make_encoder_config_request preserves explicitness markers") {
    FfmpegPipeOptions options;
    options.codec = "h264";
    options.hardware_encoder = "nvenc";
    options.rate_control_mode = "qp";
    options.rate_control_mode_explicit = true;
    options.qp = 23;
    options.qp_explicit = true;
    options.preset = "p4";
    options.preset_explicit = true;

    const auto request = make_encoder_config_request(options);
    CHECK(request.codec == "h264");
    CHECK(request.hardware_encoder == "nvenc");
    CHECK(request.rate_control_mode == "qp");
    CHECK(request.rate_control_explicit);
    CHECK(request.qp == 23);
    CHECK(request.qp_explicit);
    CHECK(request.preset == "p4");
    CHECK(request.preset_explicit);
    CHECK_FALSE(request.crf_explicit);
    CHECK_FALSE(request.bitrate_explicit);
    CHECK_FALSE(request.tune_explicit);
}

TEST_CASE("FfmpegPipeOptions shared preset default resolves consistently by backend") {
    FfmpegPipeOptions options;
    options.codec = "h264";
    options.preset = std::string(kDefaultEncoderPreset);
    CHECK(options.preset == kDefaultEncoderPreset);

    options.hardware_encoder = "nvenc";
    const auto nvenc = resolve_encoder_config(make_encoder_config_request(options));
    REQUIRE(nvenc.has_value());
    CHECK(nvenc.value().rate_control() == ResolvedEncoderRateControl::DriverDefault);
    CHECK(nvenc.value().preset().empty());

    options.hardware_encoder = "none";
    const auto software = resolve_encoder_config(make_encoder_config_request(options));
    REQUIRE(software.has_value());
    CHECK(software.value().preset() == "ultrafast");
}

// ═══════════════════════════════════════════════════════════════════════════
// Hardware-encoder field vocabulary (AUDIT P1-2)
//
// The field is user input — a CLI flag or an IPC payload. It used to be
// classified by a literal `hardware_encoder == "nvenc"` at ~10 sites while the
// canonical parser matched case-insensitively, so a payload spelling `NVENC`
// asked for hardware encoding and still reached the requirement builder with
// `gpu_required = false`. These assertions pin the single classifier, the fact
// that the CODEC axis must not decide it, and that the CLI function is a
// projection of the media-layer vocabulary rather than a second implementation.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("hardware_nvenc_requested: user input is classified case-insensitively") {
    CHECK(hardware_nvenc_requested("nvenc"));
    CHECK(hardware_nvenc_requested("NVENC"));
    CHECK(hardware_nvenc_requested("Nvenc"));
    CHECK(hardware_nvenc_requested("nVeNc"));

    CHECK_FALSE(hardware_nvenc_requested("none"));
    CHECK_FALSE(hardware_nvenc_requested("qsv"));
    CHECK_FALSE(hardware_nvenc_requested("software"));
    CHECK_FALSE(hardware_nvenc_requested(""));
    // The codec axis is a different question: an nvenc *codec* name must not
    // make the hardware FIELD look like an NVENC request.
    CHECK_FALSE(hardware_nvenc_requested("h264_nvenc"));
    CHECK_FALSE(hardware_nvenc_requested("hevc_nvenc"));
}

TEST_CASE("normalize_hardware_encoder_field: canonical token, unknown values untouched") {
    CHECK(normalize_hardware_encoder_field("NVENC") == "nvenc");
    CHECK(normalize_hardware_encoder_field("Nvenc") == "nvenc");
    CHECK(normalize_hardware_encoder_field("nvenc") == "nvenc");
    // Unrecognized backends keep their spelling so the encoder resolution
    // still produces its own explicit error instead of a silent rewrite.
    CHECK(normalize_hardware_encoder_field("qsv") == "qsv");
    CHECK(normalize_hardware_encoder_field("none") == "none");
    CHECK(normalize_hardware_encoder_field("") == "");
}

// ═══════════════════════════════════════════════════════════════════════════
// Extension vocabulary containment (AUDIT P1-3)
//
// Two vocabularies, two declared owners, one legal relationship: everything the
// muxer can PUBLISH must also be INGESTIBLE as a source. Before unification four
// literal tables answered this question differently (`.m4v` and `.webm` flipped
// answer depending on which layer asked), so the containment was neither true
// nor stated.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("every publishable container is also an ingestible video source") {
    using chronon3d::assets::PreparedAssetKind;
    for (const char* token : {".mp4", ".MP4", ".mkv", ".webm", "mp4", "WebM"}) {
        const auto container = chronon3d::media::video::classify_video_container(token);
        REQUIRE(container.has_value());
        CHECK(chronon3d::assets::asset_kind_accepts_extension(
            PreparedAssetKind::Video, token));
    }
    // The ingest side is deliberately wider: a source may be decodable even when
    // the muxer cannot publish it.
    CHECK_FALSE(chronon3d::media::video::classify_video_container(".mov").has_value());
    CHECK(chronon3d::assets::asset_kind_accepts_extension(
        PreparedAssetKind::Video, ".mov"));
    CHECK(chronon3d::assets::asset_kind_accepts_extension(
        PreparedAssetKind::Video, ".m4v"));
    CHECK(chronon3d::assets::asset_kind_accepts_extension(
        PreparedAssetKind::Video, ".M4V"));
    CHECK_FALSE(chronon3d::assets::asset_kind_accepts_extension(
        PreparedAssetKind::Video, ".png"));
    CHECK_FALSE(chronon3d::media::video::classify_video_container(".png").has_value());
}

TEST_CASE("the job output-path vocabulary is the container vocabulary plus .mov") {
    CHECK(chronon3d::media::video::is_supported_video_output_extension(".mp4"));
    CHECK(chronon3d::media::video::is_supported_video_output_extension(".MOV"));
    CHECK(chronon3d::media::video::is_supported_video_output_extension(".mkv"));
    CHECK(chronon3d::media::video::is_supported_video_output_extension(".webm"));
    CHECK_FALSE(chronon3d::media::video::is_supported_video_output_extension(".png"));
    CHECK_FALSE(chronon3d::media::video::is_supported_video_output_extension(""));
}

TEST_CASE("the CLI classifier is a projection of the media-layer authority") {
    for (const char* sample : {"nvenc", "NVENC", "Nvenc", "none", "qsv", ""}) {
        CHECK(hardware_nvenc_requested(sample) ==
              chronon3d::media::hardware_encoder_requests_nvenc(sample));
        CHECK(normalize_hardware_encoder_field(sample) ==
              chronon3d::media::normalize_hardware_encoder_field(sample));
    }
    // And the token itself has exactly one spelling.
    CHECK(std::string{chronon3d::media::kNvencHardwareEncoder} == "nvenc");
}
