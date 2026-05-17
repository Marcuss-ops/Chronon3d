#pragma once

#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/math/camera_pose.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/quat.hpp>
#include <chronon3d/scene/layer/resolved_types.hpp>
#include <unordered_map>
#include <vector>
#include <string>
#include <string_view>
#include <functional>

namespace chronon3d {

// Removed ResolvedLayer and ResolvedCamera definitions (now in resolved_types.hpp)

inline Transform combine_transforms_simple(const Transform& parent, const Transform& child) {
    Transform out = child;

    // Correct world position: parent_pos + rotate(parent_rot, child_pos * parent_scale)
    // Note: We also need to account for parent scale when propagating child position.
    Vec3 scaled_child_pos = child.position * parent.scale;
    out.position = parent.position + (parent.rotation * scaled_child_pos);

    out.rotation = parent.rotation * child.rotation;
    out.scale    = parent.scale * child.scale;
    out.opacity  = parent.opacity * child.opacity;

    return out;
}

namespace detail {

class LayerHierarchyResolver {
public:
    LayerHierarchyResolver(const std::pmr::vector<Layer>& layers, std::pmr::memory_resource* res)
        : m_layers(layers)
        , m_resolved(res)
        , m_state(layers.size(), VisitState::Unvisited) {
        m_resolved.resize(layers.size());
        m_name_to_index.reserve(layers.size());

        for (usize i = 0; i < layers.size(); ++i) {
            m_name_to_index.emplace(std::string_view(layers[i].name), i);
            m_resolved[i].layer = &layers[i];
            m_resolved[i].world_transform = layers[i].transform;
            m_resolved[i].world_matrix = layers[i].transform.to_mat4();
            m_resolved[i].insertion_index = i;
        }
    }

    [[nodiscard]] std::pmr::vector<ResolvedLayer> resolve_layers(Frame frame) {
        for (usize i = 0; i < m_layers.size(); ++i) {
            if (m_layers[i].active_at(frame)) {
                resolve_one(i);
            }
        }
        return m_resolved;
    }

    [[nodiscard]] ResolvedCamera resolve_camera(const Camera2_5DRuntime& input_camera) {
        ResolvedCamera out_camera;
        out_camera.camera = input_camera;
        out_camera.world_transform.position = input_camera.position;
        out_camera.world_transform.rotation = math::camera_rotation_quat(input_camera.rotation);
        out_camera.world_matrix = out_camera.world_transform.to_mat4();

        if (input_camera.hierarchy_baked) {
            return out_camera;
        }

        if (!input_camera.parent_name.empty()) {
            auto it = m_name_to_index.find(std::string_view(input_camera.parent_name));
            if (it != m_name_to_index.end()) {
                const usize parent_index = it->second;
                const Transform parent_world = resolve_one(parent_index);
                const Mat4 parent_matrix = m_resolved[parent_index].world_matrix;
                const Mat4 local_matrix = out_camera.world_transform.to_mat4();
                const Mat4 world_matrix = parent_matrix * local_matrix;

                out_camera.world_matrix = world_matrix;
                out_camera.world_transform = from_mat4(world_matrix);
                out_camera.camera.position = out_camera.world_transform.position;
                out_camera.camera.rotation = math::camera_rotation_euler(out_camera.world_transform.rotation);
            }
        }

        if (!input_camera.target_name.empty()) {
            auto it = m_name_to_index.find(std::string_view(input_camera.target_name));
            if (it != m_name_to_index.end()) {
                Transform target_world = resolve_one(it->second);
                out_camera.camera.point_of_interest = target_world.position;
                out_camera.camera.point_of_interest_enabled = true;
            }
        }

        return out_camera;
    }

private:
    enum class VisitState { Unvisited, Visiting, Visited };

    Transform resolve_one(usize index) {
        if (m_state[index] == VisitState::Visited) {
            return m_resolved[index].world_transform;
        }

        if (m_state[index] == VisitState::Visiting) {
            m_resolved[index].cycle_detected = true;
            m_state[index] = VisitState::Visited;
            m_resolved[index].world_transform = m_layers[index].transform;
            m_resolved[index].world_matrix = m_layers[index].transform.to_mat4();
            return m_resolved[index].world_transform;
        }

        m_state[index] = VisitState::Visiting;

        const Layer& layer = m_layers[index];
        Transform world = layer.transform;
        Mat4 world_matrix = layer.transform.to_mat4();

        if (layer.hierarchy_resolved) {
            if (!layer.parent_name.empty()) {
                m_resolved[index].has_parent = true;
                auto it = m_name_to_index.find(std::string_view(layer.parent_name));
                if (it == m_name_to_index.end()) {
                    m_resolved[index].parent_missing = true;
                } else if (it->second == index) {
                    m_resolved[index].cycle_detected = true;
                }
            }

            m_resolved[index].world_transform = world;
            m_resolved[index].world_matrix = world_matrix;
            m_state[index] = VisitState::Visited;
            return world;
        }

        if (!layer.parent_name.empty()) {
            m_resolved[index].has_parent = true;

            auto it = m_name_to_index.find(std::string_view(layer.parent_name));
            if (it == m_name_to_index.end()) {
                m_resolved[index].parent_missing = true;
            } else {
                const usize parent_index = it->second;

                if (parent_index == index) {
                    m_resolved[index].cycle_detected = true;
                } else {
                    Transform parent_world = resolve_one(parent_index);
                    const Mat4 parent_matrix = m_resolved[parent_index].world_matrix;
                    world = combine_transforms_simple(parent_world, layer.transform);
                    world_matrix = parent_matrix * layer.transform.to_mat4();
                }
            }
        }

        m_resolved[index].world_transform = world;
        m_resolved[index].world_matrix = world_matrix;
        m_state[index] = VisitState::Visited;
        return world;
    }

    const std::pmr::vector<Layer>& m_layers;
    std::pmr::vector<ResolvedLayer> m_resolved;
    std::unordered_map<std::string_view, usize> m_name_to_index;
    std::vector<VisitState> m_state;
};

} // namespace detail

inline std::pmr::vector<ResolvedLayer> resolve_layer_hierarchy(
    const std::pmr::vector<Layer>& layers,
    Frame frame,
    std::pmr::memory_resource* res
) {
    detail::LayerHierarchyResolver resolver(layers, res);
    return resolver.resolve_layers(frame);
}

inline ResolvedCamera resolve_camera_hierarchy(
    const std::pmr::vector<Layer>& layers,
    std::pmr::memory_resource* res,
    const Camera2_5DRuntime& input_camera
) {
    detail::LayerHierarchyResolver resolver(layers, res);
    return resolver.resolve_camera(input_camera);
}

[[deprecated("Use resolve_layer_hierarchy and resolve_camera_hierarchy separately")]]
inline std::pmr::vector<ResolvedLayer> resolve_layer_hierarchy(
    const std::pmr::vector<Layer>& layers,
    Frame frame,
    std::pmr::memory_resource* res,
    const Camera2_5DRuntime* input_camera,
    ResolvedCamera* out_camera
) {
    auto resolved = resolve_layer_hierarchy(layers, frame, res);
    if (input_camera && out_camera) {
        *out_camera = resolve_camera_hierarchy(layers, res, *input_camera);
    }
    return resolved;
}

} // namespace chronon3d
