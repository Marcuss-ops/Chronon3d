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

#include <chronon3d/sdk/render_settings.hpp>
#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/project.hpp>

#include <cstdio>
#include <chronon3d/text/text_definition.hpp>

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
            if (ctx.runtime && ctx.runtime->font_engine()) s.font_engine(ctx.runtime->font_engine());

            s.layer("bg", [](c3d::LayerBuilder& l) {
                l.fill(c3d::Color{0.1f, 0.1f, 0.15f, 1.0f});
            });

            s.layer("title", [&ctx](c3d::LayerBuilder& l) {
                l.font("fonts/Inter-Bold.ttf")
                 .font_size(72.0f)
                 .center();
                l.animated_text("t", c3d::TextRunSpec{
                    .text = c3d::TextDefinition{
    .content = {.value = "Hello, Chronon3D!"},
    .style = {
        .font = {.font_family = "Inter", .font_weight = 700},
        .color = c3d::Color::white()
    },
    .frame = {
        .size = {static_cast<float>(ctx.width),
                                           static_cast<float>(ctx.height)}
    }
}
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
