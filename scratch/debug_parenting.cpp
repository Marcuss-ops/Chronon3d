#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/layer/layer_hierarchy.hpp>
#include <iostream>

using namespace chronon3d;

int main() {
    std::pmr::monotonic_buffer_resource res;
    SceneBuilder s(&res);

    s.null_layer("rig", [](LayerBuilder& l) {
        l.position({100, 0, 0});
    });

    s.layer("child", [](LayerBuilder& l) {
        l.parent("rig")
         .position({50, 0, 0});
    });

    auto scene = s.build();
    std::cout << "Scene layers: " << scene.layers().size() << std::endl;
    for (const auto& l : scene.layers()) {
        std::cout << "Layer: " << l.name << " pos: " << l.transform.position.x << std::endl;
    }

    auto resolved = resolve_layer_hierarchy(scene.layers(), 0, scene.resource());
    std::cout << "Resolved child pos: " << resolved[1].world_transform.position.x << std::endl;

    return 0;
}
