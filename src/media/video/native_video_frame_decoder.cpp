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
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>

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
    RenderCounters* counters, bool enable_swscale = true) {
    if (!frame || frame->width <= 0 || frame->height <= 0) return nullptr;
    if (!enable_swscale) {
        return std::make_shared<Framebuffer>(frame->width, frame->height, false);
    }
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
    const auto conv_start = profiling::now();
    const auto convert_rows = [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            const uint8_t* row = rgba.data() + static_cast<size_t>(y) * frame->width * 4;
            Color* dst = pixels + static_cast<usize>(y) * stride;
            for (int x = 0; x < frame->width; ++x) {
                const uint8_t* p = row + static_cast<size_t>(x) * 4;
                dst[x] = Color{p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, 1.0f};
            }
        }
    };
    constexpr std::size_t kParallelPixelThreshold = 4096;
    const auto pixels_count = static_cast<std::size_t>(frame->width) *
                              static_cast<std::size_t>(frame->height);
    if (pixels_count < kParallelPixelThreshold) {
        convert_rows(0, frame->height);
    } else {
        const int grain = std::max(16, frame->height / 16);
        parallel_for_tracked(tbb::blocked_range<int>(0, frame->height, grain),
            [&](const tbb::blocked_range<int>& rows) {
                convert_rows(rows.begin(), rows.end());
            });
    }
    const auto conv_dur = static_cast<uint64_t>(profiling::duration_ms(conv_start, profiling::now()));
    if (counters) {
        counters->cpu_pixel_conversion_ms.fetch_add(conv_dur, std::memory_order_relaxed);
        counters->software_color_convert_frames.fetch_add(1, std::memory_order_relaxed);
    }
    return fb;
}

bool build_source_sample_table(AVFormatContext* fmt, int stream_index,
                               SourceSampleTable& out) {
    if (!fmt || stream_index < 0 || stream_index >= static_cast<int>(fmt->nb_streams)) return false;
    const AVStream* stream = fmt->streams[stream_index];
    out = SourceSampleTable{Rational{stream->time_base.num, stream->time_base.den}};

    const int64_t start_pts = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
    if (av_seek_frame(fmt, stream_index, start_pts, AVSEEK_FLAG_BACKWARD) < 0) {
        spdlog::error("[native-decoder] PTS table scan requires a seekable source");
        return false;
    }
    avformat_flush(fmt);

    AVPacket* packet = av_packet_alloc();
    if (!packet) return false;
    std::uint64_t source_order = 0;
    std::uint32_t continuity = 0;
    int64_t last_dts = AV_NOPTS_VALUE;
    while (av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index == stream_index && packet->pts != AV_NOPTS_VALUE) {
            if (packet->dts != AV_NOPTS_VALUE && last_dts != AV_NOPTS_VALUE && packet->dts < last_dts) {
                ++continuity;
            }
            out.add(SourceSample{
                .pts = packet->pts,
                .duration = packet->duration > 0 ? packet->duration : 0,
                .dts = packet->dts == AV_NOPTS_VALUE ? packet->pts : packet->dts,
                .source_order = source_order++,
                .continuity_id = continuity,
                .keyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0,
            });
            if (packet->dts != AV_NOPTS_VALUE) last_dts = packet->dts;
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    out.finalize();

    if (av_seek_frame(fmt, stream_index, start_pts, AVSEEK_FLAG_BACKWARD) < 0) {
        spdlog::error("[native-decoder] failed to rewind after PTS table scan");
        return false;
    }
    avformat_flush(fmt);
    return !out.empty();
}

void set_decode_diagnostic(DecodeDiagnostic* out,
                           DecodeFailureReason reason,
                           int ffmpeg_error,
                           std::int64_t pts,
                           std::int64_t dts,
                           std::uint64_t source_order,
                           std::string message) {
    if (!out) return;
    *out = DecodeDiagnostic{reason, ffmpeg_error, pts, dts, source_order, std::move(message)};
}

DecodeFailure lookup_failure(SampleLookupDisposition disposition,
                             RationalTime requested_time) {
    DecodeFailureReason reason = DecodeFailureReason::PresentationGap;
    const char* message = "presentation time falls inside a source timeline gap";
    if (disposition == SampleLookupDisposition::BeforeStart) {
        reason = DecodeFailureReason::BeforeStart;
        message = "presentation time is before the first source sample";
    }
    return DecodeFailure{DecodeDiagnostic{
        reason, 0, kNoDecodeTimestamp, kNoDecodeTimestamp, 0, message}};
}

DecodedFrame decoded_frame_from_sample(std::shared_ptr<Framebuffer> framebuffer,
                                       const SourceSample& sample) {
    return DecodedFrame{std::move(framebuffer), sample.pts, sample.dts, sample.source_order};
}

} // namespace

