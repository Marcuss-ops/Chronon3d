#include <chronon3d/backends/software/software_compositor.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <algorithm>
#include <cmath>
#include <optional>

namespace chronon3d {

void SoftwareCompositor::composite_layer(Framebuffer& dst, const Framebuffer& src, BlendMode mode, const std::optional<raster::BBox>& clip) {
    i32 x0 = 0, y0 = 0, x1 = dst.width(), y1 = dst.height();
    if (clip) {
        // clip is in canvas coordinates — convert to dst-local
        x0 = std::clamp(clip->x0 - dst.origin_x(), 0, dst.width());
        y0 = std::clamp(clip->y0 - dst.origin_y(), 0, dst.height());
        x1 = std::clamp(clip->x1 - dst.origin_x(), 0, dst.width());
        y1 = std::clamp(clip->y1 - dst.origin_y(), 0, dst.height());
    }

    if (x0 >= x1 || y0 >= y1) return;

    if (mode == BlendMode::Normal) {
        const i32 src_x0 = std::max(x0 + dst.origin_x(), src.origin_x());
        const i32 src_y0 = std::max(y0 + dst.origin_y(), src.origin_y());
        const i32 src_x1 = std::min(x1 + dst.origin_x(), src.origin_x() + src.width());
        const i32 src_y1 = std::min(y1 + dst.origin_y(), src.origin_y() + src.height());

        if (src.is_opaque() && src_x0 < src_x1 && src_y0 < src_y1) {
            for (i32 y = src_y0; y < src_y1; ++y) {
                const i32 sy = y - src.origin_y();
                const i32 sx = src_x0 - src.origin_x();
                const Color* s_row = src.pixels_row(sy);
                Color* d_row = dst.pixels_row(y - dst.origin_y());
                std::copy(s_row + sx, s_row + sx + (src_x1 - src_x0), d_row + (src_x0 - dst.origin_x()));
            }
            return;
        }

        if (src_x0 < src_x1 && src_y0 < src_y1 &&
            composite_layer_normal_optimized(dst, src, src_x0, src_y0, src_x1, src_y1)) {
            return;
        }
    }

    // ── Non-normal SIMD+parallel path (Add, Multiply, Screen, Overlay) ────
    if (mode == BlendMode::Add || mode == BlendMode::Multiply ||
        mode == BlendMode::Screen || mode == BlendMode::Overlay) {
        const i32 src_x0 = std::max(x0 + dst.origin_x(), src.origin_x());
        const i32 src_y0 = std::max(y0 + dst.origin_y(), src.origin_y());
        const i32 src_x1 = std::min(x1 + dst.origin_x(), src.origin_x() + src.width());
        const i32 src_y1 = std::min(y1 + dst.origin_y(), src.origin_y() + src.height());
        if (src_x0 < src_x1 && src_y0 < src_y1 &&
            composite_layer_non_normal_optimized(dst, src, mode, src_x0, src_y0, src_x1, src_y1)) {
            return;
        }
    }

    // ── Scalar parallelized fallback (any remaining blend/edge cases) ─────
    {
        const i32 row_count = y1 - y0;
        auto process_rows = [&](i32 row_begin, i32 row_end) {
            for (i32 y = row_begin; y < row_end; ++y) {
                const i32 canvas_y = y + dst.origin_y();
                const i32 sy = canvas_y - src.origin_y();
                if (sy < 0 || sy >= src.height()) continue;
                const Color* s_row = src.pixels_row(sy);
                Color* d_row = dst.pixels_row(y);
                for (i32 x = x0; x < x1; ++x) {
                    const i32 canvas_x = x + dst.origin_x();
                    const i32 sx = canvas_x - src.origin_x();
                    if (sx < 0 || sx >= src.width()) continue;
                    Color s = s_row[sx];
                    if (s.a <= 0.0f) continue;
                    // Guard: skip NaN/Inf source pixels to prevent framebuffer contamination.
                    if (std::isnan(s.r) || std::isnan(s.g) || std::isnan(s.b) || std::isnan(s.a) ||
                        std::isinf(s.r) || std::isinf(s.g) || std::isinf(s.b) || std::isinf(s.a)) {
                        continue;
                    }
                    d_row[x] = compositor::blend(s, d_row[x], mode);
                }
            }
        };
        if (row_count >= 32) {
            tbb::parallel_for(
                tbb::blocked_range<i32>(y0, y1),
                [&](const tbb::blocked_range<i32>& range) {
                    process_rows(range.begin(), range.end());
                }
            );
        } else {
            process_rows(y0, y1);
        }
    }
}

bool SoftwareCompositor::composite_layer_normal_optimized(Framebuffer& dst, const Framebuffer& src, i32 x0, i32 y0, i32 x1, i32 y1) {
    if (profiling::g_current_counters) {
        profiling::g_current_counters->simd_lerp_calls.fetch_add(1, std::memory_order_relaxed);
    }

    const i32 width_to_process = x1 - x0;
    const i32 height_to_process = y1 - y0;

    auto process_rows = [&](i32 row_begin, i32 row_end) {
        for (i32 y = row_begin; y < row_end; ++y) {
            Color* d_row = dst.pixels_row(y - dst.origin_y()) + (x0 - dst.origin_x());
            const Color* s_row = src.pixels_row(y - src.origin_y()) + (x0 - src.origin_x());
            simd::composite_normal_premul(d_row, s_row, width_to_process);
        }
    };

    if (height_to_process >= 32) {
        tbb::parallel_for(
            tbb::blocked_range<i32>(y0, y1),
            [&](const tbb::blocked_range<i32>& range) {
                process_rows(range.begin(), range.end());
            }
        );
    } else {
        process_rows(y0, y1);
    }

    return true;
}

bool SoftwareCompositor::composite_layer_non_normal_optimized(
    Framebuffer& dst, const Framebuffer& src, BlendMode mode,
    i32 x0, i32 y0, i32 x1, i32 y1) {
    const i32 width_to_process = x1 - x0;
    const i32 height_to_process = y1 - y0;

    auto process_rows = [&](i32 row_begin, i32 row_end) {
        for (i32 y = row_begin; y < row_end; ++y) {
            Color* d_row = dst.pixels_row(y - dst.origin_y()) + (x0 - dst.origin_x());
            const Color* s_row = src.pixels_row(y - src.origin_y()) + (x0 - src.origin_x());
            switch (mode) {
                case BlendMode::Add:
                    simd::composite_add_premul(d_row, s_row, width_to_process);
                    break;
                case BlendMode::Multiply:
                    simd::composite_multiply_premul(d_row, s_row, width_to_process);
                    break;
                case BlendMode::Screen:
                    simd::composite_screen_premul(d_row, s_row, width_to_process);
                    break;
                case BlendMode::Overlay:
                    simd::composite_overlay_premul(d_row, s_row, width_to_process);
                    break;
                default:
                    break;
            }
        }
    };

    if (height_to_process >= 32) {
        tbb::parallel_for(
            tbb::blocked_range<i32>(y0, y1),
            [&](const tbb::blocked_range<i32>& range) {
                process_rows(range.begin(), range.end());
            }
        );
    } else {
        process_rows(y0, y1);
    }

    return true;
}

} // namespace chronon3d
