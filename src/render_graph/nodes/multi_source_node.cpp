#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/render_graph/nodes/detail/bbox_projection.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <limits>

namespace chronon3d::graph {

MultiSourceNode::MultiSourceNode(
    std::string name, std::vector<MultiSourceItem> items, const cache::NodeCacheKey& key,
    bool centered, bool is_3d, bool cache_static
) : m_name(std::move(name)), m_items(std::move(items)), m_key(key),
    m_centered(centered), m_is_3d(is_3d), m_cache_static(cache_static) {}

std::optional<raster::BBox> MultiSourceNode::predicted_bbox(
    const RenderGraphContext& ctx,
    std::span<const std::optional<raster::BBox>>
) const {
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
        if (m_is_3d || m_centered) {
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

        if (!ctx.diagnostics_enabled) {
            bbox.clip_to(ctx.width, ctx.height);
        }
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

CacheFramePolicy MultiSourceNode::cache_frame_policy() const {
    return CacheFramePolicy::FrameInvariant;
}

RenderNodeCachePolicy MultiSourceNode::cache_policy() const {
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

cache::NodeCacheKey MultiSourceNode::cache_key(const RenderGraphContext& ctx) const {
    auto key = m_key;
    key.params_hash = hash_combine(key.params_hash, static_cast<u64>(ctx.modular_coordinates));
    return key;
}

std::shared_ptr<Framebuffer> MultiSourceNode::execute(
    RenderGraphContext& ctx,
    std::span<const std::shared_ptr<Framebuffer>>,
    std::span<const std::optional<raster::BBox>>
) {
    CHRONON_ZONE_C("multi_source_render", trace_category::kRasterize);
    auto fb = ctx.acquire_framebuffer(ctx.width, ctx.height, /*clear=*/true);

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

} // namespace chronon3d::graph
