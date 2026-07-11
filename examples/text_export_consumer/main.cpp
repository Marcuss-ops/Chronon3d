// examples/text_export_consumer/main.cpp
//
// ═══════════════════════════════════════════════════════════════════════════
// Chronon3D — Text Export Consumer Example
//
// Minimal standalone consumer that demonstrates:
//   1. find_package(Chronon3D) + Chronon3D::SDK (public API only)
//   2. SceneBuilder + l.text() to create text
//   3. sdk::RenderEngine to render
//   4. save_png to export the result
//
// No internal headers, no FontEngine wiring, no RenderGraph knowledge.
// Just a Composition with text → PNG.
//
// Build & run (see README.md):
//   mkdir build && cd build
//   cmake .. -DCMAKE_PREFIX_PATH=/path/to/chronon3d-install
//   cmake --build .
//   cp -r ../assets/fonts .   # font must be at CWD-relative path
//   ./text_export_consumer
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/chronon3d.hpp>

#include <cstdio>
#include <filesystem>

namespace c3d = chronon3d;

int main() {
    // ── 1. Composition with text via canonical authoring API ─────────
    //
    // The SceneBuilder automatically forwards the render pipeline's
    // FontEngine to the text layer — no manual wiring needed.
    auto comp = c3d::composition(
        {.name = "text_export_example",
         .width = 1280,
         .height = 720,
         .frame_rate = c3d::FrameRate{30, 1},
         .duration = 1,
         .assets_root = "assets"},
        [](const c3d::FrameContext& ctx) -> c3d::Scene {
            c3d::SceneBuilder s(ctx);
            if (ctx.font_engine) s.font_engine(ctx.font_engine);

            // Dark background
            s.layer("bg", [](c3d::LayerBuilder& l) {
                l.rect("bg_rect", c3d::RectParams{
                    .size = {1280.0f, 720.0f},
                    .color = c3d::Color{0.1f, 0.1f, 0.15f, 1.0f},
                    .pos = {640.0f, 360.0f, 0.0f},
                    .fill = c3d::FillStyle::solid(
                        c3d::Color{0.1f, 0.1f, 0.15f, 1.0f})
                });
            });

            // Title text — centered white text
            s.layer("title", [](c3d::LayerBuilder& l) {
                l.kind(c3d::LayerKind::Text);
                l.text("title", c3d::TextSpec{.content = {.value = "Hello, Chronon3D!"},.font = {.font_path = "fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 72.0f},.layout = {.box = {1280.0f, 720.0f},
                               .align = c3d::TextAlign::Center,
                               .vertical_align = c3d::VerticalAlign::Middle},.appearance = {.color = c3d::Color::white()},});
            });

            return s.build();
        });

    // Camera: face +Z where the scene lives at z=0.
    c3d::camera_v1::CameraDescriptor cam{};
    cam.id = "main_camera";
    cam.base.enabled = true;
    cam.base.position = c3d::Vec3{0.0f, 0.0f, -800.0f};
    cam.base.rotation = c3d::Vec3{0.0f, 180.0f, 0.0f};
    cam.base.projection = c3d::camera_v1::ZoomProjection{};
    cam.failure_policy = c3d::camera_v1::CameraFailurePolicy::Stop;
    comp.default_camera_descriptor(std::move(cam));

    // Legacy camera fallback (P3-F bridge)
    comp.camera.transform.position = c3d::Vec3{0.0f, 0.0f, -1000.0f};
    comp.camera.set_rotation_euler(c3d::Vec3{0.0f, 180.0f, 0.0f});

    // ── 2. Render with sdk::RenderEngine ────────────────────────────
    c3d::sdk::RenderSettings settings{};
    settings.width = 1280;
    settings.height = 720;
    settings.antialiasing_samples = 1;
    settings.deterministic = true;
    settings.dirty_rects = false;
    settings.motion_blur = false;
    settings.max_threads = 1;

    c3d::sdk::RenderEngine engine{settings};
    engine.set_assets_root(std::filesystem::path{"assets"});

    auto result = engine.render(comp, c3d::sdk::Frame{0});
    if (!result.has_value()) {
        std::fprintf(stderr, "Render failed: %s\n",
                     result.error().message.c_str());
        return 1;
    }

    // ── 3. Bridge RenderOutput → Framebuffer → PNG ──────────────────
    const auto& out = result.value();
    c3d::Framebuffer fb{out.width, out.height};

    const uint8_t* src = out.pixels;
    const bool is_bgra = (out.format == c3d::sdk::PixelFormat::Bgra8);
    const size_t row_stride = (out.bytes_per_row > 0)
        ? static_cast<size_t>(out.bytes_per_row)
        : static_cast<size_t>(out.width) * 4u;
    constexpr float inv = 1.0f / 255.0f;

    for (int32_t y = 0; y < out.height; ++y) {
        for (int32_t x = 0; x < out.width; ++x) {
            const size_t idx = static_cast<size_t>(y) * row_stride
                             + static_cast<size_t>(x) * 4u;
            c3d::Color c;
            if (is_bgra) {
                c = c3d::Color{src[idx+2]*inv, src[idx+1]*inv,
                               src[idx+0]*inv, 1.0f};
            } else {
                c = c3d::Color{src[idx+0]*inv, src[idx+1]*inv,
                               src[idx+2]*inv, 1.0f};
            }
            fb.set_pixel(x, y, c);
        }
    }

    const char* output = "text_export_output.png";
    if (!c3d::save_png(fb, output)) {
        std::fprintf(stderr, "save_png failed\n");
        return 1;
    }

    std::printf("Text exported to %s (%dx%d)\n", output, out.width, out.height);
    return 0;
}
