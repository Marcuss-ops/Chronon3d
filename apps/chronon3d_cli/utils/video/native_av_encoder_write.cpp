#include "native_av_encoder.hpp"
#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <chrono>
#include <optional>
#include <algorithm>

namespace chronon3d::cli {

namespace {
using Clock = std::chrono::steady_clock;
inline double elapsed_ms(const Clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
// Enough retries to match x264's internal frame queue depth (see the send
// loops): with threads=auto + thread_type=frame the encoder keeps ~10-12
// frames in flight before drain_packets() frees a slot.
constexpr int kMaxEagainRetries = 12;

} // anonymous namespace

// Map FfmpegPipeOptions to the encoder pixel format.
static video::EncoderPixelFormat resolve_encoder_pix_fmt(const FfmpegPipeOptions& opt) {
    return video::EncoderPixelFormat::YUV420P;
}

// Map FfmpegPipeOptions color space to the color matrix ID.
static int resolve_color_matrix(const FfmpegPipeOptions& opt) {
    switch (opt.color_transform.output) {
        case chronon3d::color::ColorSpace::Rec709:
            return static_cast<int>(video::YuvMatrix::BT709);
        case chronon3d::color::ColorSpace::PQ:
        case chronon3d::color::ColorSpace::HLG:
            return static_cast<int>(video::YuvMatrix::BT2020);
        case chronon3d::color::ColorSpace::SRGB:
        case chronon3d::color::ColorSpace::LinearSRGB:
        default:
            return static_cast<int>(video::YuvMatrix::BT709);
    }
}

bool NativeAvEncoder::write_frame_async(const Framebuffer& fb, std::shared_ptr<Framebuffer> owner) {
    (void)owner;
    return write_frame(fb);
}

bool NativeAvEncoder::write_frame(const Framebuffer& fb) {
    // P1 post-run resync: after a duplicate run the decoder's DPB holds the
    // run anchor's reconstruction while the encoder's DPB holds the last
    // seed's. The first real frame after a run is forced intra so both
    // reference chains resynchronize exactly.
    const bool force_intra = duplicate_run_active_ || session_restart_intra_pending_;
    duplicate_run_active_ = false;
    return write_frame_impl(fb, force_intra);
}

bool NativeAvEncoder::write_frame_impl(const Framebuffer& fb, bool force_intra) {
    if (!codec_ || !frame_ || !packet_) {
        return false;
    }

    // Z1 typed-feed guard: host RGBA frames are not part of a DirectYUV
    // session. Without this check a stray CPU feed would be staged and
    // uploaded into the CUDA NV12 hwframe behind the DirectYuv executor's
    // back, silently dropping every authored overlay of the session.
    if (options_.direct_yuv_mode) {
        spdlog::error(
            "[native_av] host-frame feed refused on a DirectYUV session "
            "(typed encoder capability mismatch)");
        return false;
    }

    std::optional<Framebuffer> ocio_frame;
    const Framebuffer* conversion_source = &fb;
    bool ocio_applied = false;
#ifdef CHRONON3D_ENABLE_OCIO
    if (ocio_output_transform_) {
        ocio_frame.emplace(fb);
        // Video has no alpha plane. Composite over black in scene-linear
        // premultiplied space before a nonlinear display transform.
        for (int y = 0; y < ocio_frame->height(); ++y) {
            Color* row = ocio_frame->pixels_row(y);
            for (int x = 0; x < ocio_frame->width(); ++x) row[x].a = 1.0f;
        }
        if (!ocio_output_transform_->apply_framebuffer(*ocio_frame)) {
            spdlog::error("[native_av] OCIO output view failed while transforming frame");
            return false;
        }
        conversion_source = &*ocio_frame;
        ocio_applied = true;
    }
#endif

    // NVENC consumes CUDA frames, while the Chronon framebuffer converter
    // writes host YUV planes.  Trying to make the CUDA frame writable (or
    // writing its device pointers as if they were host memory) fails before
    // the first FullGraph frame.  Convert into a host staging frame, then
    // upload it through FFmpeg's CUDA frames context.
    AVFrame* conversion_frame = frame_;
    AVFrame* cpu_staging = nullptr;
    if (gpu_nvenc_) {
        cpu_staging = av_frame_alloc();
        if (!cpu_staging) return false;
        cpu_staging->format = AV_PIX_FMT_YUV420P;
        cpu_staging->width = options_.width;
        cpu_staging->height = options_.height;
        if (av_frame_get_buffer(cpu_staging, 32) < 0 ||
            av_frame_make_writable(cpu_staging) < 0) {
            av_frame_free(&cpu_staging);
            spdlog::error("[native_av] CPU staging frame allocation failed");
            return false;
        }
        conversion_frame = cpu_staging;
    } else if (av_frame_make_writable(frame_) < 0) {
        spdlog::error("[native_av] av_frame_make_writable failed");
        return false;
    }

    // ── Single-entry YUV conversion cache ────────────────────────────────
    // For identical consecutive content (held/frozen frames, looped
    // intro/outro) the RGBA→YUV conversion can be skipped entirely: the AVFrame
    // planes already hold the right YUV bytes — no memcpy needed.
    //
    // Eligibility comes from `conversion_cache_key.hpp` (the single authority)
    // and is keyed on the framebuffer's CONTENT digest — the producer's claim
    // that the pixels are identical — plus geometry/format/colour parameters.
    // It is deliberately NOT keyed on `key_digest()`: that is the producing
    // node's cache-KEY digest, shared by every frame of that node, which made
    // every frame after the first hit the cache and froze rendered video.
    // An unknown content digest (0, the default) is always a miss.
    // ----------------------------------------------------------------------
    const int color_matrix = resolve_color_matrix(options_);
    const auto conversion_cache_key = video::make_conversion_cache_key(
        *conversion_source,
        options_.width,
        options_.height,
        static_cast<int>(codec_->pix_fmt),
        options_.color_transform.apply_gamma,
        color_matrix,
        options_.color_transform.output == color::ColorSpace::Rec709,
        options_.color_transform.output,
        options_.color_transform.reference_white_nits);
    const bool same_as_last =
        conversion_cache_key.usable() && conversion_cache_key == last_conversion_key_;

    // 2. Convert the framebuffer to YUV (or skip for cache hit)
    const auto t_conv0 = Clock::now();
    const uint64_t pixel_before = profiling::g_current_counters
        ? profiling::g_current_counters->pixel_format_convert_wall_ms.load(std::memory_order_relaxed) : 0;
    const uint64_t color_before = profiling::g_current_counters
        ? profiling::g_current_counters->color_space_convert_wall_ms.load(std::memory_order_relaxed) : 0;

    double frame_conv_ms = 0.0;
    double frame_send_ms = 0.0;
    if (same_as_last) {
        // ── Cache HIT: AVFrame already has correct YUV data ──
        ++cache_hits_;
        if (profiling::g_current_counters) {
            profiling::g_current_counters->converted_frame_cache_hits.fetch_add(
                1, std::memory_order_relaxed);
        }
    } else {
        // ── Cache MISS: perform the full RGBA→YUV conversion ──
        ++cache_misses_;
        if (profiling::g_current_counters) {
            profiling::g_current_counters->converted_frame_cache_misses.fetch_add(
                1, std::memory_order_relaxed);
        }

        const bool hdr_output = options_.color_transform.output == color::ColorSpace::PQ ||
                                options_.color_transform.output == color::ColorSpace::HLG;
        video::ConvertFrameRequest req{
            .src           = *conversion_source,
            .planes        = video::FramePlanes{
                .y         = conversion_frame->data[0],
                .u         = conversion_frame->data[1],
                .v         = conversion_frame->data[2],
                .uv        = nullptr,
                .stride_y  = conversion_frame->linesize[0],
                .stride_u  = conversion_frame->linesize[1],
                .stride_v  = conversion_frame->linesize[2],
                .stride_uv = 0,
            },
            .width         = options_.width,
            .height        = options_.height,
            .format        = hdr_output ? video::EncoderPixelFormat::Yuv420P10
                                       : video::EncoderPixelFormat::YUV420P,
            .matrix        = static_cast<video::YuvMatrix>(color_matrix),
            .range         = video::ColorRange::Limited,
            .apply_gamma   = hdr_output || (!ocio_applied &&
                                            options_.color_transform.apply_gamma),
            .color_transform_applied = ocio_applied,
            .rec709_oetf   = !ocio_applied &&
                             options_.color_transform.output == color::ColorSpace::Rec709,
            .output_space  = options_.color_transform.output,
            .reference_white_nits = options_.color_transform.reference_white_nits,
        };

        auto conv_result = video::convert_frame(req);
        if (!conv_result.success) {
            av_frame_free(&cpu_staging);
            spdlog::error("[native_av] convert_frame failed");
            return false;
        }

        // Update the single-entry cache state for the next frame.
        last_conversion_key_ = conversion_cache_key;
    }

    const uint64_t pixel_after = profiling::g_current_counters
        ? profiling::g_current_counters->pixel_format_convert_wall_ms.load(std::memory_order_relaxed) : 0;
    const uint64_t color_after = profiling::g_current_counters
        ? profiling::g_current_counters->color_space_convert_wall_ms.load(std::memory_order_relaxed) : 0;
    const double conv_ms = elapsed_ms(t_conv0);
    frame_conv_ms = conv_ms;
    native_convert_ms_ += conv_ms;

    if (profiling::g_current_counters) {
        profiling::g_current_counters->video_conversion_wall_ms.fetch_add(
            static_cast<uint64_t>(conv_ms), std::memory_order_relaxed);
        profiling::g_current_counters->frame_conversion_copy_wall_ms.fetch_add(
            static_cast<uint64_t>(conv_ms), std::memory_order_relaxed);
        if (same_as_last) {
            // Track how much time we saved by skipping conversion.
            profiling::g_current_counters->native_av_convert_skipped_wall_ms.fetch_add(
                static_cast<uint64_t>(conv_ms), std::memory_order_relaxed);
        } else {
            profiling::g_current_counters->native_av_convert_wall_ms.fetch_add(
                static_cast<uint64_t>(conv_ms), std::memory_order_relaxed);
        }
    }

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    if (gpu_nvenc_) {
        av_frame_unref(frame_);
        frame_->format = AV_PIX_FMT_CUDA;
        frame_->width = options_.width;
        frame_->height = options_.height;
        frame_->hw_frames_ctx = av_buffer_ref(cuda_frames_ref_);
        if (!cuda_frames_ref_ || av_hwframe_get_buffer(cuda_frames_ref_, frame_, 0) < 0 ||
            av_hwframe_transfer_data(frame_, cpu_staging, 0) < 0) {
            av_frame_free(&cpu_staging);
            spdlog::error("[native_av] CPU-to-CUDA frame transfer failed");
            return false;
        }
        av_frame_free(&cpu_staging);
    }
#endif

    // 3. Set PTS (presentation timestamp) in frame number units
    frame_->pts = static_cast<int64_t>(frames_written_);
    // P1: forced-intra frames (run anchors, post-run resync) are
    // reference-free. frame_ is reused across writes, so every send states
    // the flag explicitly instead of inheriting the previous frame's force.
    const auto frame_pts = static_cast<std::int64_t>(frames_written_);
    const bool segment_start = segmented_mux_ && std::any_of(
        options_.output_segments.begin(), options_.output_segments.end(),
        [frame_pts](const auto& segment) { return segment.start_pts == frame_pts; });
    frame_->pict_type = (force_intra || segment_start)
        ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;

    // 4. Send the frame to the encoder (with EAGAIN back-pressure loop).
    //    `send_ms` measures ONLY avcodec_send_frame time (pure submit CPU),
    //    excluding drain_packets, to avoid temporal double-counting with
    //    receive/mux counters.  `backpressure_ms` separately captures the
    //    drain+retry wait that EAGAIN triggers (encoder back-pressure).
    const auto t_send0 = Clock::now();
    int ret = avcodec_send_frame(codec_, frame_);
    double send_ms = elapsed_ms(t_send0);

    int eagain_retries = 0;
    double backpressure_ms = 0.0;
    while (ret == AVERROR(EAGAIN) && eagain_retries < kMaxEagainRetries) {
        ++eagain_retries;
        // Encoder buffer full — drain packets (back-pressure wait), then retry.
        const auto t_bp0 = Clock::now();
        if (!drain_packets()) {
            return false;
        }
        backpressure_ms += elapsed_ms(t_bp0);
        const auto t_retry = Clock::now();
        ret = avcodec_send_frame(codec_, frame_);
        send_ms += elapsed_ms(t_retry);
    }

    if (ret == AVERROR(EAGAIN)) {
        spdlog::error("[native_av] avcodec_send_frame still EAGAIN after {} retries", kMaxEagainRetries);
        return false;
    }
    if (ret < 0) {
        char err_buf[256];
        av_strerror(ret, err_buf, sizeof(err_buf));
        spdlog::error("[native_av] avcodec_send_frame error: {} ({})", ret, err_buf);
        return false;
    }
    // Cross-item session restart: the forced keyframe has been consumed.
    session_restart_intra_pending_ = false;
    ++codec_frames_submitted_;
    // P1: remember what the codec just consumed — the run-anchor seed reuses
    // exactly these buffers (cheap ref, no copy) when a duplicate run starts.
    duplicate_seed_source_.reset(av_frame_clone(frame_));

    frame_send_ms = send_ms;
    native_send_frame_ms_ += send_ms;
    native_backpressure_ms_ += backpressure_ms;
    if (profiling::g_current_counters) {
        profiling::g_current_counters->encoder_submit_cpu_ms.fetch_add(
            static_cast<uint64_t>(send_ms), std::memory_order_relaxed);
        profiling::g_current_counters->encoder_backpressure_wait_ms.fetch_add(
            static_cast<uint64_t>(backpressure_ms), std::memory_order_relaxed);
    }

    ++frames_written_;
    last_frame_telemetry_ = {
        .conversion_copy_ms = frame_conv_ms,
        .pixel_format_convert_ms = static_cast<double>(pixel_after - pixel_before),
        .color_space_convert_ms = static_cast<double>(color_after - color_before),
        .encoder_ms = frame_send_ms,
        .backpressure_wait_ms = backpressure_ms,
        .native_convert_ms = frame_conv_ms,
        .native_send_ms = frame_send_ms,
    };
    return true;
}

// ---------------------------------------------------------------------------
// P1 (TICKET-STATIC-OVERLAY-DURATION-INVARIANT) — run-anchor packet
// duplication.
//
// A frame proven identical to the previous one costs O(1) to emit: no render,
// no conversion, no encode — the run's anchor packet is re-emitted at the
// frame's own PTS. The anchor is the forced-intra output of the run's first
// frame, so every replica decodes reference-free and cannot drift, whatever
// the run length. The run's pixels are the last submitted frame's pixels
// (the duplicate proof guarantees identical content, and every send site
// captures its buffers in `duplicate_seed_source_`), so priming the codec —
// needed to push the anchor out of its frame-threaded pipeline — is a bare
// buffer re-submission.
//
// Fail-closed: every entry refuses (returns false) rather than silently
// shortening the stream, and the presentation cursor (frames_written_) is
// checked on entry so a duplicate can never shift the timeline.
// ---------------------------------------------------------------------------

bool NativeAvEncoder::write_duplicate_frame(int frame_number) {
    if (!open_complete_ || !codec_ || !frame_ || !packet_ ||
        (!mux_ && !segmented_mux_)) {
        spdlog::error("[native_av] duplicate frame submitted outside an open session");
        return false;
    }
    if (options_.direct_yuv_mode) {
        spdlog::error(
            "[native_av] duplicate frame refused on a DirectYUV session "
            "(typed encoder capability mismatch)");
        return false;
    }
    if (borrows_mux_) {
        // The borrowed container's sample slots are scheduled by the caller
        // per rendered work unit; a replicated packet would consume a slot it
        // did not render.
        spdlog::error(
            "[native_av] duplicate frame refused on a borrowed-mux session");
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    // FullGraph submits CUDA frames asynchronously. A duplicate can arrive
    // while its preceding real frame is still in pending_cuda_frames_; drain
    // that frame first so it becomes the seed source and advances the codec
    // presentation cursor before we validate the duplicate's frame number.
    // A ready drain waits for its first pending frame, then opportunistically
    // drains any following frames that are already complete.  More than one
    // frame can still be in flight, so repeat until the queue is empty before
    // checking the presentation cursor; otherwise a duplicate can overtake a
    // preceding CUDA frame that has not completed yet.
    while (!pending_cuda_frames_.empty()) {
        if (!drain_ready_cuda_frames(true)) {
            spdlog::error("[native_av] could not drain preceding native frames before duplicate {}",
                          frame_number);
            return false;
        }
    }
#endif
    if (static_cast<std::int64_t>(frame_number) !=
        static_cast<std::int64_t>(frames_written_)) {
        spdlog::error(
            "[native_av] duplicate frame {} breaks the presentation cursor {}",
            frame_number, frames_written_);
        return false;
    }

    // Run bookkeeping: the first duplicate after a real frame opens the run.
    if (!duplicate_run_active_) {
        duplicate_run_active_ = true;
        duplicate_run_start_pts_ = frame_number;
        duplicate_prerun_packets_received_ = codec_packets_received_;
        duplicate_prerun_packets_target_ = codec_frames_submitted_;
        duplicate_replicated_through_pts_ = frame_number - 1;
    }

    // Register this PTS before draining: the codec may already have produced
    // the seed submitted by the preceding duplicate call. If that reordered
    // packet reaches the mux here, a replica at an earlier PTS would violate
    // the mux DTS cursor.
    extend_duplicate_discard_range_through_(frame_number);

    // Progress the codec on every call: this drains the anchor out of the
    // frame-threaded pipeline and lets already-replicated seeds fall out of
    // the discard windows.
    if (!drain_packets()) return false;

    if (!duplicate_anchor_) {
        if (last_video_key_packet_ &&
            last_video_key_packet_pts_ < duplicate_run_start_pts_ &&
            (last_video_key_packet_->flags & AV_PKT_FLAG_KEY)) {
            duplicate_anchor_.reset(av_packet_clone(last_video_key_packet_.get()));
            if (!duplicate_anchor_) return false;
            duplicate_anchor_pts_ = last_video_key_packet_pts_;
        }
    }

    // NVENC lookahead can still hold packets for real frames preceding this
    // duplicate run. Do not mux future-timestamp replicas ahead of them: feed
    // identical frame seeds until every pre-run input has produced a packet,
    // and register each seed PTS for discard before that packet can drain.
    if (!duplicate_anchor_ || duplicate_prerun_packets_received_ <
        duplicate_prerun_packets_target_) {
        return submit_duplicate_seed_(static_cast<std::int64_t>(frame_number));
    }

    return emit_duplicate_replicas_through_(frame_number);
}

void NativeAvEncoder::extend_duplicate_discard_range_through_(
    std::int64_t frame_number) {
    if (!duplicate_discard_ranges_.empty() &&
        duplicate_discard_ranges_.back().first == duplicate_run_start_pts_) {
        duplicate_discard_ranges_.back().second = std::max(
            duplicate_discard_ranges_.back().second, frame_number);
        return;
    }
    duplicate_discard_ranges_.emplace_back(
        duplicate_run_start_pts_, frame_number);
}

bool NativeAvEncoder::emit_duplicate_replicas_through_(std::int64_t frame_number) {
    if (!duplicate_anchor_ || !mux_ ||
        frame_number < duplicate_replicated_through_pts_) {
        return false;
    }
    // Emit every sample the replication window owes, in presentation order.
    const auto t_mux0 = Clock::now();
    const std::int64_t first_pending = duplicate_replicated_through_pts_ + 1;
    for (std::int64_t pts = first_pending;
         pts <= frame_number; ++pts) {
        media::OwnedAVPacket replica(av_packet_alloc());
        if (!replica || av_packet_ref(replica.get(), duplicate_anchor_.get()) < 0) {
            spdlog::error("[native_av] duplicate packet replication allocation failed");
            return false;
        }
        replica->pts = pts;
        replica->dts = pts;
        if (!mux_->submit(
                media::EncodedPacket{
                    std::move(replica), codec_->time_base,
                    (duplicate_anchor_->flags & AV_PKT_FLAG_KEY) != 0},
                media::MuxVideoPlacement::AsSubmitted)) {
            spdlog::error("[native_av] duplicate packet submit failed at pts {}", pts);
            return false;
        }
        ++mux_packets_written_;
    }
    native_mux_write_ms_ += elapsed_ms(t_mux0);
    duplicate_replicated_through_pts_ = frame_number;
    frames_written_ = static_cast<std::uint64_t>(frame_number) + 1;
    return true;
}

bool NativeAvEncoder::submit_duplicate_seed_(std::int64_t frame_pts) {
    if (!codec_ || !duplicate_seed_source_) {
        spdlog::error(
            "[native_av] duplicate seed has no submitted frame to re-encode");
        return false;
    }
    // No conversion and no copy: the seed re-submits the last submitted
    // frame's buffers at a new PTS. A FRESH shell is mandatory — the codec may
    // still hold references to earlier shells and must not observe this
    // send's pts/pict_type retroactively. The run's first seed is forced
    // intra — its packet becomes the replication anchor.
    AVFrame* seed = av_frame_alloc();
    if (!seed) return false;
    if (av_frame_ref(seed, duplicate_seed_source_.get()) < 0) {
        av_frame_free(&seed);
        spdlog::error("[native_av] duplicate seed frame ref failed");
        return false;
    }
    seed->pts = frame_pts;
    seed->pict_type = (frame_pts == duplicate_run_start_pts_)
        ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;

    const auto t_send0 = Clock::now();
    int ret = avcodec_send_frame(codec_, seed);
    double send_ms = elapsed_ms(t_send0);
    int eagain_retries = 0;
    double backpressure_ms = 0.0;
    while (ret == AVERROR(EAGAIN) && eagain_retries < kMaxEagainRetries) {
        ++eagain_retries;
        const auto t_bp0 = Clock::now();
        if (!drain_packets()) {
            av_frame_free(&seed);
            return false;
        }
        backpressure_ms += elapsed_ms(t_bp0);
        const auto t_retry = Clock::now();
        ret = avcodec_send_frame(codec_, seed);
        send_ms += elapsed_ms(t_retry);
    }
    av_frame_free(&seed);
    if (ret == AVERROR(EAGAIN)) {
        spdlog::error(
            "[native_av] duplicate seed send remained EAGAIN after {} retries",
            kMaxEagainRetries);
        return false;
    }
    if (ret < 0) {
        char err_buf[256];
        av_strerror(ret, err_buf, sizeof(err_buf));
        spdlog::error("[native_av] duplicate seed send failed: {} ({})", ret, err_buf);
        return false;
    }
    ++codec_frames_submitted_;
    native_send_frame_ms_ += send_ms;
    native_backpressure_ms_ += backpressure_ms;
    ++frames_written_;
    return true;
}

} // namespace chronon3d::cli
