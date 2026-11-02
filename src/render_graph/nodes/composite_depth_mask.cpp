// ============================================================================
// composite_depth_mask.cpp — see composite_depth_mask.hpp for the contract.
// ============================================================================

#include "composite_depth_mask.hpp"

#include "native_surface.hpp"

#include <chronon3d/cache/framebuffer_copy.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/scene/model/camera/dof.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace chronon3d::graph {

namespace {

// Camera-space distance below which two depths count as "same surface".
// Depth units are world/camera units along the optical axis (the same the
// mesh z-buffer and DoF provenance use); 1e-3 is sub-pixel at any projective
// scale used by camera 2.5D scenes and keeps antialiased edge jitter from
// flipping the comparison.
constexpr float kOcclusionEpsilon = 1e-3f;

// Alpha at or below this value means "pixel not composited over" — the same
// coverage threshold publish_dof_provenance uses for depth stamping.
constexpr float kCoverageAlpha = 0.01f;

std::size_t frame_index(i32 width, i32 x, i32 y) noexcept {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
           static_cast<std::size_t>(x);
}

// Per-pixel camera-space depth of the top source at a frame-space pixel.
// Returns 0.0 when the source has no depth here (no coverage / uncovered
// sentinel) — the caller then treats the pixel as accepted without publishing.
float top_depth_at(
    const Framebuffer& top,
    const std::vector<float>* top_depth,
    float finite_layer_depth,
    bool has_pixel_depth,
    i32 gx, i32 gy) noexcept {
    if (has_pixel_depth) {
        const i32 local_x = gx - top.origin_x();
        const i32 local_y = gy - top.origin_y();
        if (local_x < 0 || local_y < 0 ||
            local_x >= top.width() || local_y >= top.height()) {
            return 0.0f;
        }
        return (*top_depth)[
            static_cast<std::size_t>(local_y) * static_cast<std::size_t>(top.width()) +
            static_cast<std::size_t>(local_x)];
    }
    return finite_layer_depth;
}

Color source_over(Color foreground, Color background) noexcept {
    const float remaining = 1.0f - std::clamp(foreground.a, 0.0f, 1.0f);
    return {
        foreground.r + background.r * remaining,
        foreground.g + background.g * remaining,
        foreground.b + background.b * remaining,
        foreground.a + background.a * remaining,
    };
}

} // namespace

