#include "ffmpeg_pipe_sink.hpp"

// Phase-2 (TICKET-FFMPEG-PIPE-SINK-SPLIT): include the internal access shim.
#include "ffmpeg_pipe_sink_internal.hpp"

#include <chronon3d/media/video/video_frame.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace chronon3d::media::video {

// ============================================================================
//  validate_format() — check frame format against session contract
//
//  Phase-2 migration: moved from FfmpegPipeSink::validate_format to
//  FfmpegPipeSinkInternal::validate_format. Reads self.session_format_ +
//  self.width_ + self.height_ via the friend-struct access.
// ============================================================================

bool FfmpegPipeSinkInternal::validate_format(const FfmpegPipeSink& self, const VideoFrameView& frame) noexcept {
    if (frame.pixel_format != self.session_format_) {
        return false;
    }
    if (frame.width != self.width_ || frame.height != self.height_) {
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

    if (!frame.data || !FfmpegPipeSinkInternal::validate_format(*this, frame)) {
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
        ok = FfmpegPipeSinkInternal::write_to_pipe(*this, data, tight_frame_size_);
    } else {
        // Row padding: strip stride row-by-row.
        const auto* row = data;
        for (int y = 0; y < frame.height; ++y) {
            ok = FfmpegPipeSinkInternal::write_to_pipe(*this, row, tight_row_bytes);
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
//  submit_planar() — YUV420P (stream planes directly)
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

    const size_t y_row = static_cast<size_t>(frame.width);
    const size_t uv_row = static_cast<size_t>(frame.width / 2);
    const size_t y_stride = (frame.y_stride > 0) ? frame.y_stride : y_row;
    const size_t u_stride = (frame.u_stride > 0) ? frame.u_stride : uv_row;
    const size_t v_stride = (frame.v_stride > 0) ? frame.v_stride : uv_row;
    // FFmpeg rawvideo consumes Y, then U, then V. Write each source plane
    // directly; no Chronon-owned interleaving/staging buffer is needed.
    const auto t0 = std::chrono::steady_clock::now();
    const auto write_plane = [this](const void* data, size_t row_bytes,
                                    size_t stride, int rows) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        if (stride == row_bytes) {
            return FfmpegPipeSinkInternal::write_to_pipe(
                *this, bytes, row_bytes * static_cast<size_t>(rows));
        }
        for (int y = 0; y < rows; ++y) {
            if (!FfmpegPipeSinkInternal::write_to_pipe(*this, bytes, row_bytes)) {
                return false;
            }
            bytes += stride;
        }
        return true;
    };
    if (!write_plane(frame.y_data, y_row, y_stride, frame.height)
        || !write_plane(frame.u_data, uv_row, u_stride, frame.height / 2)
        || !write_plane(frame.v_data, uv_row, v_stride, frame.height / 2)) {
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
//  submit_biplanar() — NV12 (stream planes directly)
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

    const size_t y_row = static_cast<size_t>(frame.width);
    const size_t uv_row = static_cast<size_t>(frame.width);
    const size_t y_stride = (frame.y_stride > 0) ? frame.y_stride : y_row;
    const size_t uv_stride = (frame.uv_stride > 0) ? frame.uv_stride : uv_row;
    // FFmpeg rawvideo consumes Y followed by interleaved UV. Stream both
    // source planes directly and keep the process-boundary copy explicit.
    const auto t0 = std::chrono::steady_clock::now();
    const auto write_plane = [this](const void* data, size_t row_bytes,
                                    size_t stride, int rows) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        if (stride == row_bytes) {
            return FfmpegPipeSinkInternal::write_to_pipe(
                *this, bytes, row_bytes * static_cast<size_t>(rows));
        }
        for (int y = 0; y < rows; ++y) {
            if (!FfmpegPipeSinkInternal::write_to_pipe(*this, bytes, row_bytes)) {
                return false;
            }
            bytes += stride;
        }
        return true;
    };
    if (!write_plane(frame.y_data, y_row, y_stride, frame.height)
        || !write_plane(frame.uv_data, uv_row, uv_stride, frame.height / 2)) {
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
