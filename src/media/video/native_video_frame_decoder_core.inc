std::shared_ptr<Framebuffer> NativeVideoFrameDecoder::decode_frame_internal(
    Session& session, int64_t target) {
    std::lock_guard decode_lock(session.decode_mutex);
    if (target < 0 || static_cast<std::size_t>(target) >= session.sample_table.size()) return nullptr;
    if (!session.decoded || !session.packet) return nullptr;

    const std::size_t target_index = static_cast<std::size_t>(target);
    const auto& target_sample = session.sample_table[target_index];
    const int64_t target_pts = target_sample.pts;
    const bool sequential = session.last_sample_index >= 0 &&
        session.sample_table.are_sequential(
            static_cast<std::size_t>(session.last_sample_index), target_index);

    std::size_t matches_remaining = 1;
    if (!sequential) {
        const std::size_t seek_index = session.sample_table.previous_keyframe(target_index).value_or(target_index);
        const auto& seek_sample = session.sample_table[seek_index];
        if (av_seek_frame(session.fmt, session.stream_index, seek_sample.pts, AVSEEK_FLAG_BACKWARD) < 0) {
            spdlog::error("[native-decoder] PTS seek failed: sample={} pts={}", target, seek_sample.pts);
            return nullptr;
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
    std::shared_ptr<Framebuffer> result;
    bool fatal = false;

    auto materialize = [&](AVFrame* frame) -> bool {
        if ((result = try_native_frame(session, frame))) return true;
        if (session.capture_native_frame && session.captured_native_frame) return true;
        if (m_gpu_hot_path_mode == GpuHotPathMode::RequireGpuNative ||
            m_gpu_hot_path_mode == GpuHotPathMode::RequireDirectYuv) {
            spdlog::error("[native-decoder] GPU_NATIVE_REQUIRED: exact PTS frame was not GPU-native");
            if (m_counters) m_counters->video_decode_native_fallback_frames.fetch_add(1, std::memory_order_relaxed);
            fatal = true;
            return true;
        }
        const AVFrame* render = frame;
        if (frame->format == AV_PIX_FMT_CUDA && session.hw_transfer_frame) {
            av_frame_unref(session.hw_transfer_frame);
            const auto xfer_start = profiling::now();
            if (av_hwframe_transfer_data(session.hw_transfer_frame, frame, 0) < 0) return false;
            render = session.hw_transfer_frame;
            const auto xfer_dur = static_cast<uint64_t>(profiling::duration_ms(xfer_start, profiling::now()));
            if (m_counters) {
                m_counters->video_decode_hw_transfer_wall_ms.fetch_add(xfer_dur, std::memory_order_relaxed);
                m_counters->hwframe_transfer_ms.fetch_add(xfer_dur, std::memory_order_relaxed);
                m_counters->video_decode_hw_transfer_frames.fetch_add(1, std::memory_order_relaxed);
                m_counters->hwframe_transfer_to_cpu_frames.fetch_add(1, std::memory_order_relaxed);
            }
        }
        result = frame_to_framebuffer(render, session.sws, session.rgba, m_counters,
                                      session.test_options.enable_swscale);
        return static_cast<bool>(result);
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
            if (receive < 0) return -1;

            const int64_t pts = decoded->best_effort_timestamp != AV_NOPTS_VALUE
                ? decoded->best_effort_timestamp : decoded->pts;
            if (pts == target_pts) {
                if (matches_remaining > 1) {
                    --matches_remaining;
                } else if (materialize(decoded)) {
                    return fatal ? -1 : 1;
                }
            }
            av_frame_unref(decoded);
        }
    };

    // A previous exact request may have stopped while the codec still had
    // presentation frames buffered. Drain those before submitting more input.
    if (sequential) {
        const int drained = drain_decoder();
        if (drained == 1) {
            session.last_sample_index = target;
            return result;
        }
        if (drained < 0 && fatal) return nullptr;
    }

    bool eof = false;
    while (!fatal) {
        const auto read_start = profiling::now();
        const int read = av_read_frame(session.fmt, packet);
        session.profiling.demux_read_packet_ms += profiling::duration_ms(read_start, profiling::now());
        if (read < 0) {
            eof = read == AVERROR_EOF;
            if (eof) {
                (void)avcodec_send_packet(session.codec, nullptr);
                const int drained = drain_decoder();
                if (drained == 1) break;
            }
            break;
        }
        if (packet->stream_index != session.stream_index) {
            av_packet_unref(packet);
            continue;
        }

        const auto submit_start = profiling::now();
        const int send = avcodec_send_packet(session.codec, packet);
        const auto submit_dur = profiling::duration_ms(submit_start, profiling::now());
        session.profiling.avcodec_send_packet_ms += submit_dur;
        if (m_counters) m_counters->decode_submit_ms.fetch_add(
            static_cast<uint64_t>(submit_dur), std::memory_order_relaxed);
        av_packet_unref(packet);
        if (send < 0 && send != AVERROR(EAGAIN)) break;

        const int drained = drain_decoder();
        if (drained == 1) break;
        if (drained < 0) break;
    }

    if (result || (session.capture_native_frame && session.captured_native_frame)) {
        session.last_sample_index = target;
    }
    if (!result && session.capture_native_frame && !session.captured_native_frame) {
        spdlog::error("[native-decoder] exact sample={} FAILED: pts={} continuity={} eof={}",
                      target, target_pts, target_sample.continuity_id, eof);
    }
    return result;
}