std::shared_ptr<Framebuffer> build_depth_aware_top(
    RenderGraphContext& ctx,
    const Framebuffer& bottom,
    const Framebuffer& top,
    bool has_world_position,
    const Vec3& world_position,
    float world_z,
    std::uint64_t composite_id)
{
    if (!ctx.frame_input.has_camera_2_5d ||
        !ctx.frame_input.camera_2_5d.enabled) {
        return nullptr;
    }
    // This helper builds a CPU masked copy. Device-local sources have no host
    // pixels by contract; native layer sorting remains authoritative there.
    // Never probe pixels_row() on a GPU-only framebuffer.
    if (!top.data()) return nullptr;
    const auto& camera_2_5d = ctx.frame_input.camera_2_5d;
    const i32 frame_w = ctx.frame_input.width;
    const i32 frame_h = ctx.frame_input.height;
    if (frame_w <= 0 || frame_h <= 0) return nullptr;

    // 1) Resolve the per-pixel depth source for the top layer. A mesh raster
    // attaches a full depth map; every other camera 2.5D layer falls back to
    // its constant composite camera-space depth (the value the graph has
    // always had — world position or world-z plane).
    const std::vector<float>* top_depth = top.depth_map();
    const bool has_pixel_depth =
        top_depth != nullptr &&
        top_depth->size() ==
            static_cast<std::size_t>(top.width()) * static_cast<std::size_t>(top.height());
    if (top_depth != nullptr && !has_pixel_depth) {
        // Malformed attached map: treat the layer has having none.
        top_depth = nullptr;
    }
    const float layer_depth = camera_space_depth(
        camera_2_5d,
        has_world_position
            ? world_position
            : Vec3{0.0f, 0.0f, world_z});
    const bool finite_layer_depth =
        std::isfinite(layer_depth) && layer_depth > 0.0f;
    if (!has_pixel_depth && !finite_layer_depth) return nullptr;

    // 2) Bounds of the top coverage overlap in frame coordinates.
    const i32 gx0 = std::max(0, top.origin_x());
    const i32 gy0 = std::max(0, top.origin_y());
    const i32 gx1 = std::min(frame_w, top.origin_x() + top.width());
    const i32 gy1 = std::min(frame_h, top.origin_y() + top.height());
    if (gx0 >= gx1 || gy0 >= gy1) return nullptr;

    // 3) Running occlusion buffer, lazily materialized for the frame.
    auto& occlusion = ctx.node_exec.composite_occlusion_depth_buffer();
    if (occlusion.size() !=
        static_cast<std::size_t>(frame_w) * static_cast<std::size_t>(frame_h)) {
        occlusion.assign(
            static_cast<std::size_t>(frame_w) * static_cast<std::size_t>(frame_h),
            0.0f);
    }

    // 4) Scan 1 — classify per-pixel acceptance against the running buffer
    // *without* mutating it, so the same predicate can be re-evaluated below.
    std::size_t skip_count = 0;
    bool published_any = false;
    for (i32 gy = gy0; gy < gy1; ++gy) {
        const Color* top_row = top.pixels_row(gy - top.origin_y());
        if (top_row == nullptr) continue;
        for (i32 gx = gx0; gx < gx1; ++gx) {
            const Color& source = top_row[gx - top.origin_x()];
            if (source.a <= kCoverageAlpha) continue;
            const float depth = top_depth_at(
                top, top_depth, layer_depth, has_pixel_depth, gx, gy);
            const float running = occlusion[frame_index(frame_w, gx, gy)];
            const bool accepted =
                depth <= 0.0f ||
                running <= 0.0f ||
                depth <= running + kOcclusionEpsilon;
            if (!accepted) {
                ++skip_count;
            }
        }
    }
    // 5) Masked copy only when something must be held back. The copy is
    // content-identical to `top` except that rejected pixels are transparent,
    // so the existing blend infrastructure (CPU source-over and native
    // surface composites) keeps its exact semantics.
    std::shared_ptr<Framebuffer> masked;
    if (skip_count > 0) {
        masked = ctx.acquire_owned_fb(
            top.width(), top.height(), false);
        if (!masked) return nullptr;
        FramebufferCopyAuthority::copy_rows(*masked, top);
        masked->set_origin(top.origin_x(), top.origin_y());
        masked->set_opaque(false);
    }

    // 6) Scan 2 — publish the accepted coverage (nearest writer wins). Runs
    // regardless of masking so later layers can test against the composited
    // result either way. Rejected pixels do not publish.
    for (i32 gy = gy0; gy < gy1; ++gy) {
        const Color* top_row = top.pixels_row(gy - top.origin_y());
        if (top_row == nullptr) continue;
        for (i32 gx = gx0; gx < gx1; ++gx) {
            const Color& source = top_row[gx - top.origin_x()];
            if (source.a <= kCoverageAlpha) continue;
            const std::size_t running_idx = frame_index(frame_w, gx, gy);
            const float depth = top_depth_at(
                top, top_depth, layer_depth, has_pixel_depth, gx, gy);
            float& running = occlusion[running_idx];
            const bool accepted =
                depth <= 0.0f ||
                running <= 0.0f ||
                depth <= running + kOcclusionEpsilon;
            if (!accepted) {
                if (masked) {
                    masked->set_pixel(gx - top.origin_x(), gy - top.origin_y(),
                                      Color::transparent());
                }
                continue;
            }
            if (depth > 0.0f) {
                running = (running <= 0.0f) ? depth
                    : std::min(running, depth);
                published_any = true;
            }
        }
    }

    if (ctx.policy.diagnostics_enabled && skip_count > 0) {
        spdlog::debug(
            "[camera-depth-mask] frame={} composite={} skipped={} published={}",
            static_cast<int>(ctx.frame_input.frame), composite_id,
            skip_count, published_any ? 1 : 0);
    }
    return masked;
}

namespace {

// Lazily (re)create the three frame-scoped occlusion surfaces. Returns false
// when the backend cannot own them, which permanently disables the native
// path for this frame instead of silently keeping layer-order semantics.
bool ensure_occlusion_surfaces(
    RenderGraphContext& ctx,
    NodeExecutionContext::NativeOcclusionState& state,
    i32 width, i32 height) {
    const auto same = [width, height](const std::shared_ptr<Framebuffer>& fb) {
        return fb && fb->width() == width && fb->height() == height &&
               fb->surface_handle() != runtime::kInvalidRenderSurfaceHandle;
    };
    const bool recreate = !same(state.layer_depth) || !same(state.frame_depth) ||
                          !same(state.mask);
    if (recreate) {
        if (state.layer_depth) release_native_surface(ctx, *state.layer_depth);
        if (state.frame_depth) release_native_surface(ctx, *state.frame_depth);
        if (state.mask) release_native_surface(ctx, *state.mask);
        state.layer_depth = std::make_shared<Framebuffer>(width, height);
        state.frame_depth = std::make_shared<Framebuffer>(width, height);
        state.mask = std::make_shared<Framebuffer>(width, height);
        if (!ensure_empty_native_surface(ctx, *state.layer_depth) ||
            !ensure_empty_native_surface(ctx, *state.frame_depth) ||
            !ensure_empty_native_surface(ctx, *state.mask)) {
            if (state.layer_depth) release_native_surface(ctx, *state.layer_depth);
            if (state.frame_depth) release_native_surface(ctx, *state.frame_depth);
            if (state.mask) release_native_surface(ctx, *state.mask);
            state.layer_depth.reset();
            state.frame_depth.reset();
            state.mask.reset();
            return false;
        }
        state.frame = -1;  // force an accumulator reset
    }
    return true;
}

} // namespace

