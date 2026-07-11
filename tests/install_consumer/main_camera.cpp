// =============================================================================
// tests/install_consumer/main_camera.cpp
//
// P3-H + feat(api) public camera facade — external consumer SDK test.
//
// Mirrors the user-spec example exactly:
//
//   auto descriptor = camera()
//       .position({0, 0, -1200}).look_at({0, 0, 0})
//       .lens(PhysicalLens{.focal_length_mm = 50.0f, .sensor_width_mm = 36.0f})
//       .build();
//   auto program = compile_camera(descriptor).value();
//   composition.camera(program);
//   renderer.render(composition, Frame{30});
//
// Contract (P3-H strict):
//   (a) Includes ONLY headers listed in cmake/Chronon3DPublicHeaders.cmake
//   (b) Calls ONLY `chronon3d::sdk::RenderEngine::render(...)` +
//       `chronon3d::save_png(...)` + the new public facade methods
//   (c) Uses `scene.camera().descriptor/.program/.timeline/.preset` chain
//   (d) Output PNG via save_png
//   (e) Pixel-hash check: fails if every pixel has mean luminance < 5/255
//   (f) MUST NOT see `CameraSession` or `RenderGraph` — no `#include` of
//       any internal header path
//
// Output marker: `[CAMERA-OK]` on success (consumed by
// tools/sdk/run_external_consumer.sh).
// =============================================================================

#include <chronon3d/sdk/render_settings.hpp>
#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/sdk/render_output.hpp>
#include <chronon3d/sdk/render_error.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/scene_camera_facade.hpp>          // SceneCameraFacade
#include <chronon3d/scene/camera/camera_descriptor_builder.hpp>    // chronon3d::camera()
#include <chronon3d/timeline/compile_evaluate.hpp>                 // compile_camera()
#include <chronon3d/backends/image/image_writer.hpp>                // save_png

// STATIC ASSERT: the consumer must NOT see CameraSession or RenderGraph.
// These types live under include/chronon3d/internal/ which is not on the
// installed SDK's include path.  The presence of these names in the global
// namespace would be a violation of the P3-H boundary contract — but at the
// same time, we deliberately do NOT forward-declare them here, because the
// contract is enforced by the include-path gate in
// `tools/sdk/run_external_consumer.sh` (BANNED_RX) + the absence of these
// headers from `cmake/Chronon3DPublicHeaders.cmake`.
//
// A compile-time diagnostic verifies the public types ARE reachable (so
// we know the SDK install propagated the manifest correctly).
static_assert(sizeof(chronon3d::camera_v1::CameraDescriptor) > 0,
              "CameraDescriptor must be reachable from the public SDK");
static_assert(sizeof(chronon3d::camera_v1::CameraProgram) > 0,
              "CameraProgram must be reachable from the public SDK");

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>

namespace c3d = chronon3d;

