// =============================================================================
// test_bitstream_target_contract.cpp — A7 contract tests for the ENCODER-OWNED
// bitstream target contract (TICKET-HOTSPOT-DECOMPOSITION, Wave A).
//
// The splice gate compares a SOURCE bitstream against the stream the OUTPUT
// container will carry. When a re-encode participates in that output the only
// legal producer of the target is the ENCODER. These tests pin the three
// outcomes the ticket asks for:
//
//   success          the encoder reproduced the declared stream → splice safe
//   incompatibility  codec / dimensions / parameter sets / colour differ → reject
//   forbidden        a CONFIGURATION-ONLY contract, an incomplete declaration or
//                    a non-materialized encoder can never certify, and a
//                    re-encode target is never derived from the source
//                    (`source vs source` can never come back)
//
// The cases are hermetic: the parameter sets are a hand-built but well-formed
// AVC configuration record, so the real parser (`parameter_set_nals`) and the
// real comparator (`compare_bitstream_compatibility`) run without an encoder,
// a GPU or any file I/O.
// =============================================================================

#include <doctest/doctest.h>

#include "utils/video/bitstream_target_contract.hpp"
#include "utils/video/encoder_config_resolution.hpp"
#include "utils/video/ffmpeg_pipe_encoder.hpp"

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
#include "utils/video/encoder_bitstream_contract.hpp"
#include "utils/video/gop_smart_copy.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
}
#endif

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using namespace chronon3d::cli;

namespace {

/// A software H.264 pipe request: resolvable on every build and never NVENC, so
/// these tests need no GPU.
[[nodiscard]] FfmpegPipeOptions software_h264_options() {
    FfmpegPipeOptions options;
    options.width = 1920;
    options.height = 1080;
    options.fps_num = 24;
    options.fps_den = 1;
    options.codec = "h264";
    options.hardware_encoder = "none";
    options.rate_control_mode = "crf";
    options.rate_control_mode_explicit = false;
    options.crf = 18;
    options.crf_explicit = false;
    options.output_path = "contract-test.mp4";
    return options;
}

/// Declared-but-unknown contract fields. A contract built from configuration
/// alone must be honest about everything only the encoder's bitstream can
/// state: profile, level and pixel format stay unset, and the parameter sets
/// stay empty so the gate fails closed.
void check_undeclared_output_facts(const BitstreamTargetContract& target) {
    CHECK(target.encoder_materialized == false);
    CHECK(target.profile == -1);
    CHECK(target.level == -1);
    CHECK(static_cast<int>(target.pixel_format) == -1);
    CHECK(target.parameter_sets.empty());
}

} // namespace

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG

