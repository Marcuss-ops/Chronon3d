#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <cstring>

#include <spdlog/spdlog.h>

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

    if (y1 - y0 >= 24) {
        tbb::parallel_for(
            tbb::blocked_range<i32>(y0, y1),
            [&](const tbb::blocked_range<i32>& range) {
                worker(range.begin(), range.end());
            }
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

    if (y1 - y0 >= 24) {
        tbb::parallel_for(
            tbb::blocked_range<i32>(y0, y1),
            [&](const tbb::blocked_range<i32>& range) {
                worker(range.begin(), range.end());
            }
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
        // Scalar bilinear sampling (no Highway SIMD)
        for (i32 y = row_begin; y < row_end; ++y) {
            Color* dst_row = result->pixels_row(y - result->origin_y());
            const Vec3 h_row = h_col_start + h_step_y * static_cast<f32>(y - y0);
            f32 sx = h_row.x * inv_z;
            f32 sy = h_row.y * inv_z;
            for (i32 x = x0; x < x1; ++x) {
                if (sx >= x_min_src && sx < x_max_src && sy >= y_min_src && sy < y_max_src) {
                    Color src = sample_bilinear(src_data, src_w, src_h, stride, sx, sy);
                    if (src.a > 0.001f) {
                        dst_row[x - dst_origin_x] = apply_opacity(src, opacity);
                    }
                }
                sx += dsx;
                sy += dsy;
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
        for (i32 y = row_begin; y < row_end; ++y) {
            Color* dst_row = result->pixels_row(y - result->origin_y());
            Vec3 h_row = h_col_start + h_step_y * static_cast<f32>(y - y0);
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
