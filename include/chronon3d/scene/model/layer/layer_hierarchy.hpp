#pragma once

#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/render/resolved_types.hpp>
#include <vector>

namespace chronon3d {

// ── combine_transforms_simple (used by camera rig tests) ──────────────────

Transform combine_transforms_simple(const Transform& parent, const Transform& child);

// ── Free functions — thin wrappers around HierarchyResolver ────────────────
// The old LayerHierarchyResolver class has been retired in favor of the
// canonical HierarchyResolver (see include/chronon3d/scene/model/core/
// hierarchy_resolver.hpp).  These functions preserve the existing public
// API used by tests and the render-graph layer resolver.

std::pmr::vector<ResolvedLayer> resolve_layer_hierarchy(
    const std::pmr::vector<Layer>& layers,
    /// Reserved for future per-frame filtering.  Currently all nodes are
    /// resolved regardless of whether they're active at the frame, so the
    /// hierarchy of inactive parents still contributes correct world
    /// transforms to their active children.
    [[maybe_unused]] Frame frame,
    std::pmr::memory_resource* res
);

} // namespace chronon3d
