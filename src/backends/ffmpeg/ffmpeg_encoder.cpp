#include <chronon3d/backends/ffmpeg/ffmpeg_encoder.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <fmt/format.h>
#include <cmath>
#include <vector>
#include <thread>
#include <string_view>
#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace chronon3d::video {

namespace {

const AVCodec* find_first_available_codec(const std::vector<const char*>& names) {
    for (const char* name : names) {
        if (const AVCodec* codec = avcodec_find_encoder_by_name(name)) {
            return codec;
        }
    }
    return nullptr;
}

const AVCodec* resolve_codec(const VideoEncodeOptions& options, std::string& resolved_name) {
    const std::vector<const char*> software_candidates{
        "libx264",
        "libopenh264",
        "mpeg4",
    };

    const std::string requested = options.codec;
    if (!requested.empty() && requested != "auto") {
        if (const AVCodec* codec = avcodec_find_encoder_by_name(requested.c_str())) {
            resolved_name = requested;
            return codec;
        }

        spdlog::warn("Requested codec '{}' was not found; selecting the best available fallback",
                     requested);
    }

    if (options.use_hardware_accel) {
        spdlog::warn("Hardware encode path is not implemented; using software codec fallback");
    }

    if (const AVCodec* codec = find_first_available_codec(software_candidates)) {
        resolved_name = codec->name ? codec->name : "unknown";
        return codec;
    }

    resolved_name.clear();
    return nullptr;
}

bool use_fast_gamma_for_preset(std::string_view preset) {
    return preset == "ultrafast" || preset == "superfast" || preset == "veryfast";
}

std::string av_error_string(int errnum) {
    std::vector<char> buffer(AV_ERROR_MAX_STRING_SIZE);
    av_strerror(errnum, buffer.data(), buffer.size());
    return buffer.data();
}

} // namespace

struct FfmpegEncoder::Impl {
    AVFormatContext* format_context = nullptr;
    AVCodecContext* codec_context = nullptr;
    AVStream* stream = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* sws_context = nullptr;
    int64_t next_pts = 0;
    int width = 0;
    int height = 0;
    bool fast_gamma = false;
    std::vector<uint8_t> rgba_buffer;

    void cleanup() {
        if (codec_context) {
            avcodec_free_context(&codec_context);
        }
        if (frame) {
            av_frame_free(&frame);
        }
        if (packet) {
            av_packet_free(&packet);
        }
        if (format_context) {
            if (!(format_context->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&format_context->pb);
            }
            avformat_free_context(format_context);
        }
        if (sws_context) {
            sws_freeContext(sws_context);
        }
        format_context = nullptr;
        codec_context = nullptr;
        stream = nullptr;
        frame = nullptr;
        packet = nullptr;
        sws_context = nullptr;
    }

    ~Impl() {
        cleanup();
    }
};

FfmpegEncoder::FfmpegEncoder() : pimpl(std::make_unique<Impl>()) {}
FfmpegEncoder::~FfmpegEncoder() {
    close();
}

FfmpegEncoder::FfmpegEncoder(FfmpegEncoder&&) noexcept = default;
FfmpegEncoder& FfmpegEncoder::operator=(FfmpegEncoder&&) noexcept = default;

