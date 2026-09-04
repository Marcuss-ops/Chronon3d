#include "video_sink_adapter.hpp"

#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>

#include <chrono>
#include <memory>
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

bool VideoSinkEncoderAdapter::convert_and_submit(const Framebuffer& fb) {
    if (!sink_) return false;
    using media::video::PixelFormat;
    const auto fmt = map_pixel_format(input_format_);

    video::EncoderPixelFormat enc_fmt;
    switch (input_format_) {
        case PipePixelFormat::YUV420P: enc_fmt = video::EncoderPixelFormat::YUV420P; break;
        case PipePixelFormat::NV12: enc_fmt = video::EncoderPixelFormat::NV12; break;
        case PipePixelFormat::RGBA:
        default: enc_fmt = video::EncoderPixelFormat::RGBA8; break;
    }

    if (!encoder_pool_) {
        spdlog::error("[video_adapter] encoder frame pool is not initialized");
        return false;
    }
    auto encoder_frame = encoder_pool_->acquire();
    if (!encoder_frame) {
        spdlog::error("[video_adapter] encoder frame pool is exhausted");
        return false;
    }
    const size_t expected_size = encoder_frame.storage.size();

    const auto conv_t0 = profiling::now();
    const uint64_t pixel_before = counters_
        ? counters_->pixel_format_convert_wall_ms.load(std::memory_order_relaxed) : 0;
    const uint64_t color_before = counters_
        ? counters_->color_space_convert_wall_ms.load(std::memory_order_relaxed) : 0;

    const video::ConversionOptions copts{
        .width = width_,
        .height = height_,
        .format = enc_fmt,
        .apply_gamma = options_.color_transform.apply_gamma,
        .matrix = video::YuvMatrix::BT709,
        .range = video::ColorRange::Limited,
        .use_cache = false,
    };
    auto converted = conv_svc_.convert_into(
        fb, copts, encoder_frame.storage.data(), expected_size);

    const auto conv_t1 = profiling::now();
    const uint64_t pixel_after = counters_
        ? counters_->pixel_format_convert_wall_ms.load(std::memory_order_relaxed) : 0;
    const uint64_t color_after = counters_
        ? counters_->color_space_convert_wall_ms.load(std::memory_order_relaxed) : 0;
    const double conv_ms = profiling::duration_ms(conv_t0, conv_t1);
    if (!converted) {
        spdlog::error("[video_adapter] Frame conversion failed");
        return false;
    }

    last_telemetry_.conversion_copy_ms = conv_ms;
    last_telemetry_.pixel_format_convert_ms = static_cast<double>(pixel_after - pixel_before);
    last_telemetry_.color_space_convert_ms = static_cast<double>(color_after - color_before);
    last_telemetry_.conversion_bytes_written = converted.data.size();
    last_telemetry_.encoder_staging_copy_bytes = 0;
    if (encoder_pool_) {
        const auto pool_stats = encoder_pool_->stats();
        last_telemetry_.encoder_slots_allocated = pool_stats.slots_allocated;
        last_telemetry_.encoder_slot_reuses = pool_stats.slot_reuses;
    }

    if (counters_) {
        counters_->video_conversion_wall_ms.fetch_add(
            static_cast<uint64_t>(conv_ms), std::memory_order_relaxed);
        counters_->frame_conversion_copy_wall_ms.fetch_add(
            static_cast<uint64_t>(conv_ms), std::memory_order_relaxed);
        counters_->conversion_bytes_written.fetch_add(
            converted.data.size(), std::memory_order_relaxed);
        const auto pool_stats = encoder_pool_->stats();
        counters_->encoder_slots_allocated.store(
            pool_stats.slots_allocated, std::memory_order_relaxed);
        counters_->encoder_slot_reuses.store(
            pool_stats.slot_reuses, std::memory_order_relaxed);
        counters_->frame_conversion_wall_ms.fetch_add(
            static_cast<uint64_t>(conv_ms), std::memory_order_relaxed);
        counters_->video_frames_converted.fetch_add(1, std::memory_order_relaxed);
    }

    chronon3d::media::video::VideoFrameView view;
    view.data = encoder_frame.storage.data();
    view.stride_bytes = 0;
    view.width = width_;
    view.height = height_;
    view.pixel_format = fmt;
    view.pts = static_cast<int64_t>(frames_written_);

    const auto submit_t0 = std::chrono::steady_clock::now();
    const uint64_t pipe_cpu_before = counters_
        ? counters_->pipe_write_cpu_wall_us.load(std::memory_order_relaxed) : 0;
    const uint64_t pipe_bp_before = counters_
        ? counters_->pipe_backpressure_wait_wall_us.load(std::memory_order_relaxed) : 0;
    bool ok = sink_->submit(view);
    const auto submit_t1 = std::chrono::steady_clock::now();
    const uint64_t pipe_cpu_after = counters_
        ? counters_->pipe_write_cpu_wall_us.load(std::memory_order_relaxed) : 0;
    const uint64_t pipe_bp_after = counters_
        ? counters_->pipe_backpressure_wait_wall_us.load(std::memory_order_relaxed) : 0;

    if (!ok) {
        spdlog::error("[video_adapter] sink->submit() failed at frame {}: {} — {}",
                      frames_written_, to_string(sink_->last_error()), sink_->last_error_message());
        return false;
    }

    const double submit_ms = std::chrono::duration<double, std::milli>(submit_t1 - submit_t0).count();
    last_telemetry_.encoder_ms = submit_ms;
    last_telemetry_.frame_submit_ms = submit_ms;
    last_telemetry_.pipe_write_cpu_ms =
        static_cast<double>(pipe_cpu_after - pipe_cpu_before) / 1000.0;
    last_telemetry_.pipe_backpressure_wait_ms =
        static_cast<double>(pipe_bp_after - pipe_bp_before) / 1000.0;
    write_blocked_ms_ += submit_ms;

    if (counters_) {
        counters_->video_frames_submitted.fetch_add(1, std::memory_order_relaxed);
        counters_->video_pipe_write_wall_ms.fetch_add(
            static_cast<uint64_t>(submit_ms), std::memory_order_relaxed);
        counters_->video_frames_written_counter.fetch_add(1, std::memory_order_relaxed);
        counters_->frame_submit_wall_ms.fetch_add(
            static_cast<uint64_t>(submit_ms), std::memory_order_relaxed);
    }

    ++frames_written_;
    return true;
}

bool VideoSinkEncoderAdapter::write_frame(const Framebuffer& fb) {
    return sink_ && convert_and_submit(fb);
}

bool VideoSinkEncoderAdapter::write_frame_async(
    const Framebuffer& fb,
    std::shared_ptr<Framebuffer> owner) {
    const bool ok = write_frame(fb);
    owner.reset();
    return ok;
}

} // namespace chronon3d::cli
