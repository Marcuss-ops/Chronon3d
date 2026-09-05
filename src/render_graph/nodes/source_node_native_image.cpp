// ═══════════════════════════════════════════════════════════════════════════
// source_node_native_image.cpp — native (GPU) image fast path for SourceNode.
//
// Split out of source_node.cpp: strict native ImageShape promotion.  Decode
// remains owned by ImageCache; only the immutable decoded pixels are promoted
// once into GpuAssetCache.  Crop, rounded corners, masks and projective
// transforms intentionally stay on the reference path until equivalent
// native kernels exist.
// ═══════════════════════════════════════════════════════════════════════════

#include "source_node_native.hpp"

#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/cache/node_cache_identity_builder.hpp>
#include <chronon3d/render_graph/nodes/detail/bbox_projection.hpp>
#include <chronon3d/render_graph/nodes/detail/projection_helpers.hpp>
#include "detail/native_promotion.hpp"
#include "../builder/evaluated_layer_placement.hpp"
#include "detail/preflight_bbox.hpp"
#include "native_surface.hpp"
#include "detail/producer_surface_bounds.hpp"
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/media/media_placement.hpp>
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace chronon3d::graph {

namespace {

[[nodiscard]] bool try_native_image_impl(
    RenderGraphContext& ctx, Framebuffer& fb,
    const RenderNode& node, const RenderState& state) {
    const auto& image = node.shape.image();
    if (native_surface_promotion_diag_enabled()) {
        spdlog::info("[native_image_diag] path='{}' matrix3=[{:.1f},{:.1f},{:.1f},{:.1f}] anchor=[{:.1f},{:.1f}] op={:.2f} state_op={:.2f}",
                     image.path, state.matrix[3][0], state.matrix[3][1], state.matrix[0][0], state.matrix[1][1],
                     node.world_transform.anchor.x, node.world_transform.anchor.y,
                     image.opacity, state.opacity);
    }
    if (!ctx.services.image_cache || !ctx.services.gpu_asset_cache ||
        !ctx.services.backend || !ctx.services.surface_registry ||
        image.path.empty() || image.radius > 0.0f ||
        native_promotion::has_active_mask(state) ||
        !native_promotion::is_axis_aligned_affine(state)) {
        return false;
    }

    auto cached = ctx.services.image_cache->find(
        image.path, image.decode_options);
    if (!cached) {
        cached = ctx.services.image_cache->get_or_load(
            image.path, image.decode_options);
    }
    if (!cached || !cached->fb_img || !cached->valid() || cached->gpu_rgba.empty()) return false;

    const auto& key = cached->gpu_key;
    const runtime::SurfaceDesc desc{
        key.width,
        key.height,
        key.format,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::JobPersistent,
        cached->gpu_rgba.size() * sizeof(float)};
    const auto acquired = ctx.services.gpu_asset_cache->acquire(key, desc, cached->gpu_rgba);
    if (!acquired.ok()) return false;
    if (ctx.policy.diagnostics_enabled && acquired.cache_hit) {
        spdlog::debug("[gpu-asset-cache] static image '{}' reused resident handle={}",
                      image.path, static_cast<std::uint64_t>(acquired.handle));
    }

    const auto& source = *cached->fb_img;

    if (!ensure_empty_native_surface(ctx, fb)) return false;
    const Vec2 original_source_size{
        static_cast<float>(source.width()), static_cast<float>(source.height())};
    Vec2 effective_source_size = original_source_size;
    Vec2 effective_source_origin{0.0f, 0.0f};
    if (image.crop.enabled) {
        effective_source_size = image.crop.size * original_source_size;
        effective_source_origin = image.crop.origin * original_source_size;
    }
    const auto placement = compute_media_placement(
        effective_source_size, image.size, image.fit, image.focal_point);
    const Vec2 source_origin = effective_source_origin + placement.src_rect.origin;
    const Vec2 source_size = placement.src_rect.size;
    if (source_size.x <= 0.0f || source_size.y <= 0.0f ||
        placement.dst_rect.size.x <= 0.0f || placement.dst_rect.size.y <= 0.0f) {
        release_native_surface(ctx, fb);
        return false;
    }

    // SourceNode's state matrix maps ImageShape local units. The placement
    // maps the selected source rectangle into the authored image box first.
    const float sx = state.matrix[0][0] * placement.dst_rect.size.x / source_size.x;
    const float sy = state.matrix[1][1] * placement.dst_rect.size.y / source_size.y;
    // ImageShape stores its local origin at the top-left of the authored
    // box, while RenderNode's anchor is the box centre.  The native affine
    // path samples from destination pixels directly and therefore must apply
    // the same anchor subtraction as the canonical image processor.  Without
    // it, a tight ROI uses the world-space centre as its left edge and the
    // image is sampled mostly outside the source surface.
    const float tx = state.matrix[3][0] - node.world_transform.anchor.x +
                     state.matrix[0][0] * placement.dst_rect.origin.x;
    const float ty = state.matrix[3][1] - node.world_transform.anchor.y +
                     state.matrix[1][1] * placement.dst_rect.origin.y;
    if (std::abs(sx) < 1e-6f || std::abs(sy) < 1e-6f) {
        release_native_surface(ctx, fb);
        return false;
    }

    runtime::SurfaceAffineTransform transform{};
    transform.source_x[0] = 1.0f / sx;
    transform.source_x[2] = source_origin.x - tx / sx;
    transform.source_y[1] = 1.0f / sy;
    transform.source_y[2] = source_origin.y - ty / sy;
    transform.max_x = source_origin.x + source_size.x;
    transform.max_y = source_origin.y + source_size.y;
    transform.opacity = image.opacity * state.opacity;
    transform.bilinear = 1u;
    transform.destination_origin_x = fb.origin_x();
    transform.destination_origin_y = fb.origin_y();
    const float dst_x0 = tx;
    const float dst_y0 = ty;
    const float dst_x1 = tx + sx * source_size.x;
    const float dst_y1 = ty + sy * source_size.y;

    const auto img_x0 = static_cast<std::int32_t>(std::floor(std::min(dst_x0, dst_x1)));
    const auto img_y0 = static_cast<std::int32_t>(std::floor(std::min(dst_y0, dst_y1)));
    const auto img_x1 = static_cast<std::int32_t>(std::ceil(std::max(dst_x0, dst_x1)));
    const auto img_y1 = static_cast<std::int32_t>(std::ceil(std::max(dst_y0, dst_y1)));
    if (native_surface_promotion_diag_enabled()) {
        spdlog::info("[native_image_bounds] dst=[{:.1f},{:.1f} -> {:.1f},{:.1f}] img=[{},{} -> {},{}] sx={:.3f} sy={:.3f} tx={:.1f} ty={:.1f}",
                     dst_x0, dst_y0, dst_x1, dst_y1, img_x0, img_y0, img_x1, img_y1, sx, sy, tx, ty);
    }

    std::int32_t effective_clip_x0 = img_x0;
    std::int32_t effective_clip_y0 = img_y0;
    std::int32_t effective_clip_x1 = img_x1;
    std::int32_t effective_clip_y1 = img_y1;

    if (state.clip_rect) {
        effective_clip_x0 = std::max(effective_clip_x0, state.clip_rect->x0);
        effective_clip_y0 = std::max(effective_clip_y0, state.clip_rect->y0);
        effective_clip_x1 = std::min(effective_clip_x1, state.clip_rect->x1);
        effective_clip_y1 = std::min(effective_clip_y1, state.clip_rect->y1);
    }

    if (effective_clip_x1 <= effective_clip_x0 || effective_clip_y1 <= effective_clip_y0) {
        release_native_surface(ctx, fb);
        return true;
    }

    transform.clip_enabled = 1u;
    transform.clip_rect[0] = effective_clip_x0;
    transform.clip_rect[1] = effective_clip_y0;
    transform.clip_rect[2] = effective_clip_x1;
    transform.clip_rect[3] = effective_clip_y1;

    const auto result = ctx.services.backend->transform_surface_affine(
        fb.surface_handle(), acquired.handle, transform);
    if (!result.ok()) {
        release_native_surface(ctx, fb);
        return false;
    }
    return true;
}

} // namespace

bool detail::try_native_image(
    RenderGraphContext& ctx, Framebuffer& fb,
    const ::chronon3d::RenderNode& node, const RenderState& state) {
    return try_native_image_impl(ctx, fb, node, state);
}

} // namespace chronon3d::graph