NativeVideoFrameDecoder::~NativeVideoFrameDecoder() = default;
NativeVideoFrameDecoder::Session::~Session() {
    prefetch_stop.store(true, std::memory_order_relaxed);
    prefetch_cv.notify_all();
    if (prefetch_worker.joinable()) prefetch_worker.join();
    prefetch_queue.clear();
    cache.clear();
    captured_native_frame.reset();
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
    Session& session, AVFrame* frame, DecodeDiagnostic* diagnostic) {
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    profiling::ProfilingGuard profiling_guard(m_counters, nullptr);
    if (!frame || frame->format != AV_PIX_FMT_CUDA) return nullptr;
    if (session.capture_native_frame) {
        AVFrame* captured = av_frame_alloc();
        const int ref_result = captured ? av_frame_ref(captured, frame) : AVERROR(ENOMEM);
        if (!captured || ref_result < 0) {
            if (captured) av_frame_free(&captured);
            set_decode_diagnostic(diagnostic, DecodeFailureReason::NativeSurfaceUnavailable,
                ref_result, frame->pts, frame->pkt_dts, 0,
                "failed to retain native decoded frame");
            return nullptr;
        }
        session.captured_native_frame = HwFrameRef(captured);
        if (m_counters) {
            m_counters->video_decode_frames.fetch_add(1, std::memory_order_relaxed);
            m_counters->video_decode_hw_frames.fetch_add(1, std::memory_order_relaxed);
            m_counters->video_decode_native_surface_frames.fetch_add(1, std::memory_order_relaxed);
        }
        return nullptr;
    }
    if (!m_native_importer) return nullptr;
    runtime::PixelFormat format = runtime::PixelFormat::Nv12;
    if (frame->hw_frames_ctx) {
        auto* frames = reinterpret_cast<AVHWFramesContext*>(frame->hw_frames_ctx->data);
        if (frames) {
            if (frames->sw_format == AV_PIX_FMT_P010) format = runtime::PixelFormat::P010;
            else if (frames->sw_format != AV_PIX_FMT_NV12) {
                set_decode_diagnostic(diagnostic, DecodeFailureReason::UnsupportedFormatChange, 0,
                    frame->pts, frame->pkt_dts, 0,
                    "native decoded frame changed to an unsupported pixel format");
                return nullptr;
            }
        }
    }
    if (!session.native_import_session) session.native_import_session = m_native_importer->create_session();
    if (!session.native_import_session) {
        set_decode_diagnostic(diagnostic, DecodeFailureReason::NativeSurfaceUnavailable, 0,
            frame->pts, frame->pkt_dts, 0,
            "native frame importer could not create an import session");
        return nullptr;
    }
    runtime::ColorMetadata color{};
    color.matrix = frame->colorspace == AVCOL_SPC_BT2020_NCL ? runtime::ColorMatrix::Bt2020Ncl
        : ((frame->colorspace == AVCOL_SPC_BT470BG || frame->colorspace == AVCOL_SPC_SMPTE170M)
            ? runtime::ColorMatrix::Bt601 : runtime::ColorMatrix::Bt709);
    color.range = frame->color_range == AVCOL_RANGE_JPEG ? runtime::ColorRange::Full
                                                         : runtime::ColorRange::Limited;
    auto result = session.native_import_session->import(NativeDecodedFrameView{
        frame, static_cast<std::uint32_t>(frame->width), static_cast<std::uint32_t>(frame->height), format, color});
    if (result && m_counters) {
        m_counters->video_decode_hw_frames.fetch_add(1, std::memory_order_relaxed);
        m_counters->video_decode_native_surface_frames.fetch_add(1, std::memory_order_relaxed);
    } else if (!result) {
        set_decode_diagnostic(diagnostic, DecodeFailureReason::NativeSurfaceUnavailable, 0,
            frame->pts, frame->pkt_dts, 0,
            "native frame importer rejected the decoded surface");
    }
    return result;
#else
    (void)session; (void)frame; (void)diagnostic;
    return nullptr;
#endif
}

