#pragma once

#include <chronon3d/chronon3d.hpp>

namespace chronon3d::test {

// Single 3D layer at given world position with a solid-color rect.
// Camera at Z=-1000, POI at origin.
inline Composition single_layer_3d(Color color, Vec3 position, Vec2 size = {80, 80},
                                    int W = 640, int H = 360) {
    return composition({
        .name = "SingleLayer3D", .width = W, .height = H, .duration = 1
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().enable(true).position({0, 0, -1000}).zoom(1000).look_at({0, 0, 0});
        s.layer("card", [=](LayerBuilder& l) {
            l.enable_3d().position(position);
            l.rect("fill", {.size = size, .color = color, .pos = {0, 0, 0}});
        });
        return s.build();
    });
}

// Single 3D layer with rotation applied.
inline Composition single_layer_3d_rotated(Color color, Vec3 position, Vec3 rotation_deg,
                                            Vec2 size = {120, 120}, int W = 640, int H = 360) {
    return composition({
        .name = "SingleLayer3DRotated", .width = W, .height = H, .duration = 1
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().enable(true).position({0, 0, -1000}).zoom(1000).look_at({0, 0, 0});
        s.layer("card", [=](LayerBuilder& l) {
            l.enable_3d().position(position).rotate(rotation_deg);
            l.rect("fill", {.size = size, .color = color, .pos = {0, 0, 0}});
        });
        return s.build();
    });
}

} // namespace chronon3d::test
