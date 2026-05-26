#pragma once

#include <chronon3d/scene/builders/layer_builder.hpp>

namespace chronon3d::presets {

struct ParallaxLayer {
    f32 speed{1.0f};
    f32 z{0.0f};

    ParallaxLayer() = default;
    ParallaxLayer(f32 depth, f32 s) : speed(s), z(depth) {}

    ParallaxLayer& set_depth(f32 val, f32 s) {
        z = val;
        speed = s;
        return *this;
    }

    ParallaxLayer& set_speed(f32 s) {
        speed = s;
        return *this;
    }

    ParallaxLayer& set_z(f32 val) {
        z = val;
        return *this;
    }

    void apply(LayerBuilder& l, f32 cam_x, Vec3 pos = {0.0f, 0.0f, 0.0f}) const {
        l.enable_3d().position({pos.x + cam_x * speed, pos.y, z});
    }
};

} // namespace chronon3d::presets
