#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <chronon3d/render_graph/nodes/detail/bbox_projection.hpp>
#include <span>

namespace chronon3d::graph {

class SourceNode final : public RenderGraphNode {
public:
    SourceNode(std::string name, const ::chronon3d::RenderNode& node, const cache::NodeCacheKey& key,
               bool centered = false, bool is_3d = false, std::optional<Mat4> matrix_override = std::nullopt,
               std::optional<f32> opacity_override = std::nullopt, bool cache_static = false)
        : m_name(std::move(name)), m_node(node), m_key(key), m_centered(centered), m_is_3d(is_3d),
          m_matrix_override(matrix_override), m_opacity_override(opacity_override), m_cache_static(cache_static) {}

    bool cacheable() const override { return m_cache_static; }

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Source; }
    std::string name() const override { return m_name; }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> = {}
    ) const override {
        const Mat4 ssaa_scale = math::scale(Vec3(ctx.ssaa_factor, ctx.ssaa_factor, 1.0f));
        const Mat4 canvas_center = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));

        Mat4 matrix;
        if (m_centered) {
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

    [[nodiscard]] CacheFramePolicy cache_frame_policy() const override {
        return CacheFramePolicy::FrameInvariant;
    }

    [[nodiscard]] RenderNodeCachePolicy cache_policy() const override {
        if (m_cache_static) {
            return static_memory_cache("source_static");
        }
        return RenderNodeCachePolicy{
            .cacheable = true,
            .frame_dependent = true,
            .frame_invariant = false,
            .disk_cacheable = false,
            .lifetime = CacheLifetime::PerFrame,
            .invalidation = CacheInvalidation::WhenParamsChange,
            .debug_reason = "source_animated"
        };
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override { 
        auto key = m_key;
        key.params_hash = hash_combine(key.params_hash, static_cast<u64>(ctx.modular_coordinates));
        if (m_matrix_override) {
            key.params_hash = hash_combine(key.params_hash, hash_bytes(&(*m_matrix_override)[0][0], sizeof(Mat4)));
        }
        if (m_opacity_override) {
            key.params_hash = hash_combine(key.params_hash, hash_bytes(&(*m_opacity_override), sizeof(f32)));
        }
        return key; 
    }

    std::shared_ptr<Framebuffer> execute(
        RenderGraphContext& ctx,
        std::span<const std::shared_ptr<Framebuffer>>,
        std::span<const std::optional<raster::BBox>>
    ) override {
        CHRONON_ZONE_C("source_render", trace_category::kRasterize);
        bool clear = true;
        if (m_node.shape.type == ShapeType::Image && !m_matrix_override.has_value() && !m_centered) {
            const auto& t = m_node.world_transform;
            const auto& img = m_node.shape.image;
            if (t.position == Vec3(0.0f) && t.rotation == Quat(1.0f, 0.0f, 0.0f, 0.0f) && t.scale == Vec3(1.0f) &&
                t.opacity >= 0.999f && img.opacity >= 0.999f &&
                std::abs(img.size.x - static_cast<f32>(ctx.width)) < 1e-3f &&
                std::abs(img.size.y - static_cast<f32>(ctx.height)) < 1e-3f) {
                clear = false;
            }
        }

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
            
            state.opacity = m_opacity_override.value_or(m_node.world_transform.opacity);
            state.world_matrix = m_matrix_override.value_or(m_node.world_transform.to_mat4());
            state.clip_rect = ctx.clip_rect;
            
            if (ctx.has_camera_2_5d) {
                state.projection  = ctx.projection_ctx;
            }

            ctx.backend->draw_node(*fb, m_node, state, ctx.camera, ctx.width, ctx.height);
            fb->set_opaque(can_seed_full_frame(ctx));

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

                spdlog::info(
                    "[source-debug] node='{}' shape={} nonzero_pixels={} opacity={:.3f} matrix_tx={:.3f} matrix_ty={:.3f} det2d={:.6f}",
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
                    ))
                );
            }
        }
        return fb;
    }

    [[nodiscard]] bool can_seed_full_frame(const RenderGraphContext& ctx) const override {
        if (!m_cache_static || m_is_3d) {
            return false;
        }

        if (m_matrix_override && *m_matrix_override != Mat4(1.0f)) {
            return false;
        }

        if (m_node.shape.type != ShapeType::Image) {
            return false;
        }

        const auto& img = m_node.shape.image;
        const auto& tr = m_node.world_transform;
        constexpr f32 eps = 1e-3f;

        if (ctx.clip_rect) {
            const bool clip_is_full = ctx.clip_rect->x0 <= 0 && ctx.clip_rect->y0 <= 0 &&
                                      ctx.clip_rect->x1 >= ctx.width && ctx.clip_rect->y1 >= ctx.height;
            if (!clip_is_full) {
                return false;
            }
        }

        const bool full_size = std::abs(img.size.x - static_cast<f32>(ctx.width)) < eps &&
                               std::abs(img.size.y - static_cast<f32>(ctx.height)) < eps;
        const bool opaque = img.opacity >= 0.999f && tr.opacity >= 0.999f;
        const bool identity = tr.position == Vec3(0.0f) &&
                              tr.rotation == Quat(1.0f, 0.0f, 0.0f, 0.0f) &&
                              tr.scale == Vec3(1.0f);

        return full_size && opaque && identity;
    }

private:
    std::string m_name;
    ::chronon3d::RenderNode m_node;
    cache::NodeCacheKey m_key;
    bool m_centered{false};
    bool m_is_3d{false};
    std::optional<Mat4> m_matrix_override;
    std::optional<f32> m_opacity_override;
    bool m_cache_static{false};
};

