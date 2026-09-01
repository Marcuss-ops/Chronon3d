std::shared_ptr<Framebuffer> NativeVideoFrameDecoder::decode_frame_internal(Session& session, int64_t target) {
    std::lock_guard decode_lock(session.decode_mutex);
    const AVStream* st = session.fmt->streams[session.stream_index];
    AVRational fps = (st->r_frame_rate.num > 0 && st->r_frame_rate.den > 0)
        ? st->r_frame_rate
        : st->avg_frame_rate;
    AVRational duration = av_inv_q(fps);
    if (duration.num <= 0 || duration.den <= 0) duration = {1, 30};
    const int64_t target_pts = av_rescale_q(target, duration, st->time_base);
    const int64_t frame_tick = av_rescale_q(1, duration, st->time_base);
    const int64_t pts_tolerance = frame_tick > 0 ? (frame_tick / 2) : 1;
    if (session.source_eof && target >= session.eof_target) {
        if (session.capture_native_frame && session.eof_captured_native_frame) {
            session.captured_native_frame = session.eof_captured_native_frame;
            return nullptr;
        }
        if (session.eof_frame) return session.eof_frame;
    }
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
        const auto read_start = profiling::now();
        const int read = av_read_frame(session.fmt, packet);
        session.profiling.demux_read_packet_ms += profiling::duration_ms(read_start, profiling::now());
        if (read < 0) {
            eof = (read == AVERROR_EOF);
            if (eof && session.codec) {
                (void)avcodec_send_packet(session.codec, nullptr);
                while (avcodec_receive_frame(session.codec, decoded) == 0) {
                    const int64_t pts = decoded->best_effort_timestamp != AV_NOPTS_VALUE ? decoded->best_effort_timestamp : decoded->pts;
                    if (pts == target_pts || std::llabs(pts - target_pts) <= pts_tolerance) {
                        result = try_native_frame(session, decoded);
                        if (result) break;
                        if (session.capture_native_frame && session.captured_native_frame) break;
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
        const auto submit_dur = profiling::duration_ms(submit_start, profiling::now());
        session.profiling.avcodec_send_packet_ms += submit_dur;
        if (m_counters) m_counters->decode_submit_ms.fetch_add(static_cast<uint64_t>(submit_dur), std::memory_order_relaxed);
        av_packet_unref(packet);
        if (send < 0) break;
        int receive = 0;
        const auto wait_start = profiling::now();
        while ((receive = avcodec_receive_frame(session.codec, decoded)) == 0) {
            const auto wait_dur = profiling::duration_ms(wait_start, profiling::now());
            session.profiling.avcodec_receive_frame_ms += wait_dur;
            session.profiling.nvdec_wait_ms += wait_dur;
            if (m_counters) m_counters->decode_wait_ms.fetch_add(static_cast<uint64_t>(wait_dur), std::memory_order_relaxed);
            const int64_t pts = decoded->best_effort_timestamp != AV_NOPTS_VALUE ? decoded->best_effort_timestamp : decoded->pts;
            const bool match = (pts == target_pts) ||
                               (std::llabs(pts - target_pts) <= pts_tolerance) ||
                               (session.last_target >= 0 && target == session.last_target + 1);
            if (match) {
                if ((result = try_native_frame(session, decoded))) break;
                if (session.capture_native_frame && session.captured_native_frame) break;
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
                result = frame_to_framebuffer(render, session.sws, session.rgba, m_counters, session.test_options.enable_swscale); break;
            }
            if (pts != AV_NOPTS_VALUE) {
                const int64_t delta = std::llabs(pts - target_pts);
                if (delta < closest_delta) { closest_delta = delta; av_frame_unref(closest); av_frame_ref(closest, decoded); }
                if (pts > target_pts && ++overshoot >= 16) break;
            }
        }
        if (result || overshoot >= 16) break;
        if (session.capture_native_frame && session.captured_native_frame) break;
        if (receive < 0 && receive != AVERROR(EAGAIN)) break;
    }
    if (!result && closest->width > 0 && closest->height > 0) {
        if (closest->format == AV_PIX_FMT_CUDA) {
            if (!(result = try_native_frame(session, closest))) {
                if (session.capture_native_frame && session.captured_native_frame) {
                    result.reset();
                } else if (m_gpu_hot_path_mode == GpuHotPathMode::RequireGpuNative ||
                           m_gpu_hot_path_mode == GpuHotPathMode::RequireDirectYuv) {
                    spdlog::error("[native-decoder] GPU_NATIVE_REQUIRED: try_native_frame failed for closest CUDA frame, CPU fallback forbidden");
                    if (m_counters) {
                        m_counters->video_decode_native_fallback_frames.fetch_add(1, std::memory_order_relaxed);
                    }
                    return nullptr;
                } else if (session.hw_transfer_frame) {
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
                        result = frame_to_framebuffer(session.hw_transfer_frame, session.sws, session.rgba, m_counters, session.test_options.enable_swscale);
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
            result = frame_to_framebuffer(closest, session.sws, session.rgba, m_counters, session.test_options.enable_swscale);
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
    // Direct-YUV returns the decoded CUDA frame through the capture handoff
    // rather than through `result`.  Preserve the sequential decoder state
    // in that mode as well; otherwise every direct request looks like the
    // first request and may seek back to a keyframe, decoding/discarding
    // intermediate surfaces repeatedly.
    if (!result && session.capture_native_frame && session.captured_native_frame) {
        session.last_target = target;
        session.eof_captured_native_frame = session.captured_native_frame;
        if (eof) {
            session.source_eof = true;
            session.eof_target = target;
        }
    } else if (!session.captured_native_frame && eof && session.eof_captured_native_frame && session.capture_native_frame) {
        session.captured_native_frame = session.eof_captured_native_frame;
        session.source_eof = true;
        session.eof_target = session.last_target;
        session.last_target = target;
    }
    if (!result && session.capture_native_frame && !session.captured_native_frame) {
        spdlog::error("[native-decoder] target={} FAILED: target_pts={} last_target={} closest_pts={}",
                      target, target_pts, session.last_target, closest ? closest->pts : -1);
    }
    return result;
}