bool apply_native_occlusion(
    RenderGraphContext& ctx,
    Framebuffer& top,
    bool has_world_position,
    const Vec3& world_position,
    float world_z)
{
    if (!ctx.frame_input.has_camera_2_5d ||
        !ctx.frame_input.camera_2_5d.enabled) {
        return false;
    }
    if (!ctx.backend || !ctx.surface_registry ||
        !ctx.backend_stats_api()->supports_native_surfaces()) {
        return false;
    }
    auto* api = ctx.backend_surface_api();
    if (api == nullptr) return false;
    // Device-local only: a CPU-authoritative layer is occluded by
    // build_depth_aware_top() instead, and this path must never upload.
    if (top.surface_handle() == runtime::kInvalidRenderSurfaceHandle ||
        !api->is_native_surface_valid(top.surface_handle())) {
        return false;
    }
    const i32 width = ctx.frame_input.width;
    const i32 height = ctx.frame_input.height;
    if (width <= 0 || height <= 0) return false;
    if (top.width() != width || top.height() != height) return false;

    auto& state = ctx.node_exec.native_occlusion_state();
    if (state.unusable) return false;
    if (!ensure_occlusion_surfaces(ctx, state, width, height)) {
        state.unusable = true;
        return false;
    }

    const auto frame = static_cast<std::int64_t>(ctx.frame_input.frame);
    if (state.frame != frame) {
        // New frame: nothing composed yet. The reset sentinel is the same the
        // DOF stamp uses (r = -1, g = -1 = "no coverage").
        const auto reset = api->dof_depth_stamp_surface(
            state.frame_depth->surface_handle(), top.surface_handle(),
            0.0f, -1.0f, 0, 0, 0, 0, 0, 0, true);
        if (!reset.ok()) {
            state.unusable = true;
            return false;
        }
        state.frame = frame;
    }

    runtime::RenderSurfaceHandle layer_depth = top.native_depth_handle();
    if (layer_depth == runtime::kInvalidRenderSurfaceHandle ||
        !api->is_native_surface_valid(layer_depth)) {
        // No per-pixel depth: stamp the layer's constant camera depth over its
        // covered pixels, then reset the scratch for the next layer. The
        // radius slot doubles as the coverage marker (>= 0 = present).
        const float depth = camera_space_depth(
            ctx.frame_input.camera_2_5d,
            has_world_position ? world_position : Vec3{0.0f, 0.0f, world_z});
        if (!(std::isfinite(depth) && depth > 0.0f)) return false;
        const auto stamp = api->dof_depth_stamp_surface(
            state.layer_depth->surface_handle(), top.surface_handle(),
            depth, 0.0f, 0, 0, width, height,
            top.origin_x(), top.origin_y(), true);
        if (!stamp.ok()) {
            state.unusable = true;
            return false;
        }
        layer_depth = state.layer_depth->surface_handle();
    }

    const auto tag = api->occlusion_tag_surface(
        state.mask->surface_handle(), layer_depth,
        state.frame_depth->surface_handle(), true);
    if (!tag.ok()) {
        state.unusable = true;
        return false;
    }
    // Apply the visibility mask to the layer's premultiplied alpha. The matte
    // kernel mulitiplies in place per pixel, so aliasing destination/target
    // is safe; occluded pixels become exactly transparent and the existing
    // native composite keeps its unmodified source-over semantics.
    const auto masked = api->matte_surface(
        top.surface_handle(), top.surface_handle(),
        state.mask->surface_handle(), false, false);
    if (!masked.ok()) {
        state.unusable = true;
        return false;
    }
    return true;
}