int main(int argc, char* argv[]) {
    const char* assets_root = (argc > 1) ? argv[1] : "assets";

    // ── 1. Build the descriptor via the public fluent builder ──────────
    //
    // This is the user-spec example verbatim (PhysicalLens named-field
    // struct accepted by CameraDescriptorBuilder::lens(...)):
    auto descriptor = c3d::camera()
        .id("p3h_consumer_perspective")
        .position({0.0f, 0.0f, -1200.0f})
        .look_at({0.0f, 0.0f, 0.0f})
        .lens(c3d::PhysicalLens{
            .focal_length_mm = 50.0f,
            .f_stop          = 2.8f,
            .close_focus_mm  = 450.0f,
            .sensor_width_mm = 36.0f,
            .sensor_height_mm = 24.0f,
            .gate_fit        = c3d::GateFit::Fill,
        })
        .build();

    // ── 2. Compile the descriptor into an immutable program ────────────
    auto program_result = c3d::compile_camera(
        descriptor, c3d::CompositionCompileContext{});
    if (!program_result.has_value()) {
        std::fprintf(stderr,
                     "[CAMERA-FAIL] compile_camera failed: %s\n",
                     program_result.error().message.c_str());
        return 1;
    }
    auto program = std::move(program_result.value());

    // ── 3. Composition spec (1920x1080 landscape) ─────────────────────
    const c3d::CompositionSpec spec{
        /* .name         */ "p3h_camera_consumer",
        /* .width        */ 1920,
        /* .height       */ 1080,
        /* .frame_rate   */ c3d::FrameRate{30, 1},
        /* .duration     */ 1,
        /* .assets_root  */ assets_root,
    };

    // ── 4. Composition via the public camera facade ──────────────────
    //
    // The SceneCameraFacade setters chain the descriptor onto the scene
    // that the SceneBuilder produces.  External consumers see ONLY the
    // public facade — no `CameraSession` or `RenderGraph` is visible.
    auto comp = c3d::composition(
        spec,
        [&](const c3d::FrameContext& ctx) -> c3d::Scene {
            c3d::SceneBuilder s(ctx);
            if (ctx.font_engine) s.font_engine(ctx.font_engine);

            // Background layer: GridBackground (built-in, no font needed)
            s.layer("background", [&ctx](c3d::LayerBuilder& l) {
                l.kind(c3d::LayerKind::Shape);
                l.grid_background("grid_bg", c3d::GridBackgroundParams{
                    .size = {static_cast<c3d::f32>(ctx.width),
                             static_cast<c3d::f32>(ctx.height)},
                    .offset = {0.0f, 0.0f},
                    .bg_color = c3d::Color{0.05f, 0.05f, 0.07f, 1.0f},
                    .grid_color = c3d::Color{0.30f, 0.30f, 0.40f, 1.0f},
                    .spacing = 80.0f,
                    .minor_thickness = 1.0f,
                    .major_thickness = 2.0f,
                    .major_every = 4,
                    .centered = true,
                });
            });

            // Text layer — sanity for the perspective.
            s.layer("title", [&ctx](c3d::LayerBuilder& l) {
                l.kind(c3d::LayerKind::Text);
                l.text("title_text", c3d::TextSpec{
                    .content = {.value = "PERSPECTIVE OK"},
                    .font = {.font_path = "fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 64.0f},
                    .layout = {.box = {static_cast<c3d::f32>(ctx.width),
                                       static_cast<c3d::f32>(ctx.height)},
                               .align = c3d::TextAlign::Center,
                               .vertical_align = c3d::VerticalAlign::Middle},
                    .appearance = {.color = c3d::Color{1.0f, 1.0f, 1.0f, 1.0f}},
                });
            });

            c3d::Scene scene = s.build();

            // ── Public camera facade — attach the pre-compiled program.
            // P3-H + code-review round 2: pick ONE path.  We use the
            // new facade (the canonical entry for the spec example),
            // not the redundant `composition.camera(...)` call.
            scene.camera().program(program);
            return scene;
        });

    // Optional: also set on the composition so the OPP read path
    // `comp.camera()` returns the same program (consistency probe
    // for `has_camera_program()`).  This is a CONSUMER choice; the
    // spec example shows either path works.
    comp.camera(std::move(program));

    // ── 5. Render settings + engine ──────────────────────────────────
    c3d::sdk::RenderSettings settings{};
    settings.width = spec.width;
    settings.height = spec.height;
    settings.antialiasing_samples = 1;
    settings.deterministic = true;
    settings.dirty_rects = false;
    settings.motion_blur = false;
    settings.max_threads = 1;

    c3d::sdk::RenderEngine engine{settings};
    engine.set_assets_root(std::filesystem::path{spec.assets_root});

    // ── 6. Render frame 30 (the spec example) ─────────────────────────
    auto result = engine.render(comp, c3d::sdk::Frame{30});
    if (!result.has_value()) {
        std::fprintf(stderr,
                     "[CAMERA-FAIL] sdk::RenderEngine::render failed: "
                     "code=%s message=%s\n",
                     c3d::sdk::render_error_code_name(result.error().code),
                     result.error().message.c_str());
        return 1;
    }

    const c3d::sdk::RenderOutput& out = result.value();
    if (out.pixels == nullptr || out.width <= 0 || out.height <= 0) {
        std::fprintf(stderr, "[CAMERA-FAIL] render returned empty output\n");
        return 1;
    }

    // ── 7. Bridge: RenderOutput → Framebuffer ─────────────────────────
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

    // ── 8. Pixel-hash check (≥ 5/255 on ≥ 1 pixel) ───────────────────
    constexpr float kThreshold = 5.0f / 255.0f;
    std::size_t nonzero_count = 0;
    const c3d::Color* fdata = fb.data();
    for (std::size_t i = 0; i < fb.pixel_count(); ++i) {
        const c3d::Color c = fdata[i];
        if (c.r > kThreshold || c.g > kThreshold || c.b > kThreshold) {
            ++nonzero_count;
        }
    }
    if (nonzero_count == 0) {
        std::fprintf(stderr,
                     "[CAMERA-FAIL] all %zu pixels below 5/255 — "
                     "output PNG would be black\n",
                     fb.pixel_count());
        return 1;
    }

    // ── 9. Save PNG ──────────────────────────────────────────────────
    const std::filesystem::path output_path = "sdk_camera_consumer_output.png";
    if (!c3d::save_png(fb, output_path.string())) {
        std::fprintf(stderr, "[CAMERA-FAIL] save_png failed for %s\n",
                     output_path.string().c_str());
        return 1;
    }
    if (!std::filesystem::exists(output_path)
        || std::filesystem::file_size(output_path) == 0) {
        std::fprintf(stderr, "[CAMERA-FAIL] output PNG missing/empty: %s\n",
                     output_path.string().c_str());
        return 1;
    }

    // ── 10. Boundary marker ──────────────────────────────────────────
    const auto file_size = std::filesystem::file_size(output_path);
    std::printf("[CAMERA-OK] public camera facade consumer rendered %dx%d "
                "PNG (%zu bytes, %zu/%zu pixels >5/255, "
                "facade=scene.camera() + composition.camera(p), "
                "format=%s)\n",
                out.width, out.height,
                static_cast<std::size_t>(file_size),
                nonzero_count, fb.pixel_count(),
                (is_bgra ? "Bgra8" : "Rgba8"));
    return 0;
}
