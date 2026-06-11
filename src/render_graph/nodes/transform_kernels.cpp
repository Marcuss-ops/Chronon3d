#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/parallel_tracked.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <cstring>

#include <spdlog/spdlog.h>
#include <chronon3d/core/memory_utils.hpp>

#include "sampling_utils.hpp"

namespace chronon3d::graph::detail {

void execute_translate_clamped(
    Framebuffer* result, const Framebuffer* input,
    i32 x0, i32 x1, i32 y0, i32 y1,
    f32 x_min_src, f32 x_max_src, f32 y_min_src, f32 y_max_src,
    i32 itx, i32 ity, f32 opacity)
{
    const Color* src_data = input->data();
    const i32 src_w = input->stride();

    auto worker = [&](i32 row_begin, i32 row_end) {
        for (i32 y = row_begin; y < row_end; ++y) {
            const i32 iy = y - ity;
            if (iy >= static_cast<i32>(y_min_src) && iy < static_cast<i32>(y_max_src)) {
                Color* dst_row = result->pixels_row(y - result->origin_y());
                const i32 dx0 = std::max(x0, static_cast<i32>(x_min_src) + itx);
                const i32 dx1 = std::min(x1, static_cast<i32>(x_max_src) + itx);

                if (dx0 < dx1) {
                    if (opacity >= 0.999f) {
                        Color* dst_ptr = dst_row + (dx0 - result->origin_x());
                        const Color* src_ptr = src_data + (iy * src_w + (dx0 - itx));

                        if (profiling::g_current_counters) {
                            if ((reinterpret_cast<uintptr_t>(dst_ptr) % 32 != 0) || (reinterpret_cast<uintptr_t>(src_ptr) % 32 != 0)) {
                                profiling::g_current_counters->unaligned_memory_copies.fetch_add(1, std::memory_order_relaxed);
                            }
                        }

                        std::memcpy(
                            dst_ptr,
                            src_ptr,
                            static_cast<size_t>(dx1 - dx0) * sizeof(Color)
                        );
                    } else {
                        Color* dst_ptr = dst_row + (dx0 - result->origin_x());
                        const Color* src_ptr = src_data + (iy * src_w + (dx0 - itx));
                        const int count = dx1 - dx0;
                        for (int i = 0; i < count; ++i) {
                            Color src = src_ptr[i];
                            if (src.a > 0.001f) {
                                src.r *= opacity; src.g *= opacity;
                                src.b *= opacity; src.a *= opacity;
                                dst_ptr[i] = src;
                            }
                        }
                    }
                }
            }
        }
    };

    if (y1 - y0 >= 12) {
        parallel_for_tracked(
            tbb::blocked_range<i32>(y0, y1),
            [&](const tbb::blocked_range<i32>& range) {
                worker(range.begin(), range.end());
            },
            profiling::g_current_counters,
            tbb::simple_partitioner{}
        );
    } else {
        worker(y0, y1);
    }
}

void execute_translate_memcpy(
    Framebuffer* result, const Framebuffer* input,
    i32 x0, i32 x1, i32 y0, i32 y1,
    i32 itx, i32 ity, f32 opacity)
{
    CHRONON_ZONE_C("transform_fast_translate", trace_category::kRasterize);

    const i32 row_pixels = (x1 - x0);
    const size_t row_bytes = static_cast<size_t>(row_pixels) * sizeof(Color);
    const bool have_unaligned_tracking = profiling::g_current_counters != nullptr;
    const bool full_opacity = std::abs(opacity - 1.0f) < 1e-6f;

    auto worker = [&](i32 row_begin, i32 row_end) {
        for (i32 y = row_begin; y < row_end; ++y) {
            const i32 src_y = y - ity;
            const i32 src_x = x0 - itx;

            if (src_y >= 0 && src_y < input->height()) {
                const Color* src_ptr = input->pixels_row(src_y) + src_x;
                Color* dst_ptr = result->pixels_row(y - result->origin_y()) + (x0 - result->origin_x());

                if (full_opacity) {
                    if (have_unaligned_tracking) {
                        if ((reinterpret_cast<uintptr_t>(dst_ptr) % 32 != 0) || (reinterpret_cast<uintptr_t>(src_ptr) % 32 != 0)) {
                            profiling::g_current_counters->unaligned_memory_copies.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                    std::memcpy(dst_ptr, src_ptr, row_bytes);
                } else {
                    for (i32 x = 0; x < row_pixels; ++x) {
                        Color c = src_ptr[x];
                        c.r *= opacity; c.g *= opacity; c.b *= opacity; c.a *= opacity;
                        dst_ptr[x] = c;
                    }
                }
            }
        }
    };

    if (y1 - y0 >= 12) {
        parallel_for_tracked(
            tbb::blocked_range<i32>(y0, y1),
            [&](const tbb::blocked_range<i32>& range) {
                worker(range.begin(), range.end());
            },
            profiling::g_current_counters,
            tbb::simple_partitioner{}
        );
    } else {
        worker(y0, y1);
    }
}

void execute_affine_rows(
    Framebuffer* result, const Framebuffer* input,
    i32 x0, i32 x1, i32 y0,
    f32 x_min_src, f32 x_max_src, f32 y_min_src, f32 y_max_src,
    f32 opacity, SamplingMode mode,
    Vec3 h_col_start, Vec3 h_step_y,
    f32 inv_z, f32 dsx, f32 dsy,
    i32 row_begin, i32 row_end)
{
    const Color* src_data = input->data();
    const i32 src_w = input->width();
    const i32 src_h = input->height();
    const i32 stride = input->stride();
    const i32 dst_origin_x = result->origin_x();

    if (mode == SamplingMode::Nearest) {
        for (i32 y = row_begin; y < row_end; ++y) {
            Color* dst_row = result->pixels_row(y - result->origin_y());
            const Vec3 h_row = h_col_start + h_step_y * static_cast<f32>(y - y0);
            f32 sx = h_row.x * inv_z;
            f32 sy = h_row.y * inv_z;
            for (i32 x = x0; x < x1; ++x) {
                if (sx >= x_min_src && sx < x_max_src && sy >= y_min_src && sy < y_max_src) {
                    Color src = sample_nearest(src_data, src_w, src_h, stride, sx, sy);
                    if (src.a > 0.001f) {
                        dst_row[x - dst_origin_x] = apply_opacity(src, opacity);
                    }
                }
                sx += dsx;
                sy += dsy;
            }
        }
    } else {
        // Bilinear sampling with Highway SIMD lerp + interior fast path.
        //
        // Interior-pixel definition: all 4 bilinear neighbours are within
        // the source image bounds, i.e. floor(sx-0.5) >= 0,
        // floor(sx-0.5)+1 < src_w, floor(sy-0.5) >= 0, floor(sy-0.5)+1 < src_h.
        // Equivalently: sx in [0.5, src_w-0.5) and sy in [0.5, src_h-0.5).
        //
        // For each row, we pre-compute the x-range where this holds AND
        // the pixel is within the content bounding box [x_min_src, x_max_src).
        // Interior pixels use a stripped-down sampler (no bounds check).
        // Edge pixels fall back to the general clamped sampler.

        const f32 interior_x_min = std::max(0.5f, x_min_src);
        const f32 interior_x_max = std::min(static_cast<f32>(src_w) - 0.5f, x_max_src);
        const f32 interior_y_min = std::max(0.5f, y_min_src);
        const f32 interior_y_max = std::min(static_cast<f32>(src_h) - 0.5f, y_max_src);

        for (i32 y = row_begin; y < row_end; ++y) {
            Color* dst_row = result->pixels_row(y - result->origin_y());
            const Vec3 h_row = h_col_start + h_step_y * static_cast<f32>(y - y0);
            f32 sx = h_row.x * inv_z;
            f32 sy = h_row.y * inv_z;

            // ── Determine interior x-range for this row ─────────────
            // We solve: sx + k*dsx in [interior_x_min, interior_x_max)
            // with k ∈ [0, span)  and  sy + k*dsy in [interior_y_min, interior_y_max)
            const i32 span = x1 - x0;
            i32 interior_start = x1;  // default: no interior pixels
            i32 interior_end   = x1;

            if (dsx > 0.0f && sy >= interior_y_min && sy < interior_y_max) {
                // Check last pixel's y too
                const f32 sy_last = sy + static_cast<f32>(span - 1) * dsy;
                if (sy_last >= interior_y_min && sy_last < interior_y_max) {
                    // Solve sx + k*dsx >= interior_x_min
                    if (sx < interior_x_min) {
                        const f32 k_min_float = (interior_x_min - sx) / dsx;
                        interior_start = x0 + static_cast<i32>(std::ceil(k_min_float - 1e-6f));
                        interior_start = std::clamp(interior_start, x0, x1);
                    } else {
                        interior_start = x0;
                    }
                    // Solve sx + k*dsx < interior_x_max
                    const f32 sx_last = sx + static_cast<f32>(span - 1) * dsx;
                    if (sx_last >= interior_x_max) {
                        const f32 k_max_float = (interior_x_max - sx) / dsx;
                        interior_end = x0 + static_cast<i32>(std::ceil(k_max_float - 1e-6f));
                        interior_end = std::clamp(interior_end, interior_start, x1);
                    } else {
                        interior_end = x1;
                    }
                }
            }

            // ── Prefetch for the whole row ──────────────────────────
            const i32 src_y0 = static_cast<i32>(sy);
            const i32 src_y1 = src_y0 + 1;
            const i32 sx_idx = static_cast<i32>(sx);
            if (src_y0 >= 0 && src_y0 < src_h) {
                chronon3d::prefetch(
                    &src_data[static_cast<size_t>(src_y0) * stride + static_cast<size_t>(sx_idx)], false, 1);
            }
            if (src_y1 >= 0 && src_y1 < src_h) {
                chronon3d::prefetch(
                    &src_data[static_cast<size_t>(src_y1) * stride + static_cast<size_t>(sx_idx)], false, 1);
            }

            // ── Region 1: left edge (clamped sampler) ───────────────
            {
                f32 edge_sx = sx;
                f32 edge_sy = sy;
                for (i32 x = x0; x < interior_start; ++x) {
                    if (edge_sx >= x_min_src && edge_sx < x_max_src &&
                        edge_sy >= y_min_src && edge_sy < y_max_src) {
                        Color src = sample_bilinear(src_data, src_w, src_h, stride, edge_sx, edge_sy);
                        if (src.a > 0.001f) {
                            dst_row[x - dst_origin_x] = apply_opacity(src, opacity);
                        }
                    }
                    edge_sx += dsx;
                    edge_sy += dsy;
                }
            }

            // ── Region 2: interior (no bounds check, SIMD bilinear) ─
            if (interior_end > interior_start) {
                const i32 k_start = interior_start - x0;
                f32 int_sx = sx + static_cast<f32>(k_start) * dsx;
                f32 int_sy = sy + static_cast<f32>(k_start) * dsy;
                const i32 base_y = static_cast<i32>(sy - 0.5f);
                const Color* int_row0 = src_data + static_cast<size_t>(base_y) * stride;
                const Color* int_row1 = int_row0 + stride;
                i32 current_base_y = base_y;
                const Color* current_row0 = int_row0;
                const Color* current_row1 = int_row1;

                for (i32 x = interior_start; x < interior_end; ++x) {
                    const float u = int_sx - 0.5f;
                    const float v = int_sy - 0.5f;
                    const i32 ix0 = static_cast<i32>(u);
                    const i32 iy0 = static_cast<i32>(v);
                    const float tx = u - static_cast<float>(ix0);
                    const float ty = v - static_cast<float>(iy0);

                    // Update row pointers if source row changed
                    if (iy0 != current_base_y) {
                        current_base_y = iy0;
                        current_row0 = src_data + static_cast<size_t>(iy0) * stride;
                        current_row1 = current_row0 + stride;
                    }

                    Color src = sample_bilinear_interior(current_row0, current_row1, ix0, tx, ty);
                    // Note: interior pixels are guaranteed to have all 4 texels
                    // in-bounds (by the interior range computation), so no clamping
                    // is needed. The source row pointers are always valid.
                    if (src.a > 0.001f) {
                        dst_row[x - dst_origin_x] = apply_opacity(src, opacity);
                    }
                    int_sx += dsx;
                    int_sy += dsy;
                }
            }

            // ── Region 3: right edge (clamped sampler) ──────────────
            if (interior_end < x1) {
                const i32 k_end = interior_end - x0;
                f32 edge_sx = sx + static_cast<f32>(k_end) * dsx;
                f32 edge_sy = sy + static_cast<f32>(k_end) * dsy;
                for (i32 x = interior_end; x < x1; ++x) {
                    if (edge_sx >= x_min_src && edge_sx < x_max_src &&
                        edge_sy >= y_min_src && edge_sy < y_max_src) {
                        Color src = sample_bilinear(src_data, src_w, src_h, stride, edge_sx, edge_sy);
                        if (src.a > 0.001f) {
                            dst_row[x - dst_origin_x] = apply_opacity(src, opacity);
                        }
                    }
                    edge_sx += dsx;
                    edge_sy += dsy;
                }
            }
        }
    }
}

void execute_projective_rows(
    Framebuffer* result, const Framebuffer* input,
    i32 x0, i32 x1, i32 y0,
    f32 x_min_src, f32 x_max_src, f32 y_min_src, f32 y_max_src,
    f32 opacity, SamplingMode mode,
    Vec3 h_col_start, Vec3 h_step_x, Vec3 h_step_y,
    i32 row_begin, i32 row_end)
{
    const Color* src_data = input->data();
    const i32 src_w = input->width();
    const i32 src_h = input->height();
    const i32 stride = input->stride();
    const i32 dst_origin_x = result->origin_x();

    if (mode == SamplingMode::Nearest) {
        for (i32 y = row_begin; y < row_end; ++y) {
            Color* dst_row = result->pixels_row(y - result->origin_y());
            Vec3 h_row = h_col_start + h_step_y * static_cast<f32>(y - y0);
            for (i32 x = x0; x < x1; ++x) {
                if (std::abs(h_row.z) > 1e-9f) {
                    const f32 inv_z = 1.0f / h_row.z;
                    const f32 sx = h_row.x * inv_z;
                    const f32 sy = h_row.y * inv_z;
                    if (sx >= x_min_src && sx < x_max_src && sy >= y_min_src && sy < y_max_src) {
                        Color src = sample_nearest(src_data, src_w, src_h, stride, sx, sy);
                        if (src.a > 0.001f) {
                            dst_row[x - dst_origin_x] = apply_opacity(src, opacity);
                        }
                    }
                }
                h_row += h_step_x;
            }
        }
    } else {
        // Bilinear sampling with Highway SIMD lerp + source-row prefetch.
        for (i32 y = row_begin; y < row_end; ++y) {
            Color* dst_row = result->pixels_row(y - result->origin_y());
            Vec3 h_row = h_col_start + h_step_y * static_cast<f32>(y - y0);

            // Prefetch: the first h_row gives a hint for which source rows
            // will be sampled.  Prefetch two source rows around the
            // projected y coordinate.
            if (std::abs(h_row.z) > 1e-9f) {
                const f32 proj_sy = h_row.y / h_row.z;
                const f32 proj_sx = h_row.x / h_row.z;
                const i32 src_y0 = static_cast<i32>(proj_sy);
                const i32 src_y1 = src_y0 + 1;
                const i32 sx_idx = static_cast<i32>(proj_sx);
                if (src_y0 >= 0 && src_y0 < src_h) {
                    chronon3d::prefetch(
                        &src_data[static_cast<size_t>(src_y0) * stride + static_cast<size_t>(sx_idx)], false, 1);
                }
                if (src_y1 >= 0 && src_y1 < src_h) {
                    chronon3d::prefetch(
                        &src_data[static_cast<size_t>(src_y1) * stride + static_cast<size_t>(sx_idx)], false, 1);
                }
            }

            for (i32 x = x0; x < x1; ++x) {
                if (std::abs(h_row.z) > 1e-9f) {
                    const f32 inv_z = 1.0f / h_row.z;
                    const f32 sx = h_row.x * inv_z;
                    const f32 sy = h_row.y * inv_z;
                    if (sx >= x_min_src && sx < x_max_src && sy >= y_min_src && sy < y_max_src) {
                        Color src = sample_bilinear(src_data, src_w, src_h, stride, sx, sy);
                        if (src.a > 0.001f) {
                            dst_row[x - dst_origin_x] = apply_opacity(src, opacity);
                        }
                    }
                }
                h_row += h_step_x;
            }
        }
    }
}

} // namespace chronon3d::graph::detail
