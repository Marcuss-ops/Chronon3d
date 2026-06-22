#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/layout/stroked_shapes.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <tests/helpers/test_utils.hpp>

#include <cmath>
#include <filesystem>
#include <string>
using namespace chronon3d;

using namespace chronon3d::test;

namespace {

// ── Golden verification (same pattern as glow_golden_tests) ─────────────

void verify_stroke_golden_or_create(const Framebuffer& rendered, const std::string& filename) {
    const std::filesystem::path golden_dir = "test_renders/golden/stroke";
    std::filesystem::create_directories(golden_dir);
    const std::filesystem::path golden_path = golden_dir / filename;

    if (!std::filesystem::exists(golden_path)) {
        REQUIRE(save_png(rendered, golden_path.string()));
        return;
    }

    auto golden = load_png_as_framebuffer(golden_path.string());
    REQUIRE(golden != nullptr);

    if (rendered.width() != golden->width() || rendered.height() != golden->height()) {
        FAIL("Dimension mismatch: rendered="
             << rendered.width() << "x" << rendered.height()
             << ", golden=" << golden->width() << "x" << golden->height());
    }

    double total_diff = 0.0;
    int mismatched = 0;
    const int total_pixels = rendered.width() * rendered.height();

    for (int y = 0; y < rendered.height(); ++y) {
        for (int x = 0; x < rendered.width(); ++x) {
            const Color c1 = rendered.get_pixel(x, y).clamped();
            const Color c2 = golden->get_pixel(x, y).clamped();
            const float max_ch = std::max({std::abs(c1.r - c2.r),
                                           std::abs(c1.g - c2.g),
                                           std::abs(c1.b - c2.b),
                                           std::abs(c1.a - c2.a)});
            total_diff += max_ch;
            if (max_ch > 3.0f / 255.0f) ++mismatched;
        }
    }

    const float mean_err = static_cast<float>(total_diff / (total_pixels * 4));
    const float mismatch_pct = static_cast<float>(mismatched) / static_cast<float>(total_pixels);

    INFO("mismatch=" << mismatch_pct * 100.0f << "%, mean_err=" << mean_err);
    CHECK(mismatch_pct < 0.02f);
    CHECK(mean_err < 0.01f);
}

// ── Scene 1: Stroke alignment variants (Inside / Center / Outside) ─────
//
// Three rounded rects side by side with identical fill but different
// stroke alignments.  Semi-transparent fill + opaque stroke makes the
// compositing insertion-order unambiguous:
//   - Stroke behind fill → fill covers inner half of stroke
//   - Stroke on top of fill → stroke outlines the fill clearly

Composition make_stroke_alignment_scene() {
    return Composition({.width = 600, .height = 240, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](LayerBuilder& l) {
            l.fill({0.06f, 0.06f, 0.10f, 1.0f});
        });

        // Left: Inside alignment
        s.layer("inside", [](LayerBuilder& l) {
            l.position({-200.0f, 0.0f, 0.0f});
            draw_stroked_rounded_rect(l, "rect", {160.0f, 160.0f}, 24.0f,
                                      Color{0.15f, 0.55f, 0.90f, 0.40f},
                                      Color{0.95f, 0.20f, 0.80f, 1.0f},
                                      6.0f, StrokeAlignment::Inside);
        });

        // Center: Center alignment (default)
        s.layer("center", [](LayerBuilder& l) {
            l.position({0.0f, 0.0f, 0.0f});
            draw_stroked_rounded_rect(l, "rect", {160.0f, 160.0f}, 24.0f,
                                      Color{0.15f, 0.55f, 0.90f, 0.40f},
                                      Color{0.95f, 0.20f, 0.80f, 1.0f},
                                      6.0f, StrokeAlignment::Center);
        });

        // Right: Outside alignment
        s.layer("outside", [](LayerBuilder& l) {
            l.position({200.0f, 0.0f, 0.0f});
            draw_stroked_rounded_rect(l, "rect", {160.0f, 160.0f}, 24.0f,
                                      Color{0.15f, 0.55f, 0.90f, 0.40f},
                                      Color{0.95f, 0.20f, 0.80f, 1.0f},
                                      6.0f, StrokeAlignment::Outside);
        });

        return s.build();
    });
}