namespace {

void append_length_prefixed_16(std::vector<std::uint8_t>& out,
                               const std::vector<std::uint8_t>& nal) {
    out.push_back(static_cast<std::uint8_t>((nal.size() >> 8) & 0xffU));
    out.push_back(static_cast<std::uint8_t>(nal.size() & 0xffU));
    out.insert(out.end(), nal.begin(), nal.end());
}

/// AVCDecoderConfigurationRecord (avcC) with one SPS and one PPS, each
/// preceded by a 16-bit length — the shape a DEMUXER reports, and the shape
/// `parameter_set_nals` parses. Hand-built on purpose: the contract test must
/// not depend on libx264 being present or on an encode actually running.
[[nodiscard]] std::vector<std::uint8_t> avcC_record(
    const std::vector<std::uint8_t>& sps,
    const std::vector<std::uint8_t>& pps) {
    std::vector<std::uint8_t> record;
    record.push_back(1);    // configurationVersion
    record.push_back(100);  // AVCProfileIndication (High)
    record.push_back(0);    // profile_compatibility
    record.push_back(40);   // AVCLevelIndication (4.0)
    record.push_back(0xFF); // reserved(6 bits) + lengthSizeMinusOne = 3
    record.push_back(0xE1); // reserved(3 bits) + numOfSequenceParameterSets = 1
    append_length_prefixed_16(record, sps);
    record.push_back(1); // numOfPictureParameterSets
    append_length_prefixed_16(record, pps);
    return record;
}

/// Two distinct, well-formed parameter-set pairs. The bytes are not a real
/// encoder output (nothing is decoded), but the NAL header bytes are correct,
/// so the parser sees exactly the structure a production stream produces.
const std::vector<std::uint8_t>& sps_a() {
    static const std::vector<std::uint8_t> value{
        0x67, 0x64, 0x00, 0x28, 0xAC, 0xD9, 0x40, 0x50, 0x05, 0xBA, 0x10};
    return value;
}
const std::vector<std::uint8_t>& pps_a() {
    static const std::vector<std::uint8_t> value{0x68, 0xEB, 0xE3, 0xCB,
                                                 0x22, 0xC0};
    return value;
}
const std::vector<std::uint8_t>& sps_b() {
    static const std::vector<std::uint8_t> value{
        0x67, 0x42, 0x00, 0x1E, 0xAB, 0x40, 0xB0, 0x11, 0x88};
    return value;
}
const std::vector<std::uint8_t>& pps_b() {
    static const std::vector<std::uint8_t> value{0x68, 0xCE, 0x3C, 0x80};
    return value;
}

/// The stream a borrowed container was declared from: what a demuxer will hand
/// the playback decoder at the splice.
[[nodiscard]] BitstreamTargetContract declared_source_contract() {
    BitstreamTargetContract declared;
    declared.producer = BitstreamTargetProducer::Remux;
    declared.encoder_materialized = false;
    declared.codec = AV_CODEC_ID_H264;
    declared.profile = 100;
    declared.level = 40;
    declared.width = 1920;
    declared.height = 1080;
    declared.pixel_format = AV_PIX_FMT_YUV420P;
    declared.parameter_sets = avcC_record(sps_a(), pps_a());
    declared.color_range = AVCOL_RANGE_MPEG;
    declared.color_space = AVCOL_SPC_BT709;
    declared.color_primaries = AVCOL_PRI_BT709;
    declared.color_trc = AVCOL_TRC_BT709;
    return declared;
}

/// The encoder's own output when it reproduced that stream exactly.
[[nodiscard]] BitstreamTargetContract encoder_contract_reproducing(
    const BitstreamTargetContract& declared) {
    BitstreamTargetContract encoder = declared;
    encoder.producer = BitstreamTargetProducer::Encoder;
    encoder.encoder_materialized = true;
    return encoder;
}

} // namespace

// ── success ────────────────────────────────────────────────────────────────

TEST_CASE("A7 bitstream target: encoder output that reproduces the declared stream certifies") {
    const auto declared = declared_source_contract();
    const auto encoder = encoder_contract_reproducing(declared);

    CHECK(encoder_reproduces_declared_stream(declared, encoder));
}

TEST_CASE("A7 bitstream target: the declared and encoder sides may wrap the same parameter sets differently") {
    // A demuxer reports avcC while an encoder publishes Annex-B in-band. The
    // gate compares the NAL payloads, so the same stream described in the two
    // real-world wrappers must still certify.
    auto encoder = encoder_contract_reproducing(declared_source_contract());
    std::vector<std::uint8_t> annex_b;
    for (const auto& nal : {sps_a(), pps_a()}) {
        annex_b.push_back(0x00);
        annex_b.push_back(0x00);
        annex_b.push_back(0x00);
        annex_b.push_back(0x01);
        annex_b.insert(annex_b.end(), nal.begin(), nal.end());
    }
    CHECK(parameter_set_nals(encoder.codec, annex_b.data(), annex_b.size()) ==
          parameter_set_nals(encoder.codec,
                             encoder.parameter_sets.data(),
                             encoder.parameter_sets.size()));
    encoder.parameter_sets = std::move(annex_b);

    CHECK(encoder_reproduces_declared_stream(declared_source_contract(), encoder));
}

// ── incompatibility ────────────────────────────────────────────────────────

TEST_CASE("A7 bitstream target: a different codec never certifies") {
    auto encoder = encoder_contract_reproducing(declared_source_contract());
    encoder.codec = AV_CODEC_ID_HEVC;

    CHECK_FALSE(encoder_reproduces_declared_stream(declared_source_contract(), encoder));
}

TEST_CASE("A7 bitstream target: different dimensions never certify") {
    const auto declared = declared_source_contract();
    auto encoder = encoder_contract_reproducing(declared);
    encoder.width = 1280;

    CHECK_FALSE(encoder_reproduces_declared_stream(declared, encoder));

    auto narrower = encoder_contract_reproducing(declared);
    narrower.height = 720;
    CHECK_FALSE(encoder_reproduces_declared_stream(declared, narrower));
}

TEST_CASE("A7 bitstream target: different parameter sets never certify") {
    const auto declared = declared_source_contract();
    auto encoder = encoder_contract_reproducing(declared);
    encoder.parameter_sets = avcC_record(sps_b(), pps_b());

    CHECK_FALSE(encoder_reproduces_declared_stream(declared, encoder));
    // A stream with the same SPS but a different PPS is a different stream:
    // the comparison is over the whole parameter-set list, not the first NAL.
    auto other_pps = encoder_contract_reproducing(declared);
    other_pps.parameter_sets = avcC_record(sps_a(), pps_b());
    CHECK_FALSE(encoder_reproduces_declared_stream(declared, other_pps));
}

