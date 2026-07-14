#include "ffmpeg_pipe_sink.hpp"

#include <chronon3d/media/video/video_frame.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace chronon3d::media::video {

// ============================================================================
//  validate_format() — check frame format against session contract
//
//  Per-instance method.  Moved out of ffmpeg_pipe_sink.cpp per P2 item #26
//  (lives with the submit family in this TU for cohesion).
// ============================================================================

bool FfmpegPipeSink::validate_format(const VideoFrameView& frame) const noexcept {
    if (frame.pixel_format != session_format_) {
        return false;
    }
    if (frame.width != width_ || frame.height != height_) {
        return false;
    }
    return true;
}

// ============================================================================
//  submit() — interleaved/packed frame
// ============================================================================

bool FfmpegPipeSink::submit(const VideoFrameView& frame) {
    if (state_ != VideoSinkState::Open || pipe_failed_) {
        return false;
    }

    if (!frame.data || !validate_format(frame)) {
        last_error_ = VideoSinkError::InvalidFrame;
        last_error_msg_ = "frame format/dimensions don't match session contract";
        state_ = VideoSinkState::Failed;
        return false;
    }

    const auto t0 = std::chrono::steady_clock::now();
    const auto* data = static_cast<const uint8_t*>(frame.data);
    const size_t tight_row_bytes = frame_buffer_size(
        frame.pixel_format, frame.width, 1);
    const size_t actual_stride = (frame.stride_bytes > 0)
        ? frame.stride_bytes
        : tight_row_bytes;

    // Validate stride (must be >= tight row; YUV planar only tight).
    if (!validate_packed_stride(frame.pixel_format, frame.width,
                                 frame.stride_bytes)) {
        last_error_ = VideoSinkError::InvalidStride;
        last_error_msg_ = "stride < tight row bytes";
        state_ = VideoSinkState::Failed;
        return false;
    }

    bool ok;
    if (actual_stride == tight_row_bytes) {
        // Tight packing.
        ok = write_to_pipe(data, tight_frame_size_);
    } else {
        // Row padding: strip stride row-by-row.
        const auto* row = data;
        for (int y = 0; y < frame.height; ++y) {
            ok = write_to_pipe(row, tight_row_bytes);
            if (!ok) break;
            row += actual_stride;
        }
    }

    if (!ok) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    const auto t1 = std::chrono::steady_clock::now();
    stats_.total_submit_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
    stats_.submit_count++;
    stats_.frames_submitted++;
    return true;
}

// ============================================================================
//  submit_planar() — YUV420P (pack into interleaved, write)
// ============================================================================

bool FfmpegPipeSink::submit_planar(const PlanarVideoFrameView& frame) {
    if (state_ != VideoSinkState::Open || pipe_failed_) {
        return false;
    }

    if (!frame.y_data || !frame.u_data || !frame.v_data) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    // Validate dimensions and format.
    if (session_format_ != PixelFormat::YUV420P) {
        state_ = VideoSinkState::Failed;
        return false;
    }
    if (!validate_planar_frame(frame.width, frame.height,
                                frame.y_stride, frame.u_stride, frame.v_stride)) {
        state_ = VideoSinkState::Failed;
        return false;
    }
    if (frame.width != width_ || frame.height != height_) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    const size_t y_size  = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height);
    const size_t uv_size = y_size / 4;
    const size_t total   = y_size + uv_size * 2;

    if (staging_.size() < total) {
        staging_.resize(total);
    }

    const size_t y_row  = static_cast<size_t>(frame.width);
    const size_t uv_row = static_cast<size_t>(frame.width / 2);
    const size_t y_stride  = (frame.y_stride > 0) ? frame.y_stride : y_row;
    const size_t u_stride  = (frame.u_stride > 0) ? frame.u_stride : uv_row;
    const size_t v_stride  = (frame.v_stride > 0) ? frame.v_stride : uv_row;

    // Copy Y.
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

    // Copy U.
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

    // Copy V.
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

    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = write_to_pipe(staging_.data(), total);
    if (!ok) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    const auto t1 = std::chrono::steady_clock::now();
    stats_.total_submit_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
    stats_.submit_count++;
    stats_.frames_submitted++;
    return true;
}

// ============================================================================
//  submit_biplanar() — NV12 (pack into interleaved, write)
// ============================================================================

bool FfmpegPipeSink::submit_biplanar(const BiplanarVideoFrameView& frame) {
    if (state_ != VideoSinkState::Open || pipe_failed_) {
        return false;
    }

    if (!frame.y_data || !frame.uv_data) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    // Validate dimensions and format.
    if (session_format_ != PixelFormat::NV12) {
        state_ = VideoSinkState::Failed;
        return false;
    }
    if (!validate_biplanar_frame(frame.width, frame.height,
                                  frame.y_stride, frame.uv_stride)) {
        state_ = VideoSinkState::Failed;
        return false;
    }
    if (frame.width != width_ || frame.height != height_) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    const size_t y_size  = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height);
    const size_t uv_size = y_size / 2;
    const size_t total   = y_size + uv_size;

    if (staging_.size() < total) {
        staging_.resize(total);
    }

    const size_t y_row  = static_cast<size_t>(frame.width);
    const size_t uv_row = static_cast<size_t>(frame.width);
    const size_t y_stride  = (frame.y_stride > 0) ? frame.y_stride : y_row;
    const size_t uv_stride = (frame.uv_stride > 0) ? frame.uv_stride : uv_row;

    // Copy Y.
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

    // Copy UV.
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

    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = write_to_pipe(staging_.data(), total);
    if (!ok) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    const auto t1 = std::chrono::steady_clock::now();
    stats_.total_submit_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
    stats_.submit_count++;
    stats_.frames_submitted++;
    return true;
}

} // namespace chronon3d::media::video
