// ============================================================================
// composite_depth_mask.cpp — see composite_depth_mask.hpp for the contract.
// ============================================================================

#include "composite_depth_mask.hpp"

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

} // namespace chronon3d::graph
