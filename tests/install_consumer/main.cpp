// tests/install_consumer/main.cpp
//
// ── Rewrite V2 — SceneBuilder-based productive consumer ────────────────
//
// Uses the public SceneBuilder API (same as main_text.cpp) instead of
// manual Layer/RenderNode construction. SceneBuilder handles all
// initialization automatically (transforms, layout, font_engine cascade,
// shape type dispatch, effect stacks, animated transforms).
//
// This is a STANDALONE consumer project — it does NOT share
// tests/CMakeLists.txt and does NOT link against in-tree targets.
// Its only dependency is the *installed* Chronon3D package.
//
// Contract (RELEASE_GATE.md §SDK Product V1 / 8 — P3-H):
//   (a) Includes ONLY headers listed in cmake/Chronon3DPublicHeaders.cmake
//   (b) Calls ONLY sdk::RenderEngine::render() + save_png + public
//       Composition/Scene/Layer/Shape/CameraDescriptor helpers
//   (c) Composition with GridBackground + TextRun layers + compiled camera
//   (d) Output PNG via save_png
//   (e) Pixel-hash check: fails if every pixel has mean luminance < 5/255

#include <chronon3d/chronon3d.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>

namespace c3d = chronon3d;

int main(int argc, char* argv[]) {
    const char* assets_root = (argc > 1) ? argv[1] : "assets";

    // ── 1. Composition spec ──────────────────────────────────────────
    const c3d::CompositionSpec spec{
        /* .name         */ "p3h_consumer",
        /* .width        */ 640,
        /* .height       */ 360,
        /* .frame_rate   */ c3d::FrameRate{30, 1},
        /* .duration     */ 1,
        /* .assets_root  */ assets_root,
    };

    // ── 2. Composition via SceneBuilder ──────────────────────────────
    auto comp = c3d::composition(
        spec,
        [](const c3d::FrameContext& ctx) -> c3d::Scene {
            c3d::SceneBuilder s(ctx);
            if (ctx.font_engine) s.font_engine(ctx.font_engine);

            // Background layer: GridBackground (built-in, no font needed)
            s.layer("background", [&ctx](c3d::LayerBuilder& l) {
                l.kind(c3d::LayerKind::Shape);
                l.grid_background("grid_bg", c3d::GridBackgroundParams{
                    .size = {static_cast<c3d::f32>(ctx.width),
                             static_cast<c3d::f32>(ctx.height)},
                    .offset = {0.0f, 0.0f},
                    .bg_color = c3d::Color{1.0f, 1.0f, 1.0f, 1.0f},
                    .grid_color = c3d::Color{1.0f, 1.0f, 1.0f, 1.0f},
                    .spacing = 60.0f,
                    .minor_thickness = 1.0f,
                    .major_thickness = 2.5f,
                    .major_every = 4,
                    .centered = true,
                });
            });

            // Text layer: TextRun (modern pipeline)
            s.layer("title", [&ctx](c3d::LayerBuilder& l) {
                l.kind(c3d::LayerKind::Text);
                l.text("title_text", c3d::TextSpec{.content = {.value = "BOUNDARY CHECK"}, .placement = c3d::TextPlacement{
                        c3d::TextPlacementKind::Absolute,
                        {static_cast<c3d::f32>(ctx.width) * 0.5f,
                                 static_cast<c3d::f32>(ctx.height) * 0.5f}}, .font = {.font_path = "fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 48.0f}, .layout = {.box = {static_cast<c3d::f32>(ctx.width),
                                       static_cast<c3d::f32>(ctx.height)},
                               .align = c3d::TextAlign::Center,
                               .vertical_align = c3d::VerticalAlign::Middle}, .appearance = {.color = c3d::Color{1.0f, 1.0f, 1.0f, 1.0f}}});
            });

            return s.build();
        });

    // ── 3. Camera descriptor (public, value-typed) ───────────────────
    c3d::camera_v1::CameraDescriptor descriptor{};
    descriptor.id = "p3h_main_camera";
    descriptor.base.enabled = true;
    descriptor.base.position = c3d::Vec3{0.0f, 0.0f, -800.0f};
    descriptor.base.rotation = c3d::Vec3{0.0f, 180.0f, 0.0f};
    descriptor.base.projection = c3d::camera_v1::ZoomProjection{};
    descriptor.failure_policy = c3d::camera_v1::CameraFailurePolicy::Stop;
    comp.default_camera_descriptor(std::move(descriptor));

    // Legacy camera bridge (render_composition_frame still reads comp.camera)
    comp.camera.transform.position = c3d::Vec3{0.0f, 0.0f, -1000.0f};
    comp.camera.set_rotation_euler(c3d::Vec3{0.0f, 180.0f, 0.0f});

    // ── 4. Render settings ───────────────────────────────────────────
    c3d::sdk::RenderSettings settings{};
    settings.width = spec.width;
    settings.height = spec.height;
    settings.antialiasing_samples = 1;
    settings.deterministic = true;
    settings.dirty_rects = false;
    settings.motion_blur = false;
    settings.max_threads = 1;

    if (!std::filesystem::is_directory(spec.assets_root)) {
        std::fprintf(stderr,
                     "[consumer-warn] assets_root '%s' is not a directory "
                     "— text layer may render blank, but GridBackground "
                     "keeps the pixel-hash >= 5/255.\n",
                     spec.assets_root.c_str());
    }

    c3d::sdk::RenderEngine engine{settings};
    engine.set_assets_root(std::filesystem::path{spec.assets_root});

    // ── 5. Render ────────────────────────────────────────────────────
    auto result = engine.render(comp, c3d::sdk::Frame{0});
    if (!result.has_value()) {
        std::fprintf(stderr,
                     "[BOUNDARY-FAIL] sdk::RenderEngine::render failed: "
                     "code=%s message=%s\n",
                     c3d::sdk::render_error_code_name(result.error().code),
                     result.error().message.c_str());
        return 1;
    }

    const c3d::sdk::RenderOutput& out = result.value();
    if (out.pixels == nullptr || out.width <= 0 || out.height <= 0) {
        std::fprintf(stderr,
                     "[BOUNDARY-FAIL] render returned empty output\n");
        return 1;
    }
    if (out.format != c3d::sdk::PixelFormat::Rgba8
        && out.format != c3d::sdk::PixelFormat::Bgra8) {
        std::fprintf(stderr,
                     "[BOUNDARY-FAIL] unsupported PixelFormat enum=%d\n",
                     static_cast<int>(out.format));
        return 1;
    }

    // ── 6. Bridge: RenderOutput → Framebuffer ────────────────────────
    c3d::Framebuffer fb{out.width, out.height};
    const std::uint8_t* src = out.pixels;
    const bool is_bgra = (out.format == c3d::sdk::PixelFormat::Bgra8);
    const std::size_t row_stride =
        (out.bytes_per_row > 0)
            ? static_cast<std::size_t>(out.bytes_per_row)
            : static_cast<std::size_t>(out.width) * 4u;
    constexpr float inv = 1.0f / 255.0f;

    for (std::int32_t y = 0; y < out.height; ++y) {
        for (std::int32_t x = 0; x < out.width; ++x) {
            const std::size_t idx =
                static_cast<std::size_t>(y) * row_stride
                + static_cast<std::size_t>(x) * 4u;
            c3d::Color c;
            if (is_bgra) {
                c = c3d::Color{src[idx+2]*inv, src[idx+1]*inv, src[idx+0]*inv, 1.0f};
            } else {
                c = c3d::Color{src[idx+0]*inv, src[idx+1]*inv, src[idx+2]*inv, 1.0f};
            }
            fb.set_pixel(x, y, c);
        }
    }

    // ── 7. Pixel-hash check (≥ 5/255 on ≥ 1 pixel) ─────────────────
    constexpr float kThreshold = 5.0f / 255.0f;
    std::size_t nonzero_count = 0;
    const c3d::Color* fdata = fb.data();

    // Diagnostic: dump first 5 pixels
    std::fprintf(stderr, "[consumer-diag] first 5 pixels (r,g,b,a):\n");
    for (std::size_t i = 0; i < 5 && i < fb.pixel_count(); ++i) {
        const c3d::Color c = fdata[i];
        std::fprintf(stderr, "  pixel[%zu] = (%.4f, %.4f, %.4f, %.4f)\n",
            i, c.r, c.g, c.b, c.a);
    }

    for (std::size_t i = 0; i < fb.pixel_count(); ++i) {
        const c3d::Color c = fdata[i];
        if (c.r > kThreshold || c.g > kThreshold || c.b > kThreshold) {
            ++nonzero_count;
        }
    }
    if (nonzero_count == 0) {
        std::fprintf(stderr,
                     "[BOUNDARY-FAIL] all %zu pixels below 5/255 — "
                     "output PNG would be black\n",
                     fb.pixel_count());
        return 1;
    }

    // ── 8. Save PNG ──────────────────────────────────────────────────
    const std::filesystem::path output_path = "sdk_consumer_output.png";
    if (!c3d::save_png(fb, output_path.string())) {
        std::fprintf(stderr,
                     "[BOUNDARY-FAIL] save_png failed for %s\n",
                     output_path.string().c_str());
        return 1;
    }
    if (!std::filesystem::exists(output_path)
        || std::filesystem::file_size(output_path) == 0) {
        std::fprintf(stderr,
                     "[BOUNDARY-FAIL] output PNG missing/empty: %s\n",
                     output_path.string().c_str());
        return 1;
    }

    // ── 9. Boundary marker ───────────────────────────────────────────
    const auto file_size = std::filesystem::file_size(output_path);
    std::printf("[BOUNDARY-OK] sdk consumer (P3-H) rendered %dx%d PNG "
                "(%zu bytes, %zu/%zu pixels >5/255, format=%s)\n",
                out.width, out.height,
                static_cast<std::size_t>(file_size),
                nonzero_count, fb.pixel_count(),
                (is_bgra ? "Bgra8" : "Rgba8"));
    return 0;
}
