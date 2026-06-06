#include <chronon3d/backends/software/software_compositor.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <algorithm>
#include <chrono>
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

    const auto t_setup0 = std::chrono::high_resolution_clock::now();

    RenderCounters* cnt = profiling::g_current_counters;

    if (mode == BlendMode::Normal) {
        const i32 src_x0 = std::max(x0 + dst.origin_x(), src.origin_x());
        const i32 src_y0 = std::max(y0 + dst.origin_y(), src.origin_y());
        const i32 src_x1 = std::min(x1 + dst.origin_x(), src.origin_x() + src.width());
        const i32 src_y1 = std::min(y1 + dst.origin_y(), src.origin_y() + src.height());

        if (src.is_opaque() && src_x0 < src_x1 && src_y0 < src_y1) {
            const auto t_copy0 = std::chrono::high_resolution_clock::now();
            for (i32 y = src_y0; y < src_y1; ++y) {
                const i32 sy = y - src.origin_y();
                const i32 sx = src_x0 - src.origin_x();
                const Color* s_row = src.pixels_row(sy);
                Color* d_row = dst.pixels_row(y - dst.origin_y());
                std::copy(s_row + sx, s_row + sx + (src_x1 - src_x0), d_row + (src_x0 - dst.origin_x()));
            }
            if (cnt) {
                const auto t_end = std::chrono::high_resolution_clock::now();
                const auto setup_ms = static_cast<uint64_t>(std::chrono::duration<double, std::milli>(t_copy0 - t_setup0).count());
                const auto copy_ms = static_cast<uint64_t>(std::chrono::duration<double, std::milli>(t_end - t_copy0).count());
                cnt->compositenode_setup_ms.fetch_add(setup_ms, std::memory_order_relaxed);
                cnt->compositenode_copy_ms.fetch_add(copy_ms, std::memory_order_relaxed);
            }
            return;
        }

        if (src_x0 < src_x1 && src_y0 < src_y1 &&
            composite_layer_normal_optimized(dst, src, src_x0, src_y0, src_x1, src_y1, cnt, t_setup0)) {
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
            composite_layer_non_normal_optimized(dst, src, mode, src_x0, src_y0, src_x1, src_y1, cnt, t_setup0)) {
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
        const auto t_blend0 = std::chrono::high_resolution_clock::now();
        if (cnt) {
            const auto setup_ms = static_cast<uint64_t>(std::chrono::duration<double, std::milli>(t_blend0 - t_setup0).count());
            cnt->compositenode_setup_ms.fetch_add(setup_ms, std::memory_order_relaxed);
        }
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
        if (cnt) {
            const auto t_end = std::chrono::high_resolution_clock::now();
            const auto blend_ms = static_cast<uint64_t>(std::chrono::duration<double, std::milli>(t_end - t_blend0).count());
            cnt->compositenode_blend_ms.fetch_add(blend_ms, std::memory_order_relaxed);
        }
    }
}

bool SoftwareCompositor::composite_layer_normal_optimized(
    Framebuffer& dst, const Framebuffer& src, i32 x0, i32 y0, i32 x1, i32 y1,
    RenderCounters* cnt, std::chrono::high_resolution_clock::time_point t_setup0) {
    if (profiling::g_current_counters) {
        profiling::g_current_counters->simd_lerp_calls.fetch_add(1, std::memory_order_relaxed);
    }

    const i32 width_to_process = x1 - x0;
    const i32 height_to_process = y1 - y0;
    const i32 dst_ox = dst.origin_x();
    const i32 src_ox = src.origin_x();

    const auto t_blend0 = std::chrono::high_resolution_clock::now();
    if (cnt) {
        const auto setup_ms = static_cast<uint64_t>(std::chrono::duration<double, std::milli>(t_blend0 - t_setup0).count());
        cnt->compositenode_setup_ms.fetch_add(setup_ms, std::memory_order_relaxed);
    }

    // Row-setup timing is only valid in the sequential path (avoids race on TBB).
    uint64_t row_setup_ns = 0;
    const bool use_tbb = (height_to_process >= 32);

    auto process_rows = [&](i32 row_begin, i32 row_end) {
        for (i32 y = row_begin; y < row_end; ++y) {
            if (!use_tbb) {
                const auto t_rs0 = std::chrono::high_resolution_clock::now();
                Color* d_row = dst.pixels_row(y - dst_ox) + (x0 - dst_ox);
                const Color* s_row = src.pixels_row(y - src_ox) + (x0 - src_ox);
                const auto t_rs1 = std::chrono::high_resolution_clock::now();
                row_setup_ns += static_cast<uint64_t>(std::chrono::duration<double, std::nano>(t_rs1 - t_rs0).count());
                simd::composite_normal_premul(d_row, s_row, width_to_process);
            } else {
                Color* d_row = dst.pixels_row(y - dst_ox) + (x0 - dst_ox);
                const Color* s_row = src.pixels_row(y - src_ox) + (x0 - src_ox);
                simd::composite_normal_premul(d_row, s_row, width_to_process);
            }
        }
    };

    if (use_tbb) {
        tbb::parallel_for(
            tbb::blocked_range<i32>(y0, y1),
            [&](const tbb::blocked_range<i32>& range) {
                process_rows(range.begin(), range.end());
            }
        );
    } else {
        process_rows(y0, y1);
    }

    if (cnt) {
        const auto t_end = std::chrono::high_resolution_clock::now();
        const auto blend_ms = static_cast<uint64_t>(std::chrono::duration<double, std::milli>(t_end - t_blend0).count());
        const auto row_ms = static_cast<uint64_t>(static_cast<double>(row_setup_ns) / 1e6);
        cnt->compositenode_blend_ms.fetch_add(blend_ms, std::memory_order_relaxed);
        cnt->compositenode_row_ms.fetch_add(row_ms, std::memory_order_relaxed);
    }

    return true;
}

bool SoftwareCompositor::composite_layer_non_normal_optimized(
    Framebuffer& dst, const Framebuffer& src, BlendMode mode,
    i32 x0, i32 y0, i32 x1, i32 y1,
    RenderCounters* cnt, std::chrono::high_resolution_clock::time_point t_setup0) {
    const i32 width_to_process = x1 - x0;
    const i32 height_to_process = y1 - y0;
    const i32 dst_ox = dst.origin_x();
    const i32 src_ox = src.origin_x();

    const auto t_blend0 = std::chrono::high_resolution_clock::now();
    if (cnt) {
        const auto setup_ms = static_cast<uint64_t>(std::chrono::duration<double, std::milli>(t_blend0 - t_setup0).count());
        cnt->compositenode_setup_ms.fetch_add(setup_ms, std::memory_order_relaxed);
    }

    auto process_rows = [&](i32 row_begin, i32 row_end) {
        for (i32 y = row_begin; y < row_end; ++y) {
            Color* d_row = dst.pixels_row(y - dst_ox) + (x0 - dst_ox);
            const Color* s_row = src.pixels_row(y - src_ox) + (x0 - src_ox);
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

    if (cnt) {
        const auto t_end = std::chrono::high_resolution_clock::now();
        const auto blend_us = static_cast<uint64_t>(std::chrono::duration<double, std::milli>(t_end - t_blend0).count());
        cnt->compositenode_blend_ms.fetch_add(blend_us, std::memory_order_relaxed);
    }

    return true;
}

} // namespace chronon3d
