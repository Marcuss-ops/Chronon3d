#include "composite_backdrop_detail.hpp"
#include <chronon3d/scene/model/camera/dof.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <spdlog/spdlog.h>

#include "native_surface.hpp"

namespace chronon3d::graph::detail {
namespace {
// publish_dof_provenance_native() — GPU counterpart of the CPU publish for
// device-local layers. The depth surface is created once per frame and reset
// to the "no depth" sentinel; each covered layer then stamps its depth on the
// device (same last-writer-wins rule). Coverage uses the layer clip rect as a
// conservative bbox, so the DOF ROI is a superset of the CPU one.
void publish_dof_provenance_native(
    RenderGraphContext& ctx, const Framebuffer& layer,
    float camera_depth, float source_radius,
    const std::optional<raster::BBox>& clip,
    i32 framebuffer_width, i32 framebuffer_height) {
    if (!ctx.backend || !ctx.surface_registry ||
        !ctx.backend_stats_api()->supports_native_surfaces()) {
        return;
    }
    if (layer.surface_handle() == runtime::kInvalidRenderSurfaceHandle) return;
    auto& native = ctx.node_exec.native_dof_state();
    if (native.unusable) return;
    if (!native.surface) {
        auto depth = std::make_shared<Framebuffer>(framebuffer_width, framebuffer_height);
        bool ready = ensure_empty_native_surface(ctx, *depth);
        // Empty clip rect + reset: every pixel becomes the "no depth" sentinel.
        ready = ready && ctx.backend_surface_api()->dof_depth_stamp_surface(
            depth->surface_handle(), layer.surface_handle(),
            0.0f, -1.0f, 0, 0, 0, 0, 0, 0, true).ok();
        if (!ready) {
            release_native_surface(ctx, *depth);
            native.unusable = true;
            return;
        }
        native.surface = std::move(depth);
    }

    const i32 cx0 = std::clamp(clip ? clip->x0 : 0, 0, framebuffer_width);
    const i32 cy0 = std::clamp(clip ? clip->y0 : 0, 0, framebuffer_height);
    const i32 cx1 = std::clamp(clip ? clip->x1 : framebuffer_width, 0, framebuffer_width);
    const i32 cy1 = std::clamp(clip ? clip->y1 : framebuffer_height, 0, framebuffer_height);
    const auto stamp = ctx.backend_surface_api()->dof_depth_stamp_surface(
        native.surface->surface_handle(), layer.surface_handle(),
        camera_depth, source_radius, cx0, cy0, cx1, cy1,
        layer.origin_x(), layer.origin_y(), false);
    if (!stamp.ok()) {
        release_native_surface(ctx, *native.surface);
        native.surface.reset();
        native.unusable = true;
        return;
    }

    if (std::isfinite(source_radius) && source_radius >= 0.5f &&
        cx0 < cx1 && cy0 < cy1) {
        auto& coverage = ctx.node_exec.dof_sources();
        if (!coverage.source_bbox) {
            coverage.source_bbox = raster::BBox{cx0, cy0, cx1, cy1};
        } else {
            auto& bbox = *coverage.source_bbox;
            bbox.x0 = std::min(bbox.x0, cx0);
            bbox.y0 = std::min(bbox.y0, cy0);
            bbox.x1 = std::max(bbox.x1, cx1);
            bbox.y1 = std::max(bbox.y1, cy1);
        }
        coverage.max_radius = std::max(coverage.max_radius, source_radius);
    }
}

} // namespace

// publish_dof_provenance() — single authority for DOF depth/coverage
// publication. Previously this loop existed twice verbatim (pass-through
// branch and post-blend branch); the two copies had to be kept in sync by
// discipline. Writes positive camera-space depth for covered pixels and
// expands the DOF source bbox/radius coverage.
void publish_dof_provenance(
    RenderGraphContext& ctx, const Framebuffer& surface,
    u64 composite_id,
    float camera_depth, float source_radius,
    const std::optional<raster::BBox>& clip,
    i32 framebuffer_width, i32 framebuffer_height) {
    auto& dof_depth = ctx.node_exec.dof_depth_buffer();
    if (dof_depth.empty()) return;
    if (!surface.is_cpu_authoritative() || surface.data() == nullptr) {
        publish_dof_provenance_native(ctx, surface, camera_depth, source_radius,
                                      clip, framebuffer_width, framebuffer_height);
        return;
    }
    auto& mutable_surface = const_cast<Framebuffer&>(surface);
    if (ensure_native_surface(ctx, mutable_surface,
                              "CompositeNode.dof_provenance")) {
        publish_dof_provenance_native(ctx, mutable_surface, camera_depth,
                                      source_radius, clip, framebuffer_width,
                                      framebuffer_height);
        return;
    }
    // CPU depth is now authoritative for this frame: the native depth surface
    // no longer describes every covered pixel, so it must not be consumed.
    auto& native_dof = ctx.node_exec.native_dof_state();
    if (!native_dof.unusable) {
        spdlog::warn(
            "[dof-provenance] switching to mixed residency at composite={} "
            "surface={}x{} cpu_authoritative={} gpu_handle={} depth={:.2f} radius={:.2f}",
            composite_id, surface.width(), surface.height(),
            surface.is_cpu_authoritative(),
            static_cast<std::uint64_t>(surface.surface_handle()),
            camera_depth, source_radius);
    }
    native_dof.unusable = true;
    const i32 w = framebuffer_width;
    const i32 bx0 = clip ? clip->x0 : 0;
    const i32 by0 = clip ? clip->y0 : 0;
    const i32 bx1 = clip ? clip->x1 : framebuffer_width;
    const i32 by1 = clip ? clip->y1 : framebuffer_height;
    for (i32 y = by0; y < by1; ++y) {
        const i32 sy = y - surface.origin_y();
        if (sy < 0 || sy >= surface.height()) continue;
        const Color* row = surface.pixels_row(sy);
        if (row == nullptr) continue;
        for (i32 x = bx0; x < bx1; ++x) {
            const i32 sx = x - surface.origin_x();
            if (sx < 0 || sx >= surface.width() || row[sx].a <= 0.01f) continue;
            dof_depth[static_cast<size_t>(y) * w + x] = camera_depth;
            if (std::isfinite(source_radius) && source_radius >= 0.5f) {
                auto& coverage = ctx.node_exec.dof_sources();
                if (!coverage.source_bbox) {
                    coverage.source_bbox = raster::BBox{x, y, x + 1, y + 1};
                } else {
                    auto& bbox = *coverage.source_bbox;
                    bbox.x0 = std::min(bbox.x0, x);
                    bbox.y0 = std::min(bbox.y0, y);
                    bbox.x1 = std::max(bbox.x1, x + 1);
                    bbox.y1 = std::max(bbox.y1, y + 1);
                }
                coverage.max_radius = std::max(coverage.max_radius, source_radius);
            }
        }
    }
}

// dof_source_radius() — shared radius derivation for both DOF publish sites.
float dof_source_radius(RenderGraphContext& ctx, float camera_depth) {
    const auto& camera_2_5d = ctx.frame_input.camera_2_5d;
    const auto dof = camera_space_dof_settings(camera_2_5d.dof, camera_2_5d);
    return camera_2_5d.dof.enabled
        ? compute_dof_blur_radius(dof, camera_2_5d.lens,
                                  camera_depth,
                                  static_cast<float>(ctx.frame_input.width))
        : 0.0f;
}

} // namespace chronon3d::graph::detail