std::shared_ptr<NativeVideoFrameDecoder::Session>
NativeVideoFrameDecoder::open_session_locked(const std::string& path,
                                             DecodeDiagnostic* diagnostic) {
    auto it = m_sessions.find(path);
    if (it != m_sessions.end()) return it->second;
    auto session = std::make_shared<Session>();
    AVFormatContext* fmt = nullptr;
    const auto t_open = profiling::now();
    const int open_result = avformat_open_input(&fmt, path.c_str(), nullptr, nullptr);
    if (open_result != 0) {
        set_decode_diagnostic(diagnostic, DecodeFailureReason::OpenInput, open_result,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0, "failed to open media input");
        spdlog::error("[native-decoder] open input failed: {}", path); return nullptr;
    }
    session->profiling.container_open_ms = profiling::duration_ms(t_open, profiling::now());
    session->fmt = fmt;
    const auto t_probe = profiling::now();
    const int stream_info_result = avformat_find_stream_info(fmt, nullptr);
    if (stream_info_result < 0) {
        set_decode_diagnostic(diagnostic, DecodeFailureReason::StreamInfo, stream_info_result,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "failed to read media stream information");
        spdlog::error("[native-decoder] stream info failed: {}", path); return nullptr;
    }
    session->profiling.stream_probe_ms = profiling::duration_ms(t_probe, profiling::now());
    const int index = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (index < 0) {
        set_decode_diagnostic(diagnostic, DecodeFailureReason::NoVideoStream, index,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "media input has no decodable video stream");
        spdlog::error("[native-decoder] no video stream: {}", path); return nullptr;
    }
    session->stream_index = index;
    if (!build_source_sample_table(fmt, index, session->sample_table)) {
        set_decode_diagnostic(diagnostic, DecodeFailureReason::StreamInfo, 0,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "source has no usable PTS sample table or is not seekable");
        spdlog::error("[native-decoder] source has no usable PTS sample table: {}", path);
        return nullptr;
    }

    const enum AVCodecID codec_id = fmt->streams[index]->codecpar->codec_id;
    const AVCodec* codec = nullptr;
    if (codec_id == AV_CODEC_ID_H264) codec = avcodec_find_decoder_by_name("h264_cuvid");
    else if (codec_id == AV_CODEC_ID_HEVC) codec = avcodec_find_decoder_by_name("hevc_cuvid");
    else if (codec_id == AV_CODEC_ID_AV1) codec = avcodec_find_decoder_by_name("av1_cuvid");
    if (!codec) codec = avcodec_find_decoder(codec_id);
    if (!codec) {
        set_decode_diagnostic(diagnostic, DecodeFailureReason::DecoderUnavailable, 0,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "no decoder is available for the selected video stream");
        spdlog::error("[native-decoder] decoder unavailable for: {}", path); return nullptr;
    }

    const auto t_dec = profiling::now();
    AVCodecContext* cc = avcodec_alloc_context3(codec);
    if (!cc) {
        set_decode_diagnostic(diagnostic, DecodeFailureReason::DecoderUnavailable, AVERROR(ENOMEM),
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "failed to allocate decoder context");
        return nullptr;
    }
    const int parameters_result = avcodec_parameters_to_context(cc, fmt->streams[index]->codecpar);
    if (parameters_result < 0) {
        set_decode_diagnostic(diagnostic, DecodeFailureReason::DecoderUnavailable, parameters_result,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "failed to initialize decoder parameters");
        avcodec_free_context(&cc); return nullptr;
    }
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    const bool uses_nvdec = codec->name && std::string_view(codec->name).ends_with("_cuvid");
    AVBufferRef* device = nullptr;
    if (uses_nvdec && m_video_runtime) device = m_video_runtime->ref_cuda_hwdevice();
    if (!uses_nvdec) {
        spdlog::debug("[native-decoder] using software decoder {} without CUDA hwdevice", codec->name ? codec->name : "unknown");
    } else if (device) {
        auto* av_device = reinterpret_cast<AVHWDeviceContext*>(device->data);
        auto* av_cuda = av_device ? reinterpret_cast<AVCUDADeviceContext*>(av_device->hwctx) : nullptr;
        if (!av_cuda || !m_video_runtime->context_matches(reinterpret_cast<std::uintptr_t>(av_cuda->cuda_ctx))) {
            set_decode_diagnostic(diagnostic, DecodeFailureReason::NativeSurfaceUnavailable, 0,
                kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
                "shared CUDA decoder device does not match the runtime device");
            spdlog::error("[native-decoder] FAIL_CLOSED: shared hwdevice context mismatch");
            av_buffer_unref(&device); avcodec_free_context(&cc); return nullptr;
        }
        cc->hw_device_ctx = av_buffer_ref(device);
        cc->get_format = select_cuda_format;
        cc->extra_hw_frames = 8;
        session->hw_device_ctx = device;
    } else {
        set_decode_diagnostic(diagnostic, DecodeFailureReason::NativeSurfaceUnavailable, 0,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "registry-owned CUDA decoder device is unavailable");
        spdlog::error("[native-decoder] FAIL_CLOSED: no registry-owned CUDA hwdevice for decoder");
        avcodec_free_context(&cc); return nullptr;
    }
#endif
    cc->thread_count = 0;
    const int codec_open_result = avcodec_open2(cc, codec, nullptr);
    if (codec_open_result < 0) {
        set_decode_diagnostic(diagnostic, DecodeFailureReason::DecoderUnavailable, codec_open_result,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0, "failed to open video decoder");
        spdlog::error("[native-decoder] codec open failed for {}", path); avcodec_free_context(&cc); return nullptr;
    }
    session->profiling.decoder_open_ms = profiling::duration_ms(t_dec, profiling::now());
    session->codec = cc;
    session->hw_transfer_frame = av_frame_alloc();
    session->decoded = av_frame_alloc();
    session->closest_frame = av_frame_alloc();
    session->packet = av_packet_alloc();
    if (!session->decoded || !session->packet) {
        set_decode_diagnostic(diagnostic, DecodeFailureReason::DecoderUnavailable, AVERROR(ENOMEM),
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "failed to allocate decoder frame or packet state");
        return nullptr;
    }
    session->test_options = m_test_options;
    const bool prefetch_enabled = m_test_options.enable_prefetch &&
        m_gpu_hot_path_mode != GpuHotPathMode::RequireDirectYuv;
    if (prefetch_enabled) session->start_prefetch_worker(this);
    else session->direct_prefetch_disabled = true;
    m_sessions[path] = session;
    return session;
}