bool FfmpegEncoder::open(const std::string& output_path, const VideoEncodeOptions& options, int width, int height) {
    close();

    if (width <= 0 || height <= 0) {
        spdlog::error("Invalid encoder dimensions {}x{}", width, height);
        return false;
    }

    if (options.fps <= 0) {
        spdlog::error("Invalid encoder fps {}", options.fps);
        return false;
    }

    pimpl->width = width;
    pimpl->height = height;
    pimpl->fast_gamma = use_fast_gamma_for_preset(options.preset);
    pimpl->rgba_buffer.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4U);
    const std::string requested_codec = options.codec.empty() ? "auto" : options.codec;

    // Guess format from filename
    avformat_alloc_output_context2(&pimpl->format_context, nullptr, nullptr, output_path.c_str());
    if (!pimpl->format_context) {
        spdlog::error("Could not deduce output format from file extension for {}", output_path);
        pimpl->cleanup();
        return false;
    }

    std::string codec_name;
    const AVCodec* codec = resolve_codec(options, codec_name);
    if (!codec) {
        spdlog::error("No suitable video encoder was found for {}", output_path);
        pimpl->cleanup();
        return false;
    }
    spdlog::info("[video] FFmpeg {} | codec req={} -> sel={} | fps={} crf={} preset={}",
                 av_version_info(), requested_codec, codec_name, options.fps, options.crf, options.preset);
    if (requested_codec != "auto" && codec_name != requested_codec) {
        spdlog::warn("Using codec '{}' instead of requested '{}' for {}",
                     codec_name, requested_codec, output_path);
    }

    pimpl->stream = avformat_new_stream(pimpl->format_context, nullptr);
    if (!pimpl->stream) {
        spdlog::error("Could not allocate stream");
        pimpl->cleanup();
        return false;
    }

    pimpl->codec_context = avcodec_alloc_context3(codec);
    if (!pimpl->codec_context) {
        spdlog::error("Could not allocate codec context");
        pimpl->cleanup();
        return false;
    }

    pimpl->codec_context->codec_id = codec->id;
    pimpl->codec_context->bit_rate = 0; // CRF handles quality
    pimpl->codec_context->width = width;
    pimpl->codec_context->height = height;
    pimpl->codec_context->thread_count = std::max(1u, std::thread::hardware_concurrency());
    pimpl->codec_context->thread_type = FF_THREAD_FRAME;
    pimpl->codec_context->framerate = { options.fps, 1 };
    pimpl->stream->time_base = {1, options.fps};
    pimpl->stream->avg_frame_rate = pimpl->codec_context->framerate;
    pimpl->codec_context->time_base = pimpl->stream->time_base;
    pimpl->codec_context->gop_size = options.gop_size;
    pimpl->codec_context->pix_fmt = AV_PIX_FMT_YUV420P;
    pimpl->codec_context->color_range = AVCOL_RANGE_MPEG;
    pimpl->codec_context->color_primaries = AVCOL_PRI_BT709;
    pimpl->codec_context->color_trc = AVCOL_TRC_BT709;
    pimpl->codec_context->colorspace = AVCOL_SPC_BT709;
    pimpl->codec_context->chroma_sample_location = AVCHROMA_LOC_LEFT;

    if (pimpl->format_context->oformat->flags & AVFMT_GLOBALHEADER) {
        pimpl->codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    // Set codec-specific options.
    if (codec->id == AV_CODEC_ID_H264) {
        av_opt_set(pimpl->codec_context->priv_data, "preset", options.preset.c_str(), 0);
        av_opt_set(pimpl->codec_context->priv_data, "crf", std::to_string(options.crf).c_str(), 0);
    }

    int ret = avcodec_open2(pimpl->codec_context, codec, nullptr);
    if (ret < 0) {
        spdlog::error("Could not open codec: {}", av_error_string(ret));
        pimpl->cleanup();
        return false;
    }

    ret = avcodec_parameters_from_context(pimpl->stream->codecpar, pimpl->codec_context);
    if (ret < 0) {
        spdlog::error("Could not copy codec parameters: {}", av_error_string(ret));
        pimpl->cleanup();
        return false;
    }

    if (!(pimpl->format_context->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&pimpl->format_context->pb, output_path.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            spdlog::error("Could not open output file {}: {}", output_path, av_error_string(ret));
            pimpl->cleanup();
            return false;
        }
    }

    ret = avformat_write_header(pimpl->format_context, nullptr);
    if (ret < 0) {
        spdlog::error("Error occurred when opening output file: {}", av_error_string(ret));
        pimpl->cleanup();
        return false;
    }

    pimpl->frame = av_frame_alloc();
    pimpl->frame->format = pimpl->codec_context->pix_fmt;
    pimpl->frame->width = width;
    pimpl->frame->height = height;
    ret = av_frame_get_buffer(pimpl->frame, 32);
    if (ret < 0) {
        spdlog::error("Could not allocate frame data: {}", av_error_string(ret));
        pimpl->cleanup();
        return false;
    }

    pimpl->packet = av_packet_alloc();
    if (!pimpl->packet) {
        spdlog::error("Could not allocate packet");
        pimpl->cleanup();
        return false;
    }

    // Prepare SWS context for RGBA float -> YUV420P conversion
    // We'll convert float RGBA to 8-bit RGBA first, then use SWS
    pimpl->sws_context = sws_getContext(width, height, AV_PIX_FMT_RGBA,
                                        width, height, pimpl->codec_context->pix_fmt,
                                        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!pimpl->sws_context) {
        spdlog::error("Could not create swscale context");
        pimpl->cleanup();
        return false;
    }

    return true;
}

