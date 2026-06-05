#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/color/output_transform.hpp>
#include <chronon3d/video/frame_converter.hpp>
#include <chronon3d/video/converted_frame_cache.hpp>
#include <string>
#include <vector>

namespace chronon3d::cli {

struct FfmpegPipeOptions;

// ── Abstract video encoder interface ────────────────────────────────────────
// Implemented by both FfmpegPipeEncoder (external ffmpeg subprocess via pipe)
// and NativeAvEncoder (in-process libavcodec/libavformat).
struct IVideoEncoder {
    virtual ~IVideoEncoder() = default;
    virtual bool open(const FfmpegPipeOptions& options) = 0;
    virtual bool write_frame(const Framebuffer& fb) = 0;
    virtual bool close() = 0;
    [[nodiscard]] virtual uint64_t frames_written() const = 0;

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
    std::string pipe_writer{"auto"};     // "auto" → try io_uring first, fall back to classic write
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
    bool close() override;

    [[nodiscard]] uint64_t frames_written() const override { return frames_written_; }
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

    // ── Converted frame cache (LRU, replaces the old single-entry digest cache) ──
    chronon3d::video::ConvertedFrameCache frame_cache_{};
    std::vector<uint8_t> cached_frame_bytes_; // reuse buffer for non-cache writes
    size_t cached_frame_size_{0};             // last converted frame byte count
    uint64_t current_frame_{0};              // incremented each write_frame() call

    uint64_t frames_written_{0};
    uint64_t bytes_written_{0};
    double total_write_blocked_ms_{0.0};
    bool pipe_failed_{false};

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
