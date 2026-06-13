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
//  open()
// ============================================================================

bool RawVideoSink::open(const VideoSinkConfig& config) {
    if (state_ != VideoSinkState::Created && state_ != VideoSinkState::Closed) {
        return false;  // already open
    }

    const auto& stream = config.stream;
    if (stream.width <= 0 || stream.height <= 0) {
        state_ = VideoSinkState::Failed;
        return false;
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

    // Compute stride and pre-allocate staging buffer for planar conversion.
    const auto fmt = config.encoder.output_format;
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

    const auto t0 = std::chrono::steady_clock::now();
    const auto* data = static_cast<const uint8_t*>(frame.data);
    const size_t data_size = frame_buffer_size(
        frame.pixel_format, frame.width, frame.height);

    if (!data || data_size == 0) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    const bool ok = write_bytes(data, data_size);
    if (!ok) {
        state_ = VideoSinkState::Failed;
        return false;
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double, std::milli>(t1 - t0).count();
    stats_.total_submit_ms += elapsed;
    stats_.submit_count++;
    stats_.frames_submitted++;
    stats_.bytes_written += data_size;
    return true;
}

// ============================================================================
//  submit_planar()  — YUV420P (3 separate planes → interleaved)
// ============================================================================

bool RawVideoSink::submit_planar(const PlanarVideoFrameView& frame) {
    if (state_ != VideoSinkState::Open) {
        return false;
    }

    const auto t0 = std::chrono::steady_clock::now();

    // Pack Y + U + V into a single interleaved buffer.
    const size_t y_size  = static_cast<size_t>(frame.width) * frame.height;
    const size_t uv_size = y_size / 4;
    const size_t total   = y_size + uv_size * 2;  // YUV420P tight

    if (staging_.size() < total) {
        staging_.resize(total);
    }

    // Copy Y plane.
    if (frame.y_stride == 0 || frame.y_stride == static_cast<size_t>(frame.width)) {
        std::memcpy(staging_.data(), frame.y_data, y_size);
    } else {
        const auto* src = static_cast<const uint8_t*>(frame.y_data);
        auto* dst = staging_.data();
        for (int y = 0; y < frame.height; ++y) {
            std::memcpy(dst, src, static_cast<size_t>(frame.width));
            src += frame.y_stride;
            dst += static_cast<size_t>(frame.width);
        }
    }

    // Copy U plane.
    auto* u_dst = staging_.data() + y_size;
    if (frame.u_stride == 0 || frame.u_stride == static_cast<size_t>(frame.width / 2)) {
        std::memcpy(u_dst, frame.u_data, uv_size);
    } else {
        const auto* src = static_cast<const uint8_t*>(frame.u_data);
        auto* dst = u_dst;
        for (int y = 0; y < frame.height / 2; ++y) {
            std::memcpy(dst, src, static_cast<size_t>(frame.width / 2));
            src += frame.u_stride;
            dst += static_cast<size_t>(frame.width / 2);
        }
    }

    // Copy V plane.
    auto* v_dst = staging_.data() + y_size + uv_size;
    if (frame.v_stride == 0 || frame.v_stride == static_cast<size_t>(frame.width / 2)) {
        std::memcpy(v_dst, frame.v_data, uv_size);
    } else {
        const auto* src = static_cast<const uint8_t*>(frame.v_data);
        auto* dst = v_dst;
        for (int y = 0; y < frame.height / 2; ++y) {
            std::memcpy(dst, src, static_cast<size_t>(frame.width / 2));
            src += frame.v_stride;
            dst += static_cast<size_t>(frame.width / 2);
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

    const auto t0 = std::chrono::steady_clock::now();

    // Pack Y + UV into a single buffer.
    const size_t y_size  = static_cast<size_t>(frame.width) * frame.height;
    const size_t uv_size = y_size / 2;  // NV12 UV plane = w*h/2 bytes
    const size_t total   = y_size + uv_size;

    if (staging_.size() < total) {
        staging_.resize(total);
    }

    // Copy Y plane.
    if (frame.y_stride == 0 || frame.y_stride == static_cast<size_t>(frame.width)) {
        std::memcpy(staging_.data(), frame.y_data, y_size);
    } else {
        const auto* src = static_cast<const uint8_t*>(frame.y_data);
        auto* dst = staging_.data();
        for (int y = 0; y < frame.height; ++y) {
            std::memcpy(dst, src, static_cast<size_t>(frame.width));
            src += frame.y_stride;
            dst += static_cast<size_t>(frame.width);
        }
    }

    // Copy UV plane.
    auto* uv_dst = staging_.data() + y_size;
    if (frame.uv_stride == 0 || frame.uv_stride == static_cast<size_t>(frame.width)) {
        std::memcpy(uv_dst, frame.uv_data, uv_size);
    } else {
        const auto* src = static_cast<const uint8_t*>(frame.uv_data);
        auto* dst = uv_dst;
        for (int y = 0; y < frame.height / 2; ++y) {
            std::memcpy(dst, src, static_cast<size_t>(frame.width));
            src += frame.uv_stride;
            dst += static_cast<size_t>(frame.width);
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

    state_ = VideoSinkState::Flushing;
    file_.flush();
    if (!file_) {
        state_ = VideoSinkState::Failed;
        return false;
    }
    return true;
}

bool RawVideoSink::close() noexcept {
    if (state_ == VideoSinkState::Closed || state_ == VideoSinkState::Created) {
        return true;  // idempotent
    }

    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }

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

    stats_.frames_submitted++;
    stats_.bytes_written += size;
    return true;
}

} // namespace chronon3d::media::video
