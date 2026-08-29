// media/video/native_video_frame_decoder.cpp — see the header for the
// contract. Decoding is sequential-friendly: the render loop walks frames in
// order, so after a keyframe-aligned seek the decoder counts output frames to
// the target index and caches the result (bounded).
#include <chronon3d/media/video/native_video_frame_decoder.hpp>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG

#include <chronon3d/math/color.hpp>
#include <chronon3d/core/parallel_tracked.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
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
enum AVPixelFormat select_cuda_format(AVCodecContext*, const enum AVPixelFormat* formats) {
    for (const enum AVPixelFormat* it = formats; *it != AV_PIX_FMT_NONE; ++it) {
        if (*it == AV_PIX_FMT_CUDA) return *it;
    }
    return formats[0];
}

std::shared_ptr<Framebuffer> frame_to_framebuffer(
    const AVFrame* frame, SwsContext*& sws, std::vector<uint8_t>& rgba,
    RenderCounters* counters) {
    if (!frame || frame->width <= 0 || frame->height <= 0) return nullptr;
    sws = sws_getCachedContext(sws, frame->width, frame->height,
        static_cast<AVPixelFormat>(frame->format), frame->width, frame->height,
        AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) return nullptr;
    rgba.resize(static_cast<size_t>(frame->width) * frame->height * 4);
    uint8_t* dst_data[4] = {rgba.data(), nullptr, nullptr, nullptr};
    int dst_stride[4] = {frame->width * 4, 0, 0, 0};
    const auto start = profiling::now();
    const int ret = sws_scale(sws, frame->data, frame->linesize, 0, frame->height,
                              dst_data, dst_stride);
    const auto sws_dur = static_cast<uint64_t>(profiling::duration_ms(start, profiling::now()));
    if (counters) {
        counters->video_decode_sws_wall_ms.fetch_add(sws_dur, std::memory_order_relaxed);
        counters->swscale_ms.fetch_add(sws_dur, std::memory_order_relaxed);
    }
    if (ret != frame->height) return nullptr;
    profiling::FramebufferAllocationScope allocation_scope(
        profiling::FramebufferAllocationCategory::Video);
    auto fb = std::make_shared<Framebuffer>(frame->width, frame->height, false);
    const i32 stride = fb->allocated_width();
    Color* pixels = fb->data();
    const int grain = std::max(16, frame->height / 16);
    const auto conv_start = profiling::now();
    parallel_for_tracked(tbb::blocked_range<int>(0, frame->height, grain),
        [&](const tbb::blocked_range<int>& rows) {
            for (int y = rows.begin(); y < rows.end(); ++y) {
                const uint8_t* row = rgba.data() + static_cast<size_t>(y) * frame->width * 4;
                Color* dst = pixels + static_cast<usize>(y) * stride;
                for (int x = 0; x < frame->width; ++x) {
                    const uint8_t* p = row + static_cast<size_t>(x) * 4;
                    dst[x] = Color{p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, 1.0f};
                }
            }
        });
    const auto conv_dur = static_cast<uint64_t>(profiling::duration_ms(conv_start, profiling::now()));
    if (counters) {
        counters->cpu_pixel_conversion_ms.fetch_add(conv_dur, std::memory_order_relaxed);
        counters->software_color_convert_frames.fetch_add(1, std::memory_order_relaxed);
    }
    return fb;
}

} // namespace

NativeVideoFrameDecoder::~NativeVideoFrameDecoder() = default;
NativeVideoFrameDecoder::Session::~Session() {
    spdlog::info("[native-decoder] stopping prefetch worker");
    prefetch_stop.store(true, std::memory_order_relaxed);
    prefetch_cv.notify_all();
    if (prefetch_worker.joinable()) prefetch_worker.join();
    spdlog::info("[native-decoder] prefetch worker stopped");
    native_import_session.reset();
    av_frame_free(&decoded);
    av_frame_free(&closest_frame);
    av_packet_free(&packet);
    av_frame_free(&hw_transfer_frame);
    if (sws) sws_freeContext(sws);
    avcodec_free_context(&codec);
    av_buffer_unref(&hw_device_ctx);
    if (fmt) avformat_close_input(&fmt);
}

