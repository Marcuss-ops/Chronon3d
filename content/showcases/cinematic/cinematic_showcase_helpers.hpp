#pragma once
// Shared helpers for cinematic showcase compositions.
// dark_bg is used by all three; waypoint_markers by catmull_rom_showcase.

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/effects/effect_params.hpp>

#include <string>

namespace chronon3d::content::anims {

// Dark cinematic gradient background (used by all 3 showcase comps).
inline void dark_bg(SceneBuilder& s) {
    s.layer("bg", [](LayerBuilder& l) {
        l.rect("bg", {
            .size  = {1920.0f, 1080.0f},
            .color = {0.06f, 0.06f, 0.10f, 1.0f},
            .pos   = {0.0f, 0.0f, 0.0f},
            .fill  = FillStyle::linear({0,0}, {0,1}, {
                {0.0f, {0.05f, 0.05f, 0.10f, 1.0f}},
                {1.0f, {0.10f, 0.07f, 0.18f, 1.0f}},
            })
        });
    });
}

// Glowing waypoint markers on the Catmull-Rom camera path.
inline void waypoint_markers(SceneBuilder& s, std::initializer_list<Vec3> waypoints) {
    int i = 0;
    for (Vec3 p : waypoints) {
        s.layer("wp_" + std::to_string(i), [p](LayerBuilder& l) {
            l.position(p).glow(GlowParams{.radius = 24.0f, .intensity = 0.8f, .color = {1.0f, 0.85f, 0.30f, 1.0f}});
            l.circle("dot", { .radius = 14.0f, .color = {1.0f, 0.85f, 0.30f, 1.0f} });
        });
        ++i;
    }
}

} // namespace chronon3d::content::anims
