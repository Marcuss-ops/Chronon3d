#pragma once

#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/math/camera_pose.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/render/resolved_types.hpp>
#include <vector>

namespace chronon3d {

// ── combine_transforms_simple (used by camera rig tests) ──────────────────

Transform combine_transforms_simple(const Transform& parent, const Transform& child);

namespace detail {

// ── world_anchor_point (used by resolve_camera_hierarchy) ─────────────────

Vec3 world_anchor_point(const Transform& world_transform, const Mat4& world_matrix);

} // namespace detail

// ── Free functions — thin wrappers around HierarchyResolver ────────────────
// The old LayerHierarchyResolver class has been retired in favor of the
// canonical HierarchyResolver (see include/chronon3d/scene/model/core/
// hierarchy_resolver.hpp).  These functions preserve the existing public
// API used by tests and the render-graph layer resolver.

std::pmr::vector<ResolvedLayer> resolve_layer_hierarchy(
    const std::pmr::vector<Layer>& layers,
    /// Frame being resolved.  Layer anim_transform tracks (when
    /// time-dependent) are evaluated at this frame so per-frame motion
    /// reaches world matrices; static layers keep their baked transform.
    Frame frame,
    std::pmr::memory_resource* res,
    /// Frame rate for time-based animation evaluation (expressions,
    /// quaternion tracks).  Plain keyframe tracks are frame-indexed and
    /// insensitive to this value.
    double fps = 30.0
);

ResolvedCamera resolve_camera_hierarchy(
    const std::pmr::vector<Layer>& layers,
    std::pmr::memory_resource* res,
    const Camera2_5DRuntime& input_camera
);

} // namespace chronon3d