void NativeVideoFrameDecoder::Session::start_prefetch_worker(NativeVideoFrameDecoder* decoder) {
    prefetch_worker = std::thread([this, decoder]() {
        auto& io_budget = global_media_io_budget();
        while (!prefetch_stop.load(std::memory_order_relaxed)) {
            int64_t target = -1;
            uint64_t generation = 0;
            {
                std::unique_lock lock(mutex);
                prefetch_cv.wait(lock, [this] { return prefetch_stop.load() ||
                    (prefetch_queue.size() < kPrefetchCapacity && prefetch_next >= 0); });
                if (prefetch_stop.load()) break;
                if (static_cast<std::size_t>(prefetch_next) >= sample_table.size()) {
                    prefetch_next = -1;
                    continue;
                }
                target = prefetch_next++;
                generation = prefetch_generation;
                prefetch_inflight = target;
            }

            const auto width = static_cast<std::uint32_t>(std::max(codec ? codec->width : 1, 1));
            const auto height = static_cast<std::uint32_t>(std::max(codec ? codec->height : 1, 1));
            const auto estimate = static_cast<std::uint64_t>(runtime::tight_surface_bytes(
                runtime::PixelFormat::Rgba32Float, width, height));

            auto reservation = io_budget.try_reserve_prefetch(estimate);
            if (!reservation) {
                {
                    std::lock_guard lock(mutex);
                    if (generation == prefetch_generation) {
                        if (estimate > io_budget.config().max_prefetch_bytes) prefetch_next = -1;
                        else prefetch_next = target;
                    }
                    if (prefetch_inflight == target) prefetch_inflight = -1;
                    prefetch_cv.notify_all();
                }
                io_budget.wait_for_change(std::chrono::milliseconds(5));
                continue;
            }

            auto read_permit = io_budget.try_acquire_prefetch_read();
            if (!read_permit) {
                {
                    std::lock_guard lock(mutex);
                    if (generation == prefetch_generation) prefetch_next = target;
                    if (prefetch_inflight == target) prefetch_inflight = -1;
                    prefetch_cv.notify_all();
                }
                io_budget.wait_for_change(std::chrono::milliseconds(5));
                continue;
            }

            auto decode_result = decoder->decode_frame_internal(*this, target);
            auto* decoded = std::get_if<DecodedFrame>(&decode_result);
            {
                std::lock_guard lock(mutex);
                if (decoded && decoded->framebuffer && generation == prefetch_generation &&
                    !prefetch_stop.load()) {
                    prefetch_queue.push_back(PrefetchedFrame{
                        target, std::move(decoded->framebuffer), std::move(reservation)});
                } else if (generation == prefetch_generation) {
                    prefetch_next = -1;
                }
                if (prefetch_inflight == target) prefetch_inflight = -1;
                prefetch_cv.notify_all();
            }
        }
    });
}

