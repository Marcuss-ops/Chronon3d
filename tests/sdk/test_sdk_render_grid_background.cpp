// tests/sdk/test_sdk_render_grid_background.cpp
//
// TICKET-GATE-10-PHASE-4-BLACK — regression-lock test for the SDK
// facade (`sdk::RenderEngine::render`) producing a non-black
// framebuffer when the scene's only opaque Shape is a
// GridBackgroundShape.  Locks the gate #10 Phase 4 pixel-hash
// contract that was broken during the v0.1 freeze (background
// path was pre-cleared by the framebuffer pool then never repainted
// because the kernel saw `RenderState::opacity == 0` and so its
// colour-alphas zeroed out).  The fix lives in
// `src/backends/software/kernels/grid_background_kernel.cpp`
// (TICKET-GATE-10-PHASE-4-BLACK defensive opacity guard that
// promotes a degenerate zero-opacity RenderState to a sensible
// default for this kernel).
//
// Pixel-hash invariant (matches `tools/sdk/run_external_consumer.sh`
// strict `check_pixel_hash_strict` byte-15 contract):
//     at least 1 of the rendered pixels must satisfy
//         (r_byte + g_byte + b_byte) > 15
// i.e. R+G+B > 15/255 in float RGBA space.
//
// A regression that re-introduces the degenerate-opacity path
// surfaces in BOTH this unit test AND the CI consumer gate
// (consistency-by-construction).  The unit test probes a smaller
// 320x200 framebuffer so the discriminator is stable across
// preset variations (no SSAA, no motion-blur) and the gate's
// "≥ 1 such pixel" threshold catches "all-zero framebuffer"
// regressions with a stable boundary.
#include <doctest/doctest.h>

#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/sdk/render_output.hpp>
#include <chronon3d/sdk/render_error.hpp>
#include <chronon3d/sdk/render_request.hpp>
#include <chronon3d/sdk/render_settings.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/image/image_writer.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>

namespace c3d = chronon3d;

namespace {

// Mirrors the consumer-strict invariant: (r_byte+g_byte+b_byte) > 15.
// In float RGBA space, the equivalent threshold is 15/255 ≈ 0.0588.
constexpr float kSumThreshold = 15.0f / 255.0f;
constexpr int   kWidth        = 320;
constexpr int   kHeight       = 200;
constexpr int   kExpectedAboveThresholdMin = 1;

std::size_t count_non_black_pixels(const c3d::sdk::RenderOutput& out) {
    const auto* src = out.pixels;
    if (src == nullptr) return 0;
    const std::size_t row_stride = (out.bytes_per_row > 0)
        ? static_cast<std::size_t>(out.bytes_per_row)
        : static_cast<std::size_t>(out.width) * 4u;
    std::size_t above = 0;
    const bool is_bgra = (out.format == c3d::sdk::PixelFormat::Bgra8);
    for (std::int32_t y = 0; y < out.height; ++y) {
        for (std::int32_t x = 0; x < out.width; ++x) {
            const std::size_t idx =
                static_cast<std::size_t>(y) * row_stride
                + static_cast<std::size_t>(x) * 4u;
            const float r = (is_bgra ? src[idx + 2] : src[idx + 0]) * (1.0f / 255.0f);
            const float g = src[idx + 1] * (1.0f / 255.0f);
            const float b = (is_bgra ? src[idx + 0] : src[idx + 2]) * (1.0f / 255.0f);
            // TICKET-GATE-10-PHASE-4-BLACK sum-of-channels invariant:
            //   (r + g + b) > 15/255  in float RGBA
            //   ↔  (r_byte + g_byte + b_byte) > 15  in PIL byte RGBA
            // strict match to `tools/sdk/run_external_consumer.sh`
            // `check_pixel_hash_strict` so this unit test catches the
            // exact regression class the gate catches.
            if (r + g + b > kSumThreshold) {
                ++above;
            }
        }
    }
    return above;
}

c3d::Composition build_composition(int width, int height,
                                    const std::filesystem::path& assets_root) {
    const c3d::CompositionSpec spec{
        /* .name         */ "gate10_p4_grid_background_phase4_regression",
        /* .width        */ width,
        /* .height       */ height,
        /* .frame_rate   */ c3d::FrameRate{30, 1},
        /* .duration     */ 1,
        /* .assets_root  */ assets_root.string(),
    };

    return c3d::Composition{
        spec,
        [](const c3d::FrameContext& ctx) -> c3d::Scene {
            c3d::Scene scene;

            // ONLY-Opaque-Shape scene: a single LayerKind::Shape with
            // a GridBackgroundShape that should leave the framebuffer
            // visibly painted.  No text layer — the regression was
            // observed with text missing too, but the bg-only path
            // is enough to lock the contract: GridBackground must
            // visibly paint when reached via sdk::RenderEngine::render.
            c3d::Layer bg;
            bg.name      = "background";
            bg.kind      = c3d::LayerKind::Shape;
            bg.from      = c3d::Frame{0};
            bg.duration  = c3d::Frame{-1};
            bg.visible   = true;
            bg.transform = c3d::Transform{};

            c3d::RenderNode bg_node;
            bg_node.name    = "grid_bg";
            bg_node.visible = true;

            c3d::GridBackgroundShape gb;
            gb.size        = c3d::Vec2{
                static_cast<c3d::f32>(ctx.width),
                static_cast<c3d::f32>(ctx.height)};
            gb.offset      = c3d::Vec2{0.0f, 0.0f};
            gb.bg_color    = c3d::Color{0.30f, 0.32f, 0.40f, 1.0f};    // visible navy
            gb.grid_color  = c3d::Color{0.60f, 0.80f, 1.00f, 0.70f};   // bright cyan
            gb.spacing     = 50.0f;
            gb.minor_thickness = 1.0f;
            gb.major_thickness = 2.5f;
            gb.major_every = 4;
            gb.centered    = true;

            bg_node.shape = c3d::Shape{gb};
            bg.nodes.push_back(std::move(bg_node));
            scene.add_layer(std::move(bg));

            return scene;
        },
    };
}

} // namespace

