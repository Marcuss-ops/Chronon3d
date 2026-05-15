#pragma once

#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/math/camera_pose.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/quat.hpp>
#include <unordered_map>
#include <vector>
#include <string>
#include <functional>

namespace chronon3d {

struct ResolvedLayer {
    const Layer* layer{nullptr};
    Transform world_transform{};
    usize insertion_index{0};
    bool has_parent{false};
    bool parent_missing{false};
    bool cycle_detected{false};
};

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

struct ResolvedCamera {
    Camera2_5D camera;
    Transform world_transform;
};

inline std::pmr::vector<ResolvedLayer> resolve_layer_hierarchy(
    const std::pmr::vector<Layer>& layers,
    Frame frame,
    std::pmr::memory_resource* res,
    const Camera2_5D* input_camera = nullptr,
    ResolvedCamera* out_camera = nullptr
) {
    std::pmr::vector<ResolvedLayer> resolved{res};
    resolved.resize(layers.size());

    std::unordered_map<std::string_view, usize> name_to_index;
    name_to_index.reserve(layers.size());

    for (usize i = 0; i < layers.size(); ++i) {
        name_to_index.emplace(std::string_view(layers[i].name), i);
        resolved[i].layer = &layers[i];
        resolved[i].world_transform = layers[i].transform;
        resolved[i].insertion_index = i;
    }

    enum class VisitState { Unvisited, Visiting, Visited };
    std::vector<VisitState> state(layers.size(), VisitState::Unvisited);

    std::function<Transform(usize)> resolve_one = [&](usize index) -> Transform {
        if (state[index] == VisitState::Visited) {
            return resolved[index].world_transform;
        }

        if (state[index] == VisitState::Visiting) {
            resolved[index].cycle_detected = true;
            state[index] = VisitState::Visited;
            resolved[index].world_transform = layers[index].transform;
            return resolved[index].world_transform;
        }

        state[index] = VisitState::Visiting;

        const Layer& layer = layers[index];
        Transform world = layer.transform;

        if (layer.hierarchy_resolved) {
            if (!layer.parent_name.empty()) {
                resolved[index].has_parent = true;
                auto it = name_to_index.find(std::string_view(layer.parent_name));
                if (it == name_to_index.end()) {
                    resolved[index].parent_missing = true;
                } else if (it->second == index) {
                    resolved[index].cycle_detected = true;
                }
            }

            resolved[index].world_transform = world;
            state[index] = VisitState::Visited;
            return world;
        }

        if (!layer.parent_name.empty()) {
            resolved[index].has_parent = true;

            auto it = name_to_index.find(std::string_view(layer.parent_name));
            if (it == name_to_index.end()) {
                resolved[index].parent_missing = true;
                world = layer.transform;
            } else {
                const usize parent_index = it->second;

                if (parent_index == index) {
                    resolved[index].cycle_detected = true;
                    world = layer.transform;
                } else {
                    Transform parent_world = resolve_one(parent_index);
                    world = combine_transforms_simple(parent_world, layer.transform);
                }
            }
        }

        resolved[index].world_transform = world;
        state[index] = VisitState::Visited;
        return world;
    };

    for (usize i = 0; i < layers.size(); ++i) {
        if (layers[i].active_at(frame)) {
            resolve_one(i);
        }
    }

    // Resolve Camera if provided
    if (input_camera && out_camera) {
        out_camera->camera = *input_camera;
        out_camera->world_transform.position = input_camera->position;
        out_camera->world_transform.rotation = math::camera_rotation_quat(input_camera->rotation);

        if (input_camera->hierarchy_baked) {
            return resolved;
        }

        if (!input_camera->parent_name.empty()) {
            auto it = name_to_index.find(std::string_view(input_camera->parent_name));
            if (it != name_to_index.end()) {
                Transform parent_world = resolve_one(it->second);
                out_camera->world_transform = combine_transforms_simple(parent_world, out_camera->world_transform);
                out_camera->camera.position = out_camera->world_transform.position;
            }
        }

        if (!input_camera->target_name.empty()) {
            auto it = name_to_index.find(std::string_view(input_camera->target_name));
            if (it != name_to_index.end()) {
                Transform target_world = resolve_one(it->second);
                out_camera->camera.point_of_interest = target_world.position;
                out_camera->camera.point_of_interest_enabled = true;
            }
        }
    }

    return resolved;
}

} // namespace chronon3d
