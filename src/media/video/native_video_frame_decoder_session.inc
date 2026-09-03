namespace {

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
    Session& session, AVFrame* frame) {
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    profiling::ProfilingGuard profiling_guard(m_counters, nullptr);
    if (!frame || frame->format != AV_PIX_FMT_CUDA) return nullptr;
    if (session.capture_native_frame) {
        AVFrame* captured = av_frame_alloc();
        const int ref_result = captured ? av_frame_ref(captured, frame) : AVERROR(ENOMEM);
        if (!captured || ref_result < 0) {
            if (captured) av_frame_free(&captured);
            publish_decode_failure(DecodeFailureReason::NativeSurfaceUnavailable,
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
                publish_decode_failure(DecodeFailureReason::UnsupportedFormatChange, 0,
                    frame->pts, frame->pkt_dts, 0,
                    "native decoded frame changed to an unsupported pixel format");
                return nullptr;
            }
        }
    }
    if (!session.native_import_session) session.native_import_session = m_native_importer->create_session();
    if (!session.native_import_session) {
        publish_decode_failure(DecodeFailureReason::NativeSurfaceUnavailable, 0,
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
        publish_decode_failure(DecodeFailureReason::NativeSurfaceUnavailable, 0,
            frame->pts, frame->pkt_dts, 0,
            "native frame importer rejected the decoded surface");
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
    const int open_result = avformat_open_input(&fmt, path.c_str(), nullptr, nullptr);
    if (open_result != 0) {
        publish_decode_failure(DecodeFailureReason::OpenInput, open_result,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "failed to open media input");
        spdlog::error("[native-decoder] open input failed: {}", path); return nullptr;
    }
    session->profiling.container_open_ms = profiling::duration_ms(t_open, profiling::now());
    session->fmt = fmt;
    const auto t_probe = profiling::now();
    const int stream_info_result = avformat_find_stream_info(fmt, nullptr);
    if (stream_info_result < 0) {
        publish_decode_failure(DecodeFailureReason::StreamInfo, stream_info_result,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "failed to read media stream information");
        spdlog::error("[native-decoder] stream info failed: {}", path); return nullptr;
    }
    session->profiling.stream_probe_ms = profiling::duration_ms(t_probe, profiling::now());
    const int index = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (index < 0) {
        publish_decode_failure(DecodeFailureReason::NoVideoStream, index,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "media input has no decodable video stream");
        spdlog::error("[native-decoder] no video stream: {}", path); return nullptr;
    }
    session->stream_index = index;
    if (!build_source_sample_table(fmt, index, session->sample_table)) {
        publish_decode_failure(DecodeFailureReason::StreamInfo, 0,
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
        publish_decode_failure(DecodeFailureReason::DecoderUnavailable, 0,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "no decoder is available for the selected video stream");
        spdlog::error("[native-decoder] decoder unavailable for: {}", path); return nullptr;
    }

    const auto t_dec = profiling::now();
    AVCodecContext* cc = avcodec_alloc_context3(codec);
    if (!cc) {
        publish_decode_failure(DecodeFailureReason::DecoderUnavailable, AVERROR(ENOMEM),
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "failed to allocate decoder context");
        return nullptr;
    }
    const int parameters_result = avcodec_parameters_to_context(cc, fmt->streams[index]->codecpar);
    if (parameters_result < 0) {
        publish_decode_failure(DecodeFailureReason::DecoderUnavailable, parameters_result,
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
            publish_decode_failure(DecodeFailureReason::NativeSurfaceUnavailable, 0,
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
        publish_decode_failure(DecodeFailureReason::NativeSurfaceUnavailable, 0,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "registry-owned CUDA decoder device is unavailable");
        spdlog::error("[native-decoder] FAIL_CLOSED: no registry-owned CUDA hwdevice for decoder");
        avcodec_free_context(&cc); return nullptr;
    }
#endif
    cc->thread_count = 0;
    const int codec_open_result = avcodec_open2(cc, codec, nullptr);
    if (codec_open_result < 0) {
        publish_decode_failure(DecodeFailureReason::DecoderUnavailable, codec_open_result,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "failed to open video decoder");
        spdlog::error("[native-decoder] codec open failed for {}", path); avcodec_free_context(&cc); return nullptr;
    }
    session->profiling.decoder_open_ms = profiling::duration_ms(t_dec, profiling::now());
    session->codec = cc;
    session->hw_transfer_frame = av_frame_alloc();
    session->decoded = av_frame_alloc();
    session->closest_frame = av_frame_alloc();
    session->packet = av_packet_alloc();
    if (!session->decoded || !session->packet) {
        publish_decode_failure(DecodeFailureReason::DecoderUnavailable, AVERROR(ENOMEM),
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "failed to allocate decoder frame or packet state");
        return nullptr;
    }
    session->test_options = m_test_options;
    const bool prefetch_enabled = m_test_options.enable_prefetch && m_gpu_hot_path_mode != GpuHotPathMode::RequireDirectYuv;
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
                        if (estimate > io_budget.config().max_prefetch_bytes) {
                            prefetch_next = -1;
                        } else {
                            prefetch_next = target;
                        }
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

            auto fb = decoder->decode_frame_internal(*this, target);
            {
                std::lock_guard lock(mutex);
                if (fb && generation == prefetch_generation && !prefetch_stop.load()) {
                    prefetch_queue.push_back(PrefetchedFrame{
                        target, std::move(fb), std::move(reservation)});
                } else if (generation == prefetch_generation) {
                    prefetch_next = -1;
                }
                if (prefetch_inflight == target) prefetch_inflight = -1;
                prefetch_cv.notify_all();
            }
        }
    });
}