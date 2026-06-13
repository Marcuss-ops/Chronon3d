#include "ffmpeg_pipe_encoder.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <chrono>
#include <thread>
#include <spdlog/spdlog.h>

#if defined(_WIN32)
    #include <io.h>
#elif defined(__linux__)
    #include <fcntl.h>
    #include <unistd.h>
    #include <dirent.h>
    #include <ctype.h>
#endif


#if defined(__linux__)
// Find the PID of a child process whose comm starts with "ffmpeg".
// After popen(), the child is already forked and running. We scan /proc
// for processes whose parent is our PID and whose comm starts with "ffmpeg".
static int resolve_ffmpeg_child_pid() {
    const pid_t my_pid = getpid();
    DIR* proc_dir = opendir("/proc");
    if (!proc_dir) return -1;

    int found_pid = -1;
    struct dirent* entry;
    while ((entry = readdir(proc_dir)) != nullptr) {
        if (!isdigit(entry->d_name[0])) continue;
        pid_t child_pid = static_cast<pid_t>(atoi(entry->d_name));
        if (child_pid == my_pid) continue;

        char statpath[64];
        snprintf(statpath, sizeof(statpath), "/proc/%d/stat", child_pid);
        FILE* sf = fopen(statpath, "r");
        if (!sf) continue;

        char buf[512];
        if (fgets(buf, sizeof(buf), sf)) {
            const char* paren_open = strchr(buf, '(');
            const char* paren_close = strrchr(buf, ')');
            if (paren_open && paren_close && paren_close > paren_open) {
                size_t comm_len = paren_close - paren_open - 1;
                if (comm_len < sizeof(buf)) {
                    char comm[256] = {0};
                    strncpy(comm, paren_open + 1, comm_len);

                    const char* after_paren = paren_close + 1;
                    while (*after_paren == ' ') ++after_paren;
                    pid_t ppid = static_cast<pid_t>(atoi(after_paren));

                    if (ppid == my_pid && strncmp(comm, "ffmpeg", 6) == 0) {
                        found_pid = child_pid;
                    }
                }
            }
        }
        fclose(sf);
        if (found_pid != -1) break;
    }
    closedir(proc_dir);
    return found_pid;
}
#else
static int resolve_ffmpeg_child_pid() { return -1; }
#endif


