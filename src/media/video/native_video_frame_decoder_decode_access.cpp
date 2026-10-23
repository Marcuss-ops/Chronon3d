#include "native_video_frame_decoder_detail.hpp"
#ifdef CHRONON3D_ENABLE_OCIO
#include <chronon3d/color/ocio_color_transform.hpp>
#endif

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG

namespace chronon3d::media {

std::uint64_t NativeVideoFrameDecoder::indexed_sample_count(const std::string& path) {
    if (path.empty()) return 0;
    DecodeDiagnostic open_diagnostic;
    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        session = open_session_locked(path, &open_diagnostic);
    }
    return session ? static_cast<std::uint64_t>(session->sample_table.size()) : 0;
}

std::optional<RationalTime> NativeVideoFrameDecoder::indexed_presentation_time(
    const std::string& path, std::uint64_t sample_index) {
    if (path.empty()) return std::nullopt;
    DecodeDiagnostic open_diagnostic;
    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        session = open_session_locked(path, &open_diagnostic);
    }
    if (!session || sample_index >= session->sample_table.size())
        return std::nullopt;
    const auto& sample = session->sample_table[static_cast<std::size_t>(sample_index)];
    return RationalTime{sample.pts, session->sample_table.time_base()};
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

    auto lookup = session->sample_table.lookup(presentation_time);
    if (!lookup.found()) {
        if (lookup.disposition == SampleLookupDisposition::AfterEnd && !session->sample_table.empty()) {
            lookup.sample_index = session->sample_table.size() - 1;
        } else {
            return {};
        }
    }
    const int64_t target = static_cast<int64_t>(*lookup.sample_index);

    // AUDIT-08A: the prefetch teardown (stop + join + queue drain +
    // generation bump) is a ONE-TIME cost keyed on direct_prefetch_disabled.
    // Steady-state direct frames take the session mutex exactly twice —
    // to reset and to collect the captured frame — with no queue clear, no
    // generation bump, no CV notify and no CV wait (the joined worker
    // guarantees prefetch_inflight == -1 for the session's lifetime).
    if (!session->direct_prefetch_disabled && session->prefetch_worker.joinable()) {
        std::unique_lock state_lock(session->mutex);
        session->prefetch_stop.store(true, std::memory_order_relaxed);
        session->prefetch_cv.notify_all();
        state_lock.unlock();
        session->prefetch_worker.join();
        state_lock.lock();
        session->direct_prefetch_disabled = true;
        session->prefetch_queue.clear();
        ++session->prefetch_generation;
        session->prefetch_next = -1;
        // prefetch_inflight is guaranteed -1 once the worker has joined: the
        // worker clears it before re-checking prefetch_stop, and no other
        // producer exists. No CV wait needed.
        session->captured_native_frame.reset();
        session->capture_native_frame = true;
        state_lock.unlock();
    } else {
        std::lock_guard state_lock(session->mutex);
        session->captured_native_frame.reset();
        session->capture_native_frame = true;
    }

    auto io_read = global_media_io_budget().acquire_required_read();
    const auto decode_start = profiling::now();
    const auto decode_result = decode_frame_internal(*session, target);
    const double decode_dur_ms = profiling::duration_ms(decode_start, profiling::now());
    if (m_counters) m_counters->video_decode_wall_ms.fetch_add(
        static_cast<std::uint64_t>(std::llround(decode_dur_ms)), std::memory_order_relaxed);
    (void)decode_result;

    {
        std::lock_guard state_lock(session->mutex);
        session->capture_native_frame = false;
        auto result = std::move(session->captured_native_frame);
        if (result) {
            session->profiling.decoded_frames++;
            session->profiling.decode_total_ms += decode_dur_ms;
            session->profiling.frame_durations_ms.push_back(decode_dur_ms);
        }
        return result;
    }
}

NativeVideoFrameDecoder::DecodeProfilingStats NativeVideoFrameDecoder::decode_profiling_stats() const {
    DecodeProfilingStats total;
    // AUDIT-08B: m_mutex is mutable — no const_cast.
    std::lock_guard lock(m_mutex);
    for (const auto& [_, session] : m_sessions) {
        if (!session) continue;
        std::lock_guard s_lock(session->mutex);
        total.decoded_frames += session->profiling.decoded_frames;
        total.retained_replays += session->profiling.retained_replays;
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

} // namespace chronon3d::media

#endif
