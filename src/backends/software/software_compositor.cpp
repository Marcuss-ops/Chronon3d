#include <chronon3d/backends/software/software_compositor.hpp>
#include <chronon3d/core/memory_utils.hpp>
#include <chronon3d/core/parallel_tracked.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <algorithm>
#include <cmath>
#include <optional>

namespace chronon3d {

void SoftwareCompositor::composite_layer(Framebuffer& dst, const Framebuffer& src, BlendMode mode, const std::optional<raster::BBox>& clip, CompositeOperator op, bool force_scalar_normal_blend) {
    i32 x0 = 0, y0 = 0, x1 = dst.width(), y1 = dst.height();
    if (clip) {
        // clip is in canvas coordinates — convert to dst-local
        x0 = std::clamp(clip->x0 - dst.origin_x(), 0, dst.width());
        y0 = std::clamp(clip->y0 - dst.origin_y(), 0, dst.height());
        x1 = std::clamp(clip->x1 - dst.origin_x(), 0, dst.width());
        y1 = std::clamp(clip->y1 - dst.origin_y(), 0, dst.height());
    }

    if (x0 >= x1 || y0 >= y1) return;

    // ── CompositeOperator (Stencil/Silhouette) ─────────────────────────────
    if (op != CompositeOperator::SourceOver) {
        const i32 src_x0 = std::max(x0 + dst.origin_x(), src.origin_x());
        const i32 src_y0 = std::max(y0 + dst.origin_y(), src.origin_y());
        const i32 src_x1 = std::min(x1 + dst.origin_x(), src.origin_x() + src.width());
        const i32 src_y1 = std::min(y1 + dst.origin_y(), src.origin_y() + src.height());

        if (src_x0 >= src_x1 || src_y0 >= src_y1) return;

        const int width_to_process = src_x1 - src_x0;
        const bool alpha_op = (op == CompositeOperator::StencilAlpha ||
                               op == CompositeOperator::SilhouetteAlpha);
        const bool inverted = (op == CompositeOperator::SilhouetteAlpha ||
                               op == CompositeOperator::SilhouetteLuma);

        for (i32 y = src_y0; y < src_y1; ++y) {
            const i32 sy = y - src.origin_y();
            const i32 dy = y - dst.origin_y();
            if (dy < 0 || dy >= dst.height()) continue;
            const Color* s_row = src.pixels_row(sy);
            Color* d_row = dst.pixels_row(dy);
            const i32 sx0 = src_x0 - src.origin_x();
            const i32 dx0 = src_x0 - dst.origin_x();
            if (dx0 < 0 || dx0 + width_to_process > dst.width()) continue;

            if (alpha_op) {
                simd::apply_alpha_matte_premul(d_row + dx0, s_row + sx0, width_to_process, inverted);
            } else {
                simd::apply_luma_matte_premul(d_row + dx0, s_row + sx0, width_to_process, inverted);
            }
        }
        return;
    }

    // ── Standard SourceOver blend path ─────────────────────────────────────
    if (x0 >= x1 || y0 >= y1) return;

    const auto t_setup0 = profiling::now();

    RenderCounters* cnt = profiling::g_current_counters;

    if (mode == BlendMode::Normal) {
        const i32 src_x0 = std::max(x0 + dst.origin_x(), src.origin_x());
        const i32 src_y0 = std::max(y0 + dst.origin_y(), src.origin_y());
        const i32 src_x1 = std::min(x1 + dst.origin_x(), src.origin_x() + src.width());
        const i32 src_y1 = std::min(y1 + dst.origin_y(), src.origin_y() + src.height());

        if (src.is_opaque() && src_x0 < src_x1 && src_y0 < src_y1) {
            const auto t_copy0 = profiling::now();
            for (i32 y = src_y0; y < src_y1; ++y) {
                const i32 sy = y - src.origin_y();
                const i32 sx = src_x0 - src.origin_x();
                const Color* s_row = src.pixels_row(sy);
                Color* d_row = dst.pixels_row(y - dst.origin_y());
                std::copy(s_row + sx, s_row + sx + (src_x1 - src_x0), d_row + (src_x0 - dst.origin_x()));
            }
            if (cnt) {
                const auto t_end = profiling::now();
                const auto setup_ms = static_cast<uint64_t>(profiling::duration_ms(t_setup0, t_copy0));
                const auto copy_ms = static_cast<uint64_t>(profiling::duration_ms(t_copy0, t_end));
                cnt->compositenode_setup_ms.fetch_add(setup_ms, std::memory_order_relaxed);
                cnt->compositenode_copy_ms.fetch_add(copy_ms, std::memory_order_relaxed);
            }
            return;
        }

        if (src_x0 < src_x1 && src_y0 < src_y1 &&
            composite_layer_normal_optimized(dst, src, src_x0, src_y0, src_x1, src_y1, cnt, t_setup0, force_scalar_normal_blend)) {
            return;
        }
    }

    // ── Non-normal SIMD+parallel path (Add, Multiply, Screen, Overlay) ────
    if (mode == BlendMode::Add || mode == BlendMode::Multiply ||
        mode == BlendMode::Screen || mode == BlendMode::Overlay ||
        mode == BlendMode::Darken || mode == BlendMode::Lighten ||
        mode == BlendMode::Difference || mode == BlendMode::Exclusion ||
        mode == BlendMode::SoftLight || mode == BlendMode::HardLight ||
        mode == BlendMode::ColorDodge || mode == BlendMode::ColorBurn) {
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
        // Precompute src→dst offset to eliminate per-pixel/per-row bounds checks.
        const i32 y_offset = dst.origin_y() - src.origin_y();
        const i32 x_offset = dst.origin_x() - src.origin_x();
        auto process_rows = [&](i32 row_begin, i32 row_end) {
            // Clamp row range to valid src rows — avoids per-row sy bounds check.
            // sy = y + y_offset; want 0 ≤ sy < src.height() → y ≥ -y_offset, y < src.height() - y_offset.
            const i32 sy_begin = std::max(row_begin, -y_offset);
            const i32 sy_end   = std::min(row_end,   src.height() - y_offset);
            // Clamp column range to valid src columns — avoids per-pixel sx bounds check.
            // sx = x + x_offset; want 0 ≤ sx < src.width() → x ≥ -x_offset, x < src.width() - x_offset.
            const i32 sx_begin = std::max(x0,       -x_offset);
            const i32 sx_end   = std::min(x1,        src.width() - x_offset);
            for (i32 y = sy_begin; y < sy_end; ++y) {
                const i32 sy = y + y_offset;
                const Color* s_row = src.pixels_row(sy);
                Color* d_row = dst.pixels_row(y);
                for (i32 x = sx_begin; x < sx_end; ++x) {
                    const i32 sx = x + x_offset;
                    // Prefetch next cache line every 16 pixels
                    if ((x & 15) == 0 && x + 16 < sx_end) {
                        chronon3d::prefetch(&s_row[sx + 16], false, 1);
                        chronon3d::prefetch(&d_row[x + 16], true, 1);
                    }
                    Color s = s_row[sx];
                    if (s.a <= 0.0f) continue;
                    if (std::isnan(s.r) || std::isnan(s.g) || std::isnan(s.b) || std::isnan(s.a) ||
                        std::isinf(s.r) || std::isinf(s.g) || std::isinf(s.b) || std::isinf(s.a)) {
                        continue;
                    }
                    d_row[x] = compositor::blend(s, d_row[x], mode);
                }
            }
        };
        const auto t_blend0 = profiling::now();
        if (cnt) {
            const auto setup_ms = static_cast<uint64_t>(profiling::duration_ms(t_setup0, t_blend0));
            cnt->compositenode_setup_ms.fetch_add(setup_ms, std::memory_order_relaxed);
        }
        if (row_count >= 8) {
            if (cnt) cnt->used_parallel_composite.fetch_add(1, std::memory_order_relaxed);
            parallel_for_tracked(
                tbb::blocked_range<i32>(y0, y1),
                [&](const tbb::blocked_range<i32>& range) {
                    process_rows(range.begin(), range.end());
                },
                cnt,
                tbb::simple_partitioner{}
            );
        } else {
            if (cnt) cnt->skipped_composite_small.fetch_add(1, std::memory_order_relaxed);
            process_rows(y0, y1);
        }
        if (cnt) {
            const auto t_end = profiling::now();
            const auto blend_ms = static_cast<uint64_t>(profiling::duration_ms(t_blend0, t_end));
            cnt->compositenode_blend_ms.fetch_add(blend_ms, std::memory_order_relaxed);
        }
    }
}

bool SoftwareCompositor::composite_layer_normal_optimized(
    Framebuffer& dst, const Framebuffer& src, i32 x0, i32 y0, i32 x1, i32 y1,
    RenderCounters* cnt, profiling::Clock::time_point t_setup0,
    bool force_scalar_normal_blend) {
    if (profiling::g_current_counters) {
        profiling::g_current_counters->simd_lerp_calls.fetch_add(1, std::memory_order_relaxed);
    }

    const i32 width_to_process = x1 - x0;
    const i32 height_to_process = y1 - y0;
    const i32 dst_ox = dst.origin_x();
    const i32 dst_oy = dst.origin_y();
    const i32 src_ox = src.origin_x();
    const i32 src_oy = src.origin_y();

    const auto t_blend0 = profiling::now();
    if (cnt) {
        const auto setup_ms = static_cast<uint64_t>(profiling::duration_ms(t_setup0, t_blend0));
        cnt->compositenode_setup_ms.fetch_add(setup_ms, std::memory_order_relaxed);
    }

    // Row-setup timing is only valid in the sequential path (avoids race on TBB).
    uint64_t row_setup_ns = 0;
    const bool use_tbb = (height_to_process >= 8);

    auto process_rows = [&](i32 row_begin, i32 row_end) {
        // Precompute row pointers at row_begin to avoid per-row multiply-add.
        Color* d_row = dst.data() + (row_begin - dst_oy) * static_cast<usize>(dst.allocated_width()) + (x0 - dst_ox);
        const Color* s_row = src.data() + (row_begin - src_oy) * static_cast<usize>(src.allocated_width()) + (x0 - src_ox);
        const i32 d_stride = dst.allocated_width();
        const i32 s_stride = src.allocated_width();
        for (i32 y = row_begin; y < row_end; ++y) {
            // Prefetch next rows every 4 rows to warm cache
            if (!use_tbb && (y & 3) == 0 && y + 4 < row_end) {
                chronon3d::prefetch(s_row + 4 * s_stride, false, 1);
                chronon3d::prefetch(d_row + 4 * d_stride, true, 1);
            }
            if (!use_tbb) {
                const auto t_rs0 = profiling::now();
                simd::composite_normal_premul(d_row, s_row, width_to_process, force_scalar_normal_blend);
                const auto t_rs1 = profiling::now();
                row_setup_ns += static_cast<uint64_t>(profiling::duration_us(t_rs0, t_rs1) * 1000.0);
            } else {
                simd::composite_normal_premul(d_row, s_row, width_to_process, force_scalar_normal_blend);
            }
            d_row += d_stride;
            s_row += s_stride;
        }
    };        if (use_tbb) {
            if (cnt) cnt->used_parallel_composite.fetch_add(1, std::memory_order_relaxed);
            parallel_for_tracked(
                tbb::blocked_range<i32>(y0, y1),
                [&](const tbb::blocked_range<i32>& range) {
                    process_rows(range.begin(), range.end());
                },
                cnt,
                tbb::simple_partitioner{}
            );
    } else {
        if (cnt) cnt->skipped_composite_small.fetch_add(1, std::memory_order_relaxed);
        process_rows(y0, y1);
    }

    if (cnt) {
        const auto t_end = profiling::now();
        const auto blend_ms = static_cast<uint64_t>(profiling::duration_ms(t_blend0, t_end));
        const auto row_ms = static_cast<uint64_t>(static_cast<double>(row_setup_ns) / 1e6);
        cnt->compositenode_blend_ms.fetch_add(blend_ms, std::memory_order_relaxed);
        cnt->compositenode_row_ms.fetch_add(row_ms, std::memory_order_relaxed);
    }

    return true;
}

bool SoftwareCompositor::composite_layer_non_normal_optimized(
    Framebuffer& dst, const Framebuffer& src, BlendMode mode,
    i32 x0, i32 y0, i32 x1, i32 y1,
    RenderCounters* cnt, profiling::Clock::time_point t_setup0) {
    const i32 width_to_process = x1 - x0;
    const i32 height_to_process = y1 - y0;
    const i32 dst_ox = dst.origin_x();
    const i32 dst_oy = dst.origin_y();
    const i32 src_ox = src.origin_x();
    const i32 src_oy = src.origin_y();

    const auto t_blend0 = profiling::now();
    if (cnt) {
        const auto setup_ms = static_cast<uint64_t>(profiling::duration_ms(t_setup0, t_blend0));
        cnt->compositenode_setup_ms.fetch_add(setup_ms, std::memory_order_relaxed);
    }

    auto process_rows = [&](i32 row_begin, i32 row_end) {
        // Precompute row pointers to avoid per-row multiply-add.
        Color* d_row = dst.data() + (row_begin - dst_oy) * static_cast<usize>(dst.allocated_width()) + (x0 - dst_ox);
        const Color* s_row = src.data() + (row_begin - src_oy) * static_cast<usize>(src.allocated_width()) + (x0 - src_ox);
        const i32 d_stride = dst.allocated_width();
        const i32 s_stride = src.allocated_width();
        for (i32 y = row_begin; y < row_end; ++y) {
            // Prefetch next rows every 4 rows to warm cache
            if ((y & 3) == 0 && y + 4 < row_end) {
                chronon3d::prefetch(s_row + 4 * s_stride, false, 1);
                chronon3d::prefetch(d_row + 4 * d_stride, true, 1);
            }
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
                case BlendMode::Darken:
                    simd::composite_darken_premul(d_row, s_row, width_to_process);
                    break;
                case BlendMode::Lighten:
                    simd::composite_lighten_premul(d_row, s_row, width_to_process);
                    break;
                case BlendMode::Difference:
                    simd::composite_difference_premul(d_row, s_row, width_to_process);
                    break;
                case BlendMode::Exclusion:
                    simd::composite_exclusion_premul(d_row, s_row, width_to_process);
                    break;
                case BlendMode::SoftLight:
                    simd::composite_soft_light_premul(d_row, s_row, width_to_process);
                    break;
                case BlendMode::HardLight:
                    simd::composite_hard_light_premul(d_row, s_row, width_to_process);
                    break;
                case BlendMode::ColorDodge:
                    simd::composite_color_dodge_premul(d_row, s_row, width_to_process);
                    break;
                case BlendMode::ColorBurn:
                    simd::composite_color_burn_premul(d_row, s_row, width_to_process);
                    break;
                default:
                    break;
            }
            d_row += d_stride;
            s_row += s_stride;
        }
    };        if (height_to_process >= 8) {
            if (cnt) cnt->used_parallel_composite.fetch_add(1, std::memory_order_relaxed);
            parallel_for_tracked(
                tbb::blocked_range<i32>(y0, y1),
                [&](const tbb::blocked_range<i32>& range) {
                    process_rows(range.begin(), range.end());
                },
                cnt,
                tbb::simple_partitioner{}
            );
    } else {
        if (cnt) cnt->skipped_composite_small.fetch_add(1, std::memory_order_relaxed);
        process_rows(y0, y1);
    }

    if (cnt) {
        const auto t_end = profiling::now();
        const auto blend_us = static_cast<uint64_t>(profiling::duration_ms(t_blend0, t_end));
        cnt->compositenode_blend_ms.fetch_add(blend_us, std::memory_order_relaxed);
    }

    return true;
}

} // namespace chronon3d
