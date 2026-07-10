// ═══════════════════════════════════════════════════════════════════════════
// Chronon3D — Getting Started (~30 lines)
//
// Full Project → Composition → Text → Render flow.
// This is the Remotion-like simplicity vision for Chronon3D.
//
// Build & run:
//   cmake --build build --target getting_started
//   cd build/chronon/linux-ci && cp -r ../../assets/fonts .
//   ./examples/getting_started/getting_started
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/project.hpp>

#include <cstdio>

namespace c3d = chronon3d;

int main() {
    // 1. Project — single entry point (Remotion's registerRoot() equivalent)
    c3d::Project project;
    project.name        = "Hello Chronon3D";
    project.default_width  = 1280;
    project.default_height = 720;
    project.assets_root    = "assets";

    // 2. Register a composition — scene lambda, ~15 lines
    project.composition("TitleCard", {.duration = c3d::Frame{1}},
        [](const c3d::FrameContext& ctx) -> c3d::Scene {
            c3d::SceneBuilder s(ctx);
            if (ctx.font_engine) s.font_engine(ctx.font_engine);

            s.layer("bg", [](c3d::LayerBuilder& l) {
                l.fill(c3d::Color{0.1f, 0.1f, 0.15f, 1.0f});
            });

            s.layer("title", [](c3d::LayerBuilder& l) {
                l.text_run("t", c3d::TextRunParams{
                    .text = c3d::TextSpec{
                        .content = {.value = "Hello, Chronon3D!"},
                        .font = {.font_path = "fonts/Inter-Bold.ttf",
                                 .font_family = "Inter",
                                 .font_weight = 700,
                                 .font_size = 72.0f},
                        .layout = {.box = {1280.0f, 720.0f},
                                   .align = c3d::TextAlign::Center,
                                   .vertical_align = c3d::VerticalAlign::Middle},
                        .appearance = {.color = c3d::Color::white()},
                        .position = {640.0f, 360.0f, 0.0f}}
                }).commit();
            });

            return s.build();
        });

    // 3. Render — SDK engine, deterministic, single-frame
    c3d::sdk::RenderSettings settings{.width = 1280, .height = 720,
                                      .deterministic = true};
    c3d::sdk::RenderEngine engine{settings};
    engine.set_assets_root("assets");

    auto result = engine.render(project.create("TitleCard"), c3d::sdk::Frame{0});
    if (!result) {
        std::fprintf(stderr, "Render failed: %s\n", result.error().message.c_str());
        return 1;
    }

    std::printf("✓ Rendered \"%s\" (%dx%d) — %zu compositions registered\n",
                project.name.c_str(), result->width, result->height, project.size());
    return 0;
}
