// ═══════════════════════════════════════════════════════════════════════════
// tests/text/visual/text_visual_metrics.cpp — Phase-2.1 P0 split
//
// Mechanical split-off of the 8-metric ScenarioMetrics canon (PR-A3 /
// docs/01-baseline-green.md §2.4-2.5).  This file owns:  // drift-allow: stale-ref
//   * kUncapturedSentinel + is_reference_captured(r)        (1-liner)
//   * RectF POD                                                (4-float POD)
//   * ScenarioMetrics POD                                      (8-metric canon)
//   * inline compute_metrics(Framebuffer, time_point)          (frame scan)
//
// Cross-TU SHARING PATTERN — non-canonical but spec-driven:
//   This file holds TU-instantiable `inline` definitions; each test TU
//   directly `#include`s it via text_visual_fixture.hpp (which has its
//   own `#include "text_visual_metrics.cpp"` line).  Because every
//   function is marked `inline`, the ODR is satisfied at link time even
//   with multiple TUs replicating the same definition.  The `.cpp`
//   extension is mandated by the user spec ("metric computation in a
//   metrics.cpp file, NOT a metrics.hpp"); we honor that without
//   sacrificing the cross-TU sharing invariant.
//
// If a future refactor wants the canonical .hpp-on-disk pattern, the
// rename is a one-line ff on the `#include "text_visual_metrics.cpp"`
// directives inside text_visual_fixture.hpp + every test TU.  Until
// then, this is the documented cross-TU hook.
// ═══════════════════════════════════════════════════════════════════════════
#pragma once

#include <cmath>
#include <cstdint>
#include <cstddef>
#include <chrono>

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/helpers/pixel_assertions.hpp>

namespace chrono3d_text_visual_metrics {

// Sentinel-capture helpers (mirror PR-A3 file).
inline constexpr std::uint64_t kUncapturedSentinel = 0xDEADBEEFDEADBEEFULL;

inline bool is_reference_captured(std::uint64_t r) noexcept {
    return r != kUncapturedSentinel;
}

// TICKET-038 / TXT-00 — POD `RectF`.  The SDK has no public canonical
// RectF; the 8-metric ScenarioMetrics canon uses
// `RectF{float,float,float,float}` (x0,y0,w,h).  TXT-00 forbids cross-
// package aliasing and renaming canonical types, so each test TU
// declares a 4-float POD inside its own anon namespace.  Default
// member initializers keep it an aggregate under C++17/20.
struct RectF {
    float x{0.0f}, y{0.0f}, w{0.0f}, h{0.0f};
};

// 8-metric ScenarioMetrics canon (PR-A3 / docs/01-baseline-green.md §2.4-2.5).  // drift-allow: stale-ref
struct ScenarioMetrics {
    std::uint64_t hash{0};
    RectF         ink_bbox{};
    std::size_t   ink_pixels{0};
    float         mean_luminance{0.0f};
    float         alpha_coverage{0.0f};
    chronon3d::Vec2 visual_center{0.0f, 0.0f};
    float         render_ms{0.0f};
};

// Compute all 8 metrics from a single rendered framebuffer.  Hash is
// delegated to chronon3d::test::framebuffer_hash (test-utils helper);
// ink_scan walks every pixel with `a > 0.05f` filter to count ink +
// bbox + visual_center; render_ms is the wall-clock between the
// caller-supplied `t0` (typically renderer.render_frame entry) and the
// function return.
inline ScenarioMetrics compute_metrics(const chronon3d::Framebuffer& fb,
                                         std::chrono::steady_clock::time_point t0) {
    ScenarioMetrics m;
    m.hash = chronon3d::test::framebuffer_hash(fb);

    const int W = fb.width();
    const int H = fb.height();
    int xmin = W, ymin = H, xmax = -1, ymax = -1;
    int ink = 0;
    double sum_l = 0.0;
    double sum_a = 0.0;
    double sum_x = 0.0, sum_y = 0.0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            chronon3d::Color c = fb.get_pixel(x, y);
            const float a = c.a;
            if (a > 0.05f) {
                ink += 1;
                xmin = std::min(xmin, x); ymin = std::min(ymin, y);
                xmax = std::max(xmax, x); ymax = std::max(ymax, y);
                sum_l += chronon3d::luma(c);
                sum_a += a;
                sum_x += x * a; sum_y += y * a;
            }
        }
    }
    const std::size_t total = static_cast<std::size_t>(W) * static_cast<std::size_t>(H);
    m.ink_pixels     = ink;
    m.alpha_coverage = total > 0 ? static_cast<float>(ink) / static_cast<float>(total) : 0.0f;
    m.mean_luminance = ink > 0 ? static_cast<float>(sum_l / static_cast<double>(ink)) : 0.0f;
    if (ink > 0) {
        m.ink_bbox = RectF{static_cast<float>(xmin), static_cast<float>(ymin),
                           static_cast<float>(xmax - xmin + 1),
                           static_cast<float>(ymax - ymin + 1)};
        m.visual_center = chronon3d::Vec2{static_cast<float>(sum_x / sum_a),
                                            static_cast<float>(sum_y / sum_a)};
    }
    auto t1 = std::chrono::steady_clock::now();
    m.render_ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
    return m;
}

} // namespace chrono3d_text_visual_metrics

// Pull the symbols into the global namespace so callers can use them
// unqualified (matches every original callsite's qualifier).
using chrono3d_text_visual_metrics::kUncapturedSentinel;
using chrono3d_text_visual_metrics::is_reference_captured;
using chrono3d_text_visual_metrics::RectF;
using chrono3d_text_visual_metrics::ScenarioMetrics;
using chrono3d_text_visual_metrics::compute_metrics;
