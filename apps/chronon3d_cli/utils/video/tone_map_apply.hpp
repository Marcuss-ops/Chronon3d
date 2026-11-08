#pragma once

// ---------------------------------------------------------------------------
// tone_map_apply.hpp — CLI-side application of the engine tone-map pass.
//
// Both encoder lanes (native libav and the shared VideoSink) must apply the
// same compression before their linear→sRGB/YUV conversion. This helper keeps
// that ordering and its evidence logging in one place.
// ---------------------------------------------------------------------------

#include <chronon3d/color/output_transform.hpp>
#include <chronon3d/color/tone_map_framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <spdlog/spdlog.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace chronon3d::cli {

[[nodiscard]] inline bool tone_map_requested(
    const color::OutputTransformOptions& options) noexcept {
    return options.tone_map != color::ToneMapOperator::None ||
           options.exposure != 1.0f;
}

/// Return a compressed copy of `source`, or std::nullopt when the output is
/// already SDR. The caller converts the returned frame instead of the source.
[[nodiscard]] inline std::optional<Framebuffer> apply_output_tone_map(
    const Framebuffer& source,
    const color::OutputTransformOptions& options,
    std::string_view lane,
    std::int64_t frame) {
    if (!tone_map_requested(options)) return std::nullopt;

    Framebuffer mapped(source);
    if (!mapped.data()) {
        // The encoder boundary receives read-back frames. A GPU-only surface
        // here means the caller skipped readback, and tone-mapping an empty
        // CPU shadow would silently emit black.
        throw std::runtime_error(
            "output tone map requires a CPU-resident framebuffer");
    }
    const auto stats = color::tone_map_framebuffer(
        mapped, options.tone_map, options.exposure);

    // Sparse evidence line: enough to certify the HDR range at the boundary
    // without flooding a 180-frame export log.
    if (frame == 0 || frame % 30 == 0) {
        spdlog::info(
            "[{}] tone-map={} exposure={} frame={} max_linear_luma={:.4f} "
            "max_mapped_luma={:.4f}",
            lane, color::tone_map_operator_name(options.tone_map),
            options.exposure, frame, stats.max_linear, stats.max_mapped);
    }
    return mapped;
}

} // namespace chronon3d::cli
