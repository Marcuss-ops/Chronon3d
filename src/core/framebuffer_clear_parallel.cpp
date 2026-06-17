// framebuffer_clear_parallel.cpp
//
// Parallel framebuffer clear for large framebuffers.
// Uses TBB parallel_for to split the work across all cores — each core
// clears its own band of rows via a single contiguous memset.
//
// This is the "8 pizzaioli puliscono 8 fette del banco" strategy:
// instead of one thread clearing the entire framebuffer (memset),
// we split the framebuffer into bands and clear each band in parallel.

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/parallel_tracked.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <algorithm>
#include <thread>

namespace chronon3d {

void framebuffer_clear_parallel(
    Framebuffer& fb, const Color& color,
    const std::optional<raster::BBox>& clip)
{
    const i32 fb_w = fb.width();
    const i32 fb_h = fb.height();
    const i32 stride = fb.stride();

    // Resolve clip rect: if no clip, clear the whole framebuffer.
    raster::BBox rect;
    if (clip) {
        rect = *clip;
        rect.clip_to(fb_w, fb_h);
    } else {
        rect = {0, 0, fb_w, fb_h};
    }

    if (rect.is_empty()) return;

    const i32 x0 = rect.x0;
    const i32 y0 = rect.y0;
    const i32 w  = rect.x1 - rect.x0;
    const i32 h  = rect.y1 - rect.y0;

    if (w <= 0 || h <= 0) return;

    const int num_threads = static_cast<int>(std::thread::hardware_concurrency());

    // Threshold: ~256KB to justify TBB overhead — 4096 pixels ~ 64×64 tile.
    const int64_t pixel_count = static_cast<int64_t>(w) * h;
    const bool use_parallel = (pixel_count >= 4096 && h >= 8);

    // Track decision: used_parallel_clear vs skipped_clear_small
    auto* decision_cnt = profiling::g_current_counters;
    if (use_parallel) {
        if (decision_cnt) {
            decision_cnt->used_parallel_clear.fetch_add(1, std::memory_order_relaxed);
        }
    } else {
        if (decision_cnt) {
            decision_cnt->skipped_clear_small.fetch_add(1, std::memory_order_relaxed);
        }
    }

    if (use_parallel) {
        // Split the rows into bands, each band gets a contiguous chunk.
        // Minimum band size of 16 rows ensures meaningful work per thread.
        const int band_size = std::max(4, h / std::max(1, num_threads * 2));

        Color* base = fb.data();  // fb.data() may set content_cleared = false,
                                   // but clear is about to overwrite everything.

        if (x0 == 0 && w == stride) {
            // Full-width case: each band clears a contiguous block.
            parallel_for_tracked(
                tbb::blocked_range<i32>(0, h, band_size),
                [&](const tbb::blocked_range<i32>& range) {
                    const usize offset = static_cast<usize>(y0 + range.begin()) * stride;
                    const usize count  = static_cast<usize>(range.size()) * stride;
                    simd::clear_framebuffer(std::span<Color>(base + offset, static_cast<int>(count)), color);
                }
            );
        } else {
            // Partial-width case: each band clears per-row segments.
            parallel_for_tracked(
                tbb::blocked_range<i32>(0, h, band_size),
                [&](const tbb::blocked_range<i32>& range) {
                    Color* row = base + static_cast<usize>(y0 + range.begin()) * stride + x0;
                    for (i32 yy = 0; yy < range.size(); ++yy) {
                        simd::clear_framebuffer(std::span<Color>(row, w), color);
                        row += static_cast<size_t>(stride);
                    }
                }
            );
        }

        fb.set_opaque(color.a >= 0.999f);
        if (color.r == 0.0f && color.g == 0.0f && color.b == 0.0f && color.a == 0.0f) {
            fb.set_content_cleared(true);
        }

    } else {
        // Fall back to the standard inline clear for small areas.
        // No counters updated here — the caller is responsible for tracking.
        std::optional<raster::BBox> opt_clip;
        if (clip) opt_clip = *clip;
        fb.clear(color, opt_clip);
    }
}

} // namespace chronon3d
