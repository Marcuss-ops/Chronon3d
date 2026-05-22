#include "ffmpeg_pipe_encoder.hpp"

#include <chronon3d/math/color.hpp>
#include <fmt/format.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <chrono>

#if defined(_WIN32)
    #include <io.h>
#endif

#ifdef __linux__
    #include <sys/mman.h>
    #include <sys/syscall.h>
    #include <unistd.h>
    #include <linux/io_uring.h>
    #include <atomic>
    #include <cstring>
#endif

namespace chronon3d::cli {
namespace {

std::string quote_path(const std::string& path) {
    std::string out = "\"";
    for (char c : path) {
        if (c == '"') {
            out += "\\\"";
        } else {
            out += c;
        }
    }
    out += "\"";
    return out;
}

std::string pix_fmt_to_ffmpeg_str(PipePixelFormat fmt) {
    switch (fmt) {
        case PipePixelFormat::RGBA:    return "rgba";
        case PipePixelFormat::YUV420P: return "yuv420p";
        case PipePixelFormat::NV12:    return "nv12";
    }
    return "rgba";
}

} // namespace

std::string build_ffmpeg_raw_pipe_command(const FfmpegPipeOptions& options) {
    const std::string log_flags = options.verbose
        ? ""
        : "-hide_banner -loglevel error ";

    return fmt::format(
        "ffmpeg -y "
        "{}"
        "-f rawvideo "
        "-pix_fmt {} "
        "-s {}x{} "
        "-r {} "
        "-i - "
        "-an "
        "-c:v {} "
        "-crf {} "
        "-preset {} "
        "-pix_fmt {} "
        "{}",
        log_flags,
        pix_fmt_to_ffmpeg_str(options.input_format),
        options.width,
        options.height,
        options.fps,
        options.codec,
        options.crf,
        options.preset,
        options.output_pix_fmt,
        quote_path(options.output_path)
    );
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
    frames_written_ = 0;
    bytes_written_ = 0;

    const size_t w = static_cast<size_t>(options_.width);
    const size_t h = static_cast<size_t>(options_.height);

    switch (options_.input_format) {
        case PipePixelFormat::RGBA:
            rgba_buffer_.assign(w * h * 4u, 0);
            break;
        case PipePixelFormat::YUV420P:
            y_plane_.assign(w * h, 0);
            u_plane_.assign(w * h / 4u, 0);
            v_plane_.assign(w * h / 4u, 0);
            break;
        case PipePixelFormat::NV12:
            y_plane_.assign(w * h, 0);
            u_plane_.assign(w * h / 4u, 0);
            v_plane_.assign(w * h / 4u, 0);
            nv12_uv_plane_.assign(w * h / 2u, 0);
            break;
    }

#ifdef __linux__
    ring_buffer_size_ = (options_.input_format == PipePixelFormat::RGBA) ? (w * h * 4) : (w * h * 3 / 2);
#endif

    const std::string cmd = build_ffmpeg_raw_pipe_command(options_);

#if defined(_WIN32)
    pipe_ = _popen(cmd.c_str(), "wb");
#else
    pipe_ = popen(cmd.c_str(), "w");
#endif

#ifdef __linux__
    if (pipe_ && options_.pipe_writer == "io_uring") {
        if (init_uring()) {
            use_uring_ = true;
        }
    }
#endif

    return pipe_ != nullptr;
}

bool FfmpegPipeEncoder::convert_framebuffer_to_rgba(const Framebuffer& fb, uint8_t* dst) {
    if (fb.width() != options_.width || fb.height() != options_.height) {
        return false;
    }

    const size_t count =
        static_cast<size_t>(options_.width) *
        static_cast<size_t>(options_.height);

    if (!dst && rgba_buffer_.size() != count * 4u) {
        return false;
    }

    const Color* src = fb.data();
    uint8_t* dst_ptr = dst ? dst : rgba_buffer_.data();

    for (size_t i = 0; i < count; ++i) {
        const auto rgb = color::linear_to_output_rgb8(src[i], options_.color_transform);
        dst_ptr[i * 4 + 0] = rgb.r;
        dst_ptr[i * 4 + 1] = rgb.g;
        dst_ptr[i * 4 + 2] = rgb.b;
        // Alpha is not gamma-encoded — store as-is (linear → sRGB is purely for
        // alpha channel visualisation, not a color-space transform).
        dst_ptr[i * 4 + 3] = Color::linear_to_srgb8(src[i].a);
    }

    return true;
}

// BT.709 coefficients for HD content
// Y =  0.2126 R + 0.7152 G + 0.0722 B
// U = -0.1146 R - 0.3854 G + 0.5000 B + 128
// V =  0.5000 R - 0.4542 G - 0.0458 B + 128
bool FfmpegPipeEncoder::convert_framebuffer_to_yuv420p(const Framebuffer& fb, uint8_t* dst) {
    if (fb.width() != options_.width || fb.height() != options_.height) {
        return false;
    }

    const int w = options_.width;
    const int h = options_.height;

    if (w % 2 != 0 || h % 2 != 0) {
        return false; // YUV420p requires even dimensions
    }

    const size_t y_size  = static_cast<size_t>(w) * static_cast<size_t>(h);
    const size_t uv_size = y_size / 4u;

    if (!dst && (y_plane_.size() != y_size || u_plane_.size() != uv_size || v_plane_.size() != uv_size)) {
        return false;
    }

    uint8_t* y_ptr = dst ? dst : y_plane_.data();
    uint8_t* u_ptr = dst ? (dst + y_size) : u_plane_.data();
    uint8_t* v_ptr = dst ? (dst + y_size + uv_size) : v_plane_.data();

    const Color* src = fb.data();

    // Float-level transform avoids uint8 round-trip, preserving precision
    // for the YUV matrix that follows.
    auto srgb_float = [&](const Color& c) -> std::array<float, 3> {
        return color::linear_to_output_rgb_float(c, options_.color_transform);
    };

    // Luma plane (full resolution)
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Color& c = src[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
            const auto srgb = srgb_float(c);
            const float r = srgb[0];
            const float g = srgb[1];
            const float b = srgb[2];

            float yy = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            yy = std::clamp(yy, 0.0f, 1.0f);

            y_ptr[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] =
                static_cast<uint8_t>(yy * 219.0f + 16.0f + 0.5f);
        }
    }

