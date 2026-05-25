#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/detail/bbox_projection.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <limits>

namespace chronon3d::graph {

namespace {

constexpr f32 kSeedFrameEpsilon = 1e-3f;

[[nodiscard]] bool nearly_equal(f32 a, f32 b, f32 eps = kSeedFrameEpsilon) {
    return std::abs(a - b) <= eps;
}

[[nodiscard]] bool covers_full_frame_as_rectangle(
    const Mat4& matrix,
    f32 width,
    f32 height
) {
    const Vec4 corners[4] = {
        matrix * Vec4(0.0f, 0.0f, 0.0f, 1.0f),
        matrix * Vec4(width, 0.0f, 0.0f, 1.0f),
        matrix * Vec4(width, height, 0.0f, 1.0f),
        matrix * Vec4(0.0f, height, 0.0f, 1.0f)
    };

    std::array<f32, 4> xs{};
    std::array<f32, 4> ys{};
    f32 min_x = std::numeric_limits<f32>::max();
    f32 min_y = std::numeric_limits<f32>::max();
    f32 max_x = std::numeric_limits<f32>::lowest();
    f32 max_y = std::numeric_limits<f32>::lowest();

    for (std::size_t i = 0; i < 4; ++i) {
        const auto& c = corners[i];
        if (std::abs(c.w) < 1e-6f) {
            return false;
        }

        xs[i] = c.x / c.w;
        ys[i] = c.y / c.w;
        min_x = std::min(min_x, xs[i]);
        min_y = std::min(min_y, ys[i]);
        max_x = std::max(max_x, xs[i]);
        max_y = std::max(max_y, ys[i]);
    }

    auto distinct_count = [](const std::array<f32, 4>& values) {
        std::array<f32, 4> unique{};
        std::size_t count = 0;
        for (f32 value : values) {
            bool seen = false;
            for (std::size_t i = 0; i < count; ++i) {
                if (nearly_equal(value, unique[i])) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                unique[count++] = value;
            }
        }
        return count;
    };

    if (distinct_count(xs) != 2 || distinct_count(ys) != 2) {
        return false;
    }

    return nearly_equal(min_x, 0.0f) &&
           nearly_equal(min_y, 0.0f) &&
           nearly_equal(max_x, width) &&
           nearly_equal(max_y, height);
}

} // namespace

std::optional<raster::BBox> SourceNode::predicted_bbox(
    const RenderGraphContext& ctx,
    std::span<const std::optional<raster::BBox>>
) const {
    const Mat4 ssaa_scale = math::scale(Vec3(ctx.ssaa_factor, ctx.ssaa_factor, 1.0f));
    const Mat4 canvas_center = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));

    Mat4 matrix;
    if (m_is_3d || m_centered) {
        matrix = canvas_center * ssaa_scale * m_matrix_override.value_or(m_node.world_transform.to_mat4());
    } else {
        matrix = ssaa_scale * m_matrix_override.value_or(m_node.world_transform.to_mat4());
    }

    f32 spread = 0.0f;
    if (m_node.shadow.enabled) {
        spread = std::max(spread, m_node.shadow.radius + std::max(std::abs(m_node.shadow.offset.x), std::abs(m_node.shadow.offset.y)));
    }
    if (m_node.glow.enabled) {
        spread = std::max(spread, m_node.glow.radius);
    }
    spread += 8.0f;

    if (ctx.has_camera_2_5d &&
        (m_node.shape.type == ShapeType::FakeBox3D || m_node.shape.type == ShapeType::GridPlane)) {
        const Mat4 world_matrix = m_matrix_override.value_or(m_node.world_transform.to_mat4());
        if (auto bbox = detail::projected_native_3d_bbox(ctx, m_node, world_matrix, spread)) {
            return bbox;
        }
        return raster::BBox{0, 0, ctx.width, ctx.height};
    }

    auto bbox = renderer::compute_world_bbox(m_node.shape, matrix, spread);
    bbox.clip_to(ctx.width, ctx.height);
    if (bbox.is_empty()) {
        return raster::BBox{0, 0, 0, 0};
    }
    return bbox;
}

cache::NodeCacheKey SourceNode::cache_key(const RenderGraphContext& ctx) const {
    auto key = m_key;
    key.params_hash = hash_combine(key.params_hash, static_cast<u64>(ctx.modular_coordinates));
    if (m_matrix_override) {
        key.params_hash = hash_combine(key.params_hash, hash_bytes(&(*m_matrix_override)[0][0], sizeof(Mat4)));
    }
    if (m_opacity_override) {
        key.params_hash = hash_combine(key.params_hash, hash_bytes(&(*m_opacity_override), sizeof(f32)));
    }
    if (m_is_3d && ctx.has_camera_2_5d) {
        const auto& cam = ctx.camera_2_5d;
        key.params_hash = hash_combine(key.params_hash, hash_bytes(&cam.position, sizeof(Vec3)));
        key.params_hash = hash_combine(key.params_hash, hash_bytes(&cam.rotation, sizeof(Vec3)));
        key.params_hash = hash_combine(key.params_hash, hash_bytes(&cam.zoom, sizeof(f32)));
        key.params_hash = hash_combine(key.params_hash, hash_bytes(&cam.fov_deg, sizeof(f32)));
        if (cam.point_of_interest_enabled) {
            key.params_hash = hash_combine(key.params_hash, hash_bytes(&cam.point_of_interest, sizeof(Vec3)));
        }
    }
    return key;
}