namespace chronon3d::cli {

[[nodiscard]] int encode_color_matrix_id(const FfmpegPipeOptions& options) {
    switch (options.color_transform.output) {
        case chronon3d::color::ColorSpace::Rec709:
        case chronon3d::color::ColorSpace::SRGB:
        case chronon3d::color::ColorSpace::LinearSRGB:
        default:
            return 0;
    }
}

bool FfmpegPipeEncoder::open(const FfmpegPipeOptions& options) {
    if (pipe_) {
        return false;
    }

    if (options.width <= 0 || options.height <= 0 ||
        options.fps <= 0 || options.output_path.empty()) {
        return false;
    }

    options_ = options;
    if ((options_.input_format == PipePixelFormat::YUV420P || options_.input_format == PipePixelFormat::NV12) &&
        (options_.width % 2 != 0 || options_.height % 2 != 0)) {
        return false;
    }

    frames_written_ = 0;
    bytes_written_ = 0;
    pipe_failed_ = false;

    const size_t w = static_cast<size_t>(options_.width);
    const size_t h = static_cast<size_t>(options_.height);

    if (options_.input_format == PipePixelFormat::YUV420P || options_.input_format == PipePixelFormat::NV12) {
        const size_t size = w * h + (w * h) / 2u;
        yuv_buffer_.assign(size + 256, 0);
#ifdef __linux__
        ring_buffer_size_ = size;
#endif
        // Pre-size the cached-frame reuse buffer so write_frame() never
        // needs to check-and-resize in the hot path.
        cached_frame_bytes_.assign(size + 256, 0);
    } else {
        rgba_buffer_.assign(w * h * 4u + 256, 0);
#ifdef __linux__
        ring_buffer_size_ = w * h * 4;
#endif
        cached_frame_bytes_.assign(w * h * 4u + 256, 0);
    }

    const std::string cmd = build_ffmpeg_raw_pipe_command(options_);

#if defined(_WIN32)
    pipe_ = _popen(cmd.c_str(), "wb");
#else
    pipe_ = popen(cmd.c_str(), "w");
    if (pipe_) {
        setvbuf(pipe_, nullptr, _IOFBF, 1 << 20);
#if defined(__linux__)
        // ffmpeg reads from a kernel pipe; enlarge it when possible so the
        // producer can absorb short bursts without blocking immediately.
        const int fd = fileno(pipe_);
        if (fd >= 0) {
            const size_t frame_pixels = static_cast<size_t>(options_.width) *
                                        static_cast<size_t>(options_.height);
            const size_t frame_bytes = options_.input_format == PipePixelFormat::RGBA
                ? frame_pixels * 4ULL
                : frame_pixels * 3ULL / 2ULL;
            const int desired = static_cast<int>(std::clamp<size_t>(frame_bytes * 2ULL, 1ULL * 1024ULL * 1024ULL, 16ULL * 1024ULL * 1024ULL));
            (void)fcntl(fd, F_SETPIPE_SZ, desired);
        }
        // Resolve the FFmpeg child PID for CPU% monitoring
        ffmpeg_pid_ = resolve_ffmpeg_child_pid();
#endif
    }
#endif

#ifdef __linux__
    // ── Async pipe I/O via io_uring (opt-in, experimental) ──────────────
    // io_uring replaces blocking fwrite() with submission-queue writes.
    // Disabled by default because it can produce corrupted output on some
    // kernels and lacks full EINTR/short-write handling.  Enable explicitly
    // with --pipe-writer io_uring.
    if (pipe_ && options_.pipe_writer == "io_uring") {
        if (init_uring()) {
            use_uring_ = true;
        } else {
            spdlog::warn("[video] io_uring init failed, falling back to classic write");
        }
    }
#endif

    return pipe_ != nullptr;
}

void FfmpegPipeEncoder::drain_staged_slots() {
    // Convert any slots still in Staged state (state==2) synchronously.
    // This runs on the calling thread (close path) after new frames are
    // no longer accepted, so there is no producer contention.
    for (size_t i = 0; i < kAsyncConversionSlots; ++i) {
        auto& slot = async_slots_[i];
        int expected = static_cast<int>(SlotState::Staged);
        if (!slot.state.compare_exchange_strong(
                expected, static_cast<int>(SlotState::Reserved),
                std::memory_order_acquire, std::memory_order_relaxed)) {
            continue;
        }

        const size_t req_size =
            (options_.input_format == PipePixelFormat::RGBA)
                ? static_cast<size_t>(options_.width) *
                  static_cast<size_t>(options_.height) * 4u
                : static_cast<size_t>(options_.width) *
                  static_cast<size_t>(options_.height) * 3u / 2u;
        if (slot.converted.size() < req_size) {
            slot.converted.resize(req_size);
        }

        bool converted = false;
        if (slot.can_cache) {
            const auto* hit = frame_cache_.lookup(slot.cache_key);
            if (hit) {
                if (slot.converted.size() < hit->data_size) {
                    slot.converted.resize(hit->data_size);
                }
                std::memcpy(slot.converted.data(), hit->data.data(), hit->data_size);
                converted = true;
            }
        }

        if (!converted) {
            bool ok = false;
            switch (options_.input_format) {
                case PipePixelFormat::YUV420P:
                    ok = convert_framebuffer_to_yuv420p(*slot.fb, slot.converted.data());
                    break;
                case PipePixelFormat::NV12:
                    ok = convert_framebuffer_to_nv12(*slot.fb, slot.converted.data());
                    break;
                case PipePixelFormat::RGBA:
                    ok = convert_framebuffer_to_rgba(*slot.fb, slot.converted.data());
                    break;
            }
            if (ok && slot.can_cache) {
                frame_cache_.insert(slot.cache_key, slot.converted.data(), req_size);
            }
            if (!ok) {
                slot.state.store(static_cast<int>(SlotState::Failed),
                                 std::memory_order_release);
                slot.fb.reset();
                continue;
            }
        }

        slot.fb.reset();
        slot.state.store(static_cast<int>(SlotState::Converted),
                         std::memory_order_release);
    }
}

bool FfmpegPipeEncoder::close() {
    if (!pipe_) {
        return true;
    }

    // ── Protocol: stop converter first, then drain ────────────────────
    // The caller guarantees no new frames are submitted after close().
    // Stop the converter thread first to avoid racing on staged slots.
    converter_stop_.store(true, std::memory_order_release);
    if (converter_thread_.joinable()) {
        converter_thread_.join();
    }

    // 1. Convert any remaining staged slots synchronously (converter is stopped).
    drain_staged_slots();

    // 2. Flush all converted frames to the pipe in FIFO order.
    while (write_next_converted_frame()) {}

#ifdef __linux__
    if (use_uring_) {
        while (pending_writes_count_ > 0) {
            reap_completed_uring(true);
        }
        cleanup_uring();
    }
#endif

#if defined(_WIN32)
    const int rc = _pclose(pipe_);
#else
    const int rc = pclose(pipe_);
#endif

    pipe_ = nullptr;
    return rc == 0;
}

FfmpegPipeEncoder::~FfmpegPipeEncoder() {
    if (pipe_) {
        close();
    }
}



} // namespace chronon3d::cli