    // Chroma planes (2x2 subsampled)
    for (int y = 0; y < h / 2; ++y) {
        for (int x = 0; x < w / 2; ++x) {
            // Average 2x2 block
            const int base_x = x * 2;
            const int base_y = y * 2;

            float r_sum = 0.0f, g_sum = 0.0f, b_sum = 0.0f;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const Color& c = src[static_cast<size_t>(base_y + dy) * static_cast<size_t>(w) + static_cast<size_t>(base_x + dx)];
                    const auto srgb = srgb_float(c);
                    r_sum += srgb[0];
                    g_sum += srgb[1];
                    b_sum += srgb[2];
                }
            }

            const float r = r_sum / 4.0f;
            const float g = g_sum / 4.0f;
            const float b = b_sum / 4.0f;

            float uu = -0.1146f * r - 0.3854f * g + 0.5000f * b;
            uu = std::clamp(uu, -0.5f, 0.5f);
            u_ptr[static_cast<size_t>(y) * static_cast<size_t>(w / 2) + static_cast<size_t>(x)] =
                static_cast<uint8_t>(uu * 224.0f + 128.0f + 0.5f);

            float vv = 0.5000f * r - 0.4542f * g - 0.0458f * b;
            vv = std::clamp(vv, -0.5f, 0.5f);
            v_ptr[static_cast<size_t>(y) * static_cast<size_t>(w / 2) + static_cast<size_t>(x)] =
                static_cast<uint8_t>(vv * 224.0f + 128.0f + 0.5f);
        }
    }

    return true;
}