DecodeResult NativeVideoFrameDecoder::decode_frame_internal(
    Session& session, int64_t target) {
    std::lock_guard decode_lock(session.decode_mutex);
    if (target < 0 || static_cast<std::size_t>(target) >= session.sample_table.size()) {
        return DecodeFailure{DecodeDiagnostic{DecodeFailureReason::UnexpectedEof, 0,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "requested sample index is outside the source sample table"}};
    }

    const std::size_t target_index = static_cast<std::size_t>(target);
    const auto& target_sample = session.sample_table[target_index];
    const int64_t target_pts = target_sample.pts;
    if (!session.decoded || !session.packet || !session.codec || !session.fmt) {
        return DecodeFailure{DecodeDiagnostic{DecodeFailureReason::DecoderUnavailable, 0,
            target_sample.pts, target_sample.dts, target_sample.source_order,
            "decoder session is missing required native state"}};
    }

    const bool sequential = session.last_sample_index >= 0 &&
        session.sample_table.are_sequential(
            static_cast<std::size_t>(session.last_sample_index), target_index);

    std::size_t matches_remaining = 1;
    if (!sequential) {
        const std::size_t seek_index = session.sample_table.previous_keyframe(target_index).value_or(target_index);
        const auto& seek_sample = session.sample_table[seek_index];
        const int seek_result = av_seek_frame(
            session.fmt, session.stream_index, seek_sample.pts, AVSEEK_FLAG_BACKWARD);
        if (seek_result < 0) {
            spdlog::error("[native-decoder] PTS seek failed: sample={} pts={}", target, seek_sample.pts);
            return DecodeFailure{DecodeDiagnostic{DecodeFailureReason::SeekFailure, seek_result,
                seek_sample.pts, seek_sample.dts, seek_sample.source_order,
                "failed to seek to the keyframe preceding the requested PTS"}};
        }
        avformat_flush(session.fmt);
        avcodec_flush_buffers(session.codec);
        session.last_sample_index = -1;
        for (std::size_t i = seek_index; i < target_index; ++i) {
            const auto& sample = session.sample_table[i];
            if (sample.continuity_id == target_sample.continuity_id && sample.pts == target_pts) {
                ++matches_remaining;
            }
        }
    }

    av_frame_unref(session.decoded);
    av_packet_unref(session.packet);
    AVFrame* decoded = session.decoded;
    AVPacket* packet = session.packet;
    std::shared_ptr<Framebuffer> framebuffer;
    DecodeDiagnostic failure_diagnostic;
    bool fatal = false;

    auto fail = [&](DecodeFailureReason reason, int ffmpeg_error,
                    std::int64_t pts, std::int64_t dts,
                    std::uint64_t source_order, std::string message) {
        failure_diagnostic = DecodeDiagnostic{
            reason, ffmpeg_error, pts, dts, source_order, std::move(message)};
    };

    auto materialize = [&](AVFrame* frame) -> bool {
        DecodeDiagnostic native_diagnostic;
        if ((framebuffer = try_native_frame(session, frame, &native_diagnostic))) return true;
        if (session.capture_native_frame && session.captured_native_frame) return true;
        if (native_diagnostic.failed()) failure_diagnostic = std::move(native_diagnostic);
        if (m_gpu_hot_path_mode == GpuHotPathMode::RequireGpuNative ||
            m_gpu_hot_path_mode == GpuHotPathMode::RequireDirectYuv) {
            if (!failure_diagnostic.failed()) {
                fail(DecodeFailureReason::NativeSurfaceUnavailable, 0,
                    frame ? frame->pts : target_sample.pts,
                    frame ? frame->pkt_dts : target_sample.dts,
                    target_sample.source_order,
                    "exact PTS frame was decoded but no required GPU-native surface was available");
            }
            spdlog::error("[native-decoder] GPU_NATIVE_REQUIRED: exact PTS frame was not GPU-native");
            if (m_counters) m_counters->video_decode_native_fallback_frames.fetch_add(1, std::memory_order_relaxed);
            fatal = true;
            return true;
        }
        const AVFrame* render = frame;
        if (frame->format == AV_PIX_FMT_CUDA && session.hw_transfer_frame) {
            av_frame_unref(session.hw_transfer_frame);
            const auto xfer_start = profiling::now();
            const int transfer_result = av_hwframe_transfer_data(session.hw_transfer_frame, frame, 0);
            if (transfer_result < 0) {
                fail(DecodeFailureReason::NativeSurfaceUnavailable, transfer_result,
                    frame->pts, frame->pkt_dts, target_sample.source_order,
                    "failed to transfer the decoded hardware frame to CPU memory");
                return false;
            }
            render = session.hw_transfer_frame;
            const auto xfer_dur = static_cast<uint64_t>(profiling::duration_ms(xfer_start, profiling::now()));
            if (m_counters) {
                m_counters->video_decode_hw_transfer_wall_ms.fetch_add(xfer_dur, std::memory_order_relaxed);
                m_counters->hwframe_transfer_ms.fetch_add(xfer_dur, std::memory_order_relaxed);
                m_counters->video_decode_hw_transfer_frames.fetch_add(1, std::memory_order_relaxed);
                m_counters->hwframe_transfer_to_cpu_frames.fetch_add(1, std::memory_order_relaxed);
            }
        }
        framebuffer = frame_to_framebuffer(render, session.sws, session.rgba, m_counters,
                                           session.test_options.enable_swscale);
        if (!framebuffer) {
            fail(DecodeFailureReason::UnsupportedFormatChange, 0,
                render ? render->pts : target_sample.pts,
                render ? render->pkt_dts : target_sample.dts,
                target_sample.source_order,
                "decoded frame could not be converted into the render working format");
        }
        return static_cast<bool>(framebuffer);
    };

    auto drain_decoder = [&]() -> int {
        while (true) {
            const auto wait_start = profiling::now();
            const int receive = avcodec_receive_frame(session.codec, decoded);
            const auto wait_dur = profiling::duration_ms(wait_start, profiling::now());
            session.profiling.avcodec_receive_frame_ms += wait_dur;
            session.profiling.nvdec_wait_ms += wait_dur;
            if (m_counters) m_counters->decode_wait_ms.fetch_add(
                static_cast<uint64_t>(wait_dur), std::memory_order_relaxed);
            if (receive == AVERROR(EAGAIN) || receive == AVERROR_EOF) return 0;
            if (receive < 0) {
                fail(DecodeFailureReason::DecoderReceiveFailure, receive,
                    target_sample.pts, target_sample.dts, target_sample.source_order,
                    "video decoder failed while receiving a decoded frame");
                return -1;
            }

            const int64_t pts = decoded->best_effort_timestamp != AV_NOPTS_VALUE
                ? decoded->best_effort_timestamp : decoded->pts;
            if (pts == target_pts) {
                if (matches_remaining > 1) {
                    --matches_remaining;
                } else if (materialize(decoded)) {
                    return fatal ? -1 : 1;
                } else if (failure_diagnostic.failed()) {
                    return -1;
                }
            }
            av_frame_unref(decoded);
        }
    };

    if (sequential) {
        const int drained = drain_decoder();
        if (drained == 1) {
            session.last_sample_index = target;
            return DecodedFrame{std::move(framebuffer), target_sample.pts,
                target_sample.dts, target_sample.source_order};
        }
        if (drained < 0) return DecodeFailure{std::move(failure_diagnostic)};
    }

    bool eof = false;
    while (!fatal) {
        const auto read_start = profiling::now();
        const int read = av_read_frame(session.fmt, packet);
        session.profiling.demux_read_packet_ms += profiling::duration_ms(read_start, profiling::now());
        if (read < 0) {
            eof = read == AVERROR_EOF;
            if (eof) {
                const int flush_result = avcodec_send_packet(session.codec, nullptr);
                if (flush_result < 0 && flush_result != AVERROR_EOF && flush_result != AVERROR(EAGAIN)) {
                    fail(DecodeFailureReason::DecoderSubmitFailure, flush_result,
                        target_sample.pts, target_sample.dts, target_sample.source_order,
                        "video decoder rejected end-of-stream flush");
                    break;
                }
                const int drained = drain_decoder();
                if (drained == 1) break;
                if (drained < 0) break;
            } else {
                fail(DecodeFailureReason::CorruptPacket, read,
                    target_sample.pts, target_sample.dts, target_sample.source_order,
                    "demuxer failed while reading the source packet stream");
            }
            break;
        }
        if (packet->stream_index != session.stream_index) {
            av_packet_unref(packet);
            continue;
        }
        if ((packet->flags & AV_PKT_FLAG_CORRUPT) != 0) {
            fail(DecodeFailureReason::CorruptPacket, AVERROR_INVALIDDATA,
                packet->pts, packet->dts, target_sample.source_order,
                "demuxer marked a video packet as corrupt");
            av_packet_unref(packet);
            break;
        }

        const auto submit_start = profiling::now();
        const int send = avcodec_send_packet(session.codec, packet);
        const auto submit_dur = profiling::duration_ms(submit_start, profiling::now());
        session.profiling.avcodec_send_packet_ms += submit_dur;
        if (m_counters) m_counters->decode_submit_ms.fetch_add(
            static_cast<uint64_t>(submit_dur), std::memory_order_relaxed);
        const auto packet_pts = packet->pts;
        const auto packet_dts = packet->dts;
        av_packet_unref(packet);
        if (send < 0 && send != AVERROR(EAGAIN)) {
            fail(send == AVERROR_INVALIDDATA ? DecodeFailureReason::CorruptPacket
                                             : DecodeFailureReason::DecoderSubmitFailure,
                send, packet_pts, packet_dts, target_sample.source_order,
                "video decoder rejected a source packet");
            break;
        }

        const int drained = drain_decoder();
        if (drained == 1) break;
        if (drained < 0) break;
    }

    if (framebuffer || (session.capture_native_frame && session.captured_native_frame)) {
        session.last_sample_index = target;
        return DecodedFrame{std::move(framebuffer), target_sample.pts,
            target_sample.dts, target_sample.source_order};
    }
    if (!failure_diagnostic.failed()) {
        fail(DecodeFailureReason::UnexpectedEof, eof ? AVERROR_EOF : 0,
            target_sample.pts, target_sample.dts, target_sample.source_order,
            "source ended before the requested presentation sample was decoded");
    }
    if (session.capture_native_frame && !session.captured_native_frame) {
        spdlog::error("[native-decoder] exact sample={} FAILED: pts={} continuity={} eof={}",
                      target, target_pts, target_sample.continuity_id, eof);
    }
    return DecodeFailure{std::move(failure_diagnostic)};
}

