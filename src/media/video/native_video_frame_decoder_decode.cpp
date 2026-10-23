// native_video_frame_decoder_decode.cpp — NativeVideoFrameDecoder decode
// engine: sequential/seek decode loop, prefetch worker and frame lookup.

#include "native_video_frame_decoder_detail.hpp"
#ifdef CHRONON3D_ENABLE_OCIO
#include <chronon3d/color/ocio_color_transform.hpp>
#endif

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG

namespace chronon3d::media {

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

            // Demand decode has priority over speculative work. The flag is
            // published while the foreground thread still owns session::mutex
            // below, so a prefetch task that has not entered FFmpeg yet can
            // give the decoder back immediately instead of taking the decode
            // mutex ahead of the requested frame.
            if (foreground_decode_pending.load(std::memory_order_acquire)) {
                std::lock_guard lock(mutex);
                if (generation == prefetch_generation) prefetch_next = target;
                if (prefetch_inflight == target) prefetch_inflight = -1;
                prefetch_cv.notify_all();
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
    Session& session, int64_t target
#ifdef CHRONON3D_ENABLE_OCIO
    , const color::OcioColorTransform* ocio_transform
#endif
    ) {
    std::lock_guard decode_lock(session.decode_mutex);
    if (target < 0 || static_cast<std::size_t>(target) >= session.sample_table.size()) {
        return make_decode_failure(DecodeDiagnostic{DecodeFailureReason::UnexpectedEof, 0,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "requested sample index is outside the source sample table"});
    }

    const std::size_t target_index = static_cast<std::size_t>(target);
    const auto& target_sample = session.sample_table[target_index];
    const int64_t target_pts = target_sample.pts;
    if (!session.decoded || !session.packet || !session.codec || !session.fmt) {
        return make_decode_failure(DecodeDiagnostic{DecodeFailureReason::DecoderUnavailable, 0,
            target_sample.pts, target_sample.dts, target_sample.source_order,
            "decoder session is missing required native state"});
    }

    const std::size_t last_index = session.last_sample_index >= 0
        ? static_cast<std::size_t>(session.last_sample_index) : 0;
    const bool same_continuity = session.last_sample_index >= 0 &&
        session.sample_table[last_index].continuity_id == target_sample.continuity_id;
    const bool sequential = same_continuity &&
        session.sample_table.are_sequential(last_index, target_index);
    // A small forward gap (a skipped source frame, e.g. 30fps-on-24fps
    // mapping steps +2 every few frames) is cheaper to satisfy by decoding
    // forward through the gap than by seeking back to the previous keyframe
    // and re-decoding the GOP from scratch.
    constexpr std::size_t kMaxForwardDecodeFrames = 64;
    const bool forward_gap = same_continuity &&
        target_index > last_index &&
        (target_index - last_index) <= kMaxForwardDecodeFrames;

    // Requests that fall inside the recently-decoded retained window (a
    // duplicate sample index from FPS mapping, or a slightly-out-of-order
    // request when two layers share one source path) are replayed from the
    // retained decoded frame instead of flushing the decoder and re-decoding
    // the whole GOP from its keyframe — the previous behaviour cost a full
    // keyframe re-decode (~100-200ms) for every repeated/regressed index.
    AVFrame* window_frame = nullptr;
    if (same_continuity) {
        for (const auto& retained : session.retained_frames) {
            if (retained.index == target_index) {
                window_frame = retained.frame;
                break;
            }
        }
    }
    if (window_frame
#ifdef CHRONON3D_ENABLE_OCIO
        && ocio_transform == nullptr
#endif
        ) {
        av_frame_unref(session.decoded);
        DecodeDiagnostic replay_diagnostic;
        std::shared_ptr<Framebuffer> replayed;
        const bool ok = (replayed = try_native_frame(
                             session, window_frame, &replay_diagnostic)) ||
            (session.capture_native_frame && session.captured_native_frame);
        if (ok) {
            // Satisfied from the retained window: no keyframe seek, no GOP
            // re-decode. Exposed via decode_profiling_stats() so regression
            // tests can assert deterministically that a repeated or
            // slightly-out-of-order request was replayed rather than
            // re-decoded.
            session.profiling.retained_replays++;
            return DecodedFrame{std::move(replayed), target_sample.pts,
                target_sample.dts, target_sample.source_order};
        }
        // The retained frame is no longer usable (surface retired); fall
        // through to the seek path and decode the sample again for real.
        session.captured_native_frame.reset();
    }

    std::size_t matches_remaining = 1;
    if (!sequential && !forward_gap) {
        const std::size_t seek_index = session.sample_table.previous_keyframe(target_index).value_or(target_index);
        const auto& seek_sample = session.sample_table[seek_index];
        const int seek_result = av_seek_frame(
            session.fmt, session.stream_index, seek_sample.pts, AVSEEK_FLAG_BACKWARD);
        if (seek_result < 0) {
            spdlog::error("[native-decoder] PTS seek failed: sample={} pts={}", target, seek_sample.pts);
            return make_decode_failure(DecodeDiagnostic{DecodeFailureReason::SeekFailure, seek_result,
                seek_sample.pts, seek_sample.dts, seek_sample.source_order,
                "failed to seek to the keyframe preceding the requested PTS"});
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
        if (
#ifdef CHRONON3D_ENABLE_OCIO
            ocio_transform == nullptr &&
#endif
            (framebuffer = try_native_frame(session, frame, &native_diagnostic))) {
            retain_materialized_frame(session, target_index, frame);
            return true;
        }
        if (session.capture_native_frame && session.captured_native_frame) {
            retain_materialized_frame(session, target_index, frame);
            return true;
        }
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
        framebuffer = frame_to_framebuffer(render, session.sws, session.rgba_staging,
                                           m_counters,
                                           session.test_options.enable_swscale
#ifdef CHRONON3D_ENABLE_OCIO
                                           , ocio_transform
#endif
                                           );
        if (!framebuffer) {
            fail(DecodeFailureReason::UnsupportedFormatChange, 0,
                render ? render->pts : target_sample.pts,
                render ? render->pkt_dts : target_sample.dts,
                target_sample.source_order,
                "decoded frame could not be converted into the render working format");
            return false;
        }
        retain_materialized_frame(session, target_index, frame);
        return true;
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
        if (drained < 0) return make_decode_failure(std::move(failure_diagnostic));
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
    return make_decode_failure(std::move(failure_diagnostic));
}

DecodeResult NativeVideoFrameDecoder::decode_frame_index_at(
    const std::string& path, std::int64_t sample_index, int width, int height) {
    if (path.empty() || sample_index < 0) {
        return make_decode_failure(DecodeDiagnostic{DecodeFailureReason::OpenInput, 0,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "decode source path and non-negative source sample index are required"});
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
        return make_decode_failure(std::move(open_diagnostic));
    }
    if (static_cast<std::uint64_t>(sample_index) >= session->sample_table.size()) {
        return make_decode_failure(DecodeDiagnostic{DecodeFailureReason::UnexpectedEof, 0,
            kNoDecodeTimestamp, kNoDecodeTimestamp,
            static_cast<std::uint64_t>(sample_index),
            "source sample index is beyond the indexed video frames"});
    }
    const auto sample = session->sample_table[static_cast<std::size_t>(sample_index)];
    const RationalTime presentation_time{sample.pts, session->sample_table.time_base()};
    auto decoded = decode_frame_at(path, presentation_time, width, height);
    if (const auto* frame = decoded_frame_if(decoded);
        frame != nullptr && frame->source_order != sample.source_order) {
        return make_decode_failure(DecodeDiagnostic{DecodeFailureReason::PresentationGap, 0,
            sample.pts, sample.dts, sample.source_order,
            "duplicate presentation timestamps prevent exact source-order frame selection"});
    }
    return decoded;
}


} // namespace chronon3d::media

#endif