bool FfmpegPipeEncoder::convert_framebuffer_to_nv12(const Framebuffer& fb, uint8_t* dst) {
    if (fb.width() != options_.width || fb.height() != options_.height) {
        return false;
    }

    if (options_.width % 2 != 0 || options_.height % 2 != 0) {
        return false;
    }

    // First convert to YUV420p planes
    if (!convert_framebuffer_to_yuv420p(fb, nullptr)) {
        return false;
    }

    const size_t w = static_cast<size_t>(options_.width);
    const size_t h = static_cast<size_t>(options_.height);
    const size_t y_size = w * h;
    const size_t uv_total = (w / 2) * (h / 2);

    if (!dst && nv12_uv_plane_.size() != uv_total * 2u) {
        return false;
    }

    uint8_t* y_ptr = dst ? dst : y_plane_.data();
    uint8_t* uv_ptr = dst ? (dst + y_size) : nv12_uv_plane_.data();

    if (dst) {
        std::copy(y_plane_.begin(), y_plane_.end(), y_ptr);
    }

    // Interleave U and V: UVUVUV...
    for (size_t i = 0; i < uv_total; ++i) {
        uv_ptr[i * 2 + 0] = u_plane_[i];
        uv_ptr[i * 2 + 1] = v_plane_[i];
    }

    return true;
}

bool FfmpegPipeEncoder::write_frame(const Framebuffer& fb) {
    if (!pipe_) {
        return false;
    }

#ifdef __linux__
    if (use_uring_) {
        // Wait for slot to become free if all busy
        while (pending_writes_count_ >= kRingEntries) {
            reap_completed_uring(true);
        }

        // Find the next free buffer in pool
        size_t buf_idx = ring_buffer_index_;
        while (ring_buffer_pending_[buf_idx]) {
            reap_completed_uring(true);
            buf_idx = ring_buffer_index_;
        }

        uint8_t* dst = ring_buffers_[buf_idx].data();
        bool ok = false;
        if (options_.input_format == PipePixelFormat::RGBA) {
            ok = convert_framebuffer_to_rgba(fb, dst);
        } else if (options_.input_format == PipePixelFormat::YUV420P) {
            ok = convert_framebuffer_to_yuv420p(fb, dst);
        } else if (options_.input_format == PipePixelFormat::NV12) {
            ok = convert_framebuffer_to_nv12(fb, dst);
        }

        if (!ok) return false;

        ring_buffer_pending_[buf_idx] = true;
        ring_buffer_bytes_written_[buf_idx] = 0;
        ring_buffer_index_ = (buf_idx + 1) % kRingEntries;

        const auto t0 = std::chrono::high_resolution_clock::now();
        bool written = write_uring(buf_idx, ring_buffer_size_);
        const auto t1 = std::chrono::high_resolution_clock::now();
        total_write_blocked_ms_ += std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (!written) return false;

        ++frames_written_;
        return true;
    }
#endif

    auto timed_fwrite = [&](const void* ptr, size_t size, size_t count, FILE* stream) -> size_t {
        const auto t0 = std::chrono::high_resolution_clock::now();
        size_t res = std::fwrite(ptr, size, count, stream);
        const auto t1 = std::chrono::high_resolution_clock::now();
        total_write_blocked_ms_ += std::chrono::duration<double, std::milli>(t1 - t0).count();
        return res;
    };

    switch (options_.input_format) {
        case PipePixelFormat::RGBA: {
            if (!convert_framebuffer_to_rgba(fb)) {
                return false;
            }
            const size_t written = timed_fwrite(
                rgba_buffer_.data(), 1, rgba_buffer_.size(), pipe_);
            if (written != rgba_buffer_.size()) {
                return false;
            }
            bytes_written_ += written;
            break;
        }
        case PipePixelFormat::YUV420P: {
            if (!convert_framebuffer_to_yuv420p(fb)) {
                return false;
            }
            // Write planes: Y, U, V
            size_t w1 = timed_fwrite(y_plane_.data(), 1, y_plane_.size(), pipe_);
            size_t w2 = timed_fwrite(u_plane_.data(), 1, u_plane_.size(), pipe_);
            size_t w3 = timed_fwrite(v_plane_.data(), 1, v_plane_.size(), pipe_);
            if (w1 != y_plane_.size() || w2 != u_plane_.size() || w3 != v_plane_.size()) {
                return false;
            }
            bytes_written_ += w1 + w2 + w3;
            break;
        }
        case PipePixelFormat::NV12: {
            if (!convert_framebuffer_to_nv12(fb)) {
                return false;
            }
            // Write planes: Y, interleaved UV
            size_t w1 = timed_fwrite(y_plane_.data(), 1, y_plane_.size(), pipe_);
            size_t w2 = timed_fwrite(nv12_uv_plane_.data(), 1, nv12_uv_plane_.size(), pipe_);
            if (w1 != y_plane_.size() || w2 != nv12_uv_plane_.size()) {
                return false;
            }
            bytes_written_ += w1 + w2;
            break;
        }
    }

    ++frames_written_;
    return true;
}