// ─── Test — primary gate #10 Phase 4 regression lock ──────────────────
//
// A single TEST_CASE is sufficient: the additional coverage at a
// different resolution locks the same fundamental contract (sum-of-
// channels > 15/255 invariant) without exercising a structurally
// different code path.  If a future use case requires a different
// resolution (e.g. SSAA on), add per-resolution SUBCASEs from this
// test rather than spinning up a fresh TEST_CASE.

TEST_CASE("SDK-RENDERER GATE-10-PHASE-4-BLACK: sdk::RenderEngine::render with "
          "GridBackgroundShape layer MUST produce a non-black PNG") {
    const std::filesystem::path assets_root = "assets";   // no asset needed for bg-only
    c3d::Composition comp = build_composition(kWidth, kHeight, assets_root);

    // Default-camera path (no CameraDescriptor set on Composition),
    // matching the gate #10 Phase 4 narrower invocation pattern.
    comp.camera.transform.position = c3d::Vec3{0.0f, 0.0f, -1000.0f};
    comp.camera.set_rotation_euler(c3d::Vec3{0.0f, 180.0f, 0.0f});

    c3d::sdk::RenderSettings settings{};
    settings.width  = kWidth;
    settings.height = kHeight;
    settings.antialiasing_samples = 1;
    settings.deterministic       = true;
    settings.dirty_rects         = false;
    settings.motion_blur         = false;
    settings.max_threads         = 1;

    c3d::sdk::RenderEngine engine{settings};
    engine.set_assets_root(assets_root);

    const auto result = engine.render(comp, c3d::sdk::Frame{0});
    REQUIRE(result.has_value());

    const c3d::sdk::RenderOutput& out = result.value();
    REQUIRE(out.pixels != nullptr);
    REQUIRE(out.width  == kWidth);
    REQUIRE(out.height == kHeight);
    REQUIRE((out.format == c3d::sdk::PixelFormat::Rgba8
          || out.format == c3d::sdk::PixelFormat::Bgra8));

    const std::size_t above = count_non_black_pixels(out);
    std::printf("[TICKET-GATE-10-PHASE-4-BLACK] sdk render produced %zu/%zu "
                "pixels with (R+G+B)>15 (sum-of-channels invariant)\n",
                above,
                static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight));

    // Discriminator: a regression that re-introduces the
    // RenderState::opacity=0 path would leave the framebuffer
    // zero-cleared and `above == 0`.  The contract is strict:
    //   above >= kExpectedAboveThresholdMin  (== 1)
    // matching the gate #10 Phase 4 strict consumer hash check
    // (sum-of-channels > 15 per PIL check_pixel_hash_strict).
    CHECK(above >= static_cast<std::size_t>(kExpectedAboveThresholdMin));
}