DecodeResult NativeVideoFrameDecoder::decode_frame_at(
    const std::string& path, RationalTime presentation_time, int, int) {
    if (path.empty()) {
        return DecodeFailure{DecodeDiagnostic{DecodeFailureReason::OpenInput, 0,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "decode source path is empty"}};
    }

    DecodeDiagnostic open_diagnostic;
    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        session = open_session_locked(path, &open_diagnostic);
    }
    if (!session) {
        if (!open_diagnostic.failed()) {
            open_diagnostic = DecodeDiagnostic{DecodeFailureReason::DecoderUnavailable, 0,
                kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
                "decoder session could not be opened"};
        }
        return DecodeFailure{std::move(open_diagnostic)};
    }

    const auto lookup = session->sample_table.lookup(presentation_time);
    if (lookup.disposition == SampleLookupDisposition::AfterEnd) {
        return DecodeEndOfStream{presentation_time};
    }
    if (!lookup.found()) {
        return lookup_failure(lookup.disposition, presentation_time);
    }
    const auto selected = *lookup.sample_index;
    const int64_t target = static_cast<int64_t>(selected);
    const auto& sample = session->sample_table[selected];

    std::unique_lock lock(session->mutex);
    if (session->prefetch_inflight == target) {
        session->prefetch_cv.wait(lock, [&session, target] {
            return session->prefetch_inflight != target || session->prefetch_stop.load();
        });
    }
    if (session->test_options.enable_frame_cache) {
        if (auto cached = session->cache.get(target)) {
            return decoded_frame_from_sample(*cached, sample);
        }
    }
    while (!session->prefetch_queue.empty() && session->prefetch_queue.front().target < target) {
        session->prefetch_queue.pop_front();
    }
    if (!session->prefetch_queue.empty() && session->prefetch_queue.front().target == target) {
        auto framebuffer = std::move(session->prefetch_queue.front().framebuffer);
        session->prefetch_queue.pop_front();
        return decoded_frame_from_sample(std::move(framebuffer), sample);
    }
    session->prefetch_queue.clear();
    ++session->prefetch_generation;
    session->prefetch_next = selected + 1 < session->sample_table.size() ? target + 1 : -1;
    lock.unlock();

    auto io_read = global_media_io_budget().acquire_required_read();
    auto result = decode_frame_internal(*session, target);

    lock.lock();
    session->prefetch_cv.notify_all();
    if (auto* decoded = std::get_if<DecodedFrame>(&result)) {
        const bool native_surface = decoded->framebuffer &&
            decoded->framebuffer->surface_handle() != runtime::kInvalidRenderSurfaceHandle;
        if (!native_surface && decoded->framebuffer && session->test_options.enable_frame_cache) {
            session->cache.put(target, decoded->framebuffer);
        }
    }
    return result;
}