bool recompose_camera_composite(
    RenderGraphContext& ctx,
    const Framebuffer& bottom,
    const Framebuffer& top,
    Framebuffer& output,
    bool has_world_position,
    const Vec3& world_position,
    float world_z)
{
    if (!ctx.frame_input.has_camera_2_5d ||
        !ctx.frame_input.camera_2_5d.enabled ||
        !bottom.data() || !top.data() || !output.data()) {
        return false;
    }
    const i32 width = ctx.frame_input.width;
    const i32 height = ctx.frame_input.height;
    if (width <= 0 || height <= 0 || output.width() != width || output.height() != height) {
        return false;
    }

    auto& state = ctx.node_exec.camera_composite_state();
    const std::size_t pixel_count = static_cast<std::size_t>(width) * height;
    if (state.width != width || state.height != height ||
        state.fragments.size() != pixel_count) {
        state.width = width;
        state.height = height;
        state.base.assign(pixel_count, Color::transparent());
        state.initialized.assign(pixel_count, 0);
        state.fragments.clear();
        state.fragments.resize(pixel_count);
        state.resolved_depth = std::make_shared<std::vector<float>>(pixel_count, 0.0f);
    }

    const auto* depth_map = top.depth_map();
    const bool has_depth_map = depth_map &&
        depth_map->size() == static_cast<std::size_t>(top.width()) * top.height();
    const float layer_depth = camera_space_depth(
        ctx.frame_input.camera_2_5d,
        has_world_position ? world_position : Vec3{0.0f, 0.0f, world_z});
    if (!has_depth_map && (!std::isfinite(layer_depth) || layer_depth <= 0.0f)) {
        return false;
    }

    const i32 gx0 = std::max(0, top.origin_x());
    const i32 gy0 = std::max(0, top.origin_y());
    const i32 gx1 = std::min(width, top.origin_x() + top.width());
    const i32 gy1 = std::min(height, top.origin_y() + top.height());
    std::vector<std::size_t> affected;
    affected.reserve(static_cast<std::size_t>(gx1 - gx0) * (gy1 - gy0));
    for (i32 gy = gy0; gy < gy1; ++gy) {
        const Color* source_row = top.pixels_row(gy - top.origin_y());
        if (!source_row) continue;
        const Color* bottom_row = bottom.pixels_row(gy - bottom.origin_y());
        for (i32 gx = gx0; gx < gx1; ++gx) {
            const Color source = source_row[gx - top.origin_x()];
            if (!(source.a > 0.0f)) continue;
            const i32 sx = gx - top.origin_x();
            const i32 sy = gy - top.origin_y();
            const float depth = has_depth_map
                ? (*depth_map)[static_cast<std::size_t>(sy) * top.width() + sx]
                : layer_depth;
            if (!(std::isfinite(depth) && depth > 0.0f)) continue;

            const std::size_t index = frame_index(width, gx, gy);
            if (!state.initialized[index]) {
                const i32 bx = gx - bottom.origin_x();
                const i32 by = gy - bottom.origin_y();
                state.base[index] = bottom_row && bx >= 0 && by >= 0 &&
                    bx < bottom.width() && by < bottom.height()
                    ? bottom_row[bx] : Color::transparent();
                state.initialized[index] = 1;
            }
            state.fragments[index].push_back({depth, source});
            affected.push_back(index);
        }
    }

    for (const std::size_t index : affected) {
        auto& fragments = state.fragments[index];
        if (fragments.empty()) continue;
        // Stable sorting keeps authored layer order for coplanar surfaces.
        std::stable_sort(fragments.begin(), fragments.end(),
            [](const auto& a, const auto& b) { return a.depth > b.depth; });
        Color resolved = state.base[index];
        for (const auto& fragment : fragments) {
            resolved = source_over(fragment.premultiplied, resolved);
        }
        output.set_pixel(static_cast<i32>(index % width),
                         static_cast<i32>(index / width), resolved);
        (*state.resolved_depth)[index] = fragments.back().depth;
        // DOF uses the nearest visible contributor under its existing alpha
        // coverage rule, not the last layer submitted to the compositor.
        // This is a single-depth approximation for translucent mixtures; it
        // does not represent all contributing depths.
        if (ctx.policy.track_dof_depth) {
            auto& dof_depth = ctx.node_exec.dof_depth_buffer();
            if (dof_depth.size() == pixel_count) {
                const auto visible = std::find_if(fragments.rbegin(), fragments.rend(),
                    [](const auto& fragment) {
                        return fragment.premultiplied.a > kCoverageAlpha;
                    });
                if (visible != fragments.rend()) dof_depth[index] = visible->depth;
            }
        }
    }
    if (ctx.policy.track_dof_depth && !affected.empty()) {
        // Layer-radius hints were stamped before this depth resolve and may
        // exclude a nearer mesh fragment. Force DOF to analyze the full
        // resolved depth buffer rather than trusting stale ROI metadata.
        ctx.node_exec.dof_sources().reset();
    }
    output.set_opaque(false);
    output.set_depth_map_shared(state.resolved_depth);
    return true;
}

} // namespace chronon3d::graph
