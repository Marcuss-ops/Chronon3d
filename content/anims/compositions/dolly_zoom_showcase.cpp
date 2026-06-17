// content/anims/compositions/dolly_zoom_showcase.cpp
// DollyZoomShowcase — dolly + zoom counter-motion (Vertigo effect).
//
// Render:
//   chronon3d_cli render DollyZoomShowcase --frame 30 -o output/dolly.png

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/animation/easing/easing.hpp>

#include "cinematic_showcase_helpers.hpp"

#include <string>
#include <cmath>

namespace chronon3d::content::anims {

Composition dolly_zoom_showcase() {
    return composition({
        .name     = "DollyZoomShowcase",
        .width    = 1280,
        .height   = 720,
        .duration = 90,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        dark_bg(s);

        // Asymmetric far background grid — visibly squishes during dolly-zoom.
        s.layer("grid_far", [](LayerBuilder& l) {
            l.position({-300.0f, 0.0f, 1500.0f}).enable_3d().opacity(0.7f);
            l.grid_background("grid_bg", {
                .size = {3000.0f, 1800.0f},
                .grid_color = {0.40f, 0.60f, 1.00f, 1.0f},
                .spacing = 100.0f,
                .minor_thickness = 1.0f,
                .major_thickness = 2.0f,
                .major_every = 4,
                .centered = true,
            });
        });

        // Floor grid: another parallax reference. Visible in lower half.
        s.layer("floor", [](LayerBuilder& l) {
            l.position({0.0f, 350.0f, 0.0f}).enable_3d().opacity(0.9f);
            l.grid_background("floor_g", {
                .size = {5000.0f, 5000.0f},
                .grid_color = {0.90f, 0.45f, 0.20f, 1.0f},
                .spacing = 120.0f,
                .minor_thickness = 1.0f,
                .major_thickness = 3.0f,
                .major_every = 2,
                .centered = true,
            });
        });

        // A single bright vertical pillar at the side — non-symmetric, makes
        // the dolly-zoom compression visible.
        s.layer("pillar", [](LayerBuilder& l) {
            l.position({500.0f, 0.0f, 0.0f}).enable_3d().opacity(0.95f);
            l.rect("p", {
                .size  = {40.0f, 800.0f},
                .color = {1.0f, 0.85f, 0.30f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f},
            });
        });
        s.layer("pillar2", [](LayerBuilder& l) {
            l.position({-500.0f, 0.0f, 0.0f}).enable_3d().opacity(0.95f);
            l.rect("p", {
                .size  = {40.0f, 800.0f},
                .color = {0.30f, 0.85f, 1.0f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f},
            });
        });

        // Subject in the center, plus background grid to make the perspective
        // shift obvious.
        s.layer("subject", [](LayerBuilder& l) {
            l.position({0.0f, 0.0f, 0.0f}).glow(GlowParams{.radius = 60.0f, .intensity = 0.9f, .color = {1.0f, 0.5f, 0.7f, 1.0f}});
            l.circle("dot", { .radius = 36.0f, .color = {1.0f, 0.5f, 0.7f, 1.0f} });
        });

        // Dolly-zoom: as we move the camera closer, we widen the FOV so the
        // subject stays the same size, but the background squishes.
        const f32 p = ctx.progress();
        const f32 from_z = -1400.0f;
        const f32 to_z   =  -600.0f;
        const f32 from_zoom = 1400.0f;
        const f32 to_zoom   =  600.0f;
        const f32 z_eased = from_z + (to_z - from_z) * glm::cubicEaseInOut(p);
        const f32 zoom_eased = from_zoom + (to_zoom - from_zoom) * glm::cubicEaseInOut(p);

        s.camera().enable(true)
                  .position({0.0f, 0.0f, z_eased})
                  .zoom(zoom_eased)
                  .point_of_interest({0.0f, 0.0f, 0.0f});

        s.layer("hud", [ctx](LayerBuilder& l) {
            l.position({-560.0f, 320.0f, 0.0f});
            l.text("frame_label", {
                .text = "Dolly Zoom: t = " + std::to_string(static_cast<int>(ctx.frame)),
                .size = {1100, 40},
                .font_size = 18.0f,
                .color = {0.75f, 0.78f, 0.95f, 1.0f},
                .align = TextAlign::Left,
            });
        });

        return s.build();
    });
}

} // namespace chronon3d::content::anims
