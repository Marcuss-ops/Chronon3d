// ============================================================================
// layer_hierarchy.cpp — Thin wrappers around the unified HierarchyResolver
//
// These free functions maintain the existing API surface (resolve_layer_hierarchy,
// resolve_camera_hierarchy) but delegate to the canonical HierarchyResolver.
// The old LayerHierarchyResolver class is retired.
// ============================================================================

#include <chronon3d/scene/model/layer/layer_hierarchy.hpp>
#include <chronon3d/scene/model/core/hierarchy_resolver.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/math/camera_pose.hpp>
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

namespace detail {

Vec3 world_anchor_point(const Transform& world_transform, const Mat4& world_matrix) {
    const Vec4 anchor = world_matrix * Vec4(world_transform.anchor, 1.0f);
    if (std::abs(anchor.w) > 1e-7f) {
        return {anchor.x / anchor.w, anchor.y / anchor.w, anchor.z / anchor.w};
    }
    return world_transform.position;
}

} // namespace detail

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

// ── resolve_camera_hierarchy — delegates to HierarchyResolver ──────────────

ResolvedCamera resolve_camera_hierarchy(
    const std::pmr::vector<Layer>& layers,
    std::pmr::memory_resource* /*res*/,
    const Camera2_5DRuntime& input_camera
) {
    ResolvedCamera out;
    out.camera = input_camera;
    out.world_transform.position = input_camera.position;
    out.world_transform.rotation = math::camera_rotation_quat(input_camera.rotation);
    out.world_matrix = out.world_transform.to_mat4();

    if (input_camera.hierarchy_baked) return out;

    // Build name→index
    auto name_to_index = build_name_index(layers);

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
            auto it = name_to_index.find(std::string_view(layer.parent_name));
            if (it != name_to_index.end()) v.parent = it->second;
        }

        views.push_back(std::move(v));
    }

    // Resolve
    HierarchyResolver resolver(std::move(views));
    auto results = resolver.resolve();

    // Apply camera parent
    if (!input_camera.parent_name.empty()) {
        auto it = name_to_index.find(std::string_view(input_camera.parent_name));
        if (it != name_to_index.end()) {
            const std::size_t parent_idx = it->second;
            const Mat4 parent_matrix = results.at(parent_idx).world_matrix;
            const Mat4 local_matrix = out.world_transform.to_mat4();
            const Mat4 world_matrix = parent_matrix * local_matrix;

            out.world_matrix = world_matrix;
            out.world_transform = from_mat4(world_matrix);
            out.camera.position = out.world_transform.position;
            out.camera.rotation = math::camera_rotation_euler(out.world_transform.rotation);
        }
    }

    // Apply camera target
    if (!input_camera.target_name.empty()) {
        auto it = name_to_index.find(std::string_view(input_camera.target_name));
        if (it != name_to_index.end()) {
            const std::size_t target_idx = it->second;
            const auto& target_result = results.at(target_idx);
            out.camera.point_of_interest = detail::world_anchor_point(
                from_mat4(target_result.world_matrix, target_result.world_opacity),
                target_result.world_matrix);
            out.camera.point_of_interest_enabled = true;
        }
    }

    return out;
}

} // namespace chronon3d
