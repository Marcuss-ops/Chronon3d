// Native effect stack implementation unit.
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/effects/curves.hpp>
#include <chronon3d/effects/effect_param_animation.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/render_graph/effect_gpu_capability.hpp>
#include <chronon3d/runtime/frame/frame_execution.hpp>
#include <chronon3d/runtime/job/job_session.hpp>
#include "native_surface.hpp"
#include "effect_stack_node_native_api.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <initializer_list>
#include <string_view>
#include <vector>

#include "effect_stack_node_native_bulk_detail.hpp"

// Radial blur and camera-ring (Petzval field curvature) dispatch unit.
namespace chronon3d::graph {
namespace native_effects {

// ── RadialBlur ─────────────────────────────────────────────────────────────
bool try_native_full_frame_radial_blur(RenderGraphContext& ctx,
                                       const EffectStack& effect_stack,
                                       Framebuffer& result,
                                       const std::optional<raster::BBox>& clip,
                                       runtime::RenderSurfaceHandle borrowed_source) {
    using namespace bulk_detail;
    if (!clip_covers_result(clip, result) || !ctx.backend || !ctx.surface_registry ||
        effect_stack.size() != 1) {
        return false;
    }
    const auto& instance = effect_stack[0];
    if (!instance.enabled || instance.effect_type != effects::EffectType::RadialBlur) return false;
    const auto* p = std::get_if<RadialBlurParams>(&instance.params);
    if (!p) return false;
    if (!std::isfinite(p->center.x) || !std::isfinite(p->center.y) ||
        !std::isfinite(p->amount)) {
        return false;
    }
    // CPU identity fast paths: amount <= 0 or samples <= 1 forward untouched.
    if (!(p->amount > 0.0f) || p->render_samples <= 1)
        return bulk_detail::bulk_true_forward(ctx, result, borrowed_source);
    if (p->amount > 8.0f) return false;
    const int taps = p->render_samples;
    if (taps < 2 || taps > 64) return false;
    runtime::RenderSurfaceHandle source = runtime::kInvalidRenderSurfaceHandle;
    const auto destination = bulk_alloc_destination(ctx, result, borrowed_source, source);
    if (destination == runtime::kInvalidRenderSurfaceHandle) {
        // Identity-forward case already returned; reaching here with no
        // surface means failure, except the forward path handled above.
        return false;
    }
    const float cx_px =
        p->center.x * static_cast<float>(std::max(0, result.width() - 1)) + 0.5f;
    const float cy_px =
        p->center.y * static_cast<float>(std::max(0, result.height() - 1)) + 0.5f;
    const auto dispatched = ctx.backend_surface_api()->radial_blur_surface(
        destination, source, cx_px, cy_px, p->amount, taps);
    if (!dispatched.ok()) {
        bulk_release_surface(ctx, destination);
        return false;
    }
    result.set_surface_handle(destination);
    result.mark_gpu_authoritative();
    return true;
}

// ── CameraRing / Petzval field-curvature defocus ───────────────────────────
bool try_native_full_frame_camera_ring(RenderGraphContext& ctx,
                                       const EffectStack& effect_stack,
                                       Framebuffer& result,
                                       const std::optional<raster::BBox>& clip,
                                       runtime::RenderSurfaceHandle borrowed_source) {
    using namespace bulk_detail;
    if (!clip_covers_result(clip, result) || !ctx.backend || !ctx.surface_registry ||
        effect_stack.size() != 1) {
        return false;
    }
    const auto& instance = effect_stack[0];
    if (!instance.enabled || instance.effect_type != effects::EffectType::CameraRing)
        return false;
    const auto* p = std::get_if<CameraRingParams>(&instance.params);
    if (!p) return false;
    try {
        p->validate();
    } catch (...) {
        return false;
    }
    // CPU identity fast paths forward untouched without a dispatch.
    if (p->samples <= 1 || p->max_blur_radius <= 0.0f)
        return bulk_true_forward(ctx, result, borrowed_source);

    runtime::RenderSurfaceHandle source = runtime::kInvalidRenderSurfaceHandle;
    const auto destination = bulk_alloc_destination(ctx, result, borrowed_source, source);
    if (destination == runtime::kInvalidRenderSurfaceHandle) return false;
    const auto dispatched = ctx.backend_surface_api()->camera_ring_surface(
        destination, source, *p);
    if (!dispatched.ok()) {
        bulk_release_surface(ctx, destination);
        return false;
    }
    result.set_surface_handle(destination);
    result.mark_gpu_authoritative();
    return true;
}


} // namespace native_effects

} // namespace chronon3d::graph
