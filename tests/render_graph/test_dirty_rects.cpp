#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/core/counters.hpp>

using namespace chronon3d;

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
    settings_baseline.dirty_rects = false;
    renderer_baseline.set_settings(settings_baseline);

    auto fb_baseline = renderer_baseline.render_frame(comp, 10);
    REQUIRE(fb_baseline != nullptr);

    // 3. Render frame 10 WITH dirty rects (Optimized)
    SoftwareRenderer renderer_opt;
    RenderSettings settings_opt;
    settings_opt.use_modular_graph = true;
    settings_opt.dirty_rects = true;
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