// ── Scene 2: Heavy stroke to expose insertion-order artifacts ──────────
//
// Uses a very thick stroke (20px) and a nearly-opaque fill with a
// contrasting stroke colour.  If the stroke is drawn under the fill,
// the fill will cover the inner half of the stroke, making it appear
// thinner.  The golden captures the correct "stroke-on-top" rendering.

Composition make_heavy_stroke_order_scene() {
    return Composition({.width = 400, .height = 300, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](LayerBuilder& l) {
            l.fill({0.02f, 0.02f, 0.05f, 1.0f});
        });

        // Thin stroke (reference)
        s.layer("thin", [](LayerBuilder& l) {
            l.position({-100.0f, 0.0f, 0.0f});
            draw_stroked_rounded_rect(l, "rect", {140.0f, 140.0f}, 20.0f,
                                      Color{0.10f, 0.75f, 0.40f, 0.85f},
                                      Color{1.0f, 0.90f, 0.20f, 1.0f},
                                      2.0f, StrokeAlignment::Center);
        });

        // Heavy stroke — insertion-order sensitive
        s.layer("heavy", [](LayerBuilder& l) {
            l.position({100.0f, 0.0f, 0.0f});
            draw_stroked_rounded_rect(l, "rect", {140.0f, 140.0f}, 20.0f,
                                      Color{0.10f, 0.75f, 0.40f, 0.85f},
                                      Color{1.0f, 0.90f, 0.20f, 1.0f},
                                      20.0f, StrokeAlignment::Center);
        });

        return s.build();
    });
}

// ── Scene 3: draw_premium_pill via PremiumPillStyle ────────────────────
//
// Exercises the PremiumPillStyle / draw_premium_pill helper, which is
// the primary consumer of draw_stroked_rounded_rect in production code.

Composition make_premium_pill_scene() {
    return Composition({.width = 500, .height = 200, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](LayerBuilder& l) {
            l.fill({0.03f, 0.02f, 0.06f, 1.0f});
        });

        s.layer("pill", [](LayerBuilder& l) {
            l.position({0.0f, 0.0f, 0.0f});
            PremiumPillStyle style;
            style.size = {400.0f, 80.0f};
            style.radius = 40.0f;
            style.fill = {0.04f, 0.02f, 0.08f, 0.20f};
            style.stroke = {0.78f, 0.48f, 0.88f, 0.50f};
            style.stroke_width = 1.5f;
            style.stroke_alignment = StrokeAlignment::Center;
            draw_premium_pill(l, "pill_rect", style);
        });

        return s.build();
    });
}

} // namespace

TEST_CASE("StrokeGolden: stroke alignment variants (inside/center/outside)") {
    auto renderer = test::make_renderer();
    auto rendered = renderer.render_frame(make_stroke_alignment_scene(), 0);
    REQUIRE(rendered != nullptr);
    verify_stroke_golden_or_create(*rendered, "stroke_alignment_variants.png");
}

TEST_CASE("StrokeGolden: heavy stroke insertion order") {
    auto renderer = test::make_renderer();
    auto rendered = renderer.render_frame(make_heavy_stroke_order_scene(), 0);
    REQUIRE(rendered != nullptr);
    verify_stroke_golden_or_create(*rendered, "heavy_stroke_order.png");
}

TEST_CASE("StrokeGolden: premium pill default style") {
    auto renderer = test::make_renderer();
    auto rendered = renderer.render_frame(make_premium_pill_scene(), 0);
    REQUIRE(rendered != nullptr);
    verify_stroke_golden_or_create(*rendered, "premium_pill_default.png");
}
