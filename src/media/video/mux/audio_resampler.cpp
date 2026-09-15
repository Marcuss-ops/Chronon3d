#include <chronon3d/media/video/audio_resampler.hpp>

#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/version.h>
#include <libswresample/swresample.h>
}

namespace chronon3d::media {
namespace {

/// The canonical assembly-ready audio bitrate. It is the value the
/// VeloxEditing output contract's audio block declares
/// (kernel/media/assembly_contract.go: AudioBitrate "128k"), so the resample
/// stage must target it rather than the SOURCE's bitrate: inheriting a 194
/// kbit/s source rate produced "audio bitrate 194417 != 128k" at the contract
/// gate. The gate tolerates +/-25%, which a 128 kbit/s AAC encode satisfies.
constexpr std::int64_t kCanonicalAudioBitrate = 128000;

/// Fallback encoder frame size when the encoder does not declare one. 1024 is
/// the AAC long-window frame size.
constexpr int kDefaultFrameSize = 1024;

int channel_count(const AVCodecParameters* params) {
#if LIBAVUTIL_VERSION_MAJOR >= 57
    return params->ch_layout.nb_channels;
#else
    return params->channels;
#endif
}

std::uint64_t channel_layout(const AVCodecParameters* params) {
#if LIBAVUTIL_VERSION_MAJOR >= 57
    return params->ch_layout.order == AV_CHANNEL_ORDER_NATIVE
        ? params->ch_layout.u.mask : 0;
#else
    return params->channel_layout;
#endif
}

std::uint64_t default_layout(int channels) {
    return channels > 0 ? av_get_default_channel_layout(channels) : 0;
}

/// Frees a frame allocated inside a noexcept path.
void free_frame(AVFrame*& frame) noexcept {
    if (frame) av_frame_free(&frame);
}

} // namespace

AudioResampler::~AudioResampler() { release(); }

void AudioResampler::release() noexcept {
    if (fifo_) av_audio_fifo_free(fifo_);
    fifo_ = nullptr;
    swr_free(&swr_);
    if (decoder_) avcodec_free_context(&decoder_);
    if (encoder_) avcodec_free_context(&encoder_);
    if (output_params_) avcodec_parameters_free(&output_params_);
    active_ = false;
}

bool AudioResampler::open(const AVCodecParameters* source,
                          int target_sample_rate,
                          int target_bitrate,
                          std::string& reason) {
    release();
    if (!source || source->codec_type != AVMEDIA_TYPE_AUDIO) {
        reason = "source is not an audio stream";
        return false;
    }
    if (target_sample_rate <= 0) {
        reason = "target sample rate must be positive";
        return false;
    }
    const int source_channels = channel_count(source);
    if (source_channels <= 0) {
        reason = "source audio has no channel count";
        return false;
    }
    const auto source_layout = channel_layout(source) != 0
        ? channel_layout(source) : default_layout(source_channels);
    if (source->sample_rate <= 0) {
        reason = "source audio has no sample rate";
        return false;
    }

    const AVCodec* decoder_codec = avcodec_find_decoder(source->codec_id);
    if (!decoder_codec) {
        reason = "no decoder for the source audio codec";
        return false;
    }
    decoder_ = avcodec_alloc_context3(decoder_codec);
    if (!decoder_ || avcodec_parameters_to_context(decoder_, source) < 0 ||
        avcodec_open2(decoder_, decoder_codec, nullptr) < 0) {
        reason = "failed to open the source audio decoder";
        return false;
    }

    const AVCodec* encoder_codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!encoder_codec) {
        reason = "no AAC encoder is available";
        return false;
    }
    encoder_ = avcodec_alloc_context3(encoder_codec);
    if (!encoder_) {
        reason = "failed to allocate the AAC encoder context";
        return false;
    }
    encoder_->sample_rate = target_sample_rate;
    encoder_->sample_fmt = encoder_codec->sample_fmts
        ? encoder_codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
    encoder_->channels = source_channels;
    encoder_->channel_layout = source_layout;
    encoder_->bit_rate = target_bitrate > 0 ? target_bitrate : kCanonicalAudioBitrate;
    encoder_->time_base = AVRational{1, target_sample_rate};
    // MP4 must carry the AudioSpecificConfig in extradata, which the encoder
    // only emits when the global header is requested.
    encoder_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(encoder_, encoder_codec, nullptr) < 0) {
        reason = "failed to open the AAC encoder";
        return false;
    }

    swr_ = swr_alloc_set_opts(nullptr,
                              encoder_->channel_layout, encoder_->sample_fmt,
                              encoder_->sample_rate,
                              source_layout, decoder_->sample_fmt,
                              decoder_->sample_rate,
                              0, nullptr);
    if (!swr_ || swr_init(swr_) < 0) {
        reason = "failed to initialise the audio resampler";
        return false;
    }

    frame_size_ = encoder_->frame_size > 0 ? encoder_->frame_size : kDefaultFrameSize;
    fifo_ = av_audio_fifo_alloc(encoder_->sample_fmt, source_channels, frame_size_);
    if (!fifo_) {
        reason = "failed to allocate the audio staging fifo";
        return false;
    }

    output_params_ = avcodec_parameters_alloc();
    if (!output_params_ ||
        avcodec_parameters_from_context(output_params_, encoder_) < 0) {
        reason = "failed to derive the output audio codec parameters";
        return false;
    }

    time_base_ = AVRational{1, target_sample_rate};
    target_sample_rate_ = target_sample_rate;
    next_pts_ = 0;
    active_ = true;
    return true;
}