std::shared_ptr<Framebuffer> SourceNode::execute(
    RenderGraphContext& ctx,
    std::span<const std::shared_ptr<Framebuffer>>,
    std::span<const std::optional<raster::BBox>>
) {
    CHRONON_ZONE_C("source_render", trace_category::kRasterize);
    // If this source can seed the entire frame with an opaque image, skip the
    // initial clear and let the image write every pixel directly.
    const bool full_frame_seed = can_seed_full_frame(ctx);
    bool clear = !full_frame_seed;

    auto fb = ctx.acquire_framebuffer(ctx.width, ctx.height, clear);
    if (ctx.backend) {
        RenderState state;
        const Mat4 ssaa_scale = math::scale(Vec3(ctx.ssaa_factor, ctx.ssaa_factor, 1.0f));
        const Mat4 canvas_center = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));

        if (m_is_3d) {
            state.matrix = canvas_center * ssaa_scale * m_matrix_override.value_or(m_node.world_transform.to_mat4());
        } else {
            if (m_centered) {
                state.matrix = canvas_center * ssaa_scale * m_matrix_override.value_or(m_node.world_transform.to_mat4());
            } else {
                state.matrix = ssaa_scale * m_matrix_override.value_or(m_node.world_transform.to_mat4());
            }
        }

        if (m_node.name == "white_rect") {
            auto mat_over = m_matrix_override.value_or(m_node.world_transform.to_mat4());
            spdlog::info("DEBUG WHITE RECT EXECUTE: name={} centered={} canvas_center=[{}, {}] override_trans=[{}, {}] state_matrix=[{}, {}]",
                m_node.name, m_centered, canvas_center[3][0], canvas_center[3][1], mat_over[3][0], mat_over[3][1], state.matrix[3][0], state.matrix[3][1]);
        }

        state.opacity = m_opacity_override.value_or(m_node.world_transform.opacity);
        state.world_matrix = m_matrix_override.value_or(m_node.world_transform.to_mat4());
        state.clip_rect = ctx.clip_rect;

        if (ctx.has_camera_2_5d) {
            state.projection  = ctx.projection_ctx;
        }

        ctx.backend->draw_node(*fb, m_node, state, ctx.camera, ctx.width, ctx.height);
        fb->set_opaque(full_frame_seed);



        if (ctx.diagnostics_enabled) {
            int nonzero_pixels = 0;
            for (i32 y = 0; y < fb->height(); ++y) {
                const Color* row = fb->pixels_row(y);
                for (i32 x = 0; x < fb->width(); ++x) {
                    const Color& c = row[x];
                    if (c.a > 0.001f || c.r > 0.001f || c.g > 0.001f || c.b > 0.001f) {
                        ++nonzero_pixels;
                    }
                }
            }

            // Print ASCII grid
            std::string ascii_grid = "";
            int grid_size = 20;
            int step_x = fb->width() / grid_size;
            int step_y = fb->height() / grid_size;
            if (step_x < 1) step_x = 1;
            if (step_y < 1) step_y = 1;
            for (int gy = 0; gy < grid_size; ++gy) {
                std::string line = "";
                for (int gx = 0; gx < grid_size; ++gx) {
                    int px = gx * step_x;
                    int py = gy * step_y;
                    if (px < fb->width() && py < fb->height()) {
                        Color c = fb->get_pixel(px, py);
                        if (c.r > 0.5f) line += "R";
                        else if (c.b > 0.5f) line += "B";
                        else if (c.a > 0.1f) line += "A";
                        else line += ".";
                    }
                }
                ascii_grid += "\n" + line;
            }

            spdlog::info(
                "[source-debug] node='{}' shape={} nonzero_pixels={} opacity={:.3f} matrix_tx={:.3f} matrix_ty={:.3f} det2d={:.6f} grid:{}",
                m_name,
                static_cast<int>(m_node.shape.type),
                nonzero_pixels,
                state.opacity,
                state.matrix[3][0],
                state.matrix[3][1],
                glm::determinant(glm::mat3(
                    state.matrix[0][0], state.matrix[0][1], state.matrix[0][3],
                    state.matrix[1][0], state.matrix[1][1], state.matrix[1][3],
                    state.matrix[3][0], state.matrix[3][1], state.matrix[3][3]
                )),
                ascii_grid
            );
        }
    }
    return fb;
}

bool SourceNode::can_seed_full_frame(const RenderGraphContext& ctx) const {
    if (!m_cache_static || m_is_3d) {
        return false;
    }

    if (m_node.shape.type != ShapeType::Image) {
        return false;
    }

    const auto& img = m_node.shape.image;
    const auto& tr = m_node.world_transform;
    const f32 opacity = m_opacity_override.value_or(tr.opacity);

    if (ctx.clip_rect) {
        const bool clip_is_full = ctx.clip_rect->x0 <= 0 && ctx.clip_rect->y0 <= 0 &&
                                  ctx.clip_rect->x1 >= ctx.width && ctx.clip_rect->y1 >= ctx.height;
        if (!clip_is_full) {
            return false;
        }
    }

    const bool full_size = std::abs(img.size.x - static_cast<f32>(ctx.width)) < kSeedFrameEpsilon &&
                           std::abs(img.size.y - static_cast<f32>(ctx.height)) < kSeedFrameEpsilon;
    const bool opaque = img.opacity >= 0.999f && opacity >= 0.999f;
    if (!full_size || !opaque) {
        return false;
    }

    const Mat4 ssaa_scale = math::scale(Vec3(ctx.ssaa_factor, ctx.ssaa_factor, 1.0f));
    const Mat4 canvas_center = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));
    const Mat4 local_matrix = m_matrix_override.value_or(tr.to_mat4());
    const Mat4 effective_matrix = (m_is_3d || m_centered)
        ? (canvas_center * ssaa_scale * local_matrix)
        : (ssaa_scale * local_matrix);

    return covers_full_frame_as_rectangle(effective_matrix, static_cast<f32>(ctx.width), static_cast<f32>(ctx.height));
}

} // namespace chronon3d::graph
