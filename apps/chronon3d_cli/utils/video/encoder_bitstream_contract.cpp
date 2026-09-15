#include "encoder_bitstream_contract.hpp"

#include <algorithm>
#include <utility>

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

DecoderFacts probe_decoder_facts(const AVCodecContext& encoder) noexcept {
    DecoderFacts facts;
    if (encoder.codec_id == AV_CODEC_ID_NONE) return facts;
    const AVCodec* decoder = avcodec_find_decoder(encoder.codec_id);
    if (!decoder) return facts;

    AVCodecParameters* params = avcodec_parameters_alloc();
    if (!params) return facts;
    if (avcodec_parameters_from_context(params, &encoder) < 0) {
        avcodec_parameters_free(&params);
        return facts;
    }
    AVCodecContext* context = avcodec_alloc_context3(decoder);
    if (!context || avcodec_parameters_to_context(context, params) < 0) {
        avcodec_free_context(&context);
        avcodec_parameters_free(&params);
        return facts;
    }
    avcodec_parameters_free(&params);

    // Opening the decoder parses the extradata (SPS/PPS) and fills the codec
    // facts. No frame is decoded.
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

    // Materialized == the splice gate has every fact it needs. A missing
    // profile/level/format or empty extradata keeps the contract fail-closed.
    target.encoder_materialized =
        target.width > 0 && target.height > 0 &&
        target.profile >= 0 && target.level >= 0 &&
        static_cast<int>(target.pixel_format) >= 0 &&
        !target.parameter_sets.empty();
    return target;
}
#endif

std::optional<BitstreamTargetContract> resolve_encoder_bitstream_contract(
    const FfmpegPipeOptions& options) noexcept {
    auto resolution = resolve_encoder_config(make_encoder_config_request(options));
    if (!resolution) return std::nullopt;
    return make_encoder_bitstream_contract(options, resolution.value());
}

} // namespace chronon3d::cli