bool FfmpegPipeEncoder::close() {
    if (!pipe_) {
        return true;
    }

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

#ifdef __linux__
bool FfmpegPipeEncoder::init_uring() {
    struct io_uring_params p;
    std::memset(&p, 0, sizeof(p));

    ring_fd_ = syscall(__NR_io_uring_setup, kRingEntries, &p);
    if (ring_fd_ < 0) {
        return false;
    }

    sq_mmap_len_ = p.sq_off.array + p.sq_entries * sizeof(unsigned);
    cq_mmap_len_ = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);
    sqes_mmap_len_ = p.sq_entries * sizeof(struct io_uring_sqe);

    void* sq_ptr = mmap(0, sq_mmap_len_, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring_fd_, IORING_OFF_SQ_RING);
    if (sq_ptr == MAP_FAILED) {
        cleanup_uring();
        return false;
    }
    sq_mmap_ = sq_ptr;

    void* cq_ptr = mmap(0, cq_mmap_len_, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring_fd_, IORING_OFF_CQ_RING);
    if (cq_ptr == MAP_FAILED) {
        cleanup_uring();
        return false;
    }
    cq_mmap_ = cq_ptr;

    void* sqes_ptr = mmap(0, sqes_mmap_len_, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring_fd_, IORING_OFF_SQES);
    if (sqes_ptr == MAP_FAILED) {
        cleanup_uring();
        return false;
    }
    sqes_mmap_ = sqes_ptr;

    unsigned char* sq_bytes = static_cast<unsigned char*>(sq_mmap_);
    sq_head_ = reinterpret_cast<unsigned*>(sq_bytes + p.sq_off.head);
    sq_tail_ = reinterpret_cast<unsigned*>(sq_bytes + p.sq_off.tail);
    sq_ring_mask_ = reinterpret_cast<unsigned*>(sq_bytes + p.sq_off.ring_mask);
    sq_ring_entries_ = reinterpret_cast<unsigned*>(sq_bytes + p.sq_off.ring_entries);
    sq_flags_ = reinterpret_cast<unsigned*>(sq_bytes + p.sq_off.flags);
    sq_array_ = reinterpret_cast<unsigned*>(sq_bytes + p.sq_off.array);
    sq_mask_ = *sq_ring_mask_;
    sqes_ = sqes_mmap_;

    unsigned char* cq_bytes = static_cast<unsigned char*>(cq_mmap_);
    cq_head_ = reinterpret_cast<unsigned*>(cq_bytes + p.cq_off.head);
    cq_tail_ = reinterpret_cast<unsigned*>(cq_bytes + p.cq_off.tail);
    cq_ring_mask_ = reinterpret_cast<unsigned*>(cq_bytes + p.cq_off.ring_mask);
    cq_ring_entries_ = reinterpret_cast<unsigned*>(cq_bytes + p.cq_off.ring_entries);
    cqes_ = cq_bytes + p.cq_off.cqes;
    cq_mask_ = *cq_ring_mask_;

    // Allocate ring buffers pool
    ring_buffers_.resize(kRingEntries);
    for (size_t i = 0; i < kRingEntries; ++i) {
        ring_buffers_[i].resize(ring_buffer_size_);
    }
    ring_buffer_pending_.assign(kRingEntries, false);
    ring_buffer_bytes_written_.assign(kRingEntries, 0);
    ring_buffer_index_ = 0;
    pending_writes_count_ = 0;

    return true;
}

