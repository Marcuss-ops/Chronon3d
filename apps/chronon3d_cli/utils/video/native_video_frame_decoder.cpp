// utils/video/native_video_frame_decoder.cpp — see the header for the
// contract. Decoding is sequential-friendly: the render loop walks frames in
// order, so after a keyframe-aligned seek the decoder counts output frames to
// the target index and caches the result (bounded). Backward or large forward
// jumps re-seek to the nearest keyframe. Frame indexing is count-based
// (correct for B-frame-free CFR content like the golden fixtures; content
// with B-frames may map to the nearest decoded frame — documented v1
// limitation).
#include "native_video_frame_decoder.hpp"

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG

#include <chronon3d/math/color.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace chronon3d::cli {

namespace {

// Bounded cache: keep the most recent decoded frames per source so repeated
// or looping frames never re-decode (the render graph node cache already
// dedupes identical frames across the timeline; this cache covers the
// decoder's own hot range).
constexpr std::size_t kMaxCachedFrames = 64;

std::shared_ptr<Framebuffer> frame_to_framebuffer(
    const AVFrame* frame, SwsContext*& sws, std::vector<uint8_t>& rgba) {
    if (!frame || frame->width <= 0 || frame->height <= 0) {
        return nullptr;
    }
    // Convert YUV → RGBA8 via swscale (source and output share the native
    // resolution; only the pixel format changes).
    sws = sws_getCachedContext(
        sws,
        frame->width, frame->height,
        static_cast<AVPixelFormat>(frame->format),
        frame->width, frame->height,
        AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) {
        spdlog::warn("[video-decoder] sws_getContext failed ({}x{})", frame->width, frame->height);
        return nullptr;
    }

    rgba.resize(static_cast<size_t>(frame->width) * frame->height * 4);
    uint8_t* dst_data[4] = {rgba.data(), nullptr, nullptr, nullptr};
    int dst_stride[4] = {frame->width * 4, 0, 0, 0};
    const int ret = sws_scale(sws,
        frame->data, frame->linesize, 0, frame->height,
        dst_data, dst_stride);
    if (ret != frame->height) {
        spdlog::warn("[video-decoder] sws_scale returned {} (expected {})", ret, frame->height);
        return nullptr;
    }

    auto fb = std::make_shared<Framebuffer>(frame->width, frame->height);
    const i32 stride = fb->allocated_width();
    Color* pixels = fb->data();
    for (int y = 0; y < frame->height; ++y) {
        const uint8_t* row = rgba.data() + static_cast<size_t>(y) * frame->width * 4;
        Color* dst_row = pixels + static_cast<usize>(y) * stride;
        for (int x = 0; x < frame->width; ++x) {
            const uint8_t* p = row + static_cast<size_t>(x) * 4;
            dst_row[x] = Color{
                static_cast<f32>(p[0]) / 255.0f,
                static_cast<f32>(p[1]) / 255.0f,
                static_cast<f32>(p[2]) / 255.0f,
                1.0f,
            };
        }
    }
    return fb;
}

}  // namespace

NativeVideoFrameDecoder::~NativeVideoFrameDecoder() = default;

NativeVideoFrameDecoder::Session::~Session() {
    if (sws) {
        sws_freeContext(sws);
    }
    if (codec) {
        avcodec_free_context(&codec);
    }
    if (fmt) {
        avformat_close_input(&fmt);
    }
}

std::shared_ptr<NativeVideoFrameDecoder::Session>
NativeVideoFrameDecoder::open_session_locked(const std::string& path) {
    auto it = m_sessions.find(path);
    if (it != m_sessions.end()) {
        return it->second;
    }

    auto session = std::make_shared<Session>();

    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) != 0) {
        spdlog::warn("[video-decoder] cannot open '{}'", path);
        return nullptr;
    }
    session->fmt = fmt;

    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        spdlog::warn("[video-decoder] cannot read stream info for '{}'", path);
        return nullptr;
    }

    const int index = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (index < 0) {
        spdlog::warn("[video-decoder] no video stream in '{}'", path);
        return nullptr;
    }
    session->stream_index = index;

    const AVCodec* codec = avcodec_find_decoder(fmt->streams[index]->codecpar->codec_id);
    if (!codec) {
        spdlog::warn("[video-decoder] no decoder for codec {} in '{}'",
            static_cast<int>(fmt->streams[index]->codecpar->codec_id), path);
        return nullptr;
    }
    AVCodecContext* cc = avcodec_alloc_context3(codec);
    if (!cc) {
        spdlog::warn("[video-decoder] cannot allocate codec context for '{}'", path);
        return nullptr;
    }
    if (avcodec_parameters_to_context(cc, fmt->streams[index]->codecpar) < 0) {
        spdlog::warn("[video-decoder] cannot copy codec parameters for '{}'", path);
        avcodec_free_context(&cc);
        return nullptr;
    }
    // Let libavcodec select the decoder thread count. The render loop remains
    // sequential, but frame decoding itself benefits substantially from slice
    // threading on 1080p sources.
    cc->thread_count = 0;
    if (avcodec_open2(cc, codec, nullptr) < 0) {
        spdlog::warn("[video-decoder] cannot open codec for '{}'", path);
        avcodec_free_context(&cc);
        return nullptr;
    }
    session->codec = cc;

    m_sessions[path] = session;
    return session;
}

