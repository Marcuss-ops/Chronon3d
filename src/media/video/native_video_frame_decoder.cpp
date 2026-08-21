// media/video/native_video_frame_decoder.cpp — see the header for the
// contract. Decoding is sequential-friendly: the render loop walks frames in
// order, so after a keyframe-aligned seek the decoder counts output frames to
// the target index and caches the result (bounded). Backward or large forward
// jumps re-seek to the nearest keyframe. Frame indexing is count-based
// (correct for B-frame-free CFR content like the golden fixtures; content
// with B-frames may map to the nearest decoded frame — documented v1
// limitation).
#include <chronon3d/media/video/native_video_frame_decoder.hpp>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG

#include <chronon3d/math/color.hpp>
#include <chronon3d/core/parallel_tracked.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
extern "C" {
#include <libavutil/hwcontext_cuda.h>
}
#endif

#include <spdlog/spdlog.h>

#include <tbb/blocked_range.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace chronon3d::media {

namespace {

// Bounded cache: keep the most recent decoded frames per source so repeated
// or looping frames never re-decode (the render graph node cache already
// dedupes identical frames across the timeline; this cache covers the
// decoder's own hot range).
constexpr std::size_t kMaxCachedFrames = 64;

enum AVPixelFormat select_cuda_format(
    AVCodecContext*, const enum AVPixelFormat* formats) {
    for (const enum AVPixelFormat* it = formats; *it != AV_PIX_FMT_NONE; ++it) {
        if (*it == AV_PIX_FMT_CUDA) {
            return *it;
        }
    }
    return formats[0];
}

std::shared_ptr<Framebuffer> frame_to_framebuffer(
    const AVFrame* frame, SwsContext*& sws, std::vector<uint8_t>& rgba,
    RenderCounters* counters) {
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
    const auto sws_start = profiling::now();
    const int ret = sws_scale(sws,
        frame->data, frame->linesize, 0, frame->height,
        dst_data, dst_stride);
    if (counters) {
        counters->video_decode_sws_wall_ms.fetch_add(
            static_cast<uint64_t>(profiling::duration_ms(sws_start, profiling::now())),
            std::memory_order_relaxed);
    }
    if (ret != frame->height) {
        spdlog::warn("[video-decoder] sws_scale returned {} (expected {})", ret, frame->height);
        return nullptr;
    }

    const auto convert_start = profiling::now();
    profiling::FramebufferAllocationScope allocation_scope(
        profiling::FramebufferAllocationCategory::Video);
    auto fb = std::make_shared<Framebuffer>(frame->width, frame->height, false);
    const i32 stride = fb->allocated_width();
    Color* pixels = fb->data();
    const int grain = std::max(16, frame->height / 16);
    parallel_for_tracked(tbb::blocked_range<int>(0, frame->height, grain),
        [&](const tbb::blocked_range<int>& rows) {
            for (int y = rows.begin(); y < rows.end(); ++y) {
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
        });
    if (counters) {
        counters->video_decode_framebuffer_wall_ms.fetch_add(
            static_cast<uint64_t>(profiling::duration_ms(convert_start, profiling::now())),
            std::memory_order_relaxed);
    }
    return fb;
}

}  // namespace

NativeVideoFrameDecoder::~NativeVideoFrameDecoder() = default;

NativeVideoFrameDecoder::Session::~Session() {
    prefetch_stop.store(true, std::memory_order_relaxed);
    prefetch_cv.notify_all();
    if (prefetch_worker.joinable()) {
        prefetch_worker.join();
    }
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    // Destroy the CUDA/Vulkan bridges while their CUDA device context is still
    // owned by hw_device_ctx.
    for (auto& slot : native_slots) {
        slot.compositor.reset();
        if (slot.surface != runtime::kInvalidRenderSurfaceHandle) {
            if (native_backend) {
                (void)native_backend->release_surface(slot.surface);
            }
            if (native_surface_registry) {
                (void)native_surface_registry->release(slot.surface);
            }
            slot.surface = runtime::kInvalidRenderSurfaceHandle;
        }
    }
#endif
    if (decoded) {
        av_frame_free(&decoded);
    }
    if (closest_frame) {
        av_frame_free(&closest_frame);
    }
    if (packet) {
        av_packet_free(&packet);
    }
    if (hw_transfer_frame) {
        av_frame_free(&hw_transfer_frame);
    }
    if (sws) {
        sws_freeContext(sws);
    }
    if (codec) {
        avcodec_free_context(&codec);
    }
    if (hw_device_ctx) {
        av_buffer_unref(&hw_device_ctx);
    }
    if (fmt) {
        avformat_close_input(&fmt);
    }
}

std::shared_ptr<Framebuffer> NativeVideoFrameDecoder::try_native_frame(
    Session& session, AVFrame* frame) {
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    profiling::ProfilingGuard profiling_guard(m_counters, nullptr);
    if (!frame || frame->format != AV_PIX_FMT_CUDA ||
        !m_backend || !m_surface_registry || !frame->hw_frames_ctx) {
        if (m_counters && frame && frame->format == AV_PIX_FMT_CUDA) {
            m_counters->video_decode_native_fallback_frames.fetch_add(1, std::memory_order_relaxed);
        }
        return nullptr;
    }
    auto* vulkan = dynamic_cast<backends::vulkan::VulkanBackend*>(m_backend);
    if (!vulkan) {
        if (m_counters) m_counters->video_decode_native_fallback_frames.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    session.native_backend = m_backend;
    session.native_surface_registry = m_surface_registry;

    auto& slot = session.native_slots[session.next_native_slot % Session::kNativeDecodeSlots];
    session.next_native_slot++;

    // A graph/pool may reclaim a Framebuffer's raw handle independently of
    // the decoder session. Never submit into a compositor whose imported
    // image has already been reclaimed; discard the stale bridge and create a
    // fresh surface below.
    if (slot.surface != runtime::kInvalidRenderSurfaceHandle &&
        (!m_surface_registry->lookup(slot.surface) ||
         !vulkan->is_native_surface_valid(slot.surface))) {
        slot.compositor.reset();
        (void)vulkan->release_surface(slot.surface);
        (void)m_surface_registry->release(slot.surface);
        slot.surface = runtime::kInvalidRenderSurfaceHandle;
    }
    auto* frames = reinterpret_cast<AVHWFramesContext*>(frame->hw_frames_ctx->data);
    if (!frames || !frames->device_ref) {
        if (m_counters) m_counters->video_decode_native_fallback_frames.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    auto* device = reinterpret_cast<AVHWDeviceContext*>(frames->device_ref->data);
    auto* cuda = device ? reinterpret_cast<AVCUDADeviceContext*>(device->hwctx) : nullptr;
    if (!cuda || !cuda->cuda_ctx) {
        if (m_counters) m_counters->video_decode_native_fallback_frames.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }

    if (slot.surface == runtime::kInvalidRenderSurfaceHandle) {
        const runtime::SurfaceDesc desc{
            static_cast<std::uint32_t>(frame->width),
            static_cast<std::uint32_t>(frame->height),
            runtime::PixelFormat::Rgba32Float,
            runtime::ResourceUsage::Storage,
            runtime::LifetimeClass::JobPersistent,
            static_cast<std::size_t>(frame->width) * frame->height * sizeof(float) * 4};
        slot.surface = m_surface_registry->create(desc);
        if (slot.surface == runtime::kInvalidRenderSurfaceHandle ||
            !vulkan->create_cuda_external_surface(slot.surface, desc).ok()) {
            if (slot.surface != runtime::kInvalidRenderSurfaceHandle)
                (void)m_surface_registry->release(slot.surface);
            slot.surface = runtime::kInvalidRenderSurfaceHandle;
            if (m_counters) m_counters->video_decode_native_fallback_frames.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }
        try {
            slot.compositor = std::make_unique<backends::vulkan::CudaNv12SurfaceCompositor>(
                vulkan->export_cuda_external_memory(slot.surface), cuda->cuda_ctx);
        } catch (const std::exception& error) {
            spdlog::warn("[video-decoder] native CUDA surface unavailable: {}", error.what());
            (void)vulkan->release_surface(slot.surface);
            (void)m_surface_registry->release(slot.surface);
            slot.surface = runtime::kInvalidRenderSurfaceHandle;
            if (m_counters) m_counters->video_decode_native_fallback_frames.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }
    }
    if (!slot.compositor || !slot.compositor->composite(
            reinterpret_cast<CUdeviceptr>(frame->data[0]), frame->linesize[0],
            reinterpret_cast<CUdeviceptr>(frame->data[1]), frame->linesize[1],
            static_cast<std::uint32_t>(frame->width),
            static_cast<std::uint32_t>(frame->height), nullptr)) {
        if (m_counters) m_counters->video_decode_native_fallback_frames.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    // The next Vulkan submission waits on cuda_to_vulkan and releases the
    // opposite semaphore for the next NVDEC frame.
    if (!vulkan->prepare_cuda_surface_for_vulkan(slot.surface).ok()) {
        if (m_counters) m_counters->video_decode_native_fallback_frames.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    // The native path only needs a logical framebuffer carrying dimensions
    // and the imported GPU surface handle.  Allocating a CPU pixel array here
    // created one framebuffer allocation per decoded frame even though no
    // CPU pixel was ever read.  The external-pixels constructor intentionally
    // keeps this wrapper storage-free; native_surface consumers must use the
    // attached GPU handle.
    auto result = std::make_shared<Framebuffer>(
        frame->width, frame->height, static_cast<Color*>(nullptr));
    result->set_surface_handle(slot.surface);
    if (m_counters) {
        m_counters->video_decode_hw_frames.fetch_add(1, std::memory_order_relaxed);
        m_counters->video_decode_native_surface_frames.fetch_add(1, std::memory_order_relaxed);
    }
    return result;
#else
    (void)session; (void)frame;
    return nullptr;
#endif
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
    // Prefer NVDEC when the selected decoder advertises CUDA frames. The
    // current render graph still consumes a CPU Framebuffer, so decode_frame
    // performs a bounded hwframe transfer until the native-surface bridge is
    // connected; this nevertheless removes software decode from the hot path.
    for (int i = 0;; ++i) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
        if (!config) break;
        if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
            config->device_type == AV_HWDEVICE_TYPE_CUDA) {
            AVBufferRef* device = nullptr;
            if (av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_CUDA,
                                       nullptr, nullptr, 0) >= 0) {
                cc->hw_device_ctx = av_buffer_ref(device);
                cc->get_format = select_cuda_format;
                session->hw_device_ctx = device;
            }
            break;
        }
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
    session->hw_transfer_frame = av_frame_alloc();
    session->decoded = av_frame_alloc();
    session->closest_frame = av_frame_alloc();
    session->packet = av_packet_alloc();
    if (!session->hw_transfer_frame || !session->decoded || !session->closest_frame || !session->packet) {
        spdlog::warn("[video-decoder] cannot allocate working AVFrames for '{}'", path);
    }

    session->start_prefetch_worker(this);
    m_sessions[path] = session;
    return session;
}

void NativeVideoFrameDecoder::Session::start_prefetch_worker(
    NativeVideoFrameDecoder* decoder) {
    prefetch_worker = std::thread([this, decoder]() {
        while (!prefetch_stop.load(std::memory_order_relaxed)) {
            int64_t target_to_fetch = -1;
            uint64_t generation = 0;
            {
                std::unique_lock<std::mutex> lock(mutex);
                prefetch_cv.wait(lock, [this]() {
                    return prefetch_stop.load(std::memory_order_relaxed) ||
                           (prefetch_queue.size() < kPrefetchCapacity && prefetch_next >= 0);
                });
                if (prefetch_stop.load(std::memory_order_relaxed)) break;
                target_to_fetch = prefetch_next++;
                generation = prefetch_generation;
                prefetch_inflight = target_to_fetch;
            }

            auto fb = decoder->decode_frame_internal(*this, target_to_fetch);

            {
                std::lock_guard<std::mutex> lock(mutex);
                // A synchronous miss can invalidate the producer while this
                // decode is in flight. Never publish a frame from the old
                // sequence into the new queue.
                if (fb && generation == prefetch_generation &&
                    !prefetch_stop.load(std::memory_order_relaxed)) {
                    prefetch_queue.push_back({target_to_fetch, std::move(fb)});
                    if (decoder->m_counters) {
                        auto depth = static_cast<uint64_t>(prefetch_queue.size());
                        auto peak = decoder->m_counters->video_prefetch_queue_depth_peak.load(
                            std::memory_order_relaxed);
                        while (depth > peak && !decoder->m_counters->video_prefetch_queue_depth_peak
                            .compare_exchange_weak(peak, depth, std::memory_order_relaxed)) {}
                    }
                } else if (generation == prefetch_generation) {
                    prefetch_next = -1;
                }
                if (prefetch_inflight == target_to_fetch) prefetch_inflight = -1;
                prefetch_cv.notify_all();
            }
        }
    });
}

std::shared_ptr<Framebuffer> NativeVideoFrameDecoder::decode_frame_internal(
    Session& session, int64_t target) {
    std::lock_guard<std::mutex> decode_lock(session.decode_mutex);

    // Frame index → stream timestamp. CFR content (the golden fixtures) has
    // exact pts = target × frame_duration; B-frame streams are handled by
    // matching on pts rather than counting decode output.
    const AVStream* st = session.fmt->streams[session.stream_index];
    AVRational frame_dur = av_inv_q(st->avg_frame_rate);
    if (frame_dur.num <= 0 || frame_dur.den <= 0) {
        frame_dur = av_inv_q(st->r_frame_rate);
    }
    if (frame_dur.num <= 0 || frame_dur.den <= 0) {
        frame_dur = {1, 30};
    }
    const int64_t target_pts = av_rescale_q(target, frame_dur, st->time_base);

    // A timeline can outlive its source clip.  After the first request past
    // EOF, hold the terminal frame and make subsequent requests O(1).
    if (session.source_eof && session.eof_frame && target >= session.eof_target) {
        session.last_target = target;
        return session.eof_frame;
    }

    // Seek when the request is behind the decode position or far ahead
    // (sequential decode otherwise). AVSEEK_FLAG_BACKWARD lands on the
    // keyframe at or before the target timestamp, which is the correct
    // restart point for B-frame streams.
    if (session.last_target < 0 ||
        target < session.last_target ||
        target > session.last_target + 8) {
        av_seek_frame(session.fmt, session.stream_index, target_pts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(session.codec);
        session.last_target = -1;
        session.source_eof = false;
        session.eof_target = -1;
        session.eof_frame.reset();
    }

    if (!session.decoded || !session.closest_frame || !session.packet) {
        return nullptr;
    }
    av_frame_unref(session.decoded);
    av_frame_unref(session.closest_frame);
    av_packet_unref(session.packet);

    AVPacket* packet = session.packet;
    AVFrame* decoded = session.decoded;
    AVFrame* closest_frame = session.closest_frame;

    std::shared_ptr<Framebuffer> result;
    int64_t closest_delta = INT64_MAX;
    int overshoot = 0;
    bool reached_eof = false;

    // Decode forward from the current position until the target pts appears.
    while (true) {
        const int read_status = av_read_frame(session.fmt, packet);
        if (read_status < 0) {
            reached_eof = read_status == AVERROR_EOF;
            break;
        }
        if (packet->stream_index != session.stream_index) {
            av_packet_unref(packet);
            continue;
        }
        const int send = avcodec_send_packet(session.codec, packet);
        av_packet_unref(packet);
        if (send < 0) {
            break;
        }
        int receive = 0;
        while ((receive = avcodec_receive_frame(session.codec, decoded)) == 0) {
            const int64_t pts = decoded->best_effort_timestamp != AV_NOPTS_VALUE
                ? decoded->best_effort_timestamp : decoded->pts;
            if (pts == target_pts) {
                if (auto native = try_native_frame(session, decoded)) {
                    result = std::move(native);
                    break;
                }
                const AVFrame* render_frame = decoded;
                if (decoded->format == AV_PIX_FMT_CUDA && session.hw_transfer_frame) {
                    const auto transfer_start = profiling::now();
                    av_frame_unref(session.hw_transfer_frame);
                    if (av_hwframe_transfer_data(session.hw_transfer_frame, decoded, 0) >= 0) {
                        render_frame = session.hw_transfer_frame;
                        if (m_counters) {
                            m_counters->video_decode_hw_frames.fetch_add(1, std::memory_order_relaxed);
                            m_counters->video_decode_hw_transfer_frames.fetch_add(1, std::memory_order_relaxed);
                            m_counters->video_decode_hw_transfer_wall_ms.fetch_add(
                                static_cast<uint64_t>(profiling::duration_ms(
                                    transfer_start, profiling::now())),
                                std::memory_order_relaxed);
                        }
                    }
                } else if (decoded->format != AV_PIX_FMT_CUDA && m_counters) {
                    m_counters->video_decode_software_frames.fetch_add(1, std::memory_order_relaxed);
                }
                result = frame_to_framebuffer(render_frame, session.sws, session.rgba, m_counters);
                break;
            }
            if (pts != AV_NOPTS_VALUE) {
                const int64_t delta = std::llabs(pts - target_pts);
                if (delta < closest_delta) {
                    closest_delta = delta;
                    av_frame_unref(closest_frame);
                    if (av_frame_ref(closest_frame, decoded) < 0) {
                        av_frame_unref(closest_frame);
                    }
                }
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
    if (!result && closest_frame->width > 0 && closest_frame->height > 0) {
        if (closest_frame->format == AV_PIX_FMT_CUDA) {
            if (auto native = try_native_frame(session, closest_frame)) {
                result = std::move(native);
            } else if (session.hw_transfer_frame) {
                const auto transfer_start = profiling::now();
                av_frame_unref(session.hw_transfer_frame);
                if (av_hwframe_transfer_data(session.hw_transfer_frame, closest_frame, 0) >= 0) {
                    if (m_counters) {
                        m_counters->video_decode_hw_frames.fetch_add(1, std::memory_order_relaxed);
                        m_counters->video_decode_hw_transfer_frames.fetch_add(1, std::memory_order_relaxed);
                        m_counters->video_decode_hw_transfer_wall_ms.fetch_add(
                            static_cast<uint64_t>(profiling::duration_ms(
                                transfer_start, profiling::now())),
                            std::memory_order_relaxed);
                    }
                    result = frame_to_framebuffer(
                        session.hw_transfer_frame, session.sws, session.rgba, m_counters);
                }
            }
        } else {
            if (m_counters) {
                m_counters->video_decode_software_frames.fetch_add(1, std::memory_order_relaxed);
            }
            result = frame_to_framebuffer(
                closest_frame, session.sws, session.rgba, m_counters);
        }
    }

    if (result) {
        session.last_target = target;
        if (reached_eof && !session.source_eof) {
            session.source_eof = true;
            session.eof_target = target;
            session.eof_frame = result;
        }
    }
    return result;
}

std::shared_ptr<Framebuffer> NativeVideoFrameDecoder::decode_frame(
    const std::string& path,
    Frame frame,
    int /*width*/,
    int /*height*/,
    float /*fps*/) {
    if (frame < 0 || path.empty()) {
        if (m_counters) {
            m_counters->video_source_inactive_frames.fetch_add(1, std::memory_order_relaxed);
        }
        return nullptr;
    }
    if (m_counters) {
        m_counters->video_source_requested_frames.fetch_add(1, std::memory_order_relaxed);
    }
    const int64_t target = frame.integral();
    const auto decode_start = profiling::now();

    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        session = open_session_locked(path);
    }
    if (!session) {
        if (m_counters) {
            m_counters->video_source_inactive_frames.fetch_add(1, std::memory_order_relaxed);
        }
        return nullptr;
    }

    if (session->last_target == target && m_counters) {
        m_counters->video_source_repeated_frames.fetch_add(1, std::memory_order_relaxed);
    }

    std::shared_ptr<Framebuffer> result;
    {
        std::unique_lock<std::mutex> lock(session->mutex);

        // If the producer is already decoding exactly this frame, wait for it
        // instead of clearing the queue and decoding the same frame again.
        // This is the common one-frame producer/consumer race on a warm
        // sequential render.
        if (session->prefetch_inflight == target) {
            const auto wait_start = profiling::now();
            session->prefetch_cv.wait(lock, [&session, target]() {
                return session->prefetch_inflight != target ||
                    session->prefetch_stop.load(std::memory_order_relaxed);
            });
            if (m_counters) m_counters->video_prefetch_wait_us.fetch_add(
                static_cast<uint64_t>(profiling::duration_us(
                    wait_start, profiling::Clock::now())), std::memory_order_relaxed);
        }

        // 1. Check software cache.
        auto cached = session->cache.find(target);
        if (cached != session->cache.end()) {
            if (m_counters) {
                m_counters->video_decode_cache_hits.fetch_add(1, std::memory_order_relaxed);
            }
            return cached->second;
        }

        // 2. Check prefetch queue.
        while (!session->prefetch_queue.empty() && session->prefetch_queue.front().target < target) {
            session->prefetch_queue.pop_front();
        }
        if (!session->prefetch_queue.empty() && session->prefetch_queue.front().target == target) {
            result = std::move(session->prefetch_queue.front().framebuffer);
            session->prefetch_queue.pop_front();
            if (m_counters) {
                m_counters->video_decode_cache_hits.fetch_add(1, std::memory_order_relaxed);
                m_counters->video_prefetch_hits.fetch_add(1, std::memory_order_relaxed);
                m_counters->video_decode_frames.fetch_add(1, std::memory_order_relaxed);
                m_counters->video_decode_wall_ms.fetch_add(
                    static_cast<uint64_t>(profiling::duration_ms(decode_start, profiling::now())),
                    std::memory_order_relaxed);
            }
            session->prefetch_cv.notify_all();
            return result;
        }

        // 3. Queue miss: invalidate the old producer sequence and decode
        // synchronously. The worker may finish its current decode, but its
        // result is discarded by the generation check above.
        if (m_counters) {
            m_counters->video_decode_cache_misses.fetch_add(1, std::memory_order_relaxed);
            m_counters->video_prefetch_misses.fetch_add(1, std::memory_order_relaxed);
            if (!session->prefetch_queue.empty() || session->prefetch_inflight >= 0) {
                m_counters->video_prefetch_queue_clear_count.fetch_add(1, std::memory_order_relaxed);
            }
        }

        session->prefetch_queue.clear();
        ++session->prefetch_generation;
        session->prefetch_next = target + 1;
        lock.unlock();

        result = decode_frame_internal(*session, target);
        lock.lock();
        session->prefetch_cv.notify_all();

        if (result) {
            const bool native_surface = result->surface_handle() !=
                runtime::kInvalidRenderSurfaceHandle;
            if (!native_surface) {
                session->cache[target] = result;
                while (session->cache.size() > kMaxCachedFrames) {
                    session->cache.erase(session->cache.begin());
                }
            }
        }
    }

    if (result && m_counters) {
        m_counters->video_decode_frames.fetch_add(1, std::memory_order_relaxed);
        m_counters->video_decode_wall_ms.fetch_add(
            static_cast<uint64_t>(profiling::duration_ms(decode_start, profiling::now())),
            std::memory_order_relaxed);
    }
    return result;
}

}  // namespace chronon3d::media

#endif  // CHRONON3D_ENABLE_NATIVE_FFMPEG