std::shared_ptr<Framebuffer> NativeVideoFrameDecoder::try_native_frame(
    Session& session, AVFrame* frame) {
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    profiling::ProfilingGuard profiling_guard(m_counters, nullptr);
    if (!frame || frame->format != AV_PIX_FMT_CUDA || !m_native_importer) return nullptr;
    runtime::PixelFormat format = runtime::PixelFormat::Nv12;
    if (frame->hw_frames_ctx) {
        auto* frames = reinterpret_cast<AVHWFramesContext*>(frame->hw_frames_ctx->data);
        if (frames) {
            if (frames->sw_format == AV_PIX_FMT_P010) {
                format = runtime::PixelFormat::P010;
            } else if (frames->sw_format != AV_PIX_FMT_NV12) {
                return nullptr;
            }
        }
    }
    if (!session.native_import_session)
        session.native_import_session = m_native_importer->create_session();
    if (!session.native_import_session) return nullptr;
    runtime::ColorMetadata color{};
    color.matrix = frame->colorspace == AVCOL_SPC_BT2020_NCL ? runtime::ColorMatrix::Bt2020Ncl
        : ((frame->colorspace == AVCOL_SPC_BT470BG || frame->colorspace == AVCOL_SPC_SMPTE170M)
            ? runtime::ColorMatrix::Bt601 : runtime::ColorMatrix::Bt709);
    color.range = frame->color_range == AVCOL_RANGE_JPEG ? runtime::ColorRange::Full
                                                         : runtime::ColorRange::Limited;
    auto result = session.native_import_session->import(NativeDecodedFrameView{
        frame, static_cast<std::uint32_t>(frame->width), static_cast<std::uint32_t>(frame->height),
        format, color});
    if (result && m_counters) {
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
    if (it != m_sessions.end()) return it->second;
    auto session = std::make_shared<Session>();
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) != 0) {
        spdlog::error("[native-decoder] open input failed: {}", path);
        return nullptr;
    }
    session->fmt = fmt;
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        spdlog::error("[native-decoder] stream info failed: {}", path);
        return nullptr;
    }
    const int index = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (index < 0) {
        spdlog::error("[native-decoder] no video stream: {}", path);
        return nullptr;
    }
    session->stream_index = index;
    const enum AVCodecID codec_id = fmt->streams[index]->codecpar->codec_id;
    const AVCodec* codec = nullptr;
    if (codec_id == AV_CODEC_ID_H264) {
        codec = avcodec_find_decoder_by_name("h264_cuvid");
    } else if (codec_id == AV_CODEC_ID_HEVC) {
        codec = avcodec_find_decoder_by_name("hevc_cuvid");
    } else if (codec_id == AV_CODEC_ID_AV1) {
        codec = avcodec_find_decoder_by_name("av1_cuvid");
    }
    if (!codec) {
        codec = avcodec_find_decoder(codec_id);
    }
    if (!codec) {
        spdlog::error("[native-decoder] decoder unavailable for: {}", path);
        return nullptr;
    }
    AVCodecContext* cc = avcodec_alloc_context3(codec);
    if (!cc || avcodec_parameters_to_context(cc, fmt->streams[index]->codecpar) < 0) {
        avcodec_free_context(&cc); return nullptr;
    }

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    AVBufferRef* device = nullptr;
    CUcontext shared_ctx = m_native_importer
        ? reinterpret_cast<CUcontext>(m_native_importer->cuda_context())
        : nullptr;
    if (shared_ctx) {
        AVBufferRef* dev = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_CUDA);
        if (dev) {
            auto* hwctx = reinterpret_cast<AVHWDeviceContext*>(dev->data);
            auto* cuda_hwctx = reinterpret_cast<AVCUDADeviceContext*>(hwctx->hwctx);
            cuda_hwctx->cuda_ctx = shared_ctx;
            if (av_hwdevice_ctx_init(dev) >= 0) {
                device = dev;
                spdlog::info("[native-decoder] bound NVDEC to shared CUDA context {}",
                             static_cast<void*>(shared_ctx));
            } else {
                spdlog::warn("[native-decoder] failed to initialize device context from shared CUDA context");
                av_buffer_unref(&dev);
            }
        }
    }
    if (!device) {
        const int hw_device_result = av_hwdevice_ctx_create(
            &device, AV_HWDEVICE_TYPE_CUDA, "0", nullptr, AV_CUDA_USE_PRIMARY_CONTEXT);
        if (hw_device_result >= 0) {
            spdlog::info("[native-decoder] bound NVDEC to primary CUDA context");
        } else {
            spdlog::warn("[native-decoder] CUDA primary hwdevice init failed: {}", hw_device_result);
        }
    }
    if (device) {
        cc->hw_device_ctx = av_buffer_ref(device);
        cc->get_format = select_cuda_format;
        cc->extra_hw_frames = 8;
        session->hw_device_ctx = device;
    } else if (m_gpu_hot_path_mode == GpuHotPathMode::RequireGpuNative ||
               m_gpu_hot_path_mode == GpuHotPathMode::RequireDirectYuv) {
        spdlog::error("[native-decoder] GPU_NATIVE_REQUIRED: failed to create CUDA hwdevice context");
        avcodec_free_context(&cc);
        return nullptr;
    }