bool AudioResampler::encode_frame(const AVFrame* frame,
                                  std::vector<OwnedAVPacket>& out) noexcept {
    int ret = avcodec_send_frame(encoder_, frame);
    if (ret < 0 && ret != AVERROR(EAGAIN)) return false;
    while (true) {
        AVPacket* packet = av_packet_alloc();
        if (!packet) return false;
        ret = avcodec_receive_packet(encoder_, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_packet_free(&packet);
            return true;
        }
        if (ret < 0) {
            av_packet_free(&packet);
            return false;
        }
        out.emplace_back(packet);
    }
}

bool AudioResampler::resample_into_fifo(const AVFrame* decoded, int count,
                                        std::vector<OwnedAVPacket>& out) noexcept {
    // Convert at most `count` input samples. A null `decoded` drains the
    // resampler's internal delay line at end of stream.
    const int capacity = swr_get_out_samples(swr_, count);
    const int out_capacity = capacity > 0 ? capacity : frame_size_;
    AVFrame* resampled = av_frame_alloc();
    if (!resampled) return false;
    resampled->format = encoder_->sample_fmt;
    resampled->channel_layout = encoder_->channel_layout;
    resampled->sample_rate = encoder_->sample_rate;
    resampled->nb_samples = out_capacity;
    if (av_frame_get_buffer(resampled, 0) < 0) {
        free_frame(resampled);
        return false;
    }
    const int produced = swr_convert(
        swr_, resampled->data, out_capacity,
        decoded ? const_cast<const std::uint8_t**>(decoded->data) : nullptr,
        decoded ? count : 0);
    if (produced < 0) {
        free_frame(resampled);
        return false;
    }
    bool ok = true;
    if (produced > 0) {
        if (av_audio_fifo_write(fifo_, reinterpret_cast<void**>(resampled->data),
                                produced) < produced) {
            ok = false;
        } else {
            ok = drain_fifo(false, out);
        }
    }
    free_frame(resampled);
    return ok;
}

bool AudioResampler::drain_fifo(bool final,
                                std::vector<OwnedAVPacket>& out) noexcept {
    while (av_audio_fifo_size(fifo_) >= (final ? 1 : frame_size_)) {
        const int take = final
            ? (av_audio_fifo_size(fifo_) < frame_size_ ? av_audio_fifo_size(fifo_)
                                                       : frame_size_)
            : frame_size_;
        AVFrame* frame = av_frame_alloc();
        if (!frame) return false;
        frame->format = encoder_->sample_fmt;
        frame->channel_layout = encoder_->channel_layout;
        frame->sample_rate = encoder_->sample_rate;
        frame->nb_samples = take;
        if (av_frame_get_buffer(frame, 0) < 0) {
            free_frame(frame);
            return false;
        }
        if (av_audio_fifo_read(fifo_, reinterpret_cast<void**>(frame->data), take) < take) {
            free_frame(frame);
            return false;
        }
        frame->pts = static_cast<std::int64_t>(next_pts_);
        next_pts_ += static_cast<std::uint64_t>(take);
        const bool ok = encode_frame(frame, out);
        free_frame(frame);
        if (!ok) return false;
    }
    return true;
}

bool AudioResampler::resample_and_encode(const AVFrame* decoded,
                                         std::vector<OwnedAVPacket>& out) noexcept {
    if (!decoded) return false;
    return resample_into_fifo(decoded, decoded->nb_samples, out);
}

bool AudioResampler::drain_decoder(std::vector<OwnedAVPacket>& out) noexcept {
    while (true) {
        AVFrame* decoded = av_frame_alloc();
        if (!decoded) return false;
        const int ret = avcodec_receive_frame(decoder_, decoded);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            free_frame(decoded);
            return true;
        }
        if (ret < 0) {
            free_frame(decoded);
            return false;
        }
        const bool ok = resample_and_encode(decoded, out);
        free_frame(decoded);
        if (!ok) return false;
    }
}

bool AudioResampler::push(const AVPacket& input,
                          std::vector<OwnedAVPacket>& out) noexcept {
    if (!active_) return false;
    const int ret = avcodec_send_packet(decoder_, &input);
    if (ret < 0 && ret != AVERROR(EAGAIN)) return false;
    return drain_decoder(out);
}

bool AudioResampler::flush(std::vector<OwnedAVPacket>& out) noexcept {
    if (!active_) return false;
    // 1. Drain the decoder, which pushes everything into the fifo.
    if (avcodec_send_packet(decoder_, nullptr) < 0) return false;
    if (!drain_decoder(out)) return false;
    // 2. Drain the resampler's own delay line, then the trailing partial frame.
    //    Bounded: each pass converts everything the delay line holds, and the
    //    guard keeps a malformed resampler state from spinning forever.
    for (int guard = 0; guard < 64; ++guard) {
        if (swr_get_out_samples(swr_, 0) <= 0) break;
        if (!resample_into_fifo(nullptr, 0, out)) return false;
    }
    if (!drain_fifo(true, out)) return false;
    // 3. Flush the encoder.
    return encode_frame(nullptr, out);
}

} // namespace chronon3d::media
