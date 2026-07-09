#pragma once
// SPDX-License-Identifier: MIT
//
// apps/chronon3d_cli/utils/job/render_job_write_frame_sanity.hpp
//
// TICKET-RENDER-PIPELINE-INTEGRITY
// Header-only testability surface for the pre-write framebuffer sanity
// scan.  Originally in render_job_write_frame.cpp's anonymous namespace;
// promoted to a public header so the cli unit tests can call
// `scan_framebuffer_sanity(fb)` directly without re-implementing the
// algorithm or instantiating a full Composition/render path.
//
// The function definition is `inline` so multiple translation units can
// include this header without ODR violation.  All callers (production
// render_job_write_frame.cpp + the unit test) see the SAME definition.

#include <cmath>
#include <cstdint>

#include <spdlog/spdlog.h>

#include <chronon3d/core/memory/framebuffer.hpp>

namespace chronon3d::cli {

// Threshold constants.  See TICKET-RENDER-PIPELINE-INTEGRITY M2 fix: the
// alpha-zero threshold was raised from 0.60 to 0.85 so legitimate
// minimalist fade-in/fade-out compositions (e.g. MinimalistTextFadeUp)
// do not get rejected as "empty canvas" during the early transparent
// frames of the animation.
constexpr float kAlphaZeroRejectFraction   = 0.85f;
constexpr float kNanInfRejectFraction     = 0.05f;
constexpr int   kSanityScanStride         = 4;  // sample every Nth pixel

struct FramebufferSanityReport {
    bool ok                = true;
    std::int64_t sampled_pixels    = 0;
    std::int64_t alpha_zero_pixels = 0;
    std::int64_t nan_or_inf_pixels = 0;
    int first_bad_x        = -1;
    int first_bad_y        = -1;
};

// scan_framebuffer_sanity — samples every `kSanityScanStride`-th pixel
// and rejects the frame if alpha-zero fraction OR NaN/Inf fraction
// exceeds the corresponding threshold.  Logs at error level on reject,
// debug level on accept.  Production callers (`write_frame_to_disk`)
// treat `!report.ok` as a hard frame-level reject.
inline FramebufferSanityReport scan_framebuffer_sanity(const Framebuffer& fb) {
    FramebufferSanityReport r;
    const int w = fb.width();
    const int h = fb.height();
    if (w <= 0 || h <= 0) {
        return r;
    }
    for (int y = 0; y < h; y += kSanityScanStride) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < w; x += kSanityScanStride) {
            const Color& c = row[x];
            ++r.sampled_pixels;
            if (c.a == 0.0f) {
                ++r.alpha_zero_pixels;
                if (r.first_bad_x < 0) { r.first_bad_x = x; r.first_bad_y = y; }
            }
            if (std::isnan(c.r) || std::isnan(c.g) || std::isnan(c.b) || std::isnan(c.a) ||
                std::isinf(c.r) || std::isinf(c.g) || std::isinf(c.b) || std::isinf(c.a)) {
                ++r.nan_or_inf_pixels;
                if (r.first_bad_x < 0) { r.first_bad_x = x; r.first_bad_y = y; }
            }
        }
    }
    const float alpha_zero_frac = (r.sampled_pixels == 0)
        ? 0.0f
        : static_cast<float>(r.alpha_zero_pixels) / static_cast<float>(r.sampled_pixels);
    const float nan_inf_frac     = (r.sampled_pixels == 0)
        ? 0.0f
        : static_cast<float>(r.nan_or_inf_pixels)     / static_cast<float>(r.sampled_pixels);
    if (alpha_zero_frac > kAlphaZeroRejectFraction) {
        r.ok = false;
        spdlog::error(
            "scan_framebuffer_sanity: rejecting — {:.1f}% of sampled pixels "
            "have alpha==0 (threshold {:.1f}%)",
            alpha_zero_frac * 100.0f, kAlphaZeroRejectFraction * 100.0f);
    }
    if (nan_inf_frac > kNanInfRejectFraction) {
        r.ok = false;
        spdlog::error(
            "scan_framebuffer_sanity: rejecting — {:.1f}% of sampled pixels "
            "are NaN/Inf (threshold {:.1f}%), first bad sample @ ({},{})",
            nan_inf_frac * 100.0f, kNanInfRejectFraction * 100.0f,
            r.first_bad_x, r.first_bad_y);
    }
    if (r.ok) {
        spdlog::debug(
            "scan_framebuffer_sanity: ok — sampled={} alpha_zero={} ({:.2f}%) "
            "nan_inf={} ({:.2f}%)",
            r.sampled_pixels, r.alpha_zero_pixels, alpha_zero_frac * 100.0f,
            r.nan_or_inf_pixels, nan_inf_frac * 100.0f);
    }
    return r;
}

} // namespace chronon3d::cli
