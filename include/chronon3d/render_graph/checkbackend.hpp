#pragma once

// ---------------------------------------------------------------------------
// render_graph/checkbackend.hpp
//
// Backend-neutral pixel-parity contract for the checkbackend harness.  The
// harness renders the same scene with the CPU reference backend
// (SoftwareBackend) and the GPU backend (VulkanBackend), then compares the
// resulting RGBA buffers with a tolerance.  This header provides only the
// comparison primitive and the per-kernel case/report model; the actual
// backend execution lives with each backend and is GPU-gated.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <string>

namespace chronon3d::graph {

/// Tolerance used when comparing a CPU reference against a GPU result.
/// A channel matches when |reference - result| is at most `absolute`, or at
/// most `epsilon * max(1, |reference|)` — a relative gate that still admits
/// an absolute floor for near-zero reference values.
struct PixelTolerance {
    float epsilon{1e-4f};
    float absolute{1e-4f};
};

/// Outcome of a single buffer comparison.  `matched` is false when any pixel
/// channel exceeds the tolerance (or when the buffers differ in length, in
/// which case `mismatched_pixels` is the sentinel
/// `std::numeric_limits<std::size_t>::max()`).
struct PixelCompareResult {
    bool matched{true};
    std::size_t mismatched_pixels{0};
    float max_delta{0.0f};
};

/// True when one RGBA channel value is within tolerance of the reference.
[[nodiscard]] inline bool channel_within_tolerance(
    float reference, float result, const PixelTolerance& tolerance) noexcept {
    const float delta = std::abs(reference - result);
    return delta <= tolerance.absolute ||
           delta <= tolerance.epsilon * std::max(1.0f, std::abs(reference));
}

/// Compare two RGBA float buffers (4 floats per pixel, row-major, matching
/// the Rgba32Float surface layout).  A pixel is counted once even when
/// several of its channels exceed the tolerance.
[[nodiscard]] inline PixelCompareResult compare_pixels(
    std::span<const float> reference,
    std::span<const float> result,
    const PixelTolerance& tolerance = {}) {
    PixelCompareResult report;
    if (reference.size() != result.size() || reference.size() % 4 != 0) {
        report.matched = false;
        report.mismatched_pixels = std::numeric_limits<std::size_t>::max();
        return report;
    }
    for (std::size_t pixel = 0; pixel < reference.size() / 4; ++pixel) {
        const std::size_t base = pixel * 4;
        bool pixel_mismatch = false;
        for (std::size_t channel = 0; channel < 4; ++channel) {
            const float ref = reference[base + channel];
            const float res = result[base + channel];
            report.max_delta = std::max(report.max_delta, std::abs(ref - res));
            if (!channel_within_tolerance(ref, res, tolerance)) {
                pixel_mismatch = true;
            }
        }
        if (pixel_mismatch) {
            report.matched = false;
            ++report.mismatched_pixels;
        }
    }
    return report;
}

/// A single checkbackend case: one named kernel, one tolerance.
struct CheckbackendCase {
    std::string kernel_name;
    PixelTolerance tolerance{};
};

/// Per-kernel checkbackend verdict.
struct CheckbackendReport {
    std::string kernel_name;
    bool passed{false};
    PixelCompareResult comparison{};
};

} // namespace chronon3d::graph
