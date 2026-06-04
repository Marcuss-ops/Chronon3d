#include <chronon3d/scene/model/transform_resolver.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace chronon3d {

namespace {
enum class VisitState { Unvisited, Visiting, Visited };
}

std::unordered_map<std::string, ResolvedTransform3D> TransformResolver3D::resolve(
    const std::vector<InputNode>& nodes
) {
    std::unordered_map<std::string, ResolvedTransform3D> results;
    std::unordered_map<std::string, const InputNode*> name_to_node;
    std::unordered_map<std::string, VisitState> states;

    results.reserve(nodes.size());
    name_to_node.reserve(nodes.size());
    states.reserve(nodes.size());

    for (const auto& node : nodes) {
        name_to_node[node.name] = &node;
        states[node.name] = VisitState::Unvisited;
        
        ResolvedTransform3D resolved;
        resolved.local = node.transform;
        resolved.world_matrix = node.transform.to_mat4();
        results[node.name] = resolved;
    }

    // Helper DFS function to resolve one node
    auto resolve_one = [&](auto& self, const std::string& name) -> Mat4 {
        auto state_it = states.find(name);
        if (state_it == states.end()) {
            return Mat4(1.0f);
        }

        if (state_it->second == VisitState::Visited) {
            return results[name].world_matrix;
        }

        if (state_it->second == VisitState::Visiting) {
            results[name].cycle_detected = true;
            states[name] = VisitState::Visited;
            // Fallback: use local matrix if cycle is detected
            results[name].world_matrix = results[name].local.to_mat4();
            return results[name].world_matrix;
        }

        states[name] = VisitState::Visiting;

        const InputNode* node = name_to_node[name];
        Mat4 local_mat = node->transform.to_mat4();
        Mat4 world_mat = local_mat;

        if (!node->transform.parent_name.empty()) {
            const std::string& parent = node->transform.parent_name;
            if (parent == name) {
                results[name].cycle_detected = true;
                results[name].world_matrix = local_mat;
            } else if (name_to_node.find(parent) == name_to_node.end()) {
                results[name].parent_missing = true;
                results[name].world_matrix = local_mat;
            } else {
                Mat4 parent_world = self(self, parent);
                
                // If the parent is in a cycle, we still want to proceed but mark child cycle status if parent is cycle-affected
                if (results[parent].cycle_detected) {
                    results[name].cycle_detected = true;
                }

                // Selective inheritance via decomposition
                Vec3 parent_scale{1.0f};
                Quat parent_rot{1.0f, 0.0f, 0.0f, 0.0f};
                Vec3 parent_trans{0.0f};
                Vec3 parent_skew{0.0f};
                glm::vec4 parent_persp{0.0f};

                if (glm::decompose(parent_world, parent_scale, parent_rot, parent_trans, parent_skew, parent_persp)) {
                    Vec3 filtered_trans = node->transform.inherits_position ? parent_trans : Vec3(0.0f);
                    Quat filtered_rot = node->transform.inherits_rotation ? parent_rot : Quat(1.0f, 0.0f, 0.0f, 0.0f);
                    Vec3 filtered_scale = node->transform.inherits_scale ? parent_scale : Vec3(1.0f);

                    Mat4 parent_filtered = glm::translate(Mat4(1.0f), filtered_trans) *
                                           glm::toMat4(filtered_rot) *
                                           glm::scale(Mat4(1.0f), filtered_scale);
                    world_mat = parent_filtered * local_mat;
                } else {
                    world_mat = parent_world * local_mat;
                }
            }
        }

        results[name].world_matrix = world_mat;
        states[name] = VisitState::Visited;
        return world_mat;
    };

    for (const auto& node : nodes) {
        resolve_one(resolve_one, node.name);
    }

    return results;
}

} // namespace chronon3d
