#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/color/output_transform.hpp>
#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/media/frame_conversion/converted_frame_cache.hpp>
#include <chronon3d/media/frame_conversion/frame_conversion_service.hpp>
#include <string>
#include <vector>
#include <array>
#include <atomic>
#include <thread>
#include <memory>

#include <tbb/task_arena.h>

namespace chronon3d::cli {

struct FfmpegPipeOptions;

struct EncoderFrameTelemetry {
    double conversion_copy_ms{0.0};
    double encoder_ms{0.0};
    double pipe_write_ms{0.0};
    double native_convert_ms{0.0};
    double native_send_ms{0.0};
    double native_receive_ms{0.0};
    double native_mux_ms{0.0};
};

// ── Abstract video encoder interface ────────────────────────────────────────
// Implemented by both FfmpegPipeEncoder (external ffmpeg subprocess via pipe)
// and NativeAvEncoder (in-process libavcodec/libavformat).
struct IVideoEncoder {
    virtual ~IVideoEncoder() = default;
    virtual bool open(const FfmpegPipeOptions& options) = 0;
    virtual bool write_frame(const Framebuffer& fb) = 0;
    virtual bool write_frame_async(const Framebuffer& fb, std::shared_ptr<Framebuffer> owner) {
        return write_frame(fb);
    }
    virtual void set_counters(chronon3d::RenderCounters* counters) { (void)counters; }
    virtual bool close() = 0;
    [[nodiscard]] virtual uint64_t frames_written() const = 0;
    [[nodiscard]] virtual EncoderFrameTelemetry last_frame_telemetry() const { return {}; }

    // ── Native encoder telemetry accessors ──
    // Return 0.0 for pipe backend (no native encoding phases).
    [[nodiscard]] virtual double native_convert_ms()     const { return 0.0; }
    [[nodiscard]] virtual double native_send_frame_ms()  const { return 0.0; }
    [[nodiscard]] virtual double native_receive_packet_ms() const { return 0.0; }
    [[nodiscard]] virtual double native_mux_write_ms()   const { return 0.0; }
    [[nodiscard]] virtual double native_trailer_ms()     const { return 0.0; }
};

enum class PipePixelFormat {
    RGBA,
    YUV420P,
    NV12
};

struct FfmpegPipeOptions {
    int width{0};
    int height{0};
    int fps{30};
    int crf{18};
    std::string preset{"medium"};
    std::string codec{"libx264"};
    std::string output_path;
    PipePixelFormat input_format{PipePixelFormat::RGBA};
    std::string output_pix_fmt{"yuv420p"};
    bool verbose{false};
    color::OutputTransformOptions color_transform{};
    std::string tune;                          // x264 tune (empty = default "zerolatency")
    std::string pipe_writer{"classic"}; // "io_uring" to opt-in to experimental async I/O
};

std::string build_ffmpeg_raw_pipe_command(const FfmpegPipeOptions& options);
[[nodiscard]] int encode_color_matrix_id(const FfmpegPipeOptions& options);

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

    void set_counters(chronon3d::RenderCounters* counters) { frame_counters_ = counters; }

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
    int ffmpeg_pid_{-1}; // child process PID, resolved via /proc/self/fd after popen()
    FfmpegPipeOptions options_{};
    std::vector<uint8_t> rgba_buffer_;
    std::vector<uint8_t> yuv_buffer_;

    // ── Centralised frame conversion service ─────────────────────────────
    chronon3d::video::FrameConversionService conv_svc_{8};

    // ── Converted frame cache (LRU, replaces the old single-entry digest cache) ──
    chronon3d::video::ConvertedFrameCache frame_cache_{};
    std::vector<uint8_t> cached_frame_bytes_; // reuse buffer for non-cache writes
    size_t cached_frame_size_{0};             // last converted frame byte count
    uint64_t current_frame_{0};              // incremented each write_frame() call

    uint64_t frames_written_{0};
    uint64_t bytes_written_{0};
    double total_write_blocked_ms_{0.0};
    EncoderFrameTelemetry last_frame_telemetry_{};
    bool pipe_failed_{false};
    chronon3d::RenderCounters* frame_counters_{nullptr};

    // ── Async conversion ring buffer (producer: writer thread, consumer: converter thread) ──
    static constexpr size_t kAsyncConversionSlots = 4;

    enum class SlotState : int {
        Empty     = 0,  // slot available for staging
        Reserved  = 1,  // producer is writing slot data
        Staged    = 2,  // data written, waiting for converter
        Converted = 3,  // conversion done, ready for writer
        Failed    = 4,  // conversion failed — must still be drained
    };

    struct AsyncSlot {
        std::atomic<int> state{static_cast<int>(SlotState::Empty)};
        std::shared_ptr<Framebuffer> fb;
        std::vector<uint8_t> converted;
        chronon3d::video::ConvertedFrameCacheKey cache_key;
        bool can_cache{false};
    };

    /// Drain all staged (state==2) slots by running the converter on them
    /// synchronously.  Called during close() before stopping the converter thread.
    void drain_staged_slots();
    std::array<AsyncSlot, kAsyncConversionSlots> async_slots_;
    std::atomic<size_t> producer_idx_{0};
    std::atomic<size_t> consumer_idx_{0};
    std::atomic<size_t> writer_idx_{0};
    std::thread converter_thread_;
    std::atomic<bool> converter_stop_{false};
    tbb::task_arena converter_arena_{tbb::task_arena::automatic, 1}; // full concurrency, isolated from global TBB arena

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

} // namespace chronon3d::cli
