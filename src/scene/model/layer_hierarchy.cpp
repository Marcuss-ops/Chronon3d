// ============================================================================
// layer_hierarchy.cpp — Thin wrappers around the unified HierarchyResolver
//
// These free functions maintain the existing API surface (resolve_layer_hierarchy,
// and delegate to the canonical HierarchyResolver.
// The old LayerHierarchyResolver class is retired.
// ============================================================================

#include <chronon3d/scene/model/layer/layer_hierarchy.hpp>
#include <chronon3d/scene/model/core/hierarchy_resolver.hpp>
#include <chronon3d/math/transform.hpp>

namespace chronon3d {

// ── combine_transforms_simple (used by camera rig tests) ──────────────────

Transform combine_transforms_simple(const Transform& parent, const Transform& child) {
    Transform out = child;
    Vec3 scaled_child_pos = child.position * parent.scale;
    out.position = parent.position + (parent.rotation * scaled_child_pos);
    out.rotation = parent.rotation * child.rotation;
    out.scale    = parent.scale * child.scale;
    out.opacity  = parent.opacity * child.opacity;
    return out;
}

// ── resolve_layer_hierarchy — delegates to HierarchyResolver ───────────────

std::pmr::vector<ResolvedLayer> resolve_layer_hierarchy(
    const std::pmr::vector<Layer>& layers,
    Frame frame,
    std::pmr::memory_resource* res
) {
    std::pmr::vector<ResolvedLayer> out(res);
    if (layers.empty()) return out;

    // Build name→index
    auto name_to_index = build_name_index(layers);

    // Short-circuit: if the scene has already resolved its hierarchy, the
    // layer.transform fields are already in world space. Re-running the
    // resolver would double-apply parent transforms. Keep the baked world
    // transform and only re-derive the diagnostic parent_missing /
    // cycle_detected flags from the surviving parent_name metadata.
    if (!layers.empty() && layers[0].hierarchy_resolved) {
        out.resize(layers.size());
        for (std::size_t i = 0; i < layers.size(); ++i) {
            const auto& layer = layers[i];
            out[i].layer = &layer;
            out[i].world_transform = layer.transform;
            out[i].world_matrix = layer.transform.to_mat4();
            out[i].insertion_index = i;
            out[i].has_parent = !layer.parent_name.empty();
            if (!layer.parent_name.empty()) {
                auto it = name_to_index.find(std::string_view(layer.parent_name));
                out[i].parent_missing = (it == name_to_index.end());
                out[i].cycle_detected = (layer.parent_name == layer.name);
            }
        }
        return out;
    }

    // Build node views
    std::vector<HierarchyNodeView> views;
    views.reserve(layers.size());
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const auto& layer = layers[i];
        HierarchyNodeView v;
        v.id = i;
        v.local_matrix = layer.transform.to_mat4();
        v.local_opacity = layer.transform.opacity;

        if (!layer.parent_name.empty()) {
            v.parent_declared = true;
            auto it = name_to_index.find(std::string_view(layer.parent_name));
            if (it != name_to_index.end()) v.parent = it->second;
        }

        views.push_back(std::move(v));
    }

    // Resolve
    HierarchyResolver resolver(std::move(views));
    auto results = resolver.resolve();

    // Convert to ResolvedLayer output
    out.resize(layers.size());
    for (std::size_t i = 0; i < layers.size(); ++i) {
        out[i].layer = &layers[i];
        out[i].world_transform = from_mat4(results[i].world_matrix, results[i].world_opacity);
        out[i].world_matrix = results[i].world_matrix;
        out[i].insertion_index = i;
        out[i].cycle_detected = results[i].cycle_detected;
        out[i].parent_missing = results[i].parent_missing;
        out[i].has_parent = layers[i].parent_name.empty() ? false : true;
    }

    return out;
}

} // namespace chronon3d
