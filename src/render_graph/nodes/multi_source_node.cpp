#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/render_graph/nodes/detail/bbox_projection.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <limits>

namespace chronon3d::graph {

MultiSourceNode::MultiSourceNode(
    std::string name, std::vector<MultiSourceItem> items, const cache::NodeCacheKey& key,
    bool centered, bool uses_2_5d_projection, bool cache_static
) : m_name(std::move(name)), m_items(std::move(items)), m_key(key),
    m_centered(centered), m_uses_2_5d_projection(uses_2_5d_projection), m_cache_static(cache_static) {}

std::optional<raster::BBox> MultiSourceNode::predicted_bbox(
    const RenderGraphContext& ctx,
    std::span<const std::optional<raster::BBox>>
) const {
    const Mat4 ssaa_scale = glm::scale(Mat4(1.0f), Vec3(ctx.options.ssaa_factor, ctx.options.ssaa_factor, 1.0f));
    const Mat4 canvas_center = glm::translate(Mat4(1.0f), Vec3(ctx.frame.width * 0.5f, ctx.frame.height * 0.5f, 0.0f));

    i32 x0 = std::numeric_limits<i32>::max();
    i32 y0 = std::numeric_limits<i32>::max();
    i32 x1 = std::numeric_limits<i32>::min();
    i32 y1 = std::numeric_limits<i32>::min();
    bool has_any = false;

    for (const auto& item : m_items) {
        if (!item.node) continue;
        Mat4 matrix;
        if (m_uses_2_5d_projection || m_centered) {
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
        if (ctx.camera.has_camera_2_5d &&
            (item.node->shape.type == ShapeType::FakeBox3D || item.node->shape.type == ShapeType::GridPlane)) {
            if (auto proj_bbox = detail::projected_native_3d_bbox(ctx, *item.node, item.matrix, spread)) {
                bbox = *proj_bbox;
            } else {
                bbox = raster::BBox{0, 0, ctx.frame.width, ctx.frame.height};
            }
        } else {
            bbox = renderer::compute_world_bbox(item.node->shape, matrix, spread);
        }

        if (!ctx.options.diagnostics_enabled) {
            bbox.clip_to(ctx.frame.width, ctx.frame.height);
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
    // Hybrid policy for animated multi-source: the cache key is frame-
    // independent — cache_key() always sets frame = Frame{0} and embeds
    // the items' matrices in the params_hash.  PerFrame lifetime prevents
    // unbounded framebuffer accumulation (the node cache's LRU evicts
    // entries between frames, keeping peak memory ~1× a single frame).
    // Consecutive frames with the same effective transform benefit from
    // cross-frame cache hits while memory stays bounded.
    return RenderNodeCachePolicy{
        .cacheable = true,
        .frame_dependent = false,
        .frame_invariant = false,
        .disk_cacheable = false,
        .lifetime = CacheLifetime::PerFrame,
        .invalidation = CacheInvalidation::WhenParamsChange,
        .debug_reason = "multi_source_hybrid"
    };
}

cache::NodeCacheKey MultiSourceNode::cache_key(const RenderGraphContext& ctx) const {
    auto key = m_key;
    // Strip the frame number — the items' full world matrices (embedded in
    // the loop below) already capture frame-to-frame animation changes, so
    // the frame dimension adds no useful discrimination.  This lets the LRU
    // cache reuse a rendered result across frames that share the same
    // effective transform (e.g. settled tail of an animation).
    key.frame = Frame{0};
    key.params_hash = hash_combine(key.params_hash, static_cast<u64>(ctx.options.modular_coordinates));

    // Hash every item's full world matrix and opacity so the cache key
    // changes when the layer-level animation (e.g. tracking_breathing)
    // produces a different transform.
    for (const auto& item : m_items) {
        key.params_hash = hash_combine(key.params_hash, hash_value(item.matrix));
        key.params_hash = hash_combine(key.params_hash, hash_value(item.opacity));
    }

    return key;
}

OwnedFB MultiSourceNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef>,
    std::span<const std::optional<raster::BBox>>
) {
    CHRONON_ZONE_C("multi_source_render", trace_category::kRasterize);
    auto fb = ctx.acquire_owned_fb(ctx.frame.width, ctx.frame.height, /*clear=*/true);

    if (ctx.resources.backend) {
        const Mat4 ssaa_scale = glm::scale(Mat4(1.0f), Vec3(ctx.options.ssaa_factor, ctx.options.ssaa_factor, 1.0f));
        const Mat4 canvas_center = glm::translate(Mat4(1.0f), Vec3(ctx.frame.width * 0.5f, ctx.frame.height * 0.5f, 0.0f));

        for (const auto& item : m_items) {
            if (!item.node) continue;
            RenderState state;
            state.frame_number = static_cast<int>(ctx.frame.frame);
            state.ssaa_factor = ctx.options.ssaa_factor;
            if (m_uses_2_5d_projection) {
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
            state.clip_rect = ctx.tile.clip_rect;
            state.diagnostics_enabled = ctx.options.diagnostics_enabled;

            if (ctx.camera.has_camera_2_5d) {
                state.projection  = ctx.camera.projection_ctx;
            }

            ctx.resources.backend->draw_node(*fb, *item.node, state, ctx.camera.camera, ctx.frame.width, ctx.frame.height);
        }

        fb->set_opaque(false);
    }
    return fb;
}

} // namespace chronon3d::graph