std::shared_ptr<Framebuffer> NativeVideoFrameDecoder::decode_frame(
    const std::string& path,
    Frame frame,
    int /*width*/,
    int /*height*/,
    float /*fps*/) {
    if (frame < 0 || path.empty()) {
        return nullptr;
    }
    const int64_t target = frame.integral();

    std::lock_guard<std::mutex> lock(m_mutex);
    auto session = open_session_locked(path);
    if (!session) {
        return nullptr;
    }

    // Cache hit.
    auto cached = session->cache.find(target);
    if (cached != session->cache.end()) {
        return cached->second;
    }

    // Frame index → stream timestamp. CFR content (the golden fixtures) has
    // exact pts = target × frame_duration; B-frame streams are handled by
    // matching on pts rather than counting decode output.
    const AVStream* st = session->fmt->streams[session->stream_index];
    AVRational frame_dur = av_inv_q(st->avg_frame_rate);
    if (frame_dur.num <= 0 || frame_dur.den <= 0) {
        frame_dur = av_inv_q(st->r_frame_rate);
    }
    if (frame_dur.num <= 0 || frame_dur.den <= 0) {
        frame_dur = {1, 30};
    }
    const int64_t target_pts = av_rescale_q(target, frame_dur, st->time_base);

    // Seek when the request is behind the decode position or far ahead
    // (sequential decode otherwise). AVSEEK_FLAG_BACKWARD lands on the
    // keyframe at or before the target timestamp, which is the correct
    // restart point for B-frame streams.
    if (session->last_target < 0 ||
        target < session->last_target ||
        target > session->last_target + 8) {
        av_seek_frame(session->fmt, session->stream_index, target_pts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(session->codec);
        session->last_target = -1;
    }

    AVPacket packet{};
    AVFrame* decoded = av_frame_alloc();
    if (!decoded) {
        return nullptr;
    }
    std::shared_ptr<Framebuffer> result;
    std::shared_ptr<Framebuffer> closest;
    int64_t closest_delta = INT64_MAX;
    int overshoot = 0;

    // Decode forward from the current position until the target pts appears.
    // For non-exact (VFR) streams we keep the closest frame seen and bail
    // after a bounded overshoot; exact CFR pts matches always terminate first.
    while (av_read_frame(session->fmt, &packet) >= 0) {
        if (packet.stream_index != session->stream_index) {
            av_packet_unref(&packet);
            continue;
        }
        const int send = avcodec_send_packet(session->codec, &packet);
        av_packet_unref(&packet);
        if (send < 0) {
            break;
        }
        int receive = 0;
        while ((receive = avcodec_receive_frame(session->codec, decoded)) == 0) {
            const int64_t pts = decoded->best_effort_timestamp != AV_NOPTS_VALUE
                ? decoded->best_effort_timestamp : decoded->pts;
            if (pts == target_pts) {
                result = frame_to_framebuffer(decoded, session->sws, session->rgba);
                break;
            }
            if (pts != AV_NOPTS_VALUE) {
                const int64_t delta = std::llabs(pts - target_pts);
                if (delta < closest_delta) {
                    closest_delta = delta;
                    closest = frame_to_framebuffer(decoded, session->sws, session->rgba);
                }
                // Bounded scan past the target: B-frames after the first
                // overshooting reference frame may still hit the exact pts,
                // so allow a small window before falling back to the closest.
                if (pts > target_pts && ++overshoot >= 16) {
                    break;
                }
            }
        }
        if (result || overshoot >= 16) {
            break;
        }
        if (receive < 0 && receive != AVERROR(EAGAIN)) {
            break;
        }
    }
    av_frame_free(&decoded);

    if (!result) {
        result = closest;
    }
    if (result) {
        session->last_target = target;
        session->cache[target] = result;
        // Bounded cache: evict the oldest entry beyond the cap.
        while (session->cache.size() > kMaxCachedFrames) {
            session->cache.erase(session->cache.begin());
        }
    }
    return result;
}

}  // namespace chronon3d::cli

#endif  // CHRONON3D_ENABLE_NATIVE_FFMPEG