struct MultiSourceItem {
    const ::chronon3d::RenderNode* node{nullptr};
    Mat4 matrix;
    f32 opacity;
};

class MultiSourceNode final : public RenderGraphNode {
public:
    MultiSourceNode(std::string name, std::vector<MultiSourceItem> items, const cache::NodeCacheKey& key,
                    bool centered = false, bool is_3d = false, bool cache_static = false)
        : m_name(std::move(name)), m_items(std::move(items)), m_key(key), m_centered(centered), m_is_3d(is_3d),
          m_cache_static(cache_static) {}

    bool cacheable() const override { return m_cache_static; }

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Source; }
    std::string name() const override { return m_name; }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> = {}
    ) const override {
        const Mat4 ssaa_scale = math::scale(Vec3(ctx.ssaa_factor, ctx.ssaa_factor, 1.0f));
        const Mat4 canvas_center = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));

        i32 x0 = std::numeric_limits<i32>::max();
        i32 y0 = std::numeric_limits<i32>::max();
        i32 x1 = std::numeric_limits<i32>::min();
        i32 y1 = std::numeric_limits<i32>::min();
        bool has_any = false;

        for (const auto& item : m_items) {
            if (!item.node) continue;
            Mat4 matrix;
            if (m_centered) {
                matrix = canvas_center * ssaa_scale * item.matrix;
            } else {
                matrix = ssaa_scale * item.matrix;
            }

            f32 spread = 0.0f;
            if (item.node->shadow.enabled) {
                spread = std::max(spread, item.node->shadow.radius + std::max(std::abs(item.node->shadow.offset.x), std::abs(item.node->shadow.offset.y)));
            }
            if (item.node->glow.enabled) {
                spread = std::max(spread, item.node->glow.radius);
            }
            spread += 8.0f;

            raster::BBox bbox;
            if (ctx.has_camera_2_5d &&
                (item.node->shape.type == ShapeType::FakeBox3D || item.node->shape.type == ShapeType::GridPlane)) {
                if (auto proj_bbox = detail::projected_native_3d_bbox(ctx, *item.node, item.matrix, spread)) {
                    bbox = *proj_bbox;
                } else {
                    bbox = raster::BBox{0, 0, ctx.width, ctx.height};
                }
            } else {
                bbox = renderer::compute_world_bbox(item.node->shape, matrix, spread);
            }

            bbox.clip_to(ctx.width, ctx.height);
            if (!bbox.is_empty()) {
                x0 = std::min(x0, bbox.x0);
                y0 = std::min(y0, bbox.y0);
                x1 = std::max(x1, bbox.x1);
                y1 = std::max(y1, bbox.y1);
                has_any = true;
            }
        }

        if (!has_any) {
            return raster::BBox{0, 0, 0, 0};
        }
        return raster::BBox{x0, y0, x1, y1};
    }

    [[nodiscard]] CacheFramePolicy cache_frame_policy() const override {
        return CacheFramePolicy::FrameInvariant;
    }

    [[nodiscard]] RenderNodeCachePolicy cache_policy() const override {
        if (m_cache_static) {
            return static_memory_cache("source_static");
        }
        return RenderNodeCachePolicy{
            .cacheable = true,
            .frame_dependent = true,
            .frame_invariant = false,
            .disk_cacheable = false,
            .lifetime = CacheLifetime::PerFrame,
            .invalidation = CacheInvalidation::WhenParamsChange,
            .debug_reason = "source_animated"
        };
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override { 
        auto key = m_key;
        key.params_hash = hash_combine(key.params_hash, static_cast<u64>(ctx.modular_coordinates));
        return key; 
    }

    std::shared_ptr<Framebuffer> execute(
        RenderGraphContext& ctx,
        std::span<const std::shared_ptr<Framebuffer>>,
        std::span<const std::optional<raster::BBox>>
    ) override {
        CHRONON_ZONE_C("multi_source_render", trace_category::kRasterize);
        bool clear = true;
        auto fb = ctx.acquire_framebuffer(ctx.width, ctx.height, clear);
        
        if (ctx.backend) {
            const Mat4 ssaa_scale = math::scale(Vec3(ctx.ssaa_factor, ctx.ssaa_factor, 1.0f));
            const Mat4 canvas_center = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));

            for (const auto& item : m_items) {
                if (!item.node) continue;
                RenderState state;
                if (m_is_3d) {
                    state.matrix = canvas_center * ssaa_scale * item.matrix;
                } else {
                    if (m_centered) {
                        state.matrix = canvas_center * ssaa_scale * item.matrix;
                    } else {
                        state.matrix = ssaa_scale * item.matrix;
                    }
                }
                
                state.opacity = item.opacity;
                state.world_matrix = item.matrix;
                state.clip_rect = ctx.clip_rect;
                
                if (ctx.has_camera_2_5d) {
                    state.projection  = ctx.projection_ctx;
                }

                ctx.backend->draw_node(*fb, *item.node, state, ctx.camera, ctx.width, ctx.height);
            }
            
            fb->set_opaque(false);
        }
        return fb;
    }

    [[nodiscard]] bool can_seed_full_frame(const RenderGraphContext& ctx) const override {
        return false;
    }

private:
    std::string m_name;
    std::vector<MultiSourceItem> m_items;
    cache::NodeCacheKey m_key;
    bool m_centered{false};
    bool m_is_3d{false};
    bool m_cache_static{false};
};

} // namespace chronon3d::graph
