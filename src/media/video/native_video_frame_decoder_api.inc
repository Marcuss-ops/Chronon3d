void NativeVideoFrameDecoder::clear_decode_diagnostic() noexcept {
    std::lock_guard lock(m_diagnostic_mutex);
    m_last_decode_diagnostic.reset();
}

void NativeVideoFrameDecoder::publish_decode_failure(
    DecodeFailureReason reason,
    int ffmpeg_error,
    std::int64_t pts,
    std::int64_t dts,
    std::uint64_t source_order,
    std::string message) {
    std::lock_guard lock(m_diagnostic_mutex);
    m_last_decode_diagnostic = DecodeDiagnostic{
        reason, ffmpeg_error, pts, dts, source_order, std::move(message)};
}

std::optional<DecodeDiagnostic>
NativeVideoFrameDecoder::last_decode_diagnostic() const {
    std::lock_guard lock(m_diagnostic_mutex);
    return m_last_decode_diagnostic;
}

std::shared_ptr<Framebuffer> NativeVideoFrameDecoder::decode_frame_at(
    const std::string& path, RationalTime presentation_time, int, int) {
    clear_decode_diagnostic();
    if (path.empty()) {
        publish_decode_failure(DecodeFailureReason::OpenInput, 0,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "decode source path is empty");
        return {};
    }
    auto io_read = global_media_io_budget().acquire_required_read();
    std::shared_ptr<Session> session;
    { std::lock_guard<std::mutex> lock(m_mutex); session = open_session_locked(path); }
    if (!session) return {};

    const auto selected = session->sample_table.select_covering(presentation_time);
    if (!selected) {
        publish_decode_failure(DecodeFailureReason::UnexpectedEof, 0,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "presentation time does not resolve to a source sample");
        return {};
    }
    const int64_t target = static_cast<int64_t>(*selected);

    std::unique_lock lock(session->mutex);
    if (session->prefetch_inflight == target) {
        session->prefetch_cv.wait(lock, [&session, target] {
            return session->prefetch_inflight != target || session->prefetch_stop.load();
        });
    }
    if (session->test_options.enable_frame_cache) {
        if (auto cached = session->cache.get(target)) {
            clear_decode_diagnostic();
            return *cached;
        }
    }
    while (!session->prefetch_queue.empty() && session->prefetch_queue.front().target < target)
        session->prefetch_queue.pop_front();
    if (!session->prefetch_queue.empty() && session->prefetch_queue.front().target == target) {
        auto result = std::move(session->prefetch_queue.front().framebuffer);
        session->prefetch_queue.pop_front();
        clear_decode_diagnostic();
        return result;
    }
    session->prefetch_queue.clear();
    ++session->prefetch_generation;
    session->prefetch_next = *selected + 1 < session->sample_table.size() ? target + 1 : -1;
    lock.unlock();
    auto result = decode_frame_internal(*session, target);
    lock.lock();
    session->prefetch_cv.notify_all();
    if (result) {
        const bool native_surface = result->surface_handle() != runtime::kInvalidRenderSurfaceHandle;
        if (!native_surface && session->test_options.enable_frame_cache) session->cache.put(target, result);
        clear_decode_diagnostic();
    }
    return result;
}

HwFrameRef NativeVideoFrameDecoder::decode_native_frame_at(
    const std::string& path, RationalTime presentation_time, int, int) {
    clear_decode_diagnostic();
    if (path.empty()) {
        publish_decode_failure(DecodeFailureReason::OpenInput, 0,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "decode source path is empty");
        return {};
    }
    if (!m_native_importer && !m_video_runtime) {
        publish_decode_failure(DecodeFailureReason::NativeSurfaceUnavailable, 0,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "native frame decoding requires a native importer or video runtime");
        return {};
    }
    auto io_read = global_media_io_budget().acquire_required_read();
    if (m_counters) m_counters->video_source_requested_frames.fetch_add(1, std::memory_order_relaxed);
    std::shared_ptr<Session> session;
    { std::lock_guard<std::mutex> lock(m_mutex); session = open_session_locked(path); }
    if (!session) return {};
    const auto selected = session->sample_table.select_covering(presentation_time);
    if (!selected) {
        publish_decode_failure(DecodeFailureReason::UnexpectedEof, 0,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "presentation time does not resolve to a source sample");
        return {};
    }
    const int64_t target = static_cast<int64_t>(*selected);

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

    const auto decode_start = profiling::now();
    const auto ignored = decode_frame_internal(*session, target);
    const double decode_dur_ms = profiling::duration_ms(decode_start, profiling::now());
    if (m_counters) m_counters->video_decode_wall_ms.fetch_add(
        static_cast<std::uint64_t>(std::llround(decode_dur_ms)), std::memory_order_relaxed);
    (void)ignored;

    state_lock.lock();
    session->capture_native_frame = false;
    auto result = std::move(session->captured_native_frame);
    if (result) {
        session->profiling.decoded_frames++;
        session->profiling.decode_total_ms += decode_dur_ms;
        session->profiling.frame_durations_ms.push_back(decode_dur_ms);
        clear_decode_diagnostic();
    } else if (!last_decode_diagnostic()) {
        publish_decode_failure(DecodeFailureReason::NativeSurfaceUnavailable, 0,
            session->sample_table[static_cast<std::size_t>(target)].pts,
            session->sample_table[static_cast<std::size_t>(target)].dts,
            session->sample_table[static_cast<std::size_t>(target)].source_order,
            "exact source sample did not produce a native frame");
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