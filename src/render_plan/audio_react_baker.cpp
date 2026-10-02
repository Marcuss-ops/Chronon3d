#include "audio_react_baker.hpp"
#include "audio_react_contract.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/version.h>
#include <libswresample/swresample.h>
#if LIBAVUTIL_VERSION_MAJOR >= 57
#include <libavutil/channel_layout.h>
#endif
}
#endif

namespace chronon3d::render_plan::detail {

bool native_audio_react_available() noexcept {
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    return true;
#else
    return false;
#endif
}

bool decode_audio_mono_48k(const std::string& path,
                           std::vector<float>& samples,
                           std::string& error) {
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    SwrContext* resampler = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    const auto cleanup = [&] {
        av_frame_free(&frame);
        av_packet_free(&packet);
        swr_free(&resampler);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    };
    int status = avformat_open_input(&format, path.c_str(), nullptr, nullptr);
    if (status < 0) { error = "audio container open failed"; cleanup(); return false; }
    status = avformat_find_stream_info(format, nullptr);
    if (status < 0) { error = "audio stream metadata read failed"; cleanup(); return false; }
    const int stream_index = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (stream_index < 0) { error = "audio source has no audio stream"; cleanup(); return false; }
    const AVCodecParameters* parameters = format->streams[stream_index]->codecpar;
    const AVCodec* decoder = avcodec_find_decoder(parameters->codec_id);
    if (!decoder) { error = "audio codec is unsupported by this FFmpeg build"; cleanup(); return false; }
    codec = avcodec_alloc_context3(decoder);
    if (!codec || avcodec_parameters_to_context(codec, parameters) < 0 ||
        avcodec_open2(codec, decoder, nullptr) < 0) {
        error = "audio decoder initialization failed"; cleanup(); return false;
    }
#if LIBAVUTIL_VERSION_MAJOR >= 57
    AVChannelLayout output_layout{};
    av_channel_layout_default(&output_layout, 1);
    status = swr_alloc_set_opts2(&resampler, &output_layout, AV_SAMPLE_FMT_FLT, kAudioReactSampleRateHz,
        &codec->ch_layout, codec->sample_fmt, codec->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&output_layout);
#else
    const std::uint64_t in_ch_layout = codec->channel_layout != 0
        ? codec->channel_layout
        : av_get_default_channel_layout(codec->channels);
    resampler = swr_alloc_set_opts(nullptr,
        AV_CH_LAYOUT_MONO, AV_SAMPLE_FMT_FLT, kAudioReactSampleRateHz,
        in_ch_layout, codec->sample_fmt, codec->sample_rate,
        0, nullptr);
    status = resampler ? 0 : -1;
#endif
    if (status < 0 || !resampler || swr_init(resampler) < 0) {
        error = "audio resampler initialization failed"; cleanup(); return false;
    }
    packet = av_packet_alloc();
    frame = av_frame_alloc();
    if (!packet || !frame) { error = "audio decoder allocation failed"; cleanup(); return false; }

    const auto receive_frames = [&]() -> bool {
        while (true) {
            const int received = avcodec_receive_frame(codec, frame);
            if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) return true;
            if (received < 0) { error = "audio decoder failed while reading samples"; return false; }
            const int capacity = swr_get_out_samples(resampler, frame->nb_samples);
            if (capacity <= 0 || static_cast<std::size_t>(capacity) >
                120000000U - samples.size()) {
                error = "audio analysis exceeds the 120 million sample limit";
                return false;
            }
            const std::size_t old_size = samples.size();
            samples.resize(old_size + static_cast<std::size_t>(capacity));
            std::uint8_t* output[] = {reinterpret_cast<std::uint8_t*>(samples.data() + old_size)};
            const int converted = swr_convert(resampler, output, capacity,
                const_cast<const std::uint8_t**>(frame->extended_data), frame->nb_samples);
            if (converted < 0) { error = "audio resampling failed"; return false; }
            samples.resize(old_size + static_cast<std::size_t>(converted));
            av_frame_unref(frame);
        }
    };
    while ((status = av_read_frame(format, packet)) >= 0) {
        if (packet->stream_index == stream_index) {
            status = avcodec_send_packet(codec, packet);
            av_packet_unref(packet);
            if (status < 0 || !receive_frames()) {
                if (error.empty()) error = "audio decoder rejected a packet";
                cleanup(); return false;
            }
        } else av_packet_unref(packet);
    }
    if (status != AVERROR_EOF || avcodec_send_packet(codec, nullptr) < 0 || !receive_frames()) {
        if (error.empty()) error = "audio decode did not reach end of stream";
        cleanup(); return false;
    }
    // Drain the resampler's delayed output (normally empty for 48 kHz input).
    while (true) {
        const int capacity = swr_get_out_samples(resampler, 0);
        if (capacity <= 0) break;
        const auto old_size = samples.size();
        samples.resize(old_size + static_cast<std::size_t>(capacity));
        std::uint8_t* output[] = {reinterpret_cast<std::uint8_t*>(samples.data() + old_size)};
        const int converted = swr_convert(resampler, output, capacity, nullptr, 0);
        if (converted <= 0) { samples.resize(old_size); break; }
        samples.resize(old_size + static_cast<std::size_t>(converted));
    }
    cleanup();
    if (samples.empty()) { error = "audio source decoded to no samples"; return false; }
    return true;
#else
    (void)path;
    (void)samples;
    error = "audio_react compilation requires CHRONON3D_ENABLE_NATIVE_FFMPEG";
    return false;
#endif
}

} // namespace chronon3d::render_plan::detail
