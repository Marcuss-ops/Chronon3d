#pragma once

// =============================================================================
// include/chronon3d/video/pipe_encoder.hpp — FFmpeg subprocess pipe encoder
//
// Part of chronon3d_backend_video.  Encodes frames by writing raw pixel data
// to an ffmpeg subprocess via a pipe (popen / pclose).
// =============================================================================

#include "encoder.hpp"
#include <cstdio>
#include <cstdint>
#include <vector>
#include <atomic>
#include <thread>
#include <array>
#include <memory>

namespace chronon3d::video {

/// Encoder that streams raw RGBA/YUV frames to an external ffmpeg subprocess.
///
/// Uses popen() on POSIX and _popen() on Windows.  Optionally supports
/// io_uring-based async pipe I/O on Linux (opt-in via FfmpegPipeOptions).
class FfmpegPipeEncoder : public IVideoEncoder {
public:
    FfmpegPipeEncoder() = default;
    ~FfmpegPipeEncoder() override;

    FfmpegPipeEncoder(const FfmpegPipeEncoder&) = delete;
    FfmpegPipeEncoder& operator=(const FfmpegPipeEncoder&) = delete;

    bool open(const FfmpegPipeOptions& options) override;
    bool write_frame(const Framebuffer& fb) override;
    bool write_frame_async(const Framebuffer& fb, std::shared_ptr<Framebuffer> owner) override;
    bool close() override;

    void set_counters(chronon3d::RenderCounters* counters) override { frame_counters_ = counters; }

    [[nodiscard]] uint64_t frames_written() const override { return frames_written_; }
    [[nodiscard]] EncoderFrameTelemetry last_frame_telemetry() const override { return last_frame_telemetry_; }
    [[nodiscard]] uint64_t bytes_written() const { return bytes_written_; }
    [[nodiscard]] bool is_open() const { return pipe_ != nullptr; }
    [[nodiscard]] double total_write_blocked_ms() const { return total_write_blocked_ms_; }
    [[nodiscard]] int ffmpeg_pid() const { return ffmpeg_pid_; }

    bool convert_framebuffer_to_rgba(const Framebuffer& fb, uint8_t* dst = nullptr);
    bool convert_framebuffer_to_yuv420p(const Framebuffer& fb, uint8_t* dst = nullptr);
    bool convert_framebuffer_to_nv12(const Framebuffer& fb, uint8_t* dst = nullptr);

private:
    FILE* pipe_{nullptr};
    int ffmpeg_pid_{-1};
    FfmpegPipeOptions options_{};
    std::vector<uint8_t> rgba_buffer_;
    std::vector<uint8_t> yuv_buffer_;

    // ── Converted frame cache ──
    chronon3d::video::ConvertedFrameCache frame_cache_{};
    std::vector<uint8_t> cached_frame_bytes_;
    size_t cached_frame_size_{0};
    uint64_t current_frame_{0};

    uint64_t frames_written_{0};
    uint64_t bytes_written_{0};
    double total_write_blocked_ms_{0.0};
    EncoderFrameTelemetry last_frame_telemetry_{};
    bool pipe_failed_{false};
    chronon3d::RenderCounters* frame_counters_{nullptr};

    // ── Async conversion ring buffer ──
    static constexpr size_t kAsyncConversionSlots = 4;

    enum class SlotState : int {
        Empty     = 0,
        Reserved  = 1,
        Staged    = 2,
        Converted = 3,
        Failed    = 4,
    };

    struct AsyncSlot {
        std::atomic<int> state{static_cast<int>(SlotState::Empty)};
        std::shared_ptr<Framebuffer> fb;
        std::vector<uint8_t> converted;
        chronon3d::video::ConvertedFrameCacheKey cache_key;
        bool can_cache{false};
    };

    void drain_staged_slots();
    std::array<AsyncSlot, kAsyncConversionSlots> async_slots_;
    std::atomic<size_t> producer_idx_{0};
    std::atomic<size_t> consumer_idx_{0};
    std::atomic<size_t> writer_idx_{0};
    std::thread converter_thread_;
    std::atomic<bool> converter_stop_{false};
    tbb::task_arena converter_arena_{tbb::task_arena::automatic, 1};

    void converter_loop();
    bool write_next_converted_frame();
    bool write_raw_to_pipe(const uint8_t* data, size_t size);

#ifdef __linux__
    bool use_uring_{false};
    int ring_fd_{-1};
    void* sq_mmap_{nullptr};
    void* cq_mmap_{nullptr};
    void* sqes_mmap_{nullptr};
    size_t sq_mmap_len_{0};
    size_t cq_mmap_len_{0};
    size_t sqes_mmap_len_{0};

    unsigned* sq_head_{nullptr};
    unsigned* sq_tail_{nullptr};
    unsigned* sq_ring_mask_{nullptr};
    unsigned* sq_ring_entries_{nullptr};
    unsigned* sq_flags_{nullptr};
    unsigned* sq_array_{nullptr};
    void* sqes_{nullptr};

    unsigned* cq_head_{nullptr};
    unsigned* cq_tail_{nullptr};
    unsigned* cq_ring_mask_{nullptr};
    unsigned* cq_ring_entries_{nullptr};
    void* cqes_{nullptr};
    unsigned sq_mask_{0};
    unsigned cq_mask_{0};

    static constexpr size_t kRingEntries = 16;
    std::vector<std::vector<uint8_t>> ring_buffers_;
    std::vector<bool> ring_buffer_pending_;
    std::vector<size_t> ring_buffer_bytes_written_;
    size_t ring_buffer_index_{0};
    size_t ring_buffer_size_{0};
    uint64_t pending_writes_count_{0};
    bool buffers_registered_{false};

    bool init_uring();
    void cleanup_uring();
    bool write_uring(size_t buf_idx, size_t size);
    void reap_completed_uring(bool wait);
#endif
};

} // namespace chronon3d::video
