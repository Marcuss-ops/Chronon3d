#include <doctest/doctest.h>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <cmath>

using namespace chronon3d;

namespace {

bool framebuffers_match(
    const Framebuffer& a,
    const Framebuffer& b,
    int& mismatches,
    float epsilon = 1e-4f
) {
    if (a.width() != b.width() || a.height() != b.height()) {
        return false;
    }

    bool matches = true;
    mismatches = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const Color ca = a.get_pixel(x, y);
            const Color cb = b.get_pixel(x, y);
            if (std::abs(ca.r - cb.r) > epsilon ||
                std::abs(ca.g - cb.g) > epsilon ||
                std::abs(ca.b - cb.b) > epsilon ||
                std::abs(ca.a - cb.a) > epsilon) {
                matches = false;
                ++mismatches;
            }
        }
    }
    return matches;
}

} // namespace

TEST_CASE("Dirty Rectangles V1 Pixel-Perfect Equivalence & Counters Test") {
    // 1. Create a dynamic scene with a moving shape layer
    CompositionSpec spec{
        .name = "DirtyRectsEquivalenceTest",
        .width = 160,
        .height = 120,
        .duration = 30
    };

    Composition comp(spec, [](const FrameContext& ctx) {
        SceneBuilder builder(ctx.resource);
        
        // Background
        builder.rect("bg", {
            .size = {160, 120},
            .color = Color{0.1f, 0.2f, 0.3f, 1.0f},
            .pos = {80, 60, 0}
        });

        // Dynamic foreground object (position changes with frame)
        float x = 40.0f + static_cast<float>(ctx.frame) * 4.0f;
        builder.circle("ball", {
            .radius = 20.0f,
            .color = Color::red(),
            .pos = {x, 60.0f, 0.0f}
        });

        return builder.build();
    });

    // 2. Render frame 10 WITHOUT dirty rects (Baseline)
    SoftwareRenderer renderer_baseline;
    RenderSettings settings_baseline;
    settings_baseline.use_modular_graph = true;
    settings_baseline.dirty.dirty_rects_v1 = false;
    renderer_baseline.set_settings(settings_baseline);

    auto fb_baseline = renderer_baseline.render_frame(comp, 10);
    REQUIRE(fb_baseline != nullptr);

    // 3. Render frame 10 WITH dirty rects (Optimized)
    SoftwareRenderer renderer_opt;
    RenderSettings settings_opt;
    settings_opt.use_modular_graph = true;
    settings_opt.dirty.dirty_rects_v1 = true;
    renderer_opt.set_settings(settings_opt);

    auto fb_opt = renderer_opt.render_frame(comp, 10);
    REQUIRE(fb_opt != nullptr);

    // 4. Verify absolute pixel equivalence
    CHECK(fb_baseline->width() == fb_opt->width());
    CHECK(fb_baseline->height() == fb_opt->height());

    bool all_pixels_match = true;
    int mismatches = 0;
    for (int y = 0; y < fb_baseline->height(); ++y) {
        for (int x = 0; x < fb_baseline->width(); ++x) {
            Color c_base = fb_baseline->get_pixel(x, y);
            Color c_opt = fb_opt->get_pixel(x, y);

            if (std::abs(c_base.r - c_opt.r) > 1e-4f ||
                std::abs(c_base.g - c_opt.g) > 1e-4f ||
                std::abs(c_base.b - c_opt.b) > 1e-4f ||
                std::abs(c_base.a - c_opt.a) > 1e-4f) {
                all_pixels_match = false;
                mismatches++;
            }
        }
    }

    CHECK(all_pixels_match == true);
    CHECK(mismatches == 0);

    // 5. Verify that dirty rects optimization was active and recorded counters
    const auto* counters = renderer_opt.counters();
    uint64_t rect_count = counters->dirty_rect_count.load();
    uint64_t pixels_count = counters->dirty_pixels.load();
    
    CHECK(rect_count > 0);
    CHECK(pixels_count > 0);
}

TEST_CASE("Dirty rectangles stay stable for geometric backgrounds across frames") {
    CompositionSpec spec{
        .name = "DirtyRectsGeometryRegression",
        .width = 192,
        .height = 128,
        .duration = 12
    };

    Composition comp(spec, [](const FrameContext& ctx) {
        SceneBuilder builder(ctx.resource);

        builder.rect("bg", {
            .size = {192, 128},
            .color = Color{0.07f, 0.08f, 0.11f, 1.0f},
            .pos = {96.0f, 64.0f, 0.0f}
        });

        builder.rounded_rect("panel", {
            .size = {140.0f, 84.0f},
            .radius = 18.0f,
            .color = Color{0.14f, 0.16f, 0.22f, 1.0f},
            .pos = {96.0f, 64.0f, 0.0f}
        });

        builder.line("slash", {
            .from = {18.0f, 104.0f, 0.0f},
            .to = {174.0f, 24.0f, 0.0f},
            .thickness = 2.5f,
            .color = Color{0.35f, 0.75f, 1.0f, 0.75f}
        });

        const float t = static_cast<float>(ctx.frame);
        const float orb_x = 26.25f + t * 0.5f;
        const float orb_y = 30.25f + std::sin(t * 0.45f) * 0.35f;
        builder.rounded_rect("orb", {
            .size = {22.0f, 14.0f},
            .radius = 4.0f,
            .color = Color{0.96f, 0.72f, 0.24f, 0.92f},
            .pos = {orb_x, orb_y, 0.0f}
        });

        const float chip_x = 140.5f + std::cos(t * 0.3f) * 0.5f;
        builder.rect("chip", {
            .size = {18.0f, 18.0f},
            .color = Color{0.95f, 0.95f, 1.0f, 0.8f},
            .pos = {chip_x, 88.5f, 0.0f}
        });

        return builder.build();
    });

    SoftwareRenderer renderer_baseline;
    RenderSettings settings_baseline;
    settings_baseline.use_modular_graph = true;
    settings_baseline.dirty.dirty_rects_v1 = false;
    renderer_baseline.set_settings(settings_baseline);

    SoftwareRenderer renderer_opt;
    RenderSettings settings_opt;
    settings_opt.use_modular_graph = true;
    settings_opt.dirty.dirty_rects_v1 = true;
    renderer_opt.set_settings(settings_opt);

    int total_mismatches = 0;
    for (int frame = 0; frame < spec.duration; ++frame) {
        auto fb_baseline = renderer_baseline.render_frame(comp, frame);
        REQUIRE(fb_baseline != nullptr);

        auto fb_opt = renderer_opt.render_frame(comp, frame);
        REQUIRE(fb_opt != nullptr);

        CHECK(fb_baseline->width() == fb_opt->width());
        CHECK(fb_baseline->height() == fb_opt->height());

        int mismatches = 0;
        CHECK(framebuffers_match(*fb_baseline, *fb_opt, mismatches));
        CHECK(mismatches == 0);
        total_mismatches += mismatches;
    }

    CHECK(total_mismatches == 0);
    const auto* counters = renderer_opt.counters();
    CHECK(counters->dirty_rect_count.load() > 0);
    CHECK(counters->dirty_pixels.load() > 0);
}