#endif
    cc->thread_count = 0;
    if (avcodec_open2(cc, codec, nullptr) < 0) {
        spdlog::error("[native-decoder] codec open failed for {}", path);
        avcodec_free_context(&cc);
        return nullptr;
    }
    session->codec = cc;
    session->hw_transfer_frame = av_frame_alloc();
    session->decoded = av_frame_alloc();
    session->closest_frame = av_frame_alloc();
    session->packet = av_packet_alloc();
    session->start_prefetch_worker(this);
    m_sessions[path] = session;
    return session;
}

void NativeVideoFrameDecoder::Session::start_prefetch_worker(NativeVideoFrameDecoder* decoder) {
    prefetch_worker = std::thread([this, decoder]() {
        while (!prefetch_stop.load(std::memory_order_relaxed)) {
            int64_t target = -1; uint64_t generation = 0;
            {
                std::unique_lock lock(mutex);
                prefetch_cv.wait(lock, [this] { return prefetch_stop.load() ||
                    (prefetch_queue.size() < kPrefetchCapacity && prefetch_next >= 0); });
                if (prefetch_stop.load()) break;
                target = prefetch_next++; generation = prefetch_generation; prefetch_inflight = target;
            }
            auto fb = decoder->decode_frame_internal(*this, target);
            {
                std::lock_guard lock(mutex);
                if (fb && generation == prefetch_generation && !prefetch_stop.load())
                    prefetch_queue.push_back({target, std::move(fb)});
                else if (generation == prefetch_generation) prefetch_next = -1;
                if (prefetch_inflight == target) prefetch_inflight = -1;
                prefetch_cv.notify_all();
            }
        }
    });
}

