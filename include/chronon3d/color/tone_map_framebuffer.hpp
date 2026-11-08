#pragma once

// ---------------------------------------------------------------------------
// tone_map_framebuffer.hpp — full-frame scene-referred compression.
//
// The engine renders into a linear float framebuffer that may carry values
// above 1.0 (HDR highlights, additive light, bokeh accumulation). An SDR
// output transfer must compress that range BEFORE linear→sRGB quantization;
// `Color::linear_to_srgb8` would otherwise clip every >1 sample to white.
//
// This is the one full-frame tone-map pass. It operates on straight RGB in
// the linear working space and leaves alpha untouched (video output has no
// alpha plane; SDR stills are opaque at this boundary).
// ---------------------------------------------------------------------------

#include <chronon3d/color/output_transform.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/parallel_tracked.hpp>

#include <tbb/blocked_range.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>

namespace chronon3d::color {

struct ToneMapFrameStats {
    /// Maximum scene-linear luminance seen before compression.
    float max_linear{0.0f};
    /// Maximum mapped luminance after compression (normally <= 1).
    float max_mapped{0.0f};
};

/// Apply `op`/`exposure` to every pixel of `fb` in place.
///
/// Returns the luminance range that crossed the boundary. Exposure is
/// validated here so a bad call fails closed instead of silently producing
/// a black or infinite frame.
[[nodiscard]] inline ToneMapFrameStats tone_map_framebuffer(
    Framebuffer& fb, ToneMapOperator op, float exposure) {
    if (!std::isfinite(exposure) || exposure < 0.0f || exposure > 64.0f) {
        throw std::invalid_argument(
            "tone-map exposure must be finite and in [0, 64]");
    }

    ToneMapFrameStats stats{};
    if (op == ToneMapOperator::None && exposure == 1.0f) {
        return stats;
    }

    std::atomic<float> max_linear{0.0f};
    std::atomic<float> max_mapped{0.0f};
    const int width = fb.width();
    const int height = fb.height();
    if (width <= 0 || height <= 0) return stats;

    const int grain = std::max(32, height / 16);
    parallel_for_tracked(
        tbb::blocked_range<int>(0, height, grain),
        [&](const tbb::blocked_range<int>& range) {
            float local_linear = 0.0f;
            float local_mapped = 0.0f;
            for (int y = range.begin(); y < range.end(); ++y) {
                Color* row = fb.pixels_row(y);
                for (int x = 0; x < width; ++x) {
                    Color& c = row[x];
                    const float src_luma = std::max(
                        0.0f,
                        0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b);
                    local_linear = std::max(local_linear, src_luma);

                    c.r = tone_map_channel(c.r, op, exposure);
                    c.g = tone_map_channel(c.g, op, exposure);
                    c.b = tone_map_channel(c.b, op, exposure);

                    const float mapped_luma = std::max(
                        0.0f,
                        0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b);
                    local_mapped = std::max(local_mapped, mapped_luma);
                }
            }
            float previous = max_linear.load(std::memory_order_relaxed);
            while (local_linear > previous &&
                   !max_linear.compare_exchange_weak(
                       previous, local_linear, std::memory_order_relaxed)) {
            }
            previous = max_mapped.load(std::memory_order_relaxed);
            while (local_mapped > previous &&
                   !max_mapped.compare_exchange_weak(
                       previous, local_mapped, std::memory_order_relaxed)) {
            }
        });

    fb.mark_cpu_authoritative();
    stats.max_linear = max_linear.load(std::memory_order_relaxed);
    stats.max_mapped = max_mapped.load(std::memory_order_relaxed);
    return stats;
}

} // namespace chronon3d::color
