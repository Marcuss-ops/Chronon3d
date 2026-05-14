#include <chronon3d/chronon3d.hpp>
#include <cmath>

using namespace chronon3d;
Composition CameraOrbitProof() {
    return composition({
        .name = "CameraOrbitProof",
        .width = 1280,
        .height = 720,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // 1. Center Rig (Null)
        s.null_layer("center_rig", [&](LayerBuilder& l) {
            l.position({0, 0, 0})
             .rotate({0, 
                      keyframes<f32>({
                          {0, 0.0f},
                          {120, 360.0f}
                      }).value(ctx.frame), 
                      0});
        });

        // 2. Parent Camera to Rig
        s.camera_2_5d({
            .enabled = true,
            .position = {0, 0, -1200}, // 1200 units away from center
            .zoom = 1000.0f
        });
        s.camera_parent("center_rig");

        // 3. Some objects in the scene to observe orbit
        s.rect("bg", {.size={2000, 2000}, .color=Color{0.05f, 0.05f, 0.1f, 1}});

        // Grid of cubes/rects
        for (int x = -2; x <= 2; ++x) {
            for (int z = -2; z <= 2; ++z) {
                std::string name = "box_" + std::to_string(x) + "_" + std::to_string(z);
                s.layer(name, [=](LayerBuilder& l) {
                    l.enable_3d()
                     .position({x * 300.0f, 0, z * 300.0f});
                    
                    l.rounded_rect("body", {
                        .size = {100, 100},
                        .radius = 10,
                        .color = Color{0.2f, 0.5f, 0.8f, 1}
                    });
                });
            }
        }

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("CameraOrbitProof", CameraOrbitProof)
