#include "encoder_bitstream_contract.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

#include <spdlog/spdlog.h>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
extern "C" {
#include <libavutil/pixdesc.h>
}
#endif

namespace chronon3d::cli {

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
BitstreamCodecId bitstream_codec_for_encoder_name(
    std::string_view encoder_name) noexcept {
    if (encoder_name == "libx264" || encoder_name == "libx264rgb" ||
        encoder_name == "h264_nvenc") {
        return AV_CODEC_ID_H264;
    }
    if (encoder_name == "libx265" || encoder_name == "hevc_nvenc") {
        return AV_CODEC_ID_HEVC;
    }
    if (encoder_name == "av1_nvenc" || encoder_name == "libaom-av1" ||
        encoder_name == "libsvtav1") {
        return AV_CODEC_ID_AV1;
    }
    return AV_CODEC_ID_NONE;
}

EncoderColorContract encoder_color_contract(
    color::ColorSpace output_space) noexcept {
    const bool linear =
        output_space == color::ColorSpace::LinearSRGB ||
        output_space == color::ColorSpace::ACEScg;
    return EncoderColorContract{
        .color_range = AVCOL_RANGE_MPEG,
        .color_space = AVCOL_SPC_BT709,
        .color_primaries = AVCOL_PRI_BT709,
        .color_trc = linear ? AVCOL_TRC_LINEAR : AVCOL_TRC_BT709,
    };
}

namespace {

/// Decoder-visible codec facts parsed by libavcodec out of the encoder's own
/// extradata. This is the only place the target's profile/level/pixel format
/// can come from: they are properties of the produced bitstream, not of the
/// configuration that requested it.
struct DecoderFacts {
    int profile{-1};
    int level{-1};
    int pixel_format{-1};
    bool valid{false};
};

/// Decoder-visible facts for one codec configuration. `encoder` supplies the
/// stream's non-extradata facts (dimensions, format) when the caller has a
/// codec context; `extradata` is the configuration to parse — a demuxer's
/// avcC/hvcC record or an encoder's in-band Annex-B parameter sets, both of
/// which libavcodec's decoder accepts.
DecoderFacts probe_decoder_facts_for(
    AVCodecID codec_id, const std::uint8_t* extradata, int extradata_size,
    const AVCodecContext* encoder) noexcept {
    DecoderFacts facts;
    if (codec_id == AV_CODEC_ID_NONE) return facts;
    const AVCodec* decoder = avcodec_find_decoder(codec_id);
    if (!decoder) return facts;

    AVCodecParameters* params = avcodec_parameters_alloc();
    if (!params) return facts;
    if (encoder) {
        if (avcodec_parameters_from_context(params, encoder) < 0) {
            avcodec_parameters_free(&params);
            return facts;
        }
    } else {
        params->codec_type = AVMEDIA_TYPE_VIDEO;
        params->codec_id = codec_id;
    }
    if (extradata && extradata_size > 0) {
        params->extradata = static_cast<std::uint8_t*>(av_mallocz(
            static_cast<std::size_t>(extradata_size) +
            AV_INPUT_BUFFER_PADDING_SIZE));
        if (!params->extradata) {
            avcodec_parameters_free(&params);
            return facts;
        }
        std::memcpy(params->extradata, extradata,
                    static_cast<std::size_t>(extradata_size));
        params->extradata_size = extradata_size;
    }
    AVCodecContext* context = avcodec_alloc_context3(decoder);
    if (!context || avcodec_parameters_to_context(context, params) < 0) {
        avcodec_free_context(&context);
        avcodec_parameters_free(&params);
        return facts;
    }
    avcodec_parameters_free(&params);

    // Opening the decoder parses the parameter sets and fills the codec facts.
    // No frame is decoded.
    if (avcodec_open2(context, decoder, nullptr) < 0) {
        avcodec_free_context(&context);
        return facts;
    }
    facts.profile = context->profile;
    facts.level = context->level;
    facts.pixel_format = context->pix_fmt;
    facts.valid = true;
    avcodec_free_context(&context);
    return facts;
}

DecoderFacts probe_decoder_facts(const AVCodecContext& encoder) noexcept {
    return probe_decoder_facts_for(encoder.codec_id, encoder.extradata,
                                   encoder.extradata_size, &encoder);
}

/// The parameter sets libavcodec attaches to the first packet of an encoder
/// that keeps them in-band. Empty when the packet carries none.
std::vector<std::uint8_t> packet_new_extradata(const AVPacket& packet) {
    for (int index = 0; index < packet.side_data_elems; ++index) {
        const auto& side = packet.side_data[index];
        if (side.type != AV_PKT_DATA_NEW_EXTRADATA || !side.data ||
            side.size <= 0) {
            continue;
        }
        return std::vector<std::uint8_t>(side.data, side.data + side.size);
    }
    return {};
}

} // namespace
#endif

BitstreamTargetContract make_encoder_bitstream_contract(
    const FfmpegPipeOptions& options,
    const ResolvedEncoderConfig& resolved) noexcept {
    BitstreamTargetContract target;
    target.producer = BitstreamTargetProducer::Encoder;
    target.encoder_materialized = false;
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    target.codec = bitstream_codec_for_encoder_name(resolved.encoder_name());
    target.width = static_cast<std::uint32_t>(std::max(options.width, 0));
    target.height = static_cast<std::uint32_t>(std::max(options.height, 0));
    const auto color = encoder_color_contract(options.color_transform.output);
    target.color_range = color.color_range;
    target.color_space = color.color_space;
    target.color_primaries = color.color_primaries;
    target.color_trc = color.color_trc;
    // The encoder's OUTPUT facts are unknown until it runs. They stay
    // explicitly unset so the splice gate fails closed instead of assuming
    // parity with the source.
    target.profile = -1;
    target.level = -1;
    target.pixel_format = static_cast<BitstreamPixelFormat>(-1);
    target.parameter_sets.clear();
#else
    (void)options;
    (void)resolved;
#endif
    return target;
}

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
BitstreamTargetContract encoder_bitstream_contract_from_codec(
    const AVCodecContext& codec,
    const FfmpegPipeOptions& options,
    const ResolvedEncoderConfig& resolved) noexcept {
    BitstreamTargetContract target =
        make_encoder_bitstream_contract(options, resolved);
    if (codec.codec_id != AV_CODEC_ID_NONE) target.codec = codec.codec_id;
    if (codec.width > 0) target.width = static_cast<std::uint32_t>(codec.width);
    if (codec.height > 0) target.height = static_cast<std::uint32_t>(codec.height);
    if (codec.extradata && codec.extradata_size > 0) {
        target.parameter_sets.assign(
            codec.extradata, codec.extradata + codec.extradata_size);
    }
    target.color_range = codec.color_range;
    target.color_space = codec.colorspace;
    target.color_primaries = codec.color_primaries;
    target.color_trc = codec.color_trc;

    // Decoder-visible facts, parsed from the encoder's own extradata.
    const DecoderFacts facts = probe_decoder_facts(codec);
    if (facts.valid) {
        target.profile = facts.profile;
        target.level = facts.level;
        if (facts.pixel_format >= 0) {
            target.pixel_format =
                static_cast<BitstreamPixelFormat>(facts.pixel_format);
        }
    } else {
        // No decoder for this codec: the encoder context's own values are the
        // only remaining source. A hardware pixel format (CUDA surfaces) is
        // NOT the format a demuxer reports, so it never counts as materialized
        // — the hw→sw mapping must be resolved against a real run.
        if (codec.profile >= 0) target.profile = codec.profile;
        if (codec.level >= 0) target.level = codec.level;
        if (codec.pix_fmt >= 0) {
            const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(codec.pix_fmt);
            const bool hardware = desc && (desc->flags & AV_PIX_FMT_FLAG_HWACCEL);
            if (!hardware) {
                target.pixel_format =
                    static_cast<BitstreamPixelFormat>(codec.pix_fmt);
            }
        }
    }

    // Materialized == the splice gate can state the stream a decoder will
    // read out of this context. The rule is the SAME one the produced-packet
    // path applies, because both describe the same thing: a codec, its
    // dimensions and its configuration record. libavcodec's decoders do NOT
    // publish profile/level/pixel format from parameter sets alone (those are
    // per-slice facts), so demanding them here would make the two producers of
    // this contract disagree about a well-defined stream.
    target.encoder_materialized =
        target.width > 0 && target.height > 0 &&
        target.codec != static_cast<BitstreamCodecId>(0) &&
        !target.parameter_sets.empty();
    return target;
}
#endif

std::optional<BitstreamTargetContract> encoder_bitstream_contract_from_packet(
    const AVPacket& packet, const AVCodecContext& codec,
    const FfmpegPipeOptions& options,
    const ResolvedEncoderConfig& resolved) noexcept {
    // The codec is the one the RESOLVED configuration names: it is the same
    // fact make_encoder_bitstream_contract() records, and it is available even
    // when the opened context reports no codec id of its own.
    const BitstreamCodecId resolved_codec_id =
        bitstream_codec_for_encoder_name(resolved.encoder_name());
    std::vector<std::uint8_t> extradata = packet_new_extradata(packet);
    if (extradata.empty()) {
        // No side data: encoders opened WITHOUT a global header (libx264 here)
        // carry their parameter sets at the START of the keyframe payload as
        // Annex-B NALs. The prefix scan stops at the first NAL that is not a
        // parameter set, so slice data is never interpreted; a packet that
        // begins with one publishes nothing (not an assertion about the packet
        // being flagged as a keyframe, which NOT all encoders set).
        const BitstreamCodecId prefix_codec =
            codec.codec_id != AV_CODEC_ID_NONE ? codec.codec_id
                                               : resolved_codec_id;
        const std::size_t prefix = leading_parameter_set_prefix_size(
            prefix_codec, packet.data,
            packet.size > 0 ? static_cast<std::size_t>(packet.size) : 0u);
        if (prefix > 0) {
            extradata.assign(packet.data, packet.data + prefix);
        }
    }
    if (extradata.empty()) return std::nullopt;

    BitstreamTargetContract target =
        make_encoder_bitstream_contract(options, resolved);
    if (codec.codec_id != AV_CODEC_ID_NONE) target.codec = codec.codec_id;
    if (codec.width > 0) target.width = static_cast<std::uint32_t>(codec.width);
    if (codec.height > 0) target.height = static_cast<std::uint32_t>(codec.height);
    // The colour contract is applied by the encoder itself, so the context is
    // the authority for it; the parameter sets and the facts they describe
    // come from the produced stream.
    target.color_range = codec.color_range;
    target.color_space = codec.colorspace;
    target.color_primaries = codec.color_primaries;
    target.color_trc = codec.color_trc;
    target.parameter_sets = extradata;

    const auto facts = probe_decoder_facts_for(
        target.codec, extradata.data(), static_cast<int>(extradata.size()),
        nullptr);
    if (facts.valid) {
        target.profile = facts.profile;
        target.level = facts.level;
        if (facts.pixel_format >= 0) {
            target.pixel_format =
                static_cast<BitstreamPixelFormat>(facts.pixel_format);
        }
    } else {
        spdlog::warn(
            "[native_av] {} in-band extradata bytes could not be parsed by a "
            "decoder; the encoder contract stays fail-closed",
            extradata.size());
    }
    // Materialized means: this contract can state the stream a decoder will
    // read. libavcodec's decoders do NOT publish profile/level/pixel format
    // from parameter sets alone (those are per-slice facts), so the facts the
    // produced stream can be held to are the codec, the dimensions, the colour
    // contract the encoder applied, and the parameter sets themselves — which
    // encode profile and level byte for byte. The reproduction gate compares
    // exactly those and tolerates the unknown scalars.
    target.encoder_materialized =
        target.width > 0 && target.height > 0 &&
        target.codec != static_cast<BitstreamCodecId>(0) &&
        !target.parameter_sets.empty();
    if (!target.encoder_materialized) return std::nullopt;
    return target;
}

std::optional<BitstreamTargetContract> resolve_encoder_bitstream_contract(
    const FfmpegPipeOptions& options) noexcept {
    auto resolution = resolve_encoder_config(make_encoder_config_request(options));
    if (!resolution) return std::nullopt;
    return make_encoder_bitstream_contract(options, resolution.value());
}

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
bool encoder_reproduces_declared_stream(
    const BitstreamTargetContract& declared,
    const BitstreamTargetContract& encoder_contract) noexcept {
    // The declaration is only meaningful when it names a concrete stream the
    // container was opened on. An incomplete declaration can never certify.
    if (declared.width == 0 || declared.height == 0 ||
        declared.codec == static_cast<BitstreamCodecId>(0) ||
        declared.parameter_sets.empty()) {
        return false;
    }
    if (!encoder_contract.encoder_materialized) return false;

    // ── Facts BOTH sides must state, and must agree on ────────────────────
    if (encoder_contract.codec != declared.codec) return false;
    if (encoder_contract.width != declared.width ||
        encoder_contract.height != declared.height) {
        return false;
    }
    const auto colour_agrees = [](const BitstreamTargetContract& encoder,
                                  const BitstreamTargetContract& source) {
        // AVCOL_*_UNSPECIFIED means "this side did not state it" (the encoder
        // publishes the colour contract it applied; a source may legitimately
        // leave it unspecified), which is not a contradiction.
        if (encoder.color_range != AVCOL_RANGE_UNSPECIFIED &&
            encoder.color_range != source.color_range) return false;
        if (encoder.color_space != AVCOL_SPC_UNSPECIFIED &&
            encoder.color_space != source.color_space) return false;
        if (encoder.color_primaries != AVCOL_PRI_UNSPECIFIED &&
            encoder.color_primaries != source.color_primaries) return false;
        if (encoder.color_trc != AVCOL_TRC_UNSPECIFIED &&
            encoder.color_trc != source.color_trc) return false;
        return true;
    };
    if (!colour_agrees(encoder_contract, declared)) return false;

    // ── The stream's own identity: the parameter sets ─────────────────────
    // A demuxer reports an avcC/hvcC record while the encoder publishes its
    // parameter sets in-band as Annex-B, so comparing the two WRAPPERS would
    // reject a stream that was reproduced exactly. Comparing the NAL payloads
    // is the real question — and they carry profile and level byte for byte,
    // which is what makes the scalar comparison below redundant rather than
    // lenient.
    const auto declared_nals = parameter_set_nals(
        declared.codec, declared.parameter_sets.data(),
        declared.parameter_sets.size());
    const auto encoder_nals = parameter_set_nals(
        encoder_contract.codec, encoder_contract.parameter_sets.data(),
        encoder_contract.parameter_sets.size());
    if (declared_nals.empty() || declared_nals != encoder_nals) return false;

    // Identical codec + dimensions + colour + parameter sets: the encoder
    // produced the declared stream. The remaining scalars are stated through
    // the ONE comparison authority, which needs them in one representation:
    // libavcodec cannot read profile/level/pixel format out of parameter sets
    // alone, so whichever side states one states it for BOTH sides, and a
    // scalar NEITHER side states (a demuxer reports profile/level as-is and an
    // encoder context may leave them at "unknown") cannot be a contradiction —
    // it is presented as equal rather than being invented. Nothing about the
    // stream is derived from that placeholder: the identity proof above is the
    // parameter sets themselves.
    const auto stated = [](int encoder_value, int declared_value) {
        if (declared_value >= 0) return declared_value;
        if (encoder_value >= 0) return encoder_value;
        return 0;
    };
    const int profile = stated(encoder_contract.profile, declared.profile);
    const int level = stated(encoder_contract.level, declared.level);
    const int pixel_format = stated(
        static_cast<int>(encoder_contract.pixel_format),
        static_cast<int>(declared.pixel_format));
    const auto stated_color = [](auto encoder_value, auto declared_value,
                                 auto unspecified) {
        if (encoder_value != unspecified) return encoder_value;
        if (declared_value != unspecified) return declared_value;
        return encoder_value;
    };
    const auto color_range = stated_color(
        encoder_contract.color_range, declared.color_range,
        AVCOL_RANGE_UNSPECIFIED);
    const auto color_space = stated_color(
        encoder_contract.color_space, declared.color_space,
        AVCOL_SPC_UNSPECIFIED);
    const auto color_primaries = stated_color(
        encoder_contract.color_primaries, declared.color_primaries,
        AVCOL_PRI_UNSPECIFIED);
    const auto color_trc = stated_color(
        encoder_contract.color_trc, declared.color_trc, AVCOL_TRC_UNSPECIFIED);

    BitstreamTargetContract comparable = encoder_contract;
    comparable.profile = profile;
    comparable.level = level;
    comparable.pixel_format = static_cast<BitstreamPixelFormat>(pixel_format);
    comparable.parameter_sets = declared.parameter_sets;
    comparable.color_range = color_range;
    comparable.color_space = color_space;
    comparable.color_primaries = color_primaries;
    comparable.color_trc = color_trc;

    AVCodecParameters params{};
    params.codec_id = declared.codec;
    params.codec_type = AVMEDIA_TYPE_VIDEO;
    params.profile = profile;
    params.level = level;
    params.format = pixel_format;
    params.width = static_cast<int>(declared.width);
    params.height = static_cast<int>(declared.height);
    params.extradata = const_cast<std::uint8_t*>(declared.parameter_sets.data());
    params.extradata_size = static_cast<int>(declared.parameter_sets.size());
    params.color_range = color_range;
    params.color_space = color_space;
    params.color_primaries = color_primaries;
    params.color_trc = color_trc;
    return compare_bitstream_compatibility(params, comparable,
                                           /*random_access_safe=*/true)
        .safe_to_splice();
}
#endif

} // namespace chronon3d::cli
