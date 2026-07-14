// =============================================================================
// tests/install_consumer/main_full.cpp
//
// P3-H + feat(api) — full SDK surface coverage consumer.
//
// Exercises ALL 6 SDK public surface areas in ONE consumer program:
//   1. text       — TextRun via LayerBuilder::text() + TextDefinition
//   2. image      — Rect (LayerBuilder::rect with distinctive color) +
//                   GridBackground — same LayerBuilder surface as image()
//   3. camera     — CameraDescriptorBuilder + compile_camera + scene.camera().program()
//   4. timeline   — Composition + Frame + FrameRate + FrameContext
//   5. render     — sdk::RenderEngine::render(composition, frame)
//   6. PNG-save   — chronon3d::save_png(framebuffer, path)
//
// P3-H strict boundary contract (AGENTS.md §SDK Product V1 / 8):
//   (a) Includes ONLY headers listed in cmake/Chronon3DPublicHeaders.cmake
//   (b) Calls ONLY sdk::RenderEngine::render(...) + chronon3d::save_png(...)
//       + the documented public Composition/Scene/Layer/Shape/Camera helpers
//   (c) MUST NOT see any of: chronon3d/internal/, RenderGraph, FontEngine,
//       CameraSession, SoftwareRenderer, cache internals
//
// Output marker: `[FULL-OK]` on success (consumed by
// tools/verify_sdk_consumer_functional_linux.sh).
// =============================================================================

#include <chronon3d/sdk/render_settings.hpp>
#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/sdk/render_output.hpp>
#include <chronon3d/sdk/render_error.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/frame_rate.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/scene_camera_facade.hpp>
#include <chronon3d/scene/camera/camera_descriptor_builder.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/backends/image/image_writer.hpp>

// Compile-time public-surface reachability check.
static_assert(sizeof(chronon3d::camera_v1::CameraDescriptor) > 0,
              "CameraDescriptor must be reachable from the public SDK");
static_assert(sizeof(chronon3d::camera_v1::CameraProgram) > 0,
              "CameraProgram must be reachable from the public SDK");
static_assert(sizeof(chronon3d::sdk::RenderEngine) > 0,
              "sdk::RenderEngine must be reachable from the public SDK");

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <chronon3d/text/text_definition.hpp>

namespace c3d = chronon3d;