HwFrameRef NativeVideoFrameDecoder::decode_native_frame_at(
    const std::string& path, RationalTime presentation_time, int, int) {
    if (path.empty() || (!m_native_importer && !m_video_runtime)) return {};
    if (m_counters) m_counters->video_source_requested_frames.fetch_add(1, std::memory_order_relaxed);

    DecodeDiagnostic open_diagnostic;
    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        session = open_session_locked(path, &open_diagnostic);
    }
    if (!session) return {};

    auto lookup = session->sample_table.lookup(presentation_time);
    if (!lookup.found()) {
        if (lookup.disposition == SampleLookupDisposition::AfterEnd && !session->sample_table.empty()) {
            lookup.sample_index = session->sample_table.size() - 1;
        } else {
            return {};
        }
    }
    const int64_t target = static_cast<int64_t>(*lookup.sample_index);

    std::unique_lock state_lock(session->mutex);
    if (!session->direct_prefetch_disabled && session->prefetch_worker.joinable()) {
        session->prefetch_stop.store(true, std::memory_order_relaxed);
        session->prefetch_cv.notify_all();
        state_lock.unlock();
        session->prefetch_worker.join();
        state_lock.lock();
        session->direct_prefetch_disabled = true;
    }
    session->prefetch_queue.clear();
    ++session->prefetch_generation;
    session->prefetch_next = -1;
    session->prefetch_cv.notify_all();
    session->prefetch_cv.wait(state_lock, [&session] {
        return session->prefetch_inflight < 0 || session->prefetch_stop.load();
    });
    session->captured_native_frame.reset();
    session->capture_native_frame = true;
    state_lock.unlock();

    auto io_read = global_media_io_budget().acquire_required_read();
    const auto decode_start = profiling::now();
    const auto decode_result = decode_frame_internal(*session, target);
    const double decode_dur_ms = profiling::duration_ms(decode_start, profiling::now());
    if (m_counters) m_counters->video_decode_wall_ms.fetch_add(
        static_cast<std::uint64_t>(std::llround(decode_dur_ms)), std::memory_order_relaxed);
    (void)decode_result;

    state_lock.lock();
    session->capture_native_frame = false;
    auto result = std::move(session->captured_native_frame);
    if (result) {
        session->profiling.decoded_frames++;
        session->profiling.decode_total_ms += decode_dur_ms;
        session->profiling.frame_durations_ms.push_back(decode_dur_ms);
    }
    return result;
}

NativeVideoFrameDecoder::DecodeProfilingStats NativeVideoFrameDecoder::decode_profiling_stats() const {
    DecodeProfilingStats total;
    std::lock_guard lock(const_cast<std::mutex&>(m_mutex));
    for (const auto& [_, session] : m_sessions) {
        if (!session) continue;
        std::lock_guard s_lock(session->mutex);
        total.decoded_frames += session->profiling.decoded_frames;
        total.container_open_ms += session->profiling.container_open_ms;
        total.stream_probe_ms += session->profiling.stream_probe_ms;
        total.decoder_open_ms += session->profiling.decoder_open_ms;
        total.demux_read_packet_ms += session->profiling.demux_read_packet_ms;
        total.avcodec_send_packet_ms += session->profiling.avcodec_send_packet_ms;
        total.avcodec_receive_frame_ms += session->profiling.avcodec_receive_frame_ms;
        total.nvdec_wait_ms += session->profiling.nvdec_wait_ms;
        total.decode_total_ms += session->profiling.decode_total_ms;
        total.frame_durations_ms.insert(total.frame_durations_ms.end(),
            session->profiling.frame_durations_ms.begin(), session->profiling.frame_durations_ms.end());
    }
    return total;
}

} // namespace chronon3d::media

#endif
