#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/core/profiling.hpp>
#include <chronon3d/core/counters.hpp>
#include <spdlog/spdlog.h>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <span>

namespace chronon3d::graph {

std::shared_ptr<Framebuffer> TransformNode::execute(
    RenderGraphContext& ctx,
    std::span<const std::shared_ptr<Framebuffer>> inputs,
    std::span<const std::optional<raster::BBox>> input_bboxes
) {
    CHRONON_ZONE_C("transform_node", trace_category::kRasterize);
    if (ctx.counters) {
        ctx.counters->transform_calls.fetch_add(1, std::memory_order_relaxed);
    }

    if (inputs.empty() || !inputs[0]) {
        auto fb = ctx.acquire_framebuffer(ctx.width, ctx.height);
        return fb;
    }

    auto input = inputs[0];
    const auto predicted = predicted_bbox(ctx, input_bboxes);
    const raster::BBox out_bounds = predicted.value_or(raster::BBox{0, 0, ctx.width, ctx.height});
    const i32 out_w = std::max(1, out_bounds.x1 - out_bounds.x0);
    const i32 out_h = std::max(1, out_bounds.y1 - out_bounds.y0);

    const Mat4 model = m_use_matrix ? m_matrix : m_transform.to_mat4();
    const f32 opacity = m_use_matrix ? m_opacity : m_transform.opacity;

    // Fast-path: check if we are doing a no-op identity transform with opacity 1.0f
    // and input bounds match output bounds exactly.
    bool is_identity = true;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            const float expected = (i == j) ? 1.0f : 0.0f;
            if (std::abs(model[i][j] - expected) > 1e-6f) {
                is_identity = false;
                break;
            }
        }
        if (!is_identity) break;
    }

    if (is_identity && std::abs(opacity - 1.0f) < 1e-6f &&
        input->width() == out_w && input->height() == out_h &&
        input->origin_x() == out_bounds.x0 && input->origin_y() == out_bounds.y0) {
        
        const uint64_t area = static_cast<uint64_t>(out_w) * out_h;
        if (ctx.backend) {
            ctx.backend->counters()->pixels_touched.fetch_add(area, std::memory_order_relaxed);
        }
        if (ctx.counters) {
            ctx.counters->transform_pixels.fetch_add(area, std::memory_order_relaxed);
        }

        if (input.use_count() == 1) {
            return input;
        } else {
            auto result = ctx.acquire_framebuffer(out_w, out_h, true, out_bounds);
            std::copy(input->data(), input->data() + input->pixel_count(), result->data());
            result->set_opaque(input->is_opaque());
            return result;
        }
    }

    auto result = ctx.acquire_framebuffer(out_w, out_h, true, out_bounds);

    // Centering logic: both source and destination framebuffers are centered at (0,0) in scene space.
    const Mat4 dst_canvas_offset = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));
    const Mat4 src_canvas_offset = math::translate(Vec3(input->width() * 0.5f, input->height() * 0.5f, 0.0f));
    
    // Final pixel matrix: DstPixel <- DstScene <- SrcScene <- SrcPixel
    const Mat4 pixel_model = dst_canvas_offset * model * glm::inverse(src_canvas_offset);

    f32 x_min_src = 0.0f;
    f32 y_min_src = 0.0f;
    f32 x_max_src = static_cast<f32>(input->width());
    f32 y_max_src = static_cast<f32>(input->height());
    if (!input_bboxes.empty() && input_bboxes[0].has_value()) {
        const auto& in_box = *input_bboxes[0];
        x_min_src = static_cast<f32>(std::clamp(in_box.x0, 0, input->width()));
        y_min_src = static_cast<f32>(std::clamp(in_box.y0, 0, input->height()));
        x_max_src = static_cast<f32>(std::clamp(in_box.x1, 0, input->width()));
        y_max_src = static_cast<f32>(std::clamp(in_box.y1, 0, input->height()));
        if (x_min_src >= x_max_src || y_min_src >= y_max_src) {
            return result;
        }
    }

    const f32 w_src = x_max_src - x_min_src;
    const f32 h_src = y_max_src - y_min_src;

    // Extract 3x3 homography for local_z = 0 plane
    glm::mat3 H;
    H[0][0] = pixel_model[0][0]; H[0][1] = pixel_model[0][1]; H[0][2] = pixel_model[0][3];
    H[1][0] = pixel_model[1][0]; H[1][1] = pixel_model[1][1]; H[1][2] = pixel_model[1][3];
    H[2][0] = pixel_model[3][0]; H[2][1] = pixel_model[3][1]; H[2][2] = pixel_model[3][3];

    const glm::mat3 inv_pixel_model_3x3 = glm::inverse(H);

    const auto winding = projected_quad_signed_area(pixel_model, w_src, h_src);
    const bool flipped = winding.has_value() && *winding < 0.0f;
    if (ctx.counters && flipped) {
        ctx.counters->projected_winding_flips.fetch_add(1, std::memory_order_relaxed);
    }

    if (ctx.diagnostics_enabled) {
        const f32 det = glm::determinant(H);
        spdlog::info(
            "[transform-debug] node='{}' det={:.6f} winding={} H=[[{:.3f},{:.3f},{:.3f}],[{:.3f},{:.3f},{:.3f}],[{:.3f},{:.3f},{:.3f}]]",
            name(),
            det,
            winding.has_value() ? (*winding < 0.0f ? "flipped" : "ok") : "invalid",
            H[0][0], H[0][1], H[0][2],
            H[1][0], H[1][1], H[1][2],
            H[2][0], H[2][1], H[2][2]
        );
        if (flipped) {
            spdlog::warn("[transform-debug] node='{}' projected quad has negative winding", name());
        }
    }

    // Bounding box for optimization in destination pixels
    Vec4 corners[4] = {
        pixel_model * Vec4(x_min_src, y_min_src, 0, 1),
        pixel_model * Vec4(x_max_src, y_min_src, 0, 1),
        pixel_model * Vec4(x_max_src, y_max_src, 0, 1),
        pixel_model * Vec4(x_min_src, y_max_src, 0, 1)
    };

    f32 min_x = 1e10f, max_x = -1e10f;
    f32 min_y = 1e10f, max_y = -1e10f;
    for (auto& c : corners) {
        if (std::abs(c.w) < 1e-6f) continue;
        f32 px = c.x / c.w;
        f32 py = c.y / c.w;
        min_x = std::min(min_x, px);
        max_x = std::max(max_x, px);
        min_y = std::min(min_y, py);
        max_y = std::max(max_y, py);
    }

    i32 x0 = std::clamp(static_cast<i32>(std::floor(min_x)), result->origin_x(), result->origin_x() + result->width());
    i32 x1 = std::clamp(static_cast<i32>(std::ceil(max_x)), result->origin_x(), result->origin_x() + result->width());
    i32 y0 = std::clamp(static_cast<i32>(std::floor(min_y)), result->origin_y(), result->origin_y() + result->height());
    i32 y1 = std::clamp(static_cast<i32>(std::ceil(max_y)), result->origin_y(), result->origin_y() + result->height());

    if (ctx.clip_rect) {
        x0 = std::max(x0, ctx.clip_rect->x0);
        y0 = std::max(y0, ctx.clip_rect->y0);
        x1 = std::min(x1, ctx.clip_rect->x1);
        y1 = std::min(y1, ctx.clip_rect->y1);
    }

    const uint64_t area = (x1 > x0 && y1 > y0)
        ? static_cast<uint64_t>(x1 - x0) * static_cast<uint64_t>(y1 - y0)
        : 0;

    if (ctx.backend) {
        ctx.backend->counters()->pixels_touched.fetch_add(area, std::memory_order_relaxed);
    }
    if (ctx.counters) {
        ctx.counters->transform_pixels.fetch_add(area, std::memory_order_relaxed);
    }

    const bool is_pure_translation = 
        std::abs(H[0][0] - 1.0f) < 1e-6f && std::abs(H[0][1]) < 1e-6f && std::abs(H[0][2]) < 1e-6f &&
        std::abs(H[1][0]) < 1e-6f && std::abs(H[1][1] - 1.0f) < 1e-6f && std::abs(H[1][2]) < 1e-6f &&
        std::abs(H[2][2] - 1.0f) < 1e-6f;

    if (is_pure_translation) {
        const f32 tx = H[2][0];
        const f32 ty = H[2][1];
        
        const bool is_integer_translation = 
            std::abs(tx - std::round(tx)) < 1e-4f &&
            std::abs(ty - std::round(ty)) < 1e-4f;

        if (is_integer_translation) {
            const i32 itx = static_cast<i32>(std::round(tx));
            const i32 ity = static_cast<i32>(std::round(ty));
            
            const Color* src_data = input->data();
            const i32 src_w = input->width();
            
            auto process_translation_rows = [&](i32 row_begin, i32 row_end) {
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
                                    (dx1 - dx0) * sizeof(Color)
                                );
                            } else {
                                Color* dst_ptr = dst_row + (dx0 - result->origin_x());
                                const Color* src_ptr = src_data + (iy * src_w + (dx0 - itx));
                                const int count = dx1 - dx0;
                                for (int i = 0; i < count; ++i) {
                                    Color src = src_ptr[i];
                                    if (src.a > 0.001f) {
                                        src.r *= opacity; src.g *= opacity; src.b *= opacity; src.a *= opacity;
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
                        process_translation_rows(range.begin(), range.end());
                    }
                );
            } else {
                process_translation_rows(y0, y1);
            }

            return result;
        }
    }

    const bool is_affine = std::abs(H[2][0]) < 1e-9f && std::abs(H[2][1]) < 1e-9f;

    // Roadmap 3: Fast-path for pure translation
    if (is_affine && std::abs(H[0][1]) < 1e-6f && std::abs(H[1][0]) < 1e-6f &&
        std::abs(H[0][0] - 1.0f) < 1e-6f && std::abs(H[1][1] - 1.0f) < 1e-6f) {
        
        const float tx = H[2][0];
        const float ty = H[2][1];
        
        if (std::abs(tx - std::round(tx)) < 1e-4f && std::abs(ty - std::round(ty)) < 1e-4f) {
            const i32 itx = static_cast<i32>(std::round(tx));
            const i32 ity = static_cast<i32>(std::round(ty));
            
            CHRONON_ZONE_C("transform_fast_translate", trace_category::kRasterize);
            
            const i32 row_pixels = (x1 - x0);
            const size_t row_bytes = row_pixels * sizeof(Color);
            
            for (i32 y = y0; y < y1; ++y) {
                const i32 src_y = y - ity;
                const i32 src_x = x0 - itx;
                
                if (src_y >= 0 && src_y < input->height()) {
                    const Color* src_ptr = input->pixels_row(src_y) + src_x;
                    Color* dst_ptr = result->pixels_row(y - result->origin_y()) + (x0 - result->origin_x());
                    
                    if (std::abs(opacity - 1.0f) < 1e-6f) {
                        std::memcpy(dst_ptr, src_ptr, row_bytes);
                    } else {
                        // Scalar opacity path (could be SIMD-ified)
                        for (i32 x = 0; x < row_pixels; ++x) {
                            Color c = src_ptr[x];
                            c.r *= opacity; c.g *= opacity; c.b *= opacity; c.a *= opacity;
                            dst_ptr[x] = c;
                        }
                    }
                }
            }
            return result;
        }
    }

    const Vec3 h_col_start = inv_pixel_model_3x3 * Vec3(static_cast<f32>(x0) + 0.5f, static_cast<f32>(y0) + 0.5f, 1.0f);
    const Vec3 h_step_x = inv_pixel_model_3x3[0];
    const Vec3 h_step_y = inv_pixel_model_3x3[1];

    const auto process_rows = [&](i32 row_begin, i32 row_end) {
        if (is_affine) {
            const f32 inv_z = 1.0f / h_col_start.z;
            const f32 dsx = h_step_x.x * inv_z;
            const f32 dsy = h_step_x.y * inv_z;

            const Color* src_data = input->data();
            const i32 src_w = input->width();
            const i32 src_h = input->height();

            if (m_mode == SamplingMode::Nearest) {
                for (i32 y = row_begin; y < row_end; ++y) {
                    Color* dst_row = result->pixels_row(y - result->origin_y());
                    const Vec3 h_row = h_col_start + h_step_y * static_cast<f32>(y - y0);
                    f32 sx = h_row.x * inv_z;
                    f32 sy = h_row.y * inv_z;
                    for (i32 x = x0; x < x1; ++x) {
                        if (sx >= x_min_src && sx < x_max_src && sy >= y_min_src && sy < y_max_src) {
                            const i32 ix = static_cast<i32>(sx);
                            const i32 iy = static_cast<i32>(sy);
                            Color src = src_data[iy * src_w + ix];
                            if (src.a > 0.001f) {
                                if (opacity < 0.999f) {
                                    src.r *= opacity; src.g *= opacity; src.b *= opacity; src.a *= opacity;
                                }
                                dst_row[x - result->origin_x()] = src;
                            }
                        }
                        sx += dsx;
                        sy += dsy;
                    }
                }
            } else {
                // Bilinear mode
                for (i32 y = row_begin; y < row_end; ++y) {
                    Color* dst_row = result->pixels_row(y - result->origin_y());
                    const Vec3 h_row = h_col_start + h_step_y * static_cast<f32>(y - y0);
                    f32 sx = h_row.x * inv_z;
                    f32 sy = h_row.y * inv_z;
                    for (i32 x = x0; x < x1; ++x) {
                        if (sx >= x_min_src && sx < x_max_src && sy >= y_min_src && sy < y_max_src) {
                            const f32 u = sx - 0.5f;
                            const f32 v = sy - 0.5f;
                            const i32 x0_f = static_cast<i32>(u >= 0.0f ? u : std::floor(u));
                            const i32 y0_f = static_cast<i32>(v >= 0.0f ? v : std::floor(v));
                            const i32 x1_f = x0_f + 1;
                            const i32 y1_f = y0_f + 1;

                            const f32 tx = u - static_cast<f32>(x0_f);
                            const f32 ty = v - static_cast<f32>(y0_f);

                            auto get_px = [&](i32 px, i32 py) -> Color {
                                if (px < 0 || px >= src_w || py < 0 || py >= src_h) return Color::transparent();
                                return src_data[py * src_w + px];
                            };

                            const Color c00 = get_px(x0_f, y0_f);
                            const Color c10 = get_px(x1_f, y0_f);
                            const Color c01 = get_px(x0_f, y1_f);
                            const Color c11 = get_px(x1_f, y1_f);

                            Color src = lerp(lerp(c00, c10, tx), lerp(c01, c11, tx), ty);
                            if (src.a > 0.001f) {
                                if (opacity < 0.999f) {
                                    src.r *= opacity; src.g *= opacity; src.b *= opacity; src.a *= opacity;
                                }
                                dst_row[x - result->origin_x()] = src;
                            }
                        }
                        sx += dsx;
                        sy += dsy;
                    }
                }
            }
        } else {
            const Color* src_data = input->data();
            const i32 src_w = input->width();
            const i32 src_h = input->height();

            if (m_mode == SamplingMode::Nearest) {
                for (i32 y = row_begin; y < row_end; ++y) {
                    Color* dst_row = result->pixels_row(y - result->origin_y());
                    Vec3 h_row = h_col_start + h_step_y * static_cast<f32>(y - y0);
                    for (i32 x = x0; x < x1; ++x) {
                        if (std::abs(h_row.z) > 1e-9f) {
                            const f32 inv_z = 1.0f / h_row.z;
                            const f32 sx = h_row.x * inv_z;
                            const f32 sy = h_row.y * inv_z;
                            if (sx >= x_min_src && sx < x_max_src && sy >= y_min_src && sy < y_max_src) {
                                const i32 ix = static_cast<i32>(sx);
                                const i32 iy = static_cast<i32>(sy);
                                Color src = src_data[iy * src_w + ix];
                                if (src.a > 0.001f) {
                                    if (opacity < 0.999f) {
                                        src.r *= opacity; src.g *= opacity; src.b *= opacity; src.a *= opacity;
                                    }
                                    dst_row[x - result->origin_x()] = src;
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
                                const f32 u = sx - 0.5f;
                                const f32 v = sy - 0.5f;
                                const i32 x0_f = static_cast<i32>(u >= 0.0f ? u : std::floor(u));
                                const i32 y0_f = static_cast<i32>(v >= 0.0f ? v : std::floor(v));
                                const i32 x1_f = x0_f + 1;
                                const i32 y1_f = y0_f + 1;

                                const f32 tx = u - static_cast<f32>(x0_f);
                                const f32 ty = v - static_cast<f32>(y0_f);

                                auto get_px = [&](i32 px, i32 py) -> Color {
                                    if (px < 0 || px >= src_w || py < 0 || py >= src_h) return Color::transparent();
                                    return src_data[py * src_w + px];
                                };

                                const Color c00 = get_px(x0_f, y0_f);
                                const Color c10 = get_px(x1_f, y0_f);
                                const Color c01 = get_px(x0_f, y1_f);
                                const Color c11 = get_px(x1_f, y1_f);

                                Color src = lerp(lerp(c00, c10, tx), lerp(c01, c11, tx), ty);
                                if (src.a > 0.001f) {
                                    if (opacity < 0.999f) {
                                        src.r *= opacity; src.g *= opacity; src.b *= opacity; src.a *= opacity;
                                    }
                                    dst_row[x - result->origin_x()] = src;
                                }
                            }
                        }
                        h_row += h_step_x;
                    }
                }
            }
        }
    };

    if (y1 - y0 >= 64 && area >= 128 * 128) {
        tbb::parallel_for(
            tbb::blocked_range<i32>(y0, y1),
            [&](const tbb::blocked_range<i32>& range) {
                process_rows(range.begin(), range.end());
            }
        );
    } else {
        process_rows(y0, y1);
    }

    return result;
}

