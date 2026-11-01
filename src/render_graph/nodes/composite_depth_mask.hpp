// ============================================================================
// composite_depth_mask.hpp — Camera-closure per-pixel cross-layer occlusion.
//
// A 2.5D-camera composite receives the top layer as one blended surface. The
// software mesh rasterizer now attaches a per-pixel camera-space depth map to
// its layer framebuffer (framebuffer.hpp `depth_map()`), and every camera 2.5D
// composite publishes the camera-space depth of the coverage it actually
// blended into the running `node_exec.composite_occlusion_depth_buffer()`
// (positive depth, 0 = uncovered, same convention as the mesh z-buffer and
// `camera_space_depth`).
//
// This helper solves per-pixel *cross-layer* depth: a triangle that crosses a
// 2.5D plane must be occluded where it is farther than the plane and must
// occlude the plane where it is nearer, even when both run in the same
// depth-sorted layer bin. Without this channel the composite can only order
// whole layers, which is the historically failing
// `software 3D mesh crossing a 2.5D plane uses per-pixel depth` case.
// ============================================================================

#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <cstdint>
#include <memory>

namespace chronon3d {
class Framebuffer;
struct RenderGraphContext;
} // namespace chronon3d

namespace chronon3d::graph {

// Depth-aware source masking for one composite step.
//
// Returns a masked copy of `top` whose pixels that fall behind geometry
// already composited into the frame (running occlusion depth) are cleared to
// transparent, so the subsequent blend only accepts nearer coverage. The
// accepted coverage is also merged (nearest wins) into the running occlusion
// buffer for later composites.
//
// Returns nullptr when this blend must not run the depth-aware path (no 2.5D
// camera, no per-pixel top depth, no finite layer depth): the caller then
// keeps blending the original top surface unchanged.
[[nodiscard]] std::shared_ptr<Framebuffer> build_depth_aware_top(
    RenderGraphContext& ctx,
    const Framebuffer& bottom,
    const Framebuffer& top,
    bool has_world_position,
    const Vec3& world_position,
    float world_z,
    std::uint64_t composite_id);

} // namespace chronon3d::graph