TEST_CASE("A7 bitstream target: a colour contract the encoder applied but the source contradicts is rejected") {
    const auto declared = declared_source_contract();
    auto encoder = encoder_contract_reproducing(declared);
    // The encoder publishes the colour it applied; stating a different space
    // than the declared stream is a real contradiction.
    encoder.color_space = AVCOL_SPC_BT470BG;

    CHECK_FALSE(encoder_reproduces_declared_stream(declared, encoder));
}

// ── forbidden fallbacks ────────────────────────────────────────────────────

TEST_CASE("A7 bitstream target: a configuration-only contract can never certify") {
    const auto declared = declared_source_contract();
    auto config_only = encoder_contract_reproducing(declared);
    config_only.encoder_materialized = false;

    CHECK_FALSE(encoder_reproduces_declared_stream(declared, config_only));
}

TEST_CASE("A7 bitstream target: an incomplete declaration can never certify") {
    auto no_parameter_sets = declared_source_contract();
    no_parameter_sets.parameter_sets.clear();
    CHECK_FALSE(encoder_reproduces_declared_stream(
        no_parameter_sets, encoder_contract_reproducing(no_parameter_sets)));

    auto no_dimensions = declared_source_contract();
    no_dimensions.width = 0;
    CHECK_FALSE(encoder_reproduces_declared_stream(
        no_dimensions, encoder_contract_reproducing(no_dimensions)));

    auto no_codec = declared_source_contract();
    no_codec.codec = static_cast<BitstreamCodecId>(0);
    CHECK_FALSE(encoder_reproduces_declared_stream(
        no_codec, encoder_contract_reproducing(no_codec)));
}

TEST_CASE("A7 bitstream target: an encoder that published nothing never certifies") {
    // The encoder never produced its parameter sets (no packet carried them and
    // the codec context stayed empty): there is no proof the splice is
    // followable, so the gate stays closed.
    auto encoder = encoder_contract_reproducing(declared_source_contract());
    encoder.encoder_materialized = false;
    encoder.parameter_sets.clear();

    CHECK_FALSE(encoder_reproduces_declared_stream(declared_source_contract(), encoder));
}

#endif // CHRONON3D_ENABLE_NATIVE_FFMPEG

// ── the declared target: configuration facts only, never splice-certifiable ──

TEST_CASE("A7 bitstream target: a contract built from configuration alone is never materialized") {
    const auto options = software_h264_options();
    const auto resolution =
        resolve_encoder_config(make_encoder_config_request(options));
    REQUIRE(resolution.has_value());

    const auto target =
        make_encoder_bitstream_contract(options, resolution.value());

    // Producer identity: a re-encode target is the ENCODER's, never the
    // source's. This is the invariant that stops the gate from degenerating
    // into `source vs source`.
    CHECK(target.producer == BitstreamTargetProducer::Encoder);
    // Declaration-time facts ARE known, so they stay visible to diagnostics.
    CHECK(target.width == 1920u);
    CHECK(target.height == 1080u);
    // The encoder's OUTPUT facts are not, and must stay unset.
    check_undeclared_output_facts(target);
}

TEST_CASE("A7 bitstream target: resolving the target contract never falls back to the source") {
    // `resolve_encoder_bitstream_contract` is the configuration-only entry
    // point of the dispatch: it resolves what the encoder WILL be asked to
    // produce. A resolution that succeeds must still describe the encoder's
    // output (an unmaterialized encoder contract), never a source stream — and
    // an unresolvable configuration yields no target at all, because there is
    // nothing legal to copy.
    const auto options = software_h264_options();
    const auto resolved = resolve_encoder_bitstream_contract(options);
    REQUIRE(resolved.has_value());
    CHECK(resolved->producer == BitstreamTargetProducer::Encoder);
    CHECK(resolved->width == 1920u);
    CHECK(resolved->height == 1080u);
    check_undeclared_output_facts(*resolved);

    // An unresolvable configuration must produce NO target rather than a
    // permissive one.
    auto impossible = software_h264_options();
    impossible.rate_control_mode = "not-a-rate-control";
    impossible.rate_control_mode_explicit = true;
    CHECK_FALSE(resolve_encoder_bitstream_contract(impossible).has_value());
}
