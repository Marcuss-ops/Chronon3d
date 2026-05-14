#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/presets/studio_presets.hpp>

using namespace chronon3d;

/**
 * FakeExtrudedTextProof
 *
 * Tests the fake-extrusion text technique with three depth levels
 * and a combined scene with a panel behind.
 */
Composition FakeExtrudedTextProof() {
    return composition({
        .name     = "FakeExtrudedTextProof",
        .width    = 1280,
        .height   = 720,
        .duration = 60
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer("bg", [](LayerBuilder& l) {
            l.rect("fill", {.size = {4000, 4000}, .color = Color{0.07f, 0.07f, 0.10f, 1.0f}});
        });

        // Background panel
        s.layer("panel", [](LayerBuilder& l) {
            l.rounded_rect("p", {
                .size   = {900, 220},
                .radius = 18,
                .color  = Color{0.14f, 0.14f, 0.20f, 1.0f},
                .pos    = {640, 360, 0}
            });
        });

        // Three text instances at increasing depths
        // depth 0 — flat
        s.layer("label_d0", [](LayerBuilder& l) {
            l.text("t", {
                .content = "depth 0",
                .style   = {.font_path = "assets/fonts/Inter-Regular.ttf",
                            .size = 20, .color = Color{0.5f,0.5f,0.5f,1}, .align = TextAlign::Center},
                .pos     = {280, 240, 0}
            });
        });
        s.layer("text_d0", [](LayerBuilder& l) {
            l.text("t", {
                .content = "TILT",
                .style   = {.font_path = "assets/fonts/Inter-Bold.ttf",
                            .size = 100, .color = Color{0.95f,0.94f,0.90f,1}, .align = TextAlign::Center},
                .pos     = {280, 360, 0}
            });
        });

        // depth 8
        s.layer("label_d8", [](LayerBuilder& l) {
            l.text("t", {
                .content = "depth 18",
                .style   = {.font_path = "assets/fonts/Inter-Regular.ttf",
                            .size = 20, .color = Color{0.5f,0.5f,0.5f,1}, .align = TextAlign::Center},
                .pos     = {640, 240, 0}
            });
        });
        presets::Studio::fake_extruded_text(s, "text_d8", {
            .text              = "TILT",
            .pos               = {640, 360, 0},
            .font_size         = 100,
            .depth             = 14,         // trace 14px in direction (1.0, 0.5)
            .extrude_dir       = {1.0f, 0.5f},
            .extrude_z_step    = 0.0f,
            .front_color       = Color{0.96f, 0.95f, 0.91f, 1.0f},
            .side_color        = Color{0.52f, 0.46f, 0.36f, 1.0f},
            .side_fade         = 0.15f,
            .align             = TextAlign::Center,
            .highlight_opacity = 0.10f
        });

        // depth 20
        s.layer("label_d20", [](LayerBuilder& l) {
            l.text("t", {
                .content = "depth 28",
                .style   = {.font_path = "assets/fonts/Inter-Regular.ttf",
                            .size = 20, .color = Color{0.5f,0.5f,0.5f,1}, .align = TextAlign::Center},
                .pos     = {1000, 240, 0}
            });
        });
        presets::Studio::fake_extruded_text(s, "text_d20", {
            .text              = "TILT",
            .pos               = {1000, 360, 0},
            .font_size         = 100,
            .depth             = 24,         // trace 24px in direction (1.0, 0.5)
            .extrude_dir       = {1.0f, 0.5f},
            .extrude_z_step    = 0.0f,
            .front_color       = Color{0.96f, 0.95f, 0.91f, 1.0f},
            .side_color        = Color{0.48f, 0.40f, 0.30f, 1.0f},
            .side_fade         = 0.10f,
            .align             = TextAlign::Center,
            .highlight_opacity = 0.10f
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("FakeExtrudedTextProof", FakeExtrudedTextProof)
