// tests/install_consumer/main.cpp
//
// ── Real render end-to-end SDK consumer (TICKET-011 final gate) ──
//
// This is a STANDALONE consumer project — it does NOT share
// tests/CMakeLists.txt and does NOT link against in-tree targets.
// Its only dependency is the *installed* Chronon3D package.
//
// The consumer:
//   1. Creates a composition with text + camera via SceneBuilder
//   2. Renders a frame via RenderEngine
//   3. Saves the framebuffer as a PNG
//   4. Verifies the PNG exists and is non-empty
//
// Wired into CTest via the top-level CMakeLists.txt option
// CHRONON3D_BUILD_INSTALL_CONSUMER_TEST (enabled by the linux-ci preset).
// The orchestrator script `tools/install_consumer_test.sh` runs the full
// configure → build → install → consume → run pipeline against an isolated
// temp prefix and validates the output.

#include <chronon3d/chronon3d.hpp>

#include <cstdio>
#include <filesystem>

int main() {
    // ── Create a composition with text + camera ─────────────────
    auto comp = chronon3d::composition(
        {.name = "SDKConsumerTest",
         .width = 640,
         .height = 360,
         .duration = 1},
        [](const chronon3d::FrameContext& ctx) {
            chronon3d::SceneBuilder s(ctx);

            // Background grid
            s.layer("bg", [](chronon3d::LayerBuilder& l) {
                l.grid_background("grid", chronon3d::GridBackgroundParams{
                    .size = {640.0f, 360.0f},
                    .bg_color = {0.02f, 0.02f, 0.06f, 1.0f},
                    .grid_color = {0.15f, 0.55f, 1.0f, 0.10f},
                    .spacing = 60.0f,
                    .minor_thickness = 1.0f,
                    .major_thickness = 2.0f,
                    .major_every = 4,
                    .centered = true
                });
            });

            // Text layer
            s.layer("title", [](chronon3d::LayerBuilder& l) {
                l.position({0.0f, -20.0f, 0.0f});
                l.text("txt", {
                    .content = {.value = "Chronon3D SDK"},
                    .font = {.font_family = "sans-serif",
                             .font_size = 48.0f},
                    .layout = {.box = {600.0f, 80.0f},
                               .align = chronon3d::TextAlign::Center,
                               .vertical_align = chronon3d::VerticalAlign::Middle},
                    .appearance = {.color = chronon3d::Color{0.92f, 0.97f, 1.0f, 1.0f}},
                    .position = {0.0f, 0.0f, 0.0f}
                });
            });

            // Subtitle text
            s.layer("sub", [](chronon3d::LayerBuilder& l) {
                l.position({0.0f, 60.0f, 0.0f});
                l.text("sub", {
                    .content = {.value = "External Consumer OK"},
                    .font = {.font_family = "sans-serif",
                             .font_size = 22.0f},
                    .layout = {.box = {600.0f, 48.0f},
                               .align = chronon3d::TextAlign::Center,
                               .vertical_align = chronon3d::VerticalAlign::Middle},
                    .appearance = {.color = chronon3d::Color{0.62f, 0.78f, 1.0f, 1.0f}},
                    .position = {0.0f, 0.0f, 0.0f}
                });
            });

            // Camera — 2.5D with slight orbit
            s.camera().enable(true)
                .position({0.0f, 0.0f, -800.0f})
                .zoom(800.0f)
                .look_at({0.0f, 0.0f, 0.0f});

            return s.build();
        });

    // ── Render ──────────────────────────────────────────────────
    chronon3d::RenderEngine engine;
    auto fb = engine.render_frame(comp, 0);

    if (!fb) {
        std::fprintf(stderr, "[BOUNDARY-FAIL] render_frame returned null\n");
        return 1;
    }

    // ── Save PNG ────────────────────────────────────────────────
    const std::filesystem::path output_path = "sdk_consumer_output.png";
    if (!chronon3d::save_png(*fb, output_path.string())) {
        std::fprintf(stderr, "[BOUNDARY-FAIL] save_png failed\n");
        return 1;
    }

    // ── Verify output ───────────────────────────────────────────
    if (!std::filesystem::exists(output_path)) {
        std::fprintf(stderr, "[BOUNDARY-FAIL] output PNG not found: %s\n",
                     output_path.c_str());
        return 1;
    }

    const auto file_size = std::filesystem::file_size(output_path);
    if (file_size == 0) {
        std::fprintf(stderr, "[BOUNDARY-FAIL] output PNG is empty\n");
        return 1;
    }

    // ── Boundary marker ─────────────────────────────────────────
    std::printf("[BOUNDARY-OK] SDK consumer rendered %dx%d PNG (%zu bytes)\n",
                fb->width(), fb->height(), static_cast<size_t>(file_size));
    return 0;
}