void FfmpegPipeEncoder::cleanup_uring() {
    if (sqes_mmap_) {
        munmap(sqes_mmap_, sqes_mmap_len_);
        sqes_mmap_ = nullptr;
        sqes_ = nullptr;
    }
    if (cq_mmap_) {
        munmap(cq_mmap_, cq_mmap_len_);
        cq_mmap_ = nullptr;
        cqes_ = nullptr;
    }
    if (sq_mmap_) {
        munmap(sq_mmap_, sq_mmap_len_);
        sq_mmap_ = nullptr;
    }
    if (ring_fd_ >= 0) {
        ::close(ring_fd_);
        ring_fd_ = -1;
    }
    use_uring_ = false;
}

void FfmpegPipeEncoder::reap_completed_uring(bool wait) {
    if (ring_fd_ < 0) return;

    unsigned head = *cq_head_;
    unsigned tail = *cq_tail_;

    if (wait && head == tail) {
        // Wait for at least 1 event
        syscall(__NR_io_uring_enter, ring_fd_, 0, 1, IORING_ENTER_GETEVENTS, nullptr);
        tail = *cq_tail_;
    }

    struct io_uring_cqe* cqes = static_cast<struct io_uring_cqe*>(cqes_);
    while (head != tail) {
        struct io_uring_cqe* cqe = &cqes[head & cq_mask_];
        uint64_t buf_idx = cqe->user_data;
        int res = cqe->res;

        // Every CQE we process corresponds to exactly one SQE in flight,
        // so we must decrement the in-flight SQE count.
        if (pending_writes_count_ > 0) {
            pending_writes_count_--;
        }

        if (buf_idx < kRingEntries) {
            if (res < 0) {
                // If it was interrupted by a signal, we can retry (treat as 0 bytes written).
                // Otherwise, treat other errors (like EPIPE, EAGAIN) as failures.
                if (res == -EINTR) {
                    res = 0;
                } else {
                    ring_buffer_pending_[buf_idx] = false;
                    head++;
                    continue;
                }
            }

            if (res == 0) {
                // 0 bytes written indicates EOF / closed pipe. Stop to avoid infinite loop.
                ring_buffer_pending_[buf_idx] = false;
                head++;
                continue;
            }

            size_t written = static_cast<size_t>(res);
            ring_buffer_bytes_written_[buf_idx] += written;

            if (ring_buffer_bytes_written_[buf_idx] < ring_buffer_size_) {
                // Partial write, resubmit the rest of the buffer!
                // write_uring will submit the next SQE starting at the correct offset
                write_uring(buf_idx, ring_buffer_size_);
            } else {
                // Fully completed!
                ring_buffer_pending_[buf_idx] = false;
            }
        }
        head++;
    }

    std::atomic_thread_fence(std::memory_order_release);
    *cq_head_ = head;
}

bool FfmpegPipeEncoder::write_uring(size_t buf_idx, size_t size) {
    size_t offset = ring_buffer_bytes_written_[buf_idx];
    size_t remaining = size - offset;

    // Get SQE
    unsigned sq_tail = *sq_tail_;
    unsigned index = sq_tail & sq_mask_;
    struct io_uring_sqe* sqe = &static_cast<struct io_uring_sqe*>(sqes_)[index];
    std::memset(sqe, 0, sizeof(*sqe));

    sqe->opcode = IORING_OP_WRITE;
    sqe->fd = fileno(pipe_);
    sqe->addr = reinterpret_cast<uint64_t>(ring_buffers_[buf_idx].data() + offset);
    sqe->len = remaining;
    sqe->off = 0;
    sqe->user_data = buf_idx;

    sq_array_[index] = index;
    *sq_tail_ = sq_tail + 1;
    pending_writes_count_++;

    std::atomic_thread_fence(std::memory_order_release);

    int submitted = syscall(__NR_io_uring_enter, ring_fd_, 1, 0, 0, nullptr);
    if (submitted < 0) {
        return false;
    }

    bytes_written_ += remaining;
    return true;
}
#endif

} // namespace chronon3d::cli