std::optional<raster::BBox> TransformNode::predicted_bbox(
    const RenderGraphContext& ctx,
    std::span<const std::optional<raster::BBox>> input_bboxes
) const {
    const Mat4 model = m_use_matrix ? m_matrix : m_transform.to_mat4();
    const Mat4 dst_canvas_offset = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));
    
    f32 x_min_src = 0.0f;
    f32 y_min_src = 0.0f;
    f32 x_max_src = static_cast<f32>(ctx.width);
    f32 y_max_src = static_cast<f32>(ctx.height);
    if (!input_bboxes.empty() && input_bboxes[0].has_value()) {
        const auto& in_box = *input_bboxes[0];
        x_min_src = static_cast<f32>(std::clamp(in_box.x0, 0, ctx.width));
        y_min_src = static_cast<f32>(std::clamp(in_box.y0, 0, ctx.height));
        x_max_src = static_cast<f32>(std::clamp(in_box.x1, 0, ctx.width));
        y_max_src = static_cast<f32>(std::clamp(in_box.y1, 0, ctx.height));
        // Input bbox is empty (zero area) but valid.
        // Return a valid empty bbox (not nullopt) so the dirty-rect system
        // skips this node rather than triggering a full-frame fallback.
        if (x_min_src >= x_max_src || y_min_src >= y_max_src) {
            const i32 empty_x = static_cast<i32>(std::floor(x_min_src));
            const i32 empty_y = static_cast<i32>(std::floor(y_min_src));
            return raster::BBox{empty_x, empty_y, empty_x, empty_y};
        }
    }

    const Mat4 src_canvas_offset = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));
    
    const Mat4 pixel_model = dst_canvas_offset * model * glm::inverse(src_canvas_offset);

    Vec4 corners[4] = {
        pixel_model * Vec4(x_min_src, y_min_src, 0, 1),
        pixel_model * Vec4(x_max_src, y_min_src, 0, 1),
        pixel_model * Vec4(x_max_src, y_max_src, 0, 1),
        pixel_model * Vec4(x_min_src, y_max_src, 0, 1)
    };

    f32 min_x = 1e10f, max_x = -1e10f;
    f32 min_y = 1e10f, max_y = -1e10f;
    for (auto& c : corners) {
        if (std::abs(c.w) < 1e-6f) continue;
        f32 px = c.x / c.w;
        f32 py = c.y / c.w;
        min_x = std::min(min_x, px);
        max_x = std::max(max_x, px);
        min_y = std::min(min_y, py);
        max_y = std::max(max_y, py);
    }

    return raster::BBox{
        std::clamp(static_cast<i32>(std::floor(min_x)), 0, ctx.width),
        std::clamp(static_cast<i32>(std::floor(min_y)), 0, ctx.height),
        std::clamp(static_cast<i32>(std::ceil(max_x)), 0, ctx.width),
        std::clamp(static_cast<i32>(std::ceil(max_y)), 0, ctx.height)
    };
}

} // namespace chronon3d::graph
