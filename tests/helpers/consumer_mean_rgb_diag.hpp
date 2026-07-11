#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// consumer_mean_rgb_diag.hpp — Forward-point 0a helper for the
// TICKET-ACCEPTANCE-FORENSIC-SURFACE workstream (Promote forward-points
// 0a/0b/0c of TICKET-ACCEPTANCE-SUITE-PHASE-D).
//
// Usage:
//   FILE* diag = stderr;                              // or any FILE*
//   write_cumulative_mean_rgb_diag(fb, diag);         // 0 on success
//
// Output format (single line per call, predictor-friendly for the §0c
// forensic-surface consumer):
//   [chronon3d-forensic] cumulative mean RGB over <N> pixels: r=%.4f g=%.4f b=%.4f
//
// Empty-framebuffer short-circuit:
//   [chronon3d-forensic] cumulative mean RGB skipped (empty framebuffer)
//
// Lives in tests/helpers/ (NOT include/chronon3d/) per AGENTS.md §Cat-3
// (no new public SDK API surface).
// ──────────────────────────────────────────────────────────────────────────────

#include <chronon3d/chronon3d.hpp>

#include <cstddef>
#include <cstdio>

namespace chronon3d::test_forensic {

/// Compute cumulative mean RGB across every pixel in `fb` and write
/// the diagnostic to `out`.
///
/// Returns:
///   0  on success (framebuffer populated, output written)
///  -1  on output-stream failure (out == nullptr)
inline int write_cumulative_mean_rgb_diag(const chronon3d::Framebuffer& fb,
                                          std::FILE* out) noexcept {
    if (out == nullptr) {
        return -1;
    }
    const std::size_t n = fb.pixel_count();
    if (n == 0 || fb.data() == nullptr) {
        std::fprintf(out,
                     "[chronon3d-forensic] cumulative mean RGB skipped "
                     "(empty framebuffer)\n");
        return 0;
    }
    const chronon3d::Color* pix = fb.data();
    // double accumulator to avoid float-precision loss on large
    // framebuffers (1080×1920 = 2'073'600 pixels, up to UHD ≈ 8.3M).
    double sum_r = 0.0;
    double sum_g = 0.0;
    double sum_b = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sum_r += static_cast<double>(pix[i].r);
        sum_g += static_cast<double>(pix[i].g);
        sum_b += static_cast<double>(pix[i].b);
    }
    const double inv = 1.0 / static_cast<double>(n);
    std::fprintf(out,
                 "[chronon3d-forensic] cumulative mean RGB over %zu pixels: "
                 "r=%.4f g=%.4f b=%.4f\n",
                 n,
                 sum_r * inv,
                 sum_g * inv,
                 sum_b * inv);
    return 0;
}

} // namespace chronon3d::test_forensic
