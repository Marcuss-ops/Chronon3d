namespace {

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

    const auto lookup = session->sample_table.lookup(presentation_time);
    if (!lookup.found()) return {};
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
