#include <chronon3d/backends/ffmpeg/ffmpeg_encoder.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <fmt/format.h>
#include <vector>

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

    const std::vector<const char*> hardware_candidates{
        "h264_mf",
        "h264_amf",
        "h264_nvenc",
        "h264_qsv",
        "h264_vaapi",
        "h264_vulkan",
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

    if (const AVCodec* codec = find_first_available_codec(software_candidates)) {
        resolved_name = codec->name ? codec->name : "unknown";
        return codec;
    }

    if (options.use_hardware_accel) {
        if (const AVCodec* codec = find_first_available_codec(hardware_candidates)) {
            resolved_name = codec->name ? codec->name : "unknown";
            return codec;
        }
    }

    resolved_name.clear();
    return nullptr;
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

    pimpl->width = width;
    pimpl->height = height;

    // Guess format from filename
    avformat_alloc_output_context2(&pimpl->format_context, nullptr, nullptr, output_path.c_str());
    if (!pimpl->format_context) {
        spdlog::error("Could not deduce output format from file extension for {}", output_path);
        return false;
    }

    std::string codec_name;
    const AVCodec* codec = resolve_codec(options, codec_name);
    if (!codec) {
        spdlog::error("No suitable video encoder was found for {}", output_path);
        return false;
    }
    if (codec_name != options.codec && options.codec != "auto") {
        spdlog::warn("Using codec '{}' instead of requested '{}' for {}",
                     codec_name, options.codec, output_path);
    } else {
        spdlog::info("Using codec '{}' for {}", codec_name, output_path);
    }

    pimpl->stream = avformat_new_stream(pimpl->format_context, nullptr);
    if (!pimpl->stream) {
        spdlog::error("Could not allocate stream");
        return false;
    }

    pimpl->codec_context = avcodec_alloc_context3(codec);
    if (!pimpl->codec_context) {
        spdlog::error("Could not allocate codec context");
        return false;
    }

    pimpl->codec_context->codec_id = codec->id;
    pimpl->codec_context->bit_rate = 0; // CRF handles quality
    pimpl->codec_context->width = width;
    pimpl->codec_context->height = height;
    pimpl->stream->time_base = {1, options.fps};
    pimpl->codec_context->time_base = pimpl->stream->time_base;
    pimpl->codec_context->gop_size = options.gop_size;
    pimpl->codec_context->pix_fmt = AV_PIX_FMT_YUV420P;

    if (pimpl->format_context->oformat->flags & AVFMT_GLOBALHEADER) {
        pimpl->codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    // Set codec-specific options.
    if (codec->id == AV_CODEC_ID_H264) {
        av_opt_set(pimpl->codec_context->priv_data, "preset", options.preset.c_str(), 0);
        av_opt_set(pimpl->codec_context->priv_data, "crf", std::to_string(options.crf).c_str(), 0);
    }

    if (avcodec_open2(pimpl->codec_context, codec, nullptr) < 0) {
        spdlog::error("Could not open codec");
        return false;
    }

    avcodec_parameters_from_context(pimpl->stream->codecpar, pimpl->codec_context);

    if (!(pimpl->format_context->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&pimpl->format_context->pb, output_path.c_str(), AVIO_FLAG_WRITE) < 0) {
            spdlog::error("Could not open output file {}", output_path);
            return false;
        }
    }

    if (avformat_write_header(pimpl->format_context, nullptr) < 0) {
        spdlog::error("Error occurred when opening output file");
        return false;
    }

    pimpl->frame = av_frame_alloc();
    pimpl->frame->format = pimpl->codec_context->pix_fmt;
    pimpl->frame->width = width;
    pimpl->frame->height = height;
    if (av_frame_get_buffer(pimpl->frame, 32) < 0) {
        spdlog::error("Could not allocate frame data");
        return false;
    }

    pimpl->packet = av_packet_alloc();

    // Prepare SWS context for RGBA float -> YUV420P conversion
    // We'll convert float RGBA to 8-bit RGBA first, then use SWS
    pimpl->sws_context = sws_getContext(width, height, AV_PIX_FMT_RGBA,
                                        width, height, pimpl->codec_context->pix_fmt,
                                        SWS_BILINEAR, nullptr, nullptr, nullptr);

    return true;
}

bool FfmpegEncoder::push_frame(const Framebuffer& fb) {
    if (!is_open()) return false;

    if (fb.width() != pimpl->width || fb.height() != pimpl->height) {
        spdlog::error("Framebuffer dimensions mismatch: {}x{} vs {}x{}", fb.width(), fb.height(), pimpl->width, pimpl->height);
        return false;
    }

    // Convert LinearSRGB float to sRGB 8-bit RGBA
    std::vector<uint8_t> rgba(fb.width() * fb.height() * 4);
    for (int y = 0; y < fb.height(); ++y) {
        const Color* row = fb.pixels_row(y);
        uint8_t* dst = rgba.data() + (size_t)y * fb.width() * 4;
        for (int x = 0; x < fb.width(); ++x) {
            Color srgb = row[x].to_srgb();
            dst[0] = static_cast<uint8_t>(std::clamp(srgb.r * 255.0f, 0.0f, 255.0f));
            dst[1] = static_cast<uint8_t>(std::clamp(srgb.g * 255.0f, 0.0f, 255.0f));
            dst[2] = static_cast<uint8_t>(std::clamp(srgb.b * 255.0f, 0.0f, 255.0f));
            dst[3] = 255; // Alpha ignored in YUV420P anyway
            dst += 4;
        }
    }

    const uint8_t* src_data[1] = { rgba.data() };
    int src_linesize[1] = { fb.width() * 4 };

    sws_scale(pimpl->sws_context, src_data, src_linesize, 0, fb.height(),
              pimpl->frame->data, pimpl->frame->linesize);

    pimpl->frame->pts = pimpl->next_pts++;

    // Encode
    int ret = avcodec_send_frame(pimpl->codec_context, pimpl->frame);
    if (ret < 0) {
        spdlog::error("Error sending frame for encoding");
        return false;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(pimpl->codec_context, pimpl->packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            spdlog::error("Error during encoding");
            return false;
        }

        av_packet_rescale_ts(pimpl->packet, pimpl->codec_context->time_base, pimpl->stream->time_base);
        pimpl->packet->stream_index = pimpl->stream->index;
        av_interleaved_write_frame(pimpl->format_context, pimpl->packet);
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
        av_interleaved_write_frame(pimpl->format_context, pimpl->packet);
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
