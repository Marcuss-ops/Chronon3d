// ═══════════════════════════════════════════════════════════════════════════
// source_node_native_rect.cpp — GPU solid-rect fast path for SourceNode.
//
// Split out of source_node.cpp: fill an un-stroked, un-rounded, solid,
// axis-aligned rectangle directly into a native surface instead of
// rasterizing on CPU.  Returns false (leaving the framebuffer on the legacy
// draw_node path) when the backend has no surface support, the placement is
// not a pure axis-aligned affine transform, or the shape does not satisfy
// the narrow predicate — the software path is therefore unchanged.
//
// The software processor computes `node.color.to_linear()` with
// alpha *= state.opacity and source-over blends it onto the (transparent)
// framebuffer, which stores PREMULTIPLIED RGBA.  The GPU path mirrors that
// exactly: it premultiplies the same straight color and stores it into the
// surface.  Coverage matches the CPU rasterizer's pixel-center hit test for
// a solid rect: pixel (x, y) is filled iff its center maps inside [0,size).
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

namespace chronon3d::graph::detail {

bool try_native_rect_fill(RenderGraphContext& ctx, Framebuffer& fb,
                          const RenderNode& node, const RenderState& state) {
    if (node.shape.type() != ShapeType::Rect) return false;
    if (node.shape.rect().stroke.enabled) return false;
    // Render-plan color layers carry their canonical color in RenderNode::color;
    // the builder's Fill metadata may remain at its default while the shape
    // is still an ordinary solid rectangle.  Stroke/radius/mask checks below
    // keep this native route limited to the equivalent solid operation.
    if (node.corner_radius > 0.0f) return false;
    if (native_promotion::has_active_mask(state)) return false;
    if (!ctx.services.backend || !ctx.services.surface_registry) return false;
    // Axis-aligned affine placement only.  A 2D rotation/shear shows up in
    // matrix[0][1]/matrix[1][0]; a camera projection in the w-row (both are
    // owned by the canonical predicate).  A projected layer therefore falls
    // back to draw_node while a plain translation/scale (including a
    // defer-projected local source) does not.
    if (!native_promotion::is_axis_aligned_affine(state)) return false;

    bool alpha_zero = false;
    const Color fill_color =
        native_promotion::premultiply(node.color, state.opacity, &alpha_zero);
    if (alpha_zero) return false;

    // Exact pixel coverage.  compute_world_bbox() pads by kBBoxSafetyPadding
    // for the general rasterizer; the GPU fill must instead cover exactly the
    // pixels whose centers fall inside the rect, mirroring the CPU hit test.
    const auto& size = node.shape.rect().size;
    const Vec4 c00 = state.matrix * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    const Vec4 c10 = state.matrix * Vec4(size.x, 0.0f, 0.0f, 1.0f);
    const Vec4 c01 = state.matrix * Vec4(0.0f, size.y, 0.0f, 1.0f);
    const Vec4 c11 = state.matrix * Vec4(size.x, size.y, 0.0f, 1.0f);
    const f32 min_x = std::min({c00.x, c10.x, c01.x, c11.x});
    const f32 min_y = std::min({c00.y, c10.y, c01.y, c11.y});
    const f32 max_x = std::max({c00.x, c10.x, c01.x, c11.x});
    const f32 max_y = std::max({c00.y, c10.y, c01.y, c11.y});
    i32 x0 = std::max(0, static_cast<i32>(std::ceil(min_x - 0.5f)));
    i32 y0 = std::max(0, static_cast<i32>(std::ceil(min_y - 0.5f)));
    i32 x1 = std::min(fb.width(), static_cast<i32>(std::ceil(max_x - 0.5f)));
    i32 y1 = std::min(fb.height(), static_cast<i32>(std::ceil(max_y - 0.5f)));
    (void)native_promotion::intersect_clip(
        ctx.node_exec.clip_rect, fb.width(), fb.height(), x0, y0, x1, y1);

    const bool covers_full_destination =
        x0 <= 0 && y0 <= 0 && x1 >= fb.width() && y1 >= fb.height();
    // A full-frame solid rect writes every destination pixel.  It therefore
    // needs no CPU seed: allocate an empty native surface and let the GPU
    // fill kernel produce the result.  Partial rects retain the legacy
    // promotion because their untouched pixels still carry source state.
    if (covers_full_destination) {
        if (!ensure_empty_native_surface(ctx, fb)) return false;
    } else if (!ensure_native_surface(ctx, fb, "SourceNode.rect.partial")) {
        return false;
    }

    if (x0 >= x1 || y0 >= y1) {
        return true;  // fully off-canvas: surface attached, nothing to fill
    }

    const auto result = ctx.services.backend->fill_rect_surface(
        fb.surface_handle(), x0, y0, x1, y1, fill_color);
    if (!result.ok()) {
        release_native_surface(ctx, fb);
        return false;
    }
    return true;
}

} // namespace chronon3d::graph::detail
