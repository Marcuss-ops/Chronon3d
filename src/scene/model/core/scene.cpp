// ============================================================================
// scene.cpp — Scene method definitions that access Camera2_5DRuntime
//
/// @file    scene.cpp
/// @brief   Definitions for Scene methods that require the complete
///          Camera2_5D type.  Separated from scene.hpp so that the header
///          only needs a forward declaration + CameraProjectionSource.
// ============================================================================

#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/core/hierarchy_resolver.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/math/transform.hpp>

namespace chronon3d {

// ── Constructor / Destructor ────────────────────────────────────────────────

Scene::Scene(std::pmr::memory_resource* res)
    : m_nodes(res), m_layers(res), m_lights(rendering::LightContext::default_scene()) {}

Scene::~Scene() = default;

// ── Clone ───────────────────────────────────────────────────────────────────

Scene Scene::clone() const {
    Scene s(m_nodes.get_allocator().resource());
    s.m_nodes = m_nodes;
    s.m_layers = m_layers;
    s.m_clip_transitions = m_clip_transitions;
    s.m_lights = m_lights;
    s.m_rim = m_rim;
    s.m_depth_grade = m_depth_grade;
    s.m_hierarchy_baked = m_hierarchy_baked;
    s.m_assets_root = m_assets_root;
    s.m_manifest = m_manifest;
    s.m_camera_2_5d = m_camera_2_5d;
    return s;
}

// ── Camera accessors ────────────────────────────────────────────────────────

void Scene::set_camera_2_5d(Camera2_5DRuntime camera) {
    m_camera_2_5d = std::move(camera);
}

const Camera2_5DRuntime& Scene::camera_2_5d() const {
    return m_camera_2_5d;
}

CameraProjectionSource Scene::camera_projection_source() const {
    return CameraProjectionSource(m_camera_2_5d);
}

// ── Hierarchy resolution (uses unified HierarchyResolver) ───────────────────

void Scene::resolve_hierarchy(Frame frame) {
    if (m_hierarchy_baked) return;

    // Most scenes do not contain a layer or camera hierarchy.  Keep this
    // common path allocation-free: constructing the general resolver for an
    // empty relation would otherwise allocate several temporary containers
    // once per evaluated frame.
    bool has_layer_hierarchy = false;
    for (const auto& layer : m_layers) {
        if (!layer.parent_name.empty()) {
            has_layer_hierarchy = true;
            break;
        }
    }
    if (!has_layer_hierarchy) {
        for (auto& layer : m_layers) layer.hierarchy_resolved = true;
        m_hierarchy_baked = true;
        return;
    }

    // 1. Build name→index map
    auto name_to_index = build_name_index(m_layers);

    // 2. Build HierarchyNodeView from layers
    std::vector<HierarchyNodeView> nodes;
    nodes.reserve(m_layers.size());

    for (std::size_t i = 0; i < m_layers.size(); ++i) {
        const auto& layer = m_layers[i];
        HierarchyNodeView view;
        view.id = i;
        view.local_matrix = layer.transform.to_mat4();
        view.local_opacity = layer.transform.opacity;
        view.inherits_position = true;
        view.inherits_rotation = true;
        view.inherits_scale = true;

        if (!layer.parent_name.empty()) {
            auto it = name_to_index.find(std::string_view(layer.parent_name));
            if (it != name_to_index.end()) {
                view.parent = it->second;
            }
        }

        nodes.push_back(std::move(view));
    }

    // 3. Resolve hierarchy
    HierarchyResolver resolver(std::move(nodes));
    auto results = resolver.resolve();

    // 4. Apply results back to layers
    for (std::size_t i = 0; i < m_layers.size(); ++i) {
        auto& layer = m_layers[i];
        const auto& resolved = results[i];

        Vec3 original_anchor = layer.transform.anchor;
        layer.transform = from_mat4(resolved.world_matrix, resolved.world_opacity);
        layer.transform.anchor = original_anchor;
        layer.hierarchy_resolved = true;
    }

    m_hierarchy_baked = true;
}

} // namespace chronon3d
