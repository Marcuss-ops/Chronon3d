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


// Shared prologue for the native bulk dispatches.
namespace chronon3d::graph::native_effects::bulk_detail {

inline bool bulk_finite_list(std::initializer_list<float> vs) {
    for (float v : vs)
        if (!std::isfinite(v)) return false;
    return true;
}

inline int bulk_edge_to_int(sampling::EdgeMode mode, bool& ok) {
    if (mode == sampling::EdgeMode::Transparent) { ok = true; return 0; }
    if (mode == sampling::EdgeMode::Clamp) { ok = true; return 1; }
    ok = false;
    return 0;
}

// Common single-dispatch prologue: borrow or reuse the source surface and
// allocate a same-size destination. Returns invalid handle on failure.
inline runtime::RenderSurfaceHandle bulk_alloc_destination(RenderGraphContext& ctx,
                                                     Framebuffer& result,
                                                     runtime::RenderSurfaceHandle borrowed_source,
                                                     runtime::RenderSurfaceHandle& source_out) {
    const bool has_borrowed = borrowed_source != runtime::kInvalidRenderSurfaceHandle;
    const bool has_result_surface =
        result.surface_handle() != runtime::kInvalidRenderSurfaceHandle &&
        ctx.backend_surface_api()->is_native_surface_valid(result.surface_handle());
    if (!has_borrowed && !has_result_surface) return runtime::kInvalidRenderSurfaceHandle;
    source_out = has_borrowed ? borrowed_source : result.surface_handle();
    const auto desc = native_surface_desc(ctx, result.width(), result.height());
    const auto destination = ctx.surface_registry->create(desc);
    if (destination == runtime::kInvalidRenderSurfaceHandle) return destination;
    if (!ctx.backend_surface_api()->create_surface(destination, desc).ok()) {
        ctx.surface_registry->release(destination);
        return runtime::kInvalidRenderSurfaceHandle;
    }
    return destination;
}

inline void bulk_release_surface(RenderGraphContext& ctx,
                            runtime::RenderSurfaceHandle h) {
    (void)ctx.backend_surface_api()->release_surface(h);
    ctx.surface_registry->release(h);
}

// Forward helper for CPU identity fast paths (mirrors the directional-blur
// convention: attach the borrowed surface when present).
inline bool bulk_true_forward(RenderGraphContext& ctx, Framebuffer& result,
                         runtime::RenderSurfaceHandle borrowed_source) {
    if (borrowed_source != runtime::kInvalidRenderSurfaceHandle) {
        result.set_surface_handle(borrowed_source);
        result.mark_gpu_authoritative();
    }
    (void)ctx;
    return true;
}

inline runtime::RenderSurfaceHandle bulk_upload_aux(RenderGraphContext& ctx,
                                               int w, int h,
                                               std::span<const float> rgba);

// Upload a raw RGBA32F span as an aux surface (Curves tables, Lut3D table).
// The steady-state path below reuses the canonical GpuAssetCache instead:
// the table is bounded and content-addressed, so a per-frame rebuild does not
// imply a per-frame create/upload. `cache_owned` tells the caller whether the
// handle must be released after the dispatch.
inline runtime::RenderSurfaceHandle bulk_cached_lut_aux(
    RenderGraphContext& ctx, int w, int h, std::span<const float> rgba,
    bool& cache_owned) {
    cache_owned = false;
    if (ctx.gpu_asset_cache && ctx.backend &&
        ctx.gpu_asset_cache->backend() == ctx.backend) {
        const auto digest = assets::sha256_string(std::string_view(
            reinterpret_cast<const char*>(rgba.data()), rgba.size_bytes()));
        const runtime::GpuAssetKey key{
            digest, runtime::PixelFormat::Rgba32Float,
            static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h)};
        auto desc = native_surface_desc(ctx, w, h, false,
                                        runtime::PixelFormat::Rgba32Float);
        desc.lifetime = runtime::LifetimeClass::JobPersistent;
        const auto acquired = ctx.gpu_asset_cache->acquire(key, desc, rgba);
        if (acquired.ok()) {
            cache_owned = true;
            return acquired.handle;
        }
    }
    return bulk_upload_aux(ctx, w, h, rgba);
}

inline runtime::RenderSurfaceHandle bulk_upload_aux(RenderGraphContext& ctx,
                                               int w, int h,
                                               std::span<const float> rgba) {
    const auto desc = native_surface_desc(ctx, w, h);
    const auto aux = ctx.surface_registry->create(desc);
    if (aux == runtime::kInvalidRenderSurfaceHandle) return aux;
    if (!ctx.backend_surface_api()->create_surface(aux, desc).ok()) {
        ctx.surface_registry->release(aux);
        return runtime::kInvalidRenderSurfaceHandle;
    }
    if (!ctx.backend_surface_api()->upload_surface(aux, desc, rgba).ok()) {
        bulk_release_surface(ctx, aux);
        return runtime::kInvalidRenderSurfaceHandle;
    }
    return aux;
}


} // namespace bulk_detail