std::shared_ptr<Framebuffer> NativeVideoFrameDecoder::decode_frame_internal(Session& session, int64_t target) {
    std::lock_guard decode_lock(session.decode_mutex);
    const AVStream* st = session.fmt->streams[session.stream_index];
    AVRational duration = av_inv_q(st->avg_frame_rate);
    if (duration.num <= 0 || duration.den <= 0) duration = {1, 30};
    const int64_t target_pts = av_rescale_q(target, duration, st->time_base);
    if (session.source_eof && session.eof_frame && target >= session.eof_target) return session.eof_frame;
    if (session.last_target < 0 || target < session.last_target || target > session.last_target + 8) {
        av_seek_frame(session.fmt, session.stream_index, target_pts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(session.codec); session.last_target = -1;
        session.source_eof = false; session.eof_frame.reset();
    }
    if (!session.decoded || !session.closest_frame || !session.packet) return nullptr;
    av_frame_unref(session.decoded); av_frame_unref(session.closest_frame); av_packet_unref(session.packet);
    AVFrame* decoded = session.decoded; AVFrame* closest = session.closest_frame; AVPacket* packet = session.packet;
    std::shared_ptr<Framebuffer> result; int64_t closest_delta = INT64_MAX; int overshoot = 0; bool eof = false;
    while (true) {
        const int read = av_read_frame(session.fmt, packet);
        if (read < 0) {
            eof = (read == AVERROR_EOF);
            if (eof && session.codec) {
                (void)avcodec_send_packet(session.codec, nullptr);
                while (avcodec_receive_frame(session.codec, decoded) == 0) {
                    const int64_t pts = decoded->best_effort_timestamp != AV_NOPTS_VALUE ? decoded->best_effort_timestamp : decoded->pts;
                    if (pts == target_pts) {
                        result = try_native_frame(session, decoded);
                        if (result) break;
                    }
                    if (pts != AV_NOPTS_VALUE) {
                        const int64_t delta = std::llabs(pts - target_pts);
                        if (delta < closest_delta) { closest_delta = delta; av_frame_unref(closest); av_frame_ref(closest, decoded); }
                    }
                }
            }
            break;
        }
        if (packet->stream_index != session.stream_index) { av_packet_unref(packet); continue; }
        const auto submit_start = profiling::now();
        const int send = avcodec_send_packet(session.codec, packet);
        const auto submit_dur = static_cast<uint64_t>(profiling::duration_ms(submit_start, profiling::now()));
        if (m_counters) m_counters->decode_submit_ms.fetch_add(submit_dur, std::memory_order_relaxed);
        av_packet_unref(packet);
        if (send < 0) break;
        int receive = 0;
        const auto wait_start = profiling::now();
        while ((receive = avcodec_receive_frame(session.codec, decoded)) == 0) {
            const auto wait_dur = static_cast<uint64_t>(profiling::duration_ms(wait_start, profiling::now()));
            if (m_counters) m_counters->decode_wait_ms.fetch_add(wait_dur, std::memory_order_relaxed);
            const int64_t pts = decoded->best_effort_timestamp != AV_NOPTS_VALUE ? decoded->best_effort_timestamp : decoded->pts;
            if (pts == target_pts) {
                if ((result = try_native_frame(session, decoded))) break;
                if (m_gpu_hot_path_mode == GpuHotPathMode::RequireGpuNative ||
                    m_gpu_hot_path_mode == GpuHotPathMode::RequireDirectYuv) {
                    spdlog::error("[native-decoder] GPU_NATIVE_REQUIRED: try_native_frame failed for CUDA frame, CPU fallback forbidden");
                    if (m_counters) {
                        m_counters->video_decode_native_fallback_frames.fetch_add(1, std::memory_order_relaxed);
                    }
                    return nullptr;
                }
                const AVFrame* render = decoded;
                if (decoded->format == AV_PIX_FMT_CUDA && session.hw_transfer_frame) {
                    av_frame_unref(session.hw_transfer_frame);
                    const auto xfer_start = profiling::now();
                    if (av_hwframe_transfer_data(session.hw_transfer_frame, decoded, 0) >= 0) {
                        render = session.hw_transfer_frame;
                        const auto xfer_dur = static_cast<uint64_t>(profiling::duration_ms(xfer_start, profiling::now()));
                        if (m_counters) {
                            m_counters->video_decode_hw_transfer_wall_ms.fetch_add(xfer_dur, std::memory_order_relaxed);
                            m_counters->hwframe_transfer_ms.fetch_add(xfer_dur, std::memory_order_relaxed);
                            m_counters->video_decode_hw_transfer_frames.fetch_add(1, std::memory_order_relaxed);
                            m_counters->hwframe_transfer_to_cpu_frames.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }
                result = frame_to_framebuffer(render, session.sws, session.rgba, m_counters); break;
            }
            if (pts != AV_NOPTS_VALUE) {
                const int64_t delta = std::llabs(pts - target_pts);
                if (delta < closest_delta) { closest_delta = delta; av_frame_unref(closest); av_frame_ref(closest, decoded); }
                if (pts > target_pts && ++overshoot >= 16) break;
            }
        }
        if (result || overshoot >= 16) break;
        if (receive < 0 && receive != AVERROR(EAGAIN)) break;
    }
    if (!result && closest->width > 0 && closest->height > 0) {
        if (closest->format == AV_PIX_FMT_CUDA) {
            if (!(result = try_native_frame(session, closest))) {
                if (m_gpu_hot_path_mode == GpuHotPathMode::RequireGpuNative ||
                    m_gpu_hot_path_mode == GpuHotPathMode::RequireDirectYuv) {
                    spdlog::error("[native-decoder] GPU_NATIVE_REQUIRED: try_native_frame failed for closest CUDA frame, CPU fallback forbidden");
                    if (m_counters) {
                        m_counters->video_decode_native_fallback_frames.fetch_add(1, std::memory_order_relaxed);
                    }
                    return nullptr;
                }
                if (session.hw_transfer_frame) {
                    av_frame_unref(session.hw_transfer_frame);
                    const auto xfer_start = profiling::now();
                    if (av_hwframe_transfer_data(session.hw_transfer_frame, closest, 0) >= 0) {
                        const auto xfer_dur = static_cast<uint64_t>(profiling::duration_ms(xfer_start, profiling::now()));
                        if (m_counters) {
                            m_counters->video_decode_hw_transfer_wall_ms.fetch_add(xfer_dur, std::memory_order_relaxed);
                            m_counters->hwframe_transfer_ms.fetch_add(xfer_dur, std::memory_order_relaxed);
                            m_counters->video_decode_hw_transfer_frames.fetch_add(1, std::memory_order_relaxed);
                            m_counters->hwframe_transfer_to_cpu_frames.fetch_add(1, std::memory_order_relaxed);
                        }
                        result = frame_to_framebuffer(session.hw_transfer_frame, session.sws, session.rgba, m_counters);
                    }
                }
            }
        } else {
            if (m_gpu_hot_path_mode == GpuHotPathMode::RequireGpuNative ||
                m_gpu_hot_path_mode == GpuHotPathMode::RequireDirectYuv) {
                spdlog::error("[native-decoder] GPU_NATIVE_REQUIRED: decoder produced software frame (expected CUDA)");
                if (m_counters) {
                    m_counters->video_decode_native_fallback_frames.fetch_add(1, std::memory_order_relaxed);
                }
                return nullptr;
            }
            result = frame_to_framebuffer(closest, session.sws, session.rgba, m_counters);
        }
    }
    if (!result && session.eof_frame) {
        result = session.eof_frame;
    }
    if (!result && eof && session.last_target >= 0) {
        auto last_cached = session.cache.get(session.last_target);
        if (last_cached && *last_cached) {
            result = *last_cached;
            session.source_eof = true;
            session.eof_target = session.last_target;
            session.eof_frame = result;
        }
    }
    if (result) {
        session.last_target = target;
        if (eof) {
            session.source_eof = true;
            session.eof_target = target;
            session.eof_frame = result;
        }
    }
    return result;
}

std::shared_ptr<Framebuffer> NativeVideoFrameDecoder::decode_frame(const std::string& path, Frame frame, int, int, float) {
    if (frame < 0 || path.empty()) return nullptr;
    const int64_t target = frame.integral();
    std::shared_ptr<Session> session;
    { std::lock_guard lock(m_mutex); session = open_session_locked(path); }
    if (!session) return nullptr;
    std::unique_lock lock(session->mutex);
    if (session->prefetch_inflight == target) session->prefetch_cv.wait(lock, [&session, target] { return session->prefetch_inflight != target || session->prefetch_stop.load(); });
    if (auto cached = session->cache.get(target)) return *cached;
    while (!session->prefetch_queue.empty() && session->prefetch_queue.front().target < target) session->prefetch_queue.pop_front();
    if (!session->prefetch_queue.empty() && session->prefetch_queue.front().target == target) {
        auto result = std::move(session->prefetch_queue.front().framebuffer); session->prefetch_queue.pop_front(); return result;
    }
    session->prefetch_queue.clear(); ++session->prefetch_generation; session->prefetch_next = target + 1;
    lock.unlock(); auto result = decode_frame_internal(*session, target); lock.lock(); session->prefetch_cv.notify_all();
    if (result && result->surface_handle() == runtime::kInvalidRenderSurfaceHandle) {
        session->cache.put(target, result);
    }
    return result;
}

} // namespace chronon3d::media

#endif
