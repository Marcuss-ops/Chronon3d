#pragma once

#include <chronon3d/scene/model/render/render_node.hpp>
#include <filesystem>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/model/layer/layer_hierarchy.hpp>
#include <chronon3d/rendering/light_context.hpp>
#include <chronon3d/rendering/lighting_rig.hpp>
#include <chronon3d/rendering/depth_grade.hpp>
#include <chronon3d/scene/model/core/transform_resolver.hpp>
#include <vector>
#include <memory_resource>

namespace chronon3d {

class Scene {
public:
    explicit Scene(std::pmr::memory_resource* res = std::pmr::get_default_resource())
        : m_nodes(res), m_layers(res), m_lights(rendering::LightContext::default_scene()) {}

    void add_node(RenderNode node) {
        m_nodes.push_back(std::move(node));
    }

    void add_layer(Layer layer) {
        m_layers.push_back(std::move(layer));
    }

    [[nodiscard]] RenderNode& last_node() { return m_nodes.back(); }

    [[nodiscard]] const std::pmr::vector<RenderNode>& nodes() const { return m_nodes; }
    [[nodiscard]] std::pmr::vector<RenderNode>& nodes() { return m_nodes; }
    [[nodiscard]] const std::pmr::vector<Layer>& layers() const { return m_layers; }
    [[nodiscard]] std::pmr::vector<Layer>& layers() { return m_layers; }

    void set_camera_2_5d(Camera2_5DRuntime camera) { m_camera_2_5d = camera; }
    [[nodiscard]] const Camera2_5DRuntime& camera_2_5d() const { return m_camera_2_5d; }

    [[nodiscard]] rendering::LightContext& light_context() { return m_lights; }
    [[nodiscard]] const rendering::LightContext& light_context() const { return m_lights; }

    [[nodiscard]] rendering::RimLight& rim_light() { return m_rim; }
    [[nodiscard]] const rendering::RimLight& rim_light() const { return m_rim; }
    void set_rim_light(const rendering::RimLight& rim) { m_rim = rim; }

    [[nodiscard]] rendering::DepthGrade& depth_grade() { return m_depth_grade; }
    [[nodiscard]] const rendering::DepthGrade& depth_grade() const { return m_depth_grade; }
    void set_depth_grade(const rendering::DepthGrade& grade) { m_depth_grade = grade; }

    void resolve_hierarchy(Frame frame) {
        if (m_hierarchy_baked) return;

        // 1. Build SceneTransformRegistry
        SceneTransformRegistry registry;
        
        // Add all layers (both normal and null)
        for (const auto& layer : m_layers) {
            Transform3D t3d;
            t3d.position = layer.transform.position;
            t3d.rotation = glm::degrees(glm::eulerAngles(layer.transform.rotation));
            t3d.scale = layer.transform.scale;
            t3d.anchor = layer.transform.anchor;
            t3d.parent_name = std::string(layer.parent_name);
            t3d.inherits_position = true;
            t3d.inherits_rotation = true;
            t3d.inherits_scale = true;

            bool renderable = (layer.kind != LayerKind::Null);
            registry.add_node(std::string(layer.name), t3d, renderable);
        }

        // Resolve transforms
        auto results = registry.resolve_all();

        // 2. Update layers with resolved world transforms
        for (size_t i = 0; i < m_layers.size(); ++i) {
            auto& layer = m_layers[i];
            auto world_mat_opt = results.world_matrix(std::string(layer.name));
            if (world_mat_opt) {
                f32 opacity = layer.transform.opacity;
                // Walk the parent chain to accumulate inherited opacity.
                // Guard against cycles (e.g., self-parent or A→B→A) by limiting
                // the chain depth. Real parent chains are never deeper than ~10;
                // a limit of 64 catches any cycle without dangling pointers or
                // extra allocations.
                constexpr int kMaxParentDepth = 64;
                std::string current_parent = std::string(layer.parent_name);
                int safety = 0;
                while (!current_parent.empty() && safety < kMaxParentDepth) {
                    bool found = false;
                    for (const auto& p_layer : m_layers) {
                        if (std::string_view(p_layer.name) == current_parent) {
                            opacity *= p_layer.transform.opacity;
                            current_parent = std::string(p_layer.parent_name);
                            found = true;
                            break;
                        }
                    }
                    if (!found) break;
                    ++safety;
                }
                
                Vec3 original_anchor = layer.transform.anchor;
                layer.transform = from_mat4(*world_mat_opt, opacity);
                layer.transform.anchor = original_anchor;
                layer.hierarchy_resolved = true;
            }
        }

        // 3. Resolve camera
        if (m_camera_2_5d.enabled) {
            if (!m_camera_2_5d.parent_name.empty()) {
                auto parent_world_opt = results.world_matrix(std::string(m_camera_2_5d.parent_name));
                if (parent_world_opt) {
                    Mat4 local_cam_mat = glm::translate(Mat4(1.0f), m_camera_2_5d.position) *
                                         glm::toMat4(math::camera_rotation_quat(m_camera_2_5d.rotation));
                    Mat4 world_cam_mat = (*parent_world_opt) * local_cam_mat;
                    Transform world_cam_trans = from_mat4(world_cam_mat);
                    m_camera_2_5d.position = world_cam_trans.position;
                    m_camera_2_5d.rotation = math::camera_rotation_euler(world_cam_trans.rotation);
                }
            }

            if (!m_camera_2_5d.target_name.empty()) {
                auto target_world_opt = results.world_matrix(std::string(m_camera_2_5d.target_name));
                if (target_world_opt) {
                    m_camera_2_5d.point_of_interest = Vec3((*target_world_opt)[3]);
                    m_camera_2_5d.point_of_interest_enabled = true;
                }
            }
            m_camera_2_5d.hierarchy_baked = true;
        }

        m_hierarchy_baked = true;
    }

    [[nodiscard]] bool hierarchy_baked() const { return m_hierarchy_baked; }
    [[nodiscard]] bool hierarchy_resolved() const { return m_hierarchy_baked; }

    [[nodiscard]] std::pmr::memory_resource* resource() const { return m_nodes.get_allocator().resource(); }

    [[nodiscard]] const std::filesystem::path& assets_root() const { return m_assets_root; }
    void set_assets_root(std::filesystem::path root) { m_assets_root = std::move(root); }

private:
    std::pmr::vector<RenderNode> m_nodes;
    std::pmr::vector<Layer> m_layers;
    Camera2_5DRuntime m_camera_2_5d{};
    rendering::LightContext m_lights{};
    rendering::RimLight m_rim{};
    rendering::DepthGrade m_depth_grade{};
    bool m_hierarchy_baked{false};
    std::filesystem::path m_assets_root;
};

} // namespace chronon3d