bool FfmpegEncoder::push_frame(const Framebuffer& fb) {
    if (!is_open()) return false;

    if (fb.width() != pimpl->width || fb.height() != pimpl->height) {
        spdlog::error("Framebuffer dimensions mismatch: {}x{} vs {}x{}", fb.width(), fb.height(), pimpl->width, pimpl->height);
        return false;
    }

    // Convert LinearSRGB float to sRGB 8-bit RGBA
    for (int y = 0; y < fb.height(); ++y) {
        const Color* row = fb.pixels_row(y);
            uint8_t* dst = pimpl->rgba_buffer.data() + (size_t)y * fb.width() * 4;
            for (int x = 0; x < fb.width(); ++x) {
                if (pimpl->fast_gamma) {
                    dst[0] = Color::linear_to_srgb8(row[x].r);
                    dst[1] = Color::linear_to_srgb8(row[x].g);
                    dst[2] = Color::linear_to_srgb8(row[x].b);
                } else {
                    const Color srgb = row[x].to_srgb();
                    dst[0] = static_cast<uint8_t>(std::clamp(srgb.r * 255.0f, 0.0f, 255.0f));
                dst[1] = static_cast<uint8_t>(std::clamp(srgb.g * 255.0f, 0.0f, 255.0f));
                dst[2] = static_cast<uint8_t>(std::clamp(srgb.b * 255.0f, 0.0f, 255.0f));
            }
            dst[3] = 255; // Alpha ignored in YUV420P anyway
            dst += 4;
        }
    }

    const uint8_t* src_data[1] = { pimpl->rgba_buffer.data() };
    int src_linesize[1] = { fb.width() * 4 };

    const int scaled = sws_scale(pimpl->sws_context, src_data, src_linesize, 0, fb.height(),
                                 pimpl->frame->data, pimpl->frame->linesize);
    if (scaled <= 0) {
        spdlog::error("Failed to convert RGBA frame for encoding");
        return false;
    }

    pimpl->frame->pts = pimpl->next_pts++;

    // Encode
    int ret = avcodec_send_frame(pimpl->codec_context, pimpl->frame);
    if (ret < 0) {
        spdlog::error("Error sending frame for encoding: {}", av_error_string(ret));
        return false;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(pimpl->codec_context, pimpl->packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            spdlog::error("Error during encoding: {}", av_error_string(ret));
            return false;
        }

        av_packet_rescale_ts(pimpl->packet, pimpl->codec_context->time_base, pimpl->stream->time_base);
        pimpl->packet->stream_index = pimpl->stream->index;
        ret = av_interleaved_write_frame(pimpl->format_context, pimpl->packet);
        if (ret < 0) {
            spdlog::error("Error writing encoded packet: {}", av_error_string(ret));
            return false;
        }
        av_packet_unref(pimpl->packet);
    }

    return true;
}

void FfmpegEncoder::close() {
    if (!is_open()) return;

    // Flush encoder
    avcodec_send_frame(pimpl->codec_context, nullptr);
    while (true) {
        int ret = avcodec_receive_packet(pimpl->codec_context, pimpl->packet);
        if (ret < 0) break;

        av_packet_rescale_ts(pimpl->packet, pimpl->codec_context->time_base, pimpl->stream->time_base);
        pimpl->packet->stream_index = pimpl->stream->index;
        ret = av_interleaved_write_frame(pimpl->format_context, pimpl->packet);
        if (ret < 0) {
            spdlog::error("Error writing flushed packet: {}", av_error_string(ret));
            break;
        }
        av_packet_unref(pimpl->packet);
    }

    av_write_trailer(pimpl->format_context);
    pimpl->cleanup();
    pimpl->next_pts = 0;
}

bool FfmpegEncoder::is_open() const {
    return pimpl->format_context != nullptr;
}

bool FfmpegEncoder::is_available() {
    return true; // We link to it, so it's always available if it builds
}

} // namespace chronon3d::video
