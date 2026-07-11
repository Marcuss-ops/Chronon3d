#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/math/color.hpp>

#include <algorithm>

namespace chronon3d::content::images {

namespace {

// File-local canvas size.  Renamed from W / H to avoid unity-build
// redefinition collisions with image_proofs.cpp and
// image_animated_reveals.cpp, which both declare W / H in the same
// (anonymous-namespace-inside-content::images) scope.
constexpr f32 IMG_TEST_W = 1920.0f;
constexpr f32 IMG_TEST_H = 1080.0f;

} // namespace

// ── ImgGradient ─────────────────────────────────────────────────────────
Composition img_gradient() {
    return composition({.name = "ImgGradient", .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("base", [](auto& l) { l.cache_static().fullscreen_rect("base", {0.02f, 0.02f, 0.06f, 1}); });

        const i32 steps = 32;
        for (i32 i = 0; i < steps; ++i) {
            f32 t = static_cast<f32>(i) / (steps - 1);
            s.layer("band_" + std::to_string(i), [&](auto& l) {
                l.position({0, (t - 0.5f) * IMG_TEST_H, 0});
                l.rect("rect", {.size = {IMG_TEST_W, IMG_TEST_H / steps + 1}, .color = {0.05f + t * 0.2f, 0.02f + t * 0.5f, 0.04f + t * 0.8f, (2.0f / steps)}});
            });
        }

        s.layer("glow", [](auto& l) {
            l.cache_static().pin_to(Anchor::Center).circle("glow", {.radius = 300, .color = {0.25f, 0.52f, 1, 0.06f}});
        });
        return s.build();
    });
}

// ── ImgChecker ───────────────────────────────────────────────────────────
Composition img_checker() {
    return composition({.name = "ImgChecker", .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 sq = 80.0f;
        const i32 cols = static_cast<i32>(IMG_TEST_W / sq) + 1;
        const i32 rows = static_cast<i32>(IMG_TEST_H / sq) + 1;

        s.layer("bg", [](auto& l) { l.cache_static().fill({0.15f, 0.15f, 0.17f, 1}); });

        for (i32 r = 0; r < rows && r < 20; ++r) {
            for (i32 c = 0; c < cols && c < 30; ++c) {
                if ((r + c) % 2 == 0) continue;
                s.layer("check_" + std::to_string(r) + "_" + std::to_string(c), [&](auto& l) {
                    l.position({(c - cols * 0.5f) * sq, (r - rows * 0.5f) * sq, 0});
                    l.rect("sq", {.size = {sq, sq}, .color = {0.18f, 0.18f, 0.20f, 1}});
                });
            }
        }
        return s.build();
    });
}

// ── ImgGridTest ──────────────────────────────────────────────────────────
Composition img_grid_test() {
    return composition({.name = "ImgGridTest", .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](auto& l) { l.cache_static().fill({0.01f, 0.012f, 0.02f, 1}); });
        s.layer("grid", [](auto& l) {
            l.cache_static().grid_background("grid", {.size = {IMG_TEST_W, IMG_TEST_H}, .grid_color = {0.18f, 0.52f, 1, 0.15f}, .spacing = 80});
        });
        s.layer("label", [](auto& l) {
            l.pin_to(Anchor::BottomCenter, 40);
            l.text("txt", TextSpec{.content = {.value = "GRID RESOLUTION TEST"},.position = {0.0f, 0.0f, 0.0f},.font = {.font_family = "Inter", .font_weight = 800, .font_size = 24.0f},.layout = {.box = {400, 40}, .align = TextAlign::Center, .line_height = 1.2f, .tracking = 4.0f},.appearance = {.color = Color{0.6f, 0.7f, 0.9f, 0.6f}},});
        });
        return s.build();
    });
}

// ── ImgTestPattern ───────────────────────────────────────────────────────
Composition img_test_pattern() {
    return composition({.name = "ImgTestPattern", .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](auto& l) { l.cache_static().fill({0.1f, 0.1f, 0.1f, 1}); });

        for (int i = 0; i < 8; ++i) {
            s.layer("bar_" + std::to_string(i), [&](auto& l) {
                l.position({(i - 3.5f) * (IMG_TEST_W / 8), 0, 0});
                l.rect("bar", {.size = {IMG_TEST_W / 8, IMG_TEST_H}, .color = { (i & 4) ? 1.0f : 0.0f, (i & 2) ? 1.0f : 0.0f, (i & 1) ? 1.0f : 0.0f, 1.0f }});
            });
        }
        return s.build();
    });
}

} // namespace chronon3d::content::images

// Compositions are registered via content::images::register_image_compositions()
// (called by ContentExtension in content/register_content_modules.cpp).
