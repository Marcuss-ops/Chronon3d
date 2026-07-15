// content/grid/grid_showcase.cpp
// GridColorShowcase — overlapping colorful grids with different spacing, offsets, and colors
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/text/text_definition.hpp>  // F2.D — canonical DTO

namespace chronon3d::content::grid {

namespace {

constexpr f32 W = 1920.0f;
constexpr f32 H = 1080.0f;

} // namespace

Composition grid_color_showcase() {
    return composition({.name = "GridColorShowcase", .width = 1920, .height = 1080, .duration = 1},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // ── Deep dark base ──
        s.layer("bg", [](auto& l) {
            l.fill({0.015f, 0.018f, 0.030f, 1.0f});
        });

        // ── Layer 1: Electric Blue — wide grid, major every 4 ──
        s.layer("grid_blue", [](auto& l) {
            l.grid_background("gb", {
                .size = {W, H},
                .offset = {0.0f, 0.0f},
                .bg_color = {0.0f, 0.0f, 0.0f, 0.0f},
                .grid_color = {0.18f, 0.65f, 1.0f, 0.18f},
                .spacing = 160.0f,
                .minor_thickness = 1.0f,
                .major_thickness = 3.0f,
                .major_every = 4,
                .centered = true
            });
        });

        // ── Layer 2: Hot Pink / Magenta — tighter, offset down-right ──
        s.layer("grid_pink", [](auto& l) {
            l.grid_background("gp", {
                .size = {W, H},
                .offset = {80.0f, 80.0f},
                .bg_color = {0.0f, 0.0f, 0.0f, 0.0f},
                .grid_color = {1.0f, 0.22f, 0.65f, 0.14f},
                .spacing = 100.0f,
                .minor_thickness = 0.8f,
                .major_thickness = 2.5f,
                .major_every = 3,
                .centered = true
            });
        });

        // ── Layer 3: Gold / Amber — fine grid, different offset ──
        s.layer("grid_gold", [](auto& l) {
            l.grid_background("gg", {
                .size = {W, H},
                .offset = {-120.0f, 60.0f},
                .bg_color = {0.0f, 0.0f, 0.0f, 0.0f},
                .grid_color = {1.0f, 0.82f, 0.28f, 0.11f},
                .spacing = 60.0f,
                .minor_thickness = 0.6f,
                .major_thickness = 2.0f,
                .major_every = 5,
                .centered = true
            });
        });

        // ── Layer 4: Emerald Green — wide, major every 2 ──
        s.layer("grid_green", [](auto& l) {
            l.grid_background("ggr", {
                .size = {W, H},
                .offset = {160.0f, -90.0f},
                .bg_color = {0.0f, 0.0f, 0.0f, 0.0f},
                .grid_color = {0.20f, 0.90f, 0.60f, 0.10f},
                .spacing = 200.0f,
                .minor_thickness = 1.2f,
                .major_thickness = 3.5f,
                .major_every = 2,
                .centered = true
            });
        });

        // ── Layer 5: Violet / Purple — diagonal-like through tight offset ──
        s.layer("grid_violet", [](auto& l) {
            l.grid_background("gv", {
                .size = {W, H},
                .offset = {-80.0f, -160.0f},
                .bg_color = {0.0f, 0.0f, 0.0f, 0.0f},
                .grid_color = {0.70f, 0.35f, 1.0f, 0.09f},
                .spacing = 80.0f,
                .minor_thickness = 0.7f,
                .major_thickness = 2.2f,
                .major_every = 4,
                .centered = true
            });
        });

        // ── Soft center glow to anchor the composition ──
        s.layer("center_glow", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.circle("glow", {
                .radius = 380.0f,
                .color = {0.25f, 0.52f, 1.0f, 0.04f}
            });
        });

        // ── Title: "GRID SHOWCASE" ──
        s.layer("title", [](auto& l) {
            l.pin_to(Anchor::TopCenter, 80.0f);
            l.text("title", TextDefinition{
                .content = {.value = "GRID SHOWCASE"},
                .style = {.font = {.font_family = "Inter", .font_weight = 800, .font_size = 56.0f},
                          .color = Color{1.0f, 1.0f, 1.0f, 0.90f}},
                .frame = {.size = {900, 70}, .align = TextAlign::Center, .line_height = 1.2f, .tracking = 12.0f},
            });
        });

        // ── Subtitle with color key ──
        s.layer("subtitle", [](auto& l) {
            l.pin_to(Anchor::TopCenter, 150.0f);
            l.text("sub", TextDefinition{
                .content = {.value = "BLUE  ·  PINK  ·  GOLD  ·  GREEN  ·  VIOLET"},
                .style = {.font = {.font_family = "Inter", .font_weight = 600, .font_size = 24.0f},
                          .color = Color{0.65f, 0.70f, 0.85f, 0.70f}},
                .frame = {.size = {1000, 40}, .align = TextAlign::Center, .line_height = 1.2f, .tracking = 6.0f},
            });
        });

        // ── Bottom info line ──
        s.layer("footer", [](auto& l) {
            l.pin_to(Anchor::BottomCenter, 50.0f);
            l.text("foot", TextDefinition{
                .content = {.value = "5 overlapping grids · sparse procedural kernel · 1920×1080"},
                .style = {.font = {.font_family = "Inter", .font_weight = 400, .font_size = 16.0f},
                          .color = Color{0.50f, 0.55f, 0.70f, 0.50f}},
                .frame = {.size = {800, 30}, .align = TextAlign::Center, .line_height = 1.2f, .tracking = 3.0f},
            });
        });

        return s.build();
    });
}

// ── Per-domain registration ──────────────────────────────────────────────────
void register_grid_compositions(CompositionRegistry& registry) {
    registry.add(CompositionDescriptor{.id = "GridColorShowcase", .factory = [](const CompositionProps&) { return grid_color_showcase(); }});
}

} // namespace chronon3d::content::grid
