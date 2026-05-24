#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include "sampling_utils.hpp"

namespace chronon3d::graph::detail {

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
                        Color src = sample_nearest(src_data, src_w, src_h, sx, sy);
                        if (src.a > 0.001f) {
                            dst_row[x - dst_origin_x] = apply_opacity(src, opacity);
                        }
                    }
                }
                h_row += h_step_x;
            }
        }
    } else {
        // Bilinear mode
        for (i32 y = row_begin; y < row_end; ++y) {
            Color* dst_row = result->pixels_row(y - result->origin_y());
            Vec3 h_row = h_col_start + h_step_y * static_cast<f32>(y - y0);
            for (i32 x = x0; x < x1; ++x) {
                if (std::abs(h_row.z) > 1e-9f) {
                    const f32 inv_z = 1.0f / h_row.z;
                    const f32 sx = h_row.x * inv_z;
                    const f32 sy = h_row.y * inv_z;
                    if (sx >= x_min_src && sx < x_max_src && sy >= y_min_src && sy < y_max_src) {
                        Color src = sample_bilinear(src_data, src_w, src_h, sx, sy);
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
