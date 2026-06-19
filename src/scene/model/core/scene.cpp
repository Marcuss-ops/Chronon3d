// ============================================================================
// scene.cpp — Scene method definitions that access Camera2_5DRuntime
//
/// @file    scene.cpp
/// @brief   Definitions for Scene methods that require the complete
///          Camera2_5D type.  Separated from scene.hpp so that the header
///          only needs a forward declaration + CameraProjectionSource.
// ============================================================================

#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/math/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace chronon3d {

// ── Constructor / Destructor ────────────────────────────────────────────────

Scene::Scene(std::pmr::memory_resource* res)
    : m_nodes(res), m_layers(res), m_lights(rendering::LightContext::default_scene()),
      m_camera_2_5d(std::make_unique<Camera2_5DRuntime>()) {}

Scene::~Scene() = default;

// ── Clone ───────────────────────────────────────────────────────────────────

Scene Scene::clone() const {
    Scene s(m_nodes.get_allocator().resource());
    s.m_nodes = m_nodes;
    s.m_layers = m_layers;
    s.m_lights = m_lights;
    s.m_rim = m_rim;
    s.m_depth_grade = m_depth_grade;
    s.m_hierarchy_baked = m_hierarchy_baked;
    s.m_assets_root = m_assets_root;
    if (m_camera_2_5d) {
        s.m_camera_2_5d = std::make_unique<Camera2_5DRuntime>(*m_camera_2_5d);
    }
    return s;
}

// ── Camera accessors ────────────────────────────────────────────────────────

void Scene::set_camera_2_5d(Camera2_5DRuntime camera) {
    *m_camera_2_5d = std::move(camera);
}

const Camera2_5DRuntime& Scene::camera_2_5d() const {
    return *m_camera_2_5d;
}

CameraProjectionSource Scene::camera_projection_source() const {
    return CameraProjectionSource(*m_camera_2_5d);
}

// ── Hierarchy resolution ────────────────────────────────────────────────────

void Scene::resolve_hierarchy(Frame frame) {
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
    if (m_camera_2_5d->enabled) {
        if (!m_camera_2_5d->parent_name.empty()) {
            auto parent_world_opt = results.world_matrix(std::string(m_camera_2_5d->parent_name));
            if (parent_world_opt) {
                Mat4 local_cam_mat = glm::translate(Mat4(1.0f), m_camera_2_5d->position) *
                                     glm::toMat4(math::camera_rotation_quat(m_camera_2_5d->rotation));
                Mat4 world_cam_mat = (*parent_world_opt) * local_cam_mat;
                Transform world_cam_trans = from_mat4(world_cam_mat);
                m_camera_2_5d->position = world_cam_trans.position;
                m_camera_2_5d->rotation = math::camera_rotation_euler(world_cam_trans.rotation);
            }
        }

        if (!m_camera_2_5d->target_name.empty()) {
            auto target_world_opt = results.world_matrix(std::string(m_camera_2_5d->target_name));
            if (target_world_opt) {
                m_camera_2_5d->point_of_interest = Vec3((*target_world_opt)[3]);
                m_camera_2_5d->point_of_interest_enabled = true;
            }
        }
        m_camera_2_5d->hierarchy_baked = true;
    }

    m_hierarchy_baked = true;
}

} // namespace chronon3d
