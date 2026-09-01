NativeVideoFrameDecoder::~NativeVideoFrameDecoder() = default;
NativeVideoFrameDecoder::Session::~Session() {
    spdlog::info("[native-decoder] stopping prefetch worker");
    prefetch_stop.store(true, std::memory_order_relaxed);
    prefetch_cv.notify_all();
    if (prefetch_worker.joinable()) prefetch_worker.join();
    spdlog::info("[native-decoder] prefetch worker stopped");

    // Release every AVFrame/cache object which can retain an AVBufferRef to
    // the decoder's hw frames before tearing down the codec hw device.  These
    // are members, so their implicit destruction would otherwise happen
    // *after* this destructor body and after av_buffer_unref(hw_device_ctx),
    // leaving FFmpeg's buffer callbacks to run against a dead CUDA device.
    // That ordering caused the intermittent malloc corruption observed when
    // independent decoder sessions were destroyed concurrently.
    prefetch_queue.clear();
    cache.clear();
    eof_frame.reset();
    captured_native_frame.reset();
    eof_captured_native_frame.reset();
    native_import_session.reset();
    spdlog::info("[native-decoder] session buffers released");
    av_frame_free(&decoded);
    av_frame_free(&closest_frame);
    av_packet_free(&packet);
    av_frame_free(&hw_transfer_frame);
    spdlog::info("[native-decoder] session AV frames released");
    if (sws) sws_freeContext(sws);
    spdlog::info("[native-decoder] session sws released");
    avcodec_free_context(&codec);
    spdlog::info("[native-decoder] session codec released");
    av_buffer_unref(&hw_device_ctx);
    if (fmt) avformat_close_input(&fmt);
}

std::shared_ptr<Framebuffer> NativeVideoFrameDecoder::try_native_frame(
    Session& session, AVFrame* frame) {
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    profiling::ProfilingGuard profiling_guard(m_counters, nullptr);
    if (!frame || frame->format != AV_PIX_FMT_CUDA) return nullptr;
    if (session.capture_native_frame) {
        AVFrame* captured = av_frame_alloc();
        if (!captured || av_frame_ref(captured, frame) < 0) {
            if (captured) av_frame_free(&captured);
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
    const auto t_open = profiling::now();
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) != 0) {
        spdlog::error("[native-decoder] open input failed: {}", path);
        return nullptr;
    }
    session->profiling.container_open_ms = profiling::duration_ms(t_open, profiling::now());
    session->fmt = fmt;
    const auto t_probe = profiling::now();
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        spdlog::error("[native-decoder] stream info failed: {}", path);
        return nullptr;
    }
    session->profiling.stream_probe_ms = profiling::duration_ms(t_probe, profiling::now());
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
    const auto t_dec = profiling::now();
    AVCodecContext* cc = avcodec_alloc_context3(codec);
    if (!cc || avcodec_parameters_to_context(cc, fmt->streams[index]->codecpar) < 0) {
        avcodec_free_context(&cc); return nullptr;
    }

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    const bool uses_nvdec = codec->name &&
        std::string_view(codec->name).ends_with("_cuvid");
    AVBufferRef* device = nullptr;
    if (uses_nvdec && m_video_runtime) {
        // Bind NVDEC to the shared video device runtime: same FFmpeg
        // hwdevice, same primary CUDA context as NVENC.
        device = m_video_runtime->ref_cuda_hwdevice();
        if (device) {
            spdlog::info("[native-decoder] bound NVDEC to the shared video device runtime (primary CUDA context)");
        } else {
            spdlog::error("[native-decoder] FAIL_CLOSED: video device runtime has no shared CUDA hwdevice");
        }
    }
    // A selected NVDEC decoder must be bound to the registry-owned
    // VideoDeviceRuntime; unavailable shared hardware is fail-closed.
    if (!uses_nvdec) {
        spdlog::debug("[native-decoder] using software decoder {} without CUDA hwdevice",
                      codec->name ? codec->name : "unknown");
    } else if (device) {
        auto* av_device = reinterpret_cast<AVHWDeviceContext*>(device->data);
        auto* av_cuda = av_device
            ? reinterpret_cast<AVCUDADeviceContext*>(av_device->hwctx) : nullptr;
        if (!av_cuda || !m_video_runtime->context_matches(
                reinterpret_cast<std::uintptr_t>(av_cuda->cuda_ctx))) {
            spdlog::error("[native-decoder] FAIL_CLOSED: shared hwdevice context mismatch");
            av_buffer_unref(&device);
            avcodec_free_context(&cc);
            return nullptr;
        }
        cc->hw_device_ctx = av_buffer_ref(device);
        cc->get_format = select_cuda_format;
        cc->extra_hw_frames = 8;
        session->hw_device_ctx = device;
    } else {
        spdlog::error("[native-decoder] FAIL_CLOSED: no registry-owned CUDA hwdevice for decoder");
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
    session->profiling.decoder_open_ms = profiling::duration_ms(t_dec, profiling::now());
    session->codec = cc;
    session->hw_transfer_frame = av_frame_alloc();
    session->decoded = av_frame_alloc();
    session->closest_frame = av_frame_alloc();
    session->packet = av_packet_alloc();
    session->test_options = m_test_options;
    // Direct-YUV owns the sequential codec access.  Starting the ordinary
    // framebuffer prefetch worker here would race the native-frame capture
    // path on the same AVCodecContext.  The stress-harness prefetch flag
    // (NativeDecoderTestOptions::enable_prefetch) additionally disables the
    // worker so a corruption source can be bisected independently of the
    // GPU hot-path policy.
    const bool prefetch_enabled =
        m_test_options.enable_prefetch &&
        m_gpu_hot_path_mode != GpuHotPathMode::RequireDirectYuv;
    if (prefetch_enabled) {
        session->start_prefetch_worker(this);
    } else {
        session->direct_prefetch_disabled = true;
    }
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
