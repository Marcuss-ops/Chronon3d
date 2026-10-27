#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/cache/node_cache_identity_builder.hpp>
#include <chronon3d/core/parallel_tracked.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include "multi_source_node_execute_detail.hpp"
#include <chronon3d/render_graph/nodes/detail/bbox_projection.hpp>
#include <chronon3d/render_graph/nodes/detail/projection_helpers.hpp>
#include "../builder/evaluated_layer_placement.hpp"
#include "detail/preflight_bbox.hpp"
#include "native_surface.hpp"
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#ifdef CHRONON3D_ENABLE_TEXT
#include <chronon3d/text/text_run_geometry.hpp>
#include <chronon3d/text/text_run.hpp>
#include "text_run/text_run_execution.hpp"
#include "text_run/text_run_transform.hpp"
#endif
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <chronon3d/core/profiling/profiling.hpp>
#include <limits>

namespace chronon3d::graph {

NodeExecResult MultiSourceNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef>,
    std::span<const std::optional<raster::BBox>>
) {
    CHRONON_TRACE_SCOPE("chronon.node", "multi_source_render");

    // Fase A4 — null backend is a hard error (matches TextRunNode contract).
    if (!ctx.backend) {
        spdlog::error(
            "[multi-source] node='{}' cannot render: backend is null; "
            "aborting frame.", m_name);
        return NodeExecResult{NodeExecutionError{
            RenderBackendErrorCode::InvalidInput,
            m_name,
            "backend is null"
        }};
    }

    auto fb = ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height, /*clear=*/true);
    // A MultiSourceNode shares one framebuffer across its regular and text-run
    // items. In strict GPU mode this framebuffer must be materialized before
    // the first draw: otherwise native path/shape dispatch rejects the empty
    // handle, the legacy path leaves a CPU-only result, and the following
    // CompositeNode cannot preserve GPU residency.
    if (ctx.backend->supports_native_surfaces() && ctx.surface_registry &&
        !ensure_empty_native_surface(ctx, *fb)) {
        return NodeExecResult{NodeExecutionError{
            RenderBackendErrorCode::ExecutionFailure,
            m_name,
            "failed to materialize native multi-source surface"}};
    }

    // ── text_run items are dispatched to `RenderBackend::draw_text_run`
    // instead of the generic `RenderBackend::draw_node` because the
    // former routes through the dedicated text-run processor with the
    // per-glyph transform stack.  The text is rasterized directly onto
    // the SHARED `*fb` so it composites SRC_OVER any earlier non-text
    // items in the same layer (vector order).
    //
    // Fase A4 — error propagation unified with TextRunNode:
    //   - draw_text_run failure → NodeExecutionError (immediate return)
    //   - unsupported capability → NodeExecutionError
    //   - null shape → skip item (not an error — empty data)
    //
    // Regular items use draw_node() which returns void — tracked for
    // Phase C (requires coordinated API/ABI change).

    //
    // ADR-035 — SVG isolated group stage.
    //
    // Items tagged with `group` draw at FULL paint alpha into an own
    // intermediate framebuffer (cleared per group). When the group ends
    // (different group id, a non-grouped item, or the layer end) the
    // surface composites once onto the shared layer framebuffer with the
    // group alpha scaled by the item's placement alpha:
    //
    //   out.rgb   = src.rgb * g + dst.rgb * (1 - src.a * g)
    //   out.a     = src.a * g + dst.a   * (1 - src.a * g)
    //
    // (both surfaces premultiplied; g = group_opacity * item alpha).
    // Overlapping children therefore never darken each other — the group
    // fades as one unit, matching SVG/AE isolated-group compositing.
    //
    // Non-grouped items keep the historical per-item draw-time alpha fold,
    // so text and legacy shape layers are untouched by this branch.
    //
    bool has_group_items = false;
    for (const auto& item : m_items)
        if (item.group || !item.groups.empty()) { has_group_items = true; break; }
    const bool native_group_surfaces = has_group_items &&
        ctx.backend->supports_native_surfaces() && ctx.surface_registry;
    if (has_group_items && ctx.policy.is_gpu_native_required() &&
        !native_group_surfaces) {
        return NodeExecResult{NodeExecutionError{
            RenderBackendErrorCode::UnsupportedCapability, m_name,
            "svg_group_native_unsupported: native surface registry unavailable"}};
    }

    return detail::execute_multi_source_items(
        ctx, m_name, m_items, std::move(fb), native_group_surfaces);
}

} // namespace chronon3d::graph
