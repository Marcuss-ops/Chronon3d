// ============================================================================
//  write_frame() / convert_and_submit()
// ============================================================================

bool VideoSinkEncoderAdapter::convert_and_submit(const Framebuffer& fb) {
    if (!sink_) {
        return false;
    }

    // ── 1. Convert Framebuffer → tight-packed pixel buffer ─────────────
    using media::video::PixelFormat;
    const auto fmt = map_pixel_format(input_format_);

    // Map our PipePixelFormat to EncoderPixelFormat for the conversion service.
    video::EncoderPixelFormat enc_fmt;
    switch (input_format_) {
        case PipePixelFormat::YUV420P: enc_fmt = video::EncoderPixelFormat::YUV420P; break;
        case PipePixelFormat::NV12:    enc_fmt = video::EncoderPixelFormat::NV12; break;
        case PipePixelFormat::RGBA:
        default:                      enc_fmt = video::EncoderPixelFormat::RGBA8; break;
    }

    if (!encoder_pool_) {
        spdlog::error("[video_adapter] encoder frame pool is not initialized");
        return false;
    }
    auto encoder_frame = encoder_pool_->acquire();
    if (!encoder_frame) {
        spdlog::error("[video_adapter] encoder frame pool is exhausted");
        return false;
    }
    const size_t expected_size = encoder_frame.storage.size();

    const auto conv_t0 = profiling::now();
    const uint64_t pixel_before = counters_
        ? counters_->pixel_format_convert_wall_ms.load(std::memory_order_relaxed) : 0;
    const uint64_t color_before = counters_
        ? counters_->color_space_convert_wall_ms.load(std::memory_order_relaxed) : 0;

    // Use FrameConversionService to convert.
    const video::ConversionOptions copts{
        .width       = width_,
        .height      = height_,
        .format      = enc_fmt,
        .apply_gamma = options_.color_transform.apply_gamma,
        .matrix      = video::YuvMatrix::BT709,
        .range       = video::ColorRange::Limited,
        .use_cache   = false,  // caching across frames is encoder-level, not per-submit
    };

    auto converted = conv_svc_.convert_into(
        fb, copts, encoder_frame.storage.data(), expected_size);

    const auto conv_t1 = profiling::now();
    const uint64_t pixel_after = counters_
        ? counters_->pixel_format_convert_wall_ms.load(std::memory_order_relaxed) : 0;
    const uint64_t color_after = counters_
        ? counters_->color_space_convert_wall_ms.load(std::memory_order_relaxed) : 0;
    const double conv_ms = profiling::duration_ms(conv_t0, conv_t1);

    if (!converted) {
        spdlog::error("[video_adapter] Frame conversion failed");
        return false;
    }

    // Track conversion telemetry. `conversion_bytes_written` is the direct
    // conversion result; `encoder_staging_copy_bytes` remains zero because
    // no Chronon-owned staging copy exists in this path.
    last_telemetry_.conversion_copy_ms = conv_ms;
    last_telemetry_.pixel_format_convert_ms = static_cast<double>(pixel_after - pixel_before);
    last_telemetry_.color_space_convert_ms = static_cast<double>(color_after - color_before);
    last_telemetry_.conversion_bytes_written = converted.data.size();
    last_telemetry_.encoder_staging_copy_bytes = 0;
    if (encoder_pool_) {
        const auto pool_stats = encoder_pool_->stats();
        last_telemetry_.encoder_slots_allocated = pool_stats.slots_allocated;
        last_telemetry_.encoder_slot_reuses = pool_stats.slot_reuses;
    }

    if (counters_) {
        counters_->video_conversion_wall_ms.fetch_add(
            static_cast<uint64_t>(conv_ms), std::memory_order_relaxed);
        counters_->frame_conversion_copy_wall_ms.fetch_add(
            static_cast<uint64_t>(conv_ms), std::memory_order_relaxed);
        counters_->conversion_bytes_written.fetch_add(
            converted.data.size(), std::memory_order_relaxed);
        // These two counters are gauges (current pool configuration/state),
        // unlike the cumulative byte/time counters above.
        const auto pool_stats = encoder_pool_->stats();
        counters_->encoder_slots_allocated.store(
            pool_stats.slots_allocated, std::memory_order_relaxed);
        counters_->encoder_slot_reuses.store(
            pool_stats.slot_reuses, std::memory_order_relaxed);
        counters_->frame_conversion_wall_ms.fetch_add(
            static_cast<uint64_t>(conv_ms), std::memory_order_relaxed);
        counters_->video_frames_converted.fetch_add(1, std::memory_order_relaxed);
    }

    // ── 2. Build VideoFrameView ────────────────────────────────────────
    // For YUV420P/NV12 the conversion service produces tight-packed data:
    //   YUV420P: Y(w×h) + U(w/2×h/2) + V(w/2×h/2) — contiguous
    //   NV12:    Y(w×h) + UV(w×h/2) — contiguous
    //   RGBA8:   RGBA(w×h×4) — contiguous
    // For the new sink, packed formats use submit(VideoFrameView) directly.
    chronon3d::media::video::VideoFrameView view;
    view.data          = encoder_frame.storage.data();
    view.stride_bytes  = 0;  // tight packing
    view.width         = width_;
    view.height        = height_;
    view.pixel_format  = fmt;
    view.pts           = static_cast<int64_t>(frames_written_);

    // ── 3. Submit to sink ──────────────────────────────────────────────
    const auto submit_t0 = std::chrono::steady_clock::now();
    // Snapshot the pipe-write decomposition counters around submit() so the
    // per-frame CPU copy vs back-pressure wait are separable (the write
    // itself happens inside the sink's write_to_pipe, on the same thread).
    const uint64_t pipe_cpu_before = counters_
        ? counters_->pipe_write_cpu_wall_us.load(std::memory_order_relaxed) : 0;
    const uint64_t pipe_bp_before = counters_
        ? counters_->pipe_backpressure_wait_wall_us.load(std::memory_order_relaxed) : 0;
    // The conversion service produces tight-packed data for all formats.
    // For YUV420P: Y(w×h) + U(w/2×h/2) + V(w/2×h/2) — contiguous
    // For NV12:    Y(w×h) + UV(w×h/2) — contiguous
    // For RGBA8:   RGBA(w×h×4) — contiguous
    // The sink's submit() handles all packed formats uniformly.
    bool ok = sink_->submit(view);
    const auto submit_t1 = std::chrono::steady_clock::now();
    const uint64_t pipe_cpu_after = counters_
        ? counters_->pipe_write_cpu_wall_us.load(std::memory_order_relaxed) : 0;
    const uint64_t pipe_bp_after = counters_
        ? counters_->pipe_backpressure_wait_wall_us.load(std::memory_order_relaxed) : 0;

    if (!ok) {
        spdlog::error("[video_adapter] sink->submit() failed at frame {}: {} — {}",
                      frames_written_,
                      to_string(sink_->last_error()), sink_->last_error_message());
        return false;
    }

    const double submit_ms = std::chrono::duration<double, std::milli>(
        submit_t1 - submit_t0).count();
    last_telemetry_.encoder_ms = submit_ms;
    last_telemetry_.frame_submit_ms = submit_ms;
    last_telemetry_.pipe_write_cpu_ms =
        static_cast<double>(pipe_cpu_after - pipe_cpu_before) / 1000.0;
    last_telemetry_.pipe_backpressure_wait_ms =
        static_cast<double>(pipe_bp_after - pipe_bp_before) / 1000.0;
    write_blocked_ms_ += submit_ms;

    if (counters_) {
        counters_->video_frames_submitted.fetch_add(1, std::memory_order_relaxed);
        counters_->video_pipe_write_wall_ms.fetch_add(
            static_cast<uint64_t>(submit_ms), std::memory_order_relaxed);
        counters_->video_frames_written_counter.fetch_add(
            1, std::memory_order_relaxed);
        counters_->frame_submit_wall_ms.fetch_add(
            static_cast<uint64_t>(submit_ms), std::memory_order_relaxed);
    }

    ++frames_written_;
    return true;
}

bool VideoSinkEncoderAdapter::write_frame(const Framebuffer& fb) {
    if (!sink_) {
        return false;
    }
    return convert_and_submit(fb);
}

bool VideoSinkEncoderAdapter::write_frame_async(
    const Framebuffer& fb,
    std::shared_ptr<Framebuffer> owner)
{
    const bool ok = write_frame(fb);
    owner.reset();
    return ok;
}
