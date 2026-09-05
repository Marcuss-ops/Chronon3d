// native_video_frame_decoder_session.cpp — NativeVideoFrameDecoder session
// lifecycle: container/decoder open, hardware device binding and the native
// GPU surface capture/import path.

#include "native_video_frame_decoder_detail.hpp"

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG

namespace chronon3d::media {

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

} // namespace chronon3d::media

#endif
