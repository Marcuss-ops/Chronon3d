#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <cstring>

namespace chronon3d::graph::detail {

// ─── Integer translation (per-pixel source clamping, TBB) ────────────────────

void execute_translate_clamped(
    Framebuffer* result, const Framebuffer* input,
    i32 x0, i32 x1, i32 y0, i32 y1,
    f32 x_min_src, f32 x_max_src, f32 y_min_src, f32 y_max_src,
    i32 itx, i32 ity, f32 opacity)
{
    const Color* src_data = input->data();
    const i32 src_w = input->width();

    auto worker = [&](i32 row_begin, i32 row_end) {
        for (i32 y = row_begin; y < row_end; ++y) {
            const i32 iy = y - ity;
            if (iy >= static_cast<i32>(y_min_src) && iy < static_cast<i32>(y_max_src)) {
                Color* dst_row = result->pixels_row(y - result->origin_y());
                const i32 dx0 = std::max(x0, static_cast<i32>(x_min_src) + itx);
                const i32 dx1 = std::min(x1, static_cast<i32>(x_max_src) + itx);

                if (dx0 < dx1) {
                    if (opacity >= 0.999f) {
                        std::memcpy(
                            dst_row + (dx0 - result->origin_x()),
                            src_data + (iy * src_w + (dx0 - itx)),
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

// ─── Integer translation (row-memcpy, no TBB) ────────────────────────────────

void execute_translate_memcpy(
    Framebuffer* result, const Framebuffer* input,
    i32 x0, i32 x1, i32 y0, i32 y1,
    i32 itx, i32 ity, f32 opacity)
{
    CHRONON_ZONE_C("transform_fast_translate", trace_category::kRasterize);

    const i32 row_pixels = (x1 - x0);
    const size_t row_bytes = static_cast<size_t>(row_pixels) * sizeof(Color);

    for (i32 y = y0; y < y1; ++y) {
        const i32 src_y = y - ity;
        const i32 src_x = x0 - itx;

        if (src_y >= 0 && src_y < input->height()) {
            const Color* src_ptr = input->pixels_row(src_y) + src_x;
            Color* dst_ptr = result->pixels_row(y - result->origin_y()) + (x0 - result->origin_x());

            if (std::abs(opacity - 1.0f) < 1e-6f) {
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
}

} // namespace chronon3d::graph::detail
