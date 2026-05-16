#include <chronon3d/scene/scene.hpp>
#include <chronon3d/scene/layer/layer_hierarchy.hpp>

namespace chronon3d {

void Scene::resolve_hierarchy(Frame frame) {
    if (m_hierarchy_baked) return;

    detail::LayerHierarchyResolver resolver(m_layers, resource());
    auto resolved_cam = resolver.resolve_camera(m_camera_2_5d);
    auto resolved_layers = resolver.resolve_layers(frame);

    for (usize i = 0; i < resolved_layers.size(); ++i) {
        if (resolved_layers[i].layer) {
            m_layers[i].transform = resolved_layers[i].world_transform;
            m_layers[i].hierarchy_resolved = true;
        }
    }

    if (m_camera_2_5d.enabled) {
        m_camera_2_5d = resolved_cam.camera;
        m_camera_2_5d.hierarchy_baked = true;
    }

    m_hierarchy_baked = true;
}

} // namespace chronon3d