int main(int argc, char* argv[]) {
    const char* assets_root = (argc > 1) ? argv[1] : "assets";

    // ── 1. Camera descriptor (public CameraDescriptorBuilder) ────────
    // SURFACE: camera (via the new public fluent builder).
    auto descriptor = c3d::camera()
        .id("p3h_full_camera")
        .position({0.0f, 0.0f, -1200.0f})
        .look_at({0.0f, 0.0f, 0.0f})
        .lens(c3d::PhysicalLens{
            .focal_length_mm  = 50.0f,
            .f_stop           = 2.8f,
            .close_focus_mm   = 450.0f,
            .sensor_width_mm  = 36.0f,
            .sensor_height_mm = 24.0f,
            .gate_fit         = c3d::GateFit::Fill,
        })
        .build();

    // ── 2. Compile descriptor → immutable CameraProgram ───────────────
    auto program_result = c3d::compile_camera(
        descriptor, c3d::CompositionCompileContext{});
    if (!program_result.has_value()) {
        std::fprintf(stderr,
                     "[FULL-FAIL] compile_camera failed: %s\n",
                     program_result.error().message.c_str());
        return 1;
    }
    auto program = std::move(program_result.value());

    // ── 3. Composition spec (SURFACE: timeline via Composition+Frame+FrameRate) ──
    const c3d::CompositionSpec spec{
        /* .name         */ "p3h_full_consumer",
        /* .width        */ 1920,
        /* .height       */ 1080,
        /* .frame_rate   */ c3d::FrameRate{30, 1},
        /* .duration     */ 1,
        /* .assets_root  */ assets_root,
    };

    // ── 4. Composition with all 6 surface areas wired up ─────────────
    auto comp = c3d::composition(
        spec,
        [&](const c3d::FrameContext& ctx) -> c3d::Scene {
            c3d::SceneBuilder s(ctx);
            if (ctx.runtime && ctx.runtime->font_engine()) {
                s.font_engine(ctx.runtime->font_engine());
            }

            // SURFACE: image (background grid background — exercises
            // the shape/grid surface via LayerBuilder).
            s.layer("background", [&ctx](c3d::LayerBuilder& l) {
                l.kind(c3d::LayerKind::Shape);
                l.grid_background("grid_bg", c3d::GridBackgroundParams{
                    .size = {static_cast<c3d::f32>(ctx.width),
                             static_cast<c3d::f32>(ctx.height)},
                    .offset = {0.0f, 0.0f},
                    .bg_color = c3d::Color{0.05f, 0.05f, 0.07f, 1.0f},
                    .grid_color = c3d::Color{0.25f, 0.25f, 0.35f, 1.0f},
                    .spacing = 80.0f,
                    .minor_thickness = 1.0f,
                    .major_thickness = 2.0f,
                    .major_every = 4,
                    .centered = true,
                });
            });

            // SURFACE: image (real LayerBuilder::image() surface —
            // exercises the canonical image-layer pipeline via the
            // manifest-clean `asset_path` field).  The PNG asset
            // `assets/test_image.png` is shipped in-tree at
            // `tests/install_consumer/assets/test_image.png` and copied
            // to the consumer build dir by CMakeLists.txt's POST_BUILD
            // custom_command.  Per the established TICKET-VERIFY-SDK-
            // CONSUMER-IMAGE-ASSET closure (code-reviewer NIT 1 from
            // the original gate chore), the previous `rect()` proxy
            // was a gap in surface coverage; the gate now audits
            // `l.image(` (not just `l.rect(`) to close it.
            s.layer("test_image", [](c3d::LayerBuilder& l) {
                l.kind(c3d::LayerKind::Shape);
                l.image("test_image", c3d::ImageParams{
                    .asset_path = "test_image.png",
                    .size  = {600.0f, 400.0f},
                    .pos   = {660.0f, 340.0f, 0.0f},
                    .fit   = c3d::FitMode::Cover,
                    .opacity = 1.0f,
                });
            });

            // SURFACE: text (TextRun via LayerBuilder::text + TextDefinition).
            s.layer("title", [&ctx](c3d::LayerBuilder& l) {
                l.kind(c3d::LayerKind::Text);
                l.text("title_text", c3d::TextDefinition{
    .content = {.value = "SDK CONSUMER FULL COVERAGE"},
    .style = {
        .font = {.font_path   = "fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size   = 56.0f},
        .color = c3d::Color{1.0f, 1.0f, 1.0f, 1.0f}
    },
    .frame = {
        .size = {static_cast<c3d::f32>(ctx.width),
                                       static_cast<c3d::f32>(ctx.height)},
        .align = c3d::TextAlign::Center,
        .vertical_align = c3d::VerticalAlign::Top
    }
});
            });

            c3d::Scene scene = s.build();
            // SURFACE: camera (scene facade). Attach the pre-compiled
            // program to the scene so the OPP uses the V2 camera path.
            scene.camera().program(program);
            return scene;
        });

    // Also set on the composition for consistency probes.
    comp.camera_program(std::move(program));

    // ── 5. Render settings + engine (SURFACE: render) ────────────────
    c3d::sdk::RenderSettings settings{};
    settings.width               = spec.width;
    settings.height              = spec.height;
    settings.antialiasing_samples = 1;
    settings.deterministic       = true;
    settings.dirty_rects         = false;
    settings.motion_blur         = false;
    settings.max_threads         = 1;

    c3d::sdk::RenderEngine engine{settings};
    engine.set_assets_root(std::filesystem::path{spec.assets_root});

    // SURFACE: render — invoke the public RenderEngine facade.
    auto result = engine.render(comp, c3d::sdk::Frame{30});
    if (!result.has_value()) {
        std::fprintf(stderr,
                     "[FULL-FAIL] sdk::RenderEngine::render failed: "
                     "code=%s message=%s\n",
                     c3d::sdk::render_error_code_name(result.error().code),
                     result.error().message.c_str());
        return 1;
    }

    const c3d::sdk::RenderOutput& out = result.value();
    if (out.pixels == nullptr || out.width <= 0 || out.height <= 0) {
        std::fprintf(stderr, "[FULL-FAIL] render returned empty output\n");
        return 1;
    }

    // Bridge RenderOutput → Framebuffer (no internal types visible).
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

    // Pixel-hash check: at least 1 pixel must have >5/255 luminance.
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
                     "[FULL-FAIL] all %zu pixels below 5/255 — output PNG would be black\n",
                     fb.pixel_count());
        return 1;
    }

    // SURFACE: PNG-save — public image writer.
    const std::filesystem::path output_path = "sdk_full_consumer_output.png";
    if (!c3d::save_png(fb, output_path.string())) {
        std::fprintf(stderr, "[FULL-FAIL] save_png failed for %s\n",
                     output_path.string().c_str());
        return 1;
    }
    if (!std::filesystem::exists(output_path)
        || std::filesystem::file_size(output_path) == 0) {
        std::fprintf(stderr, "[FULL-FAIL] output PNG missing/empty: %s\n",
                     output_path.string().c_str());
        return 1;
    }

    // Boundary marker — emitted for tools/verify_sdk_consumer_functional_linux.sh
    // to grep + assert all 6 surfaces are exercised.
    const auto file_size = std::filesystem::file_size(output_path);
    std::printf("[FULL-OK] sdk full consumer rendered %dx%d PNG "
                "(%zu bytes, %zu/%zu pixels >5/255, surfaces=text+image+camera+timeline+render+png_save, "
                "format=%s)\n",
                out.width, out.height,
                static_cast<std::size_t>(file_size),
                nonzero_count, fb.pixel_count(),
                (is_bgra ? "Bgra8" : "Rgba8"));
    return 0;
}
