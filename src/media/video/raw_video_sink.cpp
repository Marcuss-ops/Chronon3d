#include "raw_video_sink.hpp"

#include <chrono>
#include <cstring>

namespace chronon3d::media::video {

// ============================================================================
//  Construction / Destruction
// ============================================================================

RawVideoSink::~RawVideoSink() noexcept {
    // Ensure file is closed — no exceptions in destructor.
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
    state_ = VideoSinkState::Closed;
}

// ============================================================================
//  validate_frame_match() — session contract enforcement
// ============================================================================

bool RawVideoSink::validate_frame_match(int width, int height, PixelFormat fmt) noexcept {
    if (width <= 0 || height <= 0) {
        state_ = VideoSinkState::Failed;
        return false;
    }
    if (width != expected_width_ || height != expected_height_) {
        state_ = VideoSinkState::Failed;
        return false;
    }
    if (fmt != expected_format_) {
        state_ = VideoSinkState::Failed;
        return false;
    }
    // YUV420P and NV12 require even dimensions.
    if (fmt == PixelFormat::YUV420P || fmt == PixelFormat::NV12) {
        if (width % 2 != 0 || height % 2 != 0) {
            state_ = VideoSinkState::Failed;
            return false;
        }
    }
    return true;
}

// ============================================================================
//  open()
// ============================================================================

bool RawVideoSink::open(const VideoSinkConfig& config) {
    // Only Created → Open is valid.  Closed is terminal.
    if (state_ != VideoSinkState::Created) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    const auto& stream = config.stream;
    const auto fmt = config.stream.submitted_format;

    if (stream.width <= 0 || stream.height <= 0) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    // YUV420P and NV12 require even dimensions.
    if (fmt == PixelFormat::YUV420P || fmt == PixelFormat::NV12) {
        if (stream.width % 2 != 0 || stream.height % 2 != 0) {
            state_ = VideoSinkState::Failed;
            return false;
        }
    }

    // Open the output file.
    const auto& output = config.output;
    std::ios::openmode mode = std::ios::binary;
    if (!output.overwrite) {
        // Check if file exists — if so, fail.
        if (std::filesystem::exists(output.output_path)) {
            state_ = VideoSinkState::Failed;
            return false;
        }
    }

    file_.open(output.output_path, mode);
    if (!file_) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    // Store session contract.
    expected_width_  = stream.width;
    expected_height_ = stream.height;
    expected_format_ = fmt;

    // Compute stride and pre-allocate staging buffer for planar conversion.
    switch (fmt) {
        case PixelFormat::RGBA8:
            stride_ = static_cast<std::size_t>(stream.width) * 4;
            break;
        case PixelFormat::YUV420P:
        case PixelFormat::NV12:
            stride_ = static_cast<std::size_t>(stream.width);
            break;
        case PixelFormat::RGB24:
            stride_ = static_cast<std::size_t>(stream.width) * 3;
            break;
    }

    const uint64_t frame_bytes = frame_buffer_size(fmt, stream.width, stream.height);
    staging_.reserve(static_cast<std::size_t>(frame_bytes));

    // Reset stats for this session.
    stats_ = Stats{};
    state_ = VideoSinkState::Open;
    return true;
}

// ============================================================================
//  submit()  — interleaved/packed frame
// ============================================================================

bool RawVideoSink::submit(const VideoFrameView& frame) {
    if (state_ != VideoSinkState::Open) {
        return false;
    }

    if (!frame.data) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    // Validate session contract.
    if (!validate_frame_match(frame.width, frame.height, frame.pixel_format)) {
        return false;
    }

    // Validate packed stride (must be >= tight row; YUV planar only tight).
    if (!validate_packed_stride(frame.pixel_format, frame.width,
                                 frame.stride_bytes)) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    const auto t0 = std::chrono::steady_clock::now();
    const auto* data = static_cast<const uint8_t*>(frame.data);
    const size_t tight_row_bytes = frame_buffer_size(
        frame.pixel_format, frame.width, 1);  // bytes per tight row
    const size_t tight_frame_size = frame_buffer_size(
        frame.pixel_format, frame.width, frame.height);

    const size_t actual_stride = (frame.stride_bytes > 0)
        ? frame.stride_bytes
        : tight_row_bytes;

    bool ok;
    if (actual_stride == tight_row_bytes) {
        // Tight packing: write entire buffer in one shot.
        ok = write_bytes(data, tight_frame_size);
    } else {
        // Row padding present: write row-by-row skipping padding bytes.
        const auto* row = data;
        for (int y = 0; y < frame.height; ++y) {
            ok = write_bytes(row, tight_row_bytes);
            if (!ok) break;
            row += actual_stride;
        }
    }

    if (!ok) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double, std::milli>(t1 - t0).count();
    stats_.total_submit_ms += elapsed;
    stats_.submit_count++;
    stats_.frames_submitted++;
    stats_.bytes_written += tight_frame_size;
    return true;
}

// ============================================================================
//  submit_planar()  — YUV420P (3 separate planes → interleaved)
// ============================================================================

bool RawVideoSink::submit_planar(const PlanarVideoFrameView& frame) {
    if (state_ != VideoSinkState::Open) {
        return false;
    }

    if (!frame.y_data || !frame.u_data || !frame.v_data) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    // Validate session contract.
    if (!validate_frame_match(frame.width, frame.height, PixelFormat::YUV420P)) {
        return false;
    }

    // Validate dimensions and strides.
    if (!validate_planar_frame(frame.width, frame.height,
                                frame.y_stride, frame.u_stride, frame.v_stride)) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    const size_t y_row  = static_cast<size_t>(frame.width);
    const size_t uv_row = static_cast<size_t>(frame.width / 2);
    const size_t y_stride  = (frame.y_stride > 0) ? frame.y_stride : y_row;
    const size_t u_stride  = (frame.u_stride > 0) ? frame.u_stride : uv_row;
    const size_t v_stride  = (frame.v_stride > 0) ? frame.v_stride : uv_row;

    const auto t0 = std::chrono::steady_clock::now();

    // Pack Y + U + V into a single interleaved buffer.
    const size_t y_size  = y_row * static_cast<size_t>(frame.height);
    const size_t uv_size = y_size / 4;
    const size_t total   = y_size + uv_size * 2;  // YUV420P tight

    if (staging_.size() < total) {
        staging_.resize(total);
    }

    // Copy Y plane.
    if (y_stride == y_row) {
        std::memcpy(staging_.data(), frame.y_data, y_size);
    } else {
        const auto* src = static_cast<const uint8_t*>(frame.y_data);
        auto* dst = staging_.data();
        for (int y = 0; y < frame.height; ++y) {
            std::memcpy(dst, src, y_row);
            src += y_stride;
            dst += y_row;
        }
    }

    // Copy U plane.
    auto* u_dst = staging_.data() + y_size;
    if (u_stride == uv_row) {
        std::memcpy(u_dst, frame.u_data, uv_size);
    } else {
        const auto* src = static_cast<const uint8_t*>(frame.u_data);
        auto* dst = u_dst;
        for (int y = 0; y < frame.height / 2; ++y) {
            std::memcpy(dst, src, uv_row);
            src += u_stride;
            dst += uv_row;
        }
    }

    // Copy V plane.
    auto* v_dst = staging_.data() + y_size + uv_size;
    if (v_stride == uv_row) {
        std::memcpy(v_dst, frame.v_data, uv_size);
    } else {
        const auto* src = static_cast<const uint8_t*>(frame.v_data);
        auto* dst = v_dst;
        for (int y = 0; y < frame.height / 2; ++y) {
            std::memcpy(dst, src, uv_row);
            src += v_stride;
            dst += uv_row;
        }
    }

    const bool ok = write_bytes(staging_.data(), total);
    if (!ok) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double, std::milli>(t1 - t0).count();
    stats_.total_submit_ms += elapsed;
    stats_.submit_count++;
    stats_.frames_submitted++;
    stats_.bytes_written += total;
    return true;
}

// ============================================================================
//  submit_biplanar()  — NV12 (Y plane + interleaved UV)
// ============================================================================

bool RawVideoSink::submit_biplanar(const BiplanarVideoFrameView& frame) {
    if (state_ != VideoSinkState::Open) {
        return false;
    }

    if (!frame.y_data || !frame.uv_data) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    // Validate session contract.
    if (!validate_frame_match(frame.width, frame.height, PixelFormat::NV12)) {
        return false;
    }

    // Validate dimensions and strides.
    if (!validate_biplanar_frame(frame.width, frame.height,
                                  frame.y_stride, frame.uv_stride)) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    const size_t y_row  = static_cast<size_t>(frame.width);
    const size_t uv_row = static_cast<size_t>(frame.width);
    const size_t y_stride  = (frame.y_stride > 0) ? frame.y_stride : y_row;
    const size_t uv_stride = (frame.uv_stride > 0) ? frame.uv_stride : uv_row;

    const auto t0 = std::chrono::steady_clock::now();

    // Pack Y + UV into a single buffer.
    const size_t y_size  = y_row * static_cast<size_t>(frame.height);
    const size_t uv_size = y_size / 2;  // NV12 UV plane = w*h/2 bytes
    const size_t total   = y_size + uv_size;

    if (staging_.size() < total) {
        staging_.resize(total);
    }

    // Copy Y plane.
    if (y_stride == y_row) {
        std::memcpy(staging_.data(), frame.y_data, y_size);
    } else {
        const auto* src = static_cast<const uint8_t*>(frame.y_data);
        auto* dst = staging_.data();
        for (int y = 0; y < frame.height; ++y) {
            std::memcpy(dst, src, y_row);
            src += y_stride;
            dst += y_row;
        }
    }

    // Copy UV plane.
    auto* uv_dst = staging_.data() + y_size;
    if (uv_stride == uv_row) {
        std::memcpy(uv_dst, frame.uv_data, uv_size);
    } else {
        const auto* src = static_cast<const uint8_t*>(frame.uv_data);
        auto* dst = uv_dst;
        for (int y = 0; y < frame.height / 2; ++y) {
            std::memcpy(dst, src, uv_row);
            src += uv_stride;
            dst += uv_row;
        }
    }

    const bool ok = write_bytes(staging_.data(), total);
    if (!ok) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double, std::milli>(t1 - t0).count();
    stats_.total_submit_ms += elapsed;
    stats_.submit_count++;
    stats_.frames_submitted++;
    stats_.bytes_written += total;
    return true;
}

// ============================================================================
//  flush() / close()
// ============================================================================

bool RawVideoSink::flush() {
    if (state_ != VideoSinkState::Open) {
        return false;
    }

    const auto t0 = std::chrono::steady_clock::now();
    file_.flush();
    const auto t1 = std::chrono::steady_clock::now();

    if (!file_) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    stats_.total_flush_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
    return true;
}

bool RawVideoSink::close() noexcept {
    if (state_ == VideoSinkState::Closed) {
        return true;  // already closed
    }
    if (state_ == VideoSinkState::Created) {
        // close before open: release nothing, just mark terminal.
        state_ = VideoSinkState::Closed;
        return true;
    }

    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }

    // Clear session contract.
    expected_width_ = 0;
    expected_height_ = 0;
    expected_format_ = PixelFormat::RGBA8;

    state_ = VideoSinkState::Closed;
    return true;
}

// ============================================================================
//  Internal helpers
// ============================================================================

bool RawVideoSink::write_bytes(const uint8_t* data, size_t size) {
    if (!file_) {
        return false;
    }

    file_.write(reinterpret_cast<const char*>(data),
                static_cast<std::streamsize>(size));
    if (!file_) {
        return false;
    }

    return true;
}

} // namespace chronon3d::media::video
