#include <doctest/doctest.h>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <tests/helpers/test_utils.hpp>
#include <filesystem>
#include <cmath>
#include <vector>
#include <algorithm>
#include <iostream>

using namespace chronon3d::test;

#include "content/images/compositions/image_proofs.hpp"
using namespace chronon3d;

namespace chronon3d::content::shapes {
    Composition shape_proofs();
}

namespace {

struct SuiteComparisonResult {
    bool success{true};
    float max_channel_diff{0.0f};
    float mean_error{0.0f};
    float mismatch_percentage{0.0f};
    std::string error_message;
};

// Compare rendered (linear float Framebuffer converted to sRGB) with loaded golden (sRGB float)
inline SuiteComparisonResult compare_suite_images(const Framebuffer& rendered, const Framebuffer& golden) {
    SuiteComparisonResult res;
    if (rendered.width() != golden.width() || rendered.height() != golden.height()) {
        res.success = false;
        res.error_message = "Dimension mismatch: rendered=" + std::to_string(rendered.width()) + "x" + std::to_string(rendered.height()) +
                            ", golden=" + std::to_string(golden.width()) + "x" + std::to_string(golden.height());
        return res;
    }
    
    double total_channel_diff = 0.0;
    int mismatched_pixels = 0;
    int total_pixels = rendered.width() * rendered.height();
    
    for (int y = 0; y < rendered.height(); ++y) {
        for (int x = 0; x < rendered.width(); ++x) {
            // Both rendered and golden must be compared in valid sRGB [0,1] range.
        // Rendered may contain HDR values > 1.0 (e.g. bloom on HDR content); clamp
        // to [0,1] so the 8-bit PNG roundtrip comparison is fair.
        Color c1 = rendered.get_pixel(x, y).to_srgb().clamped();
        Color c2 = golden.get_pixel(x, y).clamped();
            
            float dr = std::abs(c1.r - c2.r);
            float dg = std::abs(c1.g - c2.g);
            float db = std::abs(c1.b - c2.b);
            float da = std::abs(c1.a - c2.a);
            
            float max_diff = std::max({dr, dg, db, da});
            res.max_channel_diff = std::max(res.max_channel_diff, max_diff);
            
            total_channel_diff += dr + dg + db + da;
            
            if (max_diff > 3.0f / 255.0f) {
                mismatched_pixels++;
            }
        }
    }
    
    res.mean_error = static_cast<float>(total_channel_diff / (total_pixels * 4));
    res.mismatch_percentage = static_cast<float>(mismatched_pixels) / total_pixels;
    
    // Threshold validation:
    // maxPixelDiff: 1-2%
    // meanError: < 0.01
    // maxChannelError: < 3/255 (we allow a bit higher to be safe from CPU precision differences)
    if (res.mismatch_percentage > 0.02f || res.mean_error >= 0.01f || res.max_channel_diff >= 4.0f / 255.0f) {
        res.success = false;
        res.error_message = "Threshold exceeded: maxPixelDiff=" + std::to_string(res.mismatch_percentage * 100.0f) + "%" +
                            ", meanError=" + std::to_string(res.mean_error) +
                            ", maxChannelError=" + std::to_string(res.max_channel_diff * 255.0f) + "/255";
    }
    
    return res;
}

void verify_suite_golden_or_create(const Framebuffer& rendered, const std::string& filename) {
    const std::filesystem::path golden_dir = "test_renders/golden";
    std::filesystem::create_directories(golden_dir);
    const std::filesystem::path golden_path = golden_dir / filename;
    
    if (!std::filesystem::exists(golden_path)) {
        std::cout << "Creating golden baseline image: " << golden_path.string() << std::endl;
        REQUIRE(save_png(rendered, golden_path.string()));
    }
    
    auto golden = load_png_as_framebuffer(golden_path.string());
    REQUIRE(golden != nullptr);
    
    auto comp = compare_suite_images(rendered, *golden);
    INFO(comp.error_message);
    CHECK(comp.success);
}

#define verify_golden_or_create verify_suite_golden_or_create

} // namespace

// ── 1. TEST MANDATORY VISUAL SNAPSHOTS ────────────────────────────────────────
TEST_CASE("Chronon3d Suite: Mandatory Visual Snapshots") {
    auto renderer = make_renderer();

    // Scene 1: Linear Gradient
    Composition comp_lin(CompositionSpec{.width = 512, .height = 128}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        std::vector<graphics::GradientStop> stops = {
            {0.0f, Color::red()},
            {1.0f, Color::blue()}
        };
        s.rect("linear_box", {
            .size = {512, 128},
            .pos = {-256, -64, 0},
            .fill = FillStyle::linear({0.0f, 0.5f}, {1.0f, 0.5f}, stops)
        });
        return s.build();
    });
    auto fb_lin = renderer.render_frame(comp_lin, 0);
    REQUIRE(fb_lin != nullptr);
    verify_golden_or_create(*fb_lin, "gradient-linear.png");

    // Scene 2: Radial Gradient
    Composition comp_rad(CompositionSpec{.width = 512, .height = 512}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        std::vector<graphics::GradientStop> stops = {
            {0.0f, Color::white()},
            {1.0f, Color::black()}
        };
        s.circle("radial_circle", {
            .radius = 200.0f,
            .pos = {-200, -200, 0},
            .fill = FillStyle::radial({0.5f, 0.5f}, 0.5f, stops)
        });
        return s.build();
    });
    auto fb_rad = renderer.render_frame(comp_rad, 0);
    REQUIRE(fb_rad != nullptr);
    verify_golden_or_create(*fb_rad, "gradient-radial.png");

    // Scene 3: Multi-Stop Gradient
    Composition comp_multi(CompositionSpec{.width = 512, .height = 128}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        std::vector<graphics::GradientStop> stops = {
            {0.0f, Color::red()},
            {0.25f, Color{1, 1, 0, 1}}, // yellow
            {0.5f, Color::green()},
            {0.75f, Color{0, 1, 1, 1}}, // cyan
            {1.0f, Color::blue()}
        };
        s.rect("multi_box", {
            .size = {512, 128},
            .pos = {-256, -64, 0},
            .fill = FillStyle::linear({0.0f, 0.5f}, {1.0f, 0.5f}, stops)
        });
        return s.build();
    });
    auto fb_multi = renderer.render_frame(comp_multi, 0);
    REQUIRE(fb_multi != nullptr);
    verify_golden_or_create(*fb_multi, "gradient-multi-stop.png");

    // Scene 4: Stroke Dash / Cap / Join
    Composition comp_stroke(CompositionSpec{.width = 512, .height = 512}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        // Line 1: 1px width (offset from canvas center 256, 256)
        s.line("line_1px", {
            .from = {50 - 256, 50 - 256, 0},
            .to = {450 - 256, 50 - 256, 0},
            .thickness = 1.0f,
            .color = Color::white()
        });
        // Line 2: 10px width
        s.line("line_10px", {
            .from = {50 - 256, 100 - 256, 0},
            .to = {450 - 256, 100 - 256, 0},
            .thickness = 10.0f,
            .color = Color::red()
        });
        // Shape 3: Rect stroke 20px (offset from canvas center 256, 256)
        s.path("rect_stroke", {
            .commands = {
                PathCommand::move_to({-100, -50}),
                PathCommand::line_to({100, -50}),
                PathCommand::line_to({100, 50}),
                PathCommand::line_to({-100, 50}),
            },
            .stroke = {.width = 20.0f, .join = LineJoin::Round},
            .fill = Fill::solid_color(Color::transparent()),
            .color = Color{0, 1, 0, 1},
            .pos = {150 - 256, 220 - 256, 0},
            .closed = true
        });

        // Shape 4: Path with trim (simulating dash)
        s.path("trimmed_path", {
            .commands = {
                PathCommand::move_to({50, 350}),
                PathCommand::line_to({250, 350})
            },
            .stroke = {.width = 8.0f, .cap = LineCap::Round, .trim_start = 0.1f, .trim_end = 0.9f},
            .color = Color::blue(),
            .pos = {-256, -256, 0}
        });
        
        // Shape 5: Cap Square and Join Miter
        s.path("cap_square", {
            .commands = {
                PathCommand::move_to({50, 420}),
                PathCommand::line_to({200, 420}),
                PathCommand::line_to({200, 480})
            },
            .stroke = {.width = 12.0f, .cap = LineCap::Square, .join = LineJoin::Miter},
            .color = Color::white(),
            .pos = {-256, -256, 0}
        });

        // Shape 6: Join Bevel
        s.path("join_bevel", {
            .commands = {
                PathCommand::move_to({300, 420}),
                PathCommand::line_to({450, 420}),
                PathCommand::line_to({450, 480})
            },
            .stroke = {.width = 12.0f, .cap = LineCap::Butt, .join = LineJoin::Bevel},
            .color = Color{1, 1, 0, 1},
            .pos = {-256, -256, 0}
        });

        return s.build();
    });
    auto fb_stroke = renderer.render_frame(comp_stroke, 0);
    REQUIRE(fb_stroke != nullptr);
    verify_golden_or_create(*fb_stroke, "stroke-dash-cap-join.png");

    // Scene 5: Shadow Drop / Inner / Contact
    Composition comp_shadow(CompositionSpec{.width = 512, .height = 512}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        // White square in center with drop shadow
        s.rect("square", {
            .size = {150, 150},
            .color = Color::white(),
            .pos = {-75, -75, 0}
        });
        s.with_shadow({
            .enabled = true,
            .offset = {30.0f, 30.0f},
            .color = Color{0.0f, 0.0f, 0.0f, 0.8f},
            .radius = 20.0f
        });
        return s.build();
    });
    auto fb_shadow = renderer.render_frame(comp_shadow, 0);
    REQUIRE(fb_shadow != nullptr);
    verify_golden_or_create(*fb_shadow, "shadow-drop-inner-contact.png");

    // Scene 6: Bloom Threshold / Knee / Intensity
    Composition comp_bloom(CompositionSpec{.width = 512, .height = 512}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("l", [](LayerBuilder& l) {
            // Circle 1: Below threshold (0.5 brightness)
            l.circle("c1", {.radius = 40.0f, .color = Color{0.5f, 0.5f, 0.5f, 1.0f}, .pos = {-168, -40, 0}});
            // Circle 2: Near threshold (1.0 brightness)
            l.circle("c2", {.radius = 40.0f, .color = Color{1.0f, 1.0f, 1.0f, 1.0f}, .pos = {-40, -40, 0}});
            // Circle 3: Far above threshold (4.0 brightness - HDR)
            l.circle("c3", {.radius = 40.0f, .color = Color{4.0f, 4.0f, 4.0f, 1.0f}, .pos = {88, -40, 0}});
            
            // Bloom: Threshold 1.0, Blur 24, Intensity 1.5
            l.bloom(1.0f, 24.0f, 1.5f);
        });
        return s.build();
    });
    auto fb_bloom = renderer.render_frame(comp_bloom, 0);
    REQUIRE(fb_bloom != nullptr);
    verify_golden_or_create(*fb_bloom, "bloom-threshold-knee.png");

    // Scene 7: Color Pipeline Linear vs sRGB
    Composition comp_pipe(CompositionSpec{.width = 512, .height = 256}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        // Rect A: mix in sRGB (value converted from sRGB 0.5 => linear ~0.214)
        s.rect("rect_srgb", {
            .size = {200, 180},
            .color = Color{0.214f, 0.0f, 0.214f, 1.0f}, // mix rosso/blu in sRGB gamma mapped to linear
            .pos = {-216, -90, 0}
        });
        
        // Rect B: mix in linear space (0.5 linear)
        s.rect("rect_linear", {
            .size = {200, 180},
            .color = Color{0.5f, 0.0f, 0.5f, 1.0f}, // mix rosso/blu in pure linear space
            .pos = {16, -90, 0}
        });
        return s.build();
    });
    auto fb_pipe = renderer.render_frame(comp_pipe, 0);
    REQUIRE(fb_pipe != nullptr);
    verify_golden_or_create(*fb_pipe, "color-pipeline-linear-srgb.png");
}

// ── 2. GRADIENT TESTS ────────────────────────────────────────────────────────
TEST_CASE("Chronon3d Suite: Gradient Tests") {
    auto renderer = make_renderer();

    SUBCASE("Linear Gradient Mixing and Coordinates") {
        Composition comp(CompositionSpec{.width = 512, .height = 128}, [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            std::vector<graphics::GradientStop> stops = {
                {0.0f, Color::red()},
                {1.0f, Color::blue()}
            };
            s.rect("grad_rect", {
                .size = {512, 128},
                .pos = {0, 0, 0},
                .fill = FillStyle::linear({0.0f, 0.5f}, {1.0f, 0.5f}, stops)
            });
            return s.build();
        });
        auto fb = renderer.render_frame(comp, 0);
        REQUIRE(fb != nullptr);

        // Verification of colors:
        // Leftmost pixel (x=0) should be Red
        Color left = fb->get_pixel(1, 64);
        CHECK(left.r > 0.95f);
        CHECK(left.b < 0.05f);

        // Center pixel (x=256) should be 50% blend in linear space
        Color mid = fb->get_pixel(256, 64);
        CHECK(mid.r == doctest::Approx(0.5f).epsilon(0.05));
        CHECK(mid.b == doctest::Approx(0.5f).epsilon(0.05));

        // Rightmost pixel (x=511) should be Blue
        Color right = fb->get_pixel(510, 64);
        CHECK(right.b > 0.95f);
        CHECK(right.r < 0.05f);
    }

    SUBCASE("Multi-Stop Precision Checks") {
        Composition comp(CompositionSpec{.width = 512, .height = 128}, [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            std::vector<graphics::GradientStop> stops = {
                {0.0f, Color::red()},
                {0.25f, Color{1, 1, 0, 1}}, // yellow
                {0.5f, Color::green()},
                {0.75f, Color{0, 1, 1, 1}}, // cyan
                {1.0f, Color::blue()}
            };
            s.rect("multi_rect", {
                .size = {512, 128},
                .pos = {0, 0, 0},
                .fill = FillStyle::linear({0.0f, 0.5f}, {1.0f, 0.5f}, stops)
            });
            return s.build();
        });
        auto fb = renderer.render_frame(comp, 0);
        REQUIRE(fb != nullptr);

        // Check exact pixels at: x=0, 128, 256, 384, 511
        Color p0 = fb->get_pixel(0, 64);
        Color p128 = fb->get_pixel(128, 64);
        Color p256 = fb->get_pixel(256, 64);
        Color p384 = fb->get_pixel(384, 64);
        Color p511 = fb->get_pixel(511, 64);

        // p0 is red
        CHECK(p0.r > 0.9f);
        // p128 is yellow (mixed red/green in linear space)
        CHECK(p128.r > 0.4f);
        CHECK(p128.g > 0.4f);
        // p256 is green
        CHECK(p256.g > 0.9f);
        // p384 is cyan (mixed green/blue in linear)
        CHECK(p384.g > 0.4f);
        CHECK(p384.b > 0.4f);
        // p511 is blue
        CHECK(p511.b > 0.9f);
    }

    SUBCASE("Radial Gradient Clamping") {
        Composition comp(CompositionSpec{.width = 400, .height = 400}, [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            std::vector<graphics::GradientStop> stops = {
                {0.0f, Color::white()},
                {1.0f, Color::black()}
            };
            s.circle("radial_circle", {
                .radius = 200.0f,
                .pos = {0, 0, 0},
                .fill = FillStyle::radial({0.5f, 0.5f}, 0.5f, stops)
            });
            return s.build();
        });
        auto fb = renderer.render_frame(comp, 0);
        REQUIRE(fb != nullptr);

        // Center pixel (200, 200) should be White
        Color center = fb->get_pixel(200, 200);
        CHECK(center.r > 0.9f);
        CHECK(center.g > 0.9f);
        CHECK(center.b > 0.9f);

        // Pixel outside radius (e.g. 395, 200) should be Black
        Color outer = fb->get_pixel(395, 200);
        CHECK(outer.r < 0.05f);
        CHECK(outer.g < 0.05f);
        CHECK(outer.b < 0.05f);
    }

    SUBCASE("Conic Gradient Clamping") {
        Composition comp(CompositionSpec{.width = 400, .height = 400}, [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            std::vector<graphics::GradientStop> stops = {
                {0.0f, Color::red()},
                {1.0f, Color::blue()}
            };
            s.circle("conic_circle", {
                .radius = 200.0f,
                .pos = {0, 0, 0},
                .fill = FillStyle::conic({0.5f, 0.5f}, 0.0f, stops)
            });
            return s.build();
        });
        auto fb = renderer.render_frame(comp, 0);
        REQUIRE(fb != nullptr);

        // At angle 0 (right/east of center: e.g. x=300, y=200), t should be close to 0 (red)
        Color right = fb->get_pixel(300, 200);
        CHECK(right.r > 0.9f);
        CHECK(right.b < 0.1f);

        // At angle pi (left/west of center: e.g. x=100, y=200), t should be close to 0.5 (magenta/purple, mixed red/blue)
        Color left = fb->get_pixel(100, 200);
        CHECK(left.r > 0.4f);
        CHECK(left.b > 0.4f);

        // At angle 2*pi (approaching blue, e.g. y=190, x=300), t should be close to 1.0 (blue)
        Color bottom = fb->get_pixel(300, 190);
        CHECK(bottom.b > 0.4f);
    }
}

// ── 3. ADVANCED STROKE TESTS ──────────────────────────────────────────────────
TEST_CASE("Chronon3d Suite: Advanced Stroke Tests") {
    auto renderer = make_renderer();

    SUBCASE("Thickness 0 Does Not Draw") {
        Composition comp(CompositionSpec{.width = 100, .height = 100}, [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.line("zero_line", {
                .from = {10 - 50, 50 - 50, 0},
                .to = {90 - 50, 50 - 50, 0},
                .thickness = 0.0f,
                .color = Color::white()
            });
            return s.build();
        });
        auto fb = renderer.render_frame(comp, 0);
        REQUIRE(fb != nullptr);
        
        // Count pixels that are drawn (non-black)
        int drawn = 0;
        for (int y = 0; y < 100; ++y) {
            for (int x = 0; x < 100; ++x) {
                if (fb->get_pixel(x, y).a > 0.01f) drawn++;
            }
        }
        CHECK(drawn == 0);
    }

    SUBCASE("Negative Thickness Does Not Crash") {
        Composition comp(CompositionSpec{.width = 100, .height = 100}, [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.line("negative_line", {
                .from = {10 - 50, 50 - 50, 0},
                .to = {90 - 50, 50 - 50, 0},
                .thickness = -5.0f,
                .color = Color::white()
            });
            return s.build();
        });
        auto fb = renderer.render_frame(comp, 0);
        // Check that it doesn't crash and returns a valid framebuffer
        CHECK(fb != nullptr);
    }
}

// ── 4. SHADOW SYSTEM TESTS ──────────────────────────────────────────────────
TEST_CASE("Chronon3d Suite: Shadow System Tests") {
    auto renderer = make_renderer();

    SUBCASE("Drop Shadow Containment") {
        Composition comp(CompositionSpec{.width = 128, .height = 128}, [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.rect("box", {
                .size = {40, 40},
                .color = Color::white(),
                .pos = {0, 0, 0}
            });
            s.with_shadow({
                .enabled = true,
                .offset = {20.0f, 20.0f},
                .color = Color{0, 0, 0, 1},
                .radius = 0.0f // sharp shadow
            });
            return s.build();
        });
        comp.camera.transform.position.x = 0.0f;
        comp.camera.transform.position.y = 0.0f;
        comp.camera.transform.position.z = -comp.camera.focal_length(128.0f);

        auto fb = renderer.render_frame(comp, 0);
        REQUIRE(fb != nullptr);

        // Center should be white
        Color center = fb->get_pixel(64, 64);
        CHECK(center.r == 1.0f);
        CHECK(center.g == 1.0f);

        // Shadow offset by +20, +20: center 64+20 = 84, 84
        Color shadow_pixel = fb->get_pixel(84, 84);
        CHECK(shadow_pixel.r == 0.0f); // Should be black (shadow drawn behind)
        CHECK(shadow_pixel.a == doctest::Approx(1.0f).epsilon(0.001f));
    }
}

// ── 5. CINEMATIC BLOOM TESTS ──────────────────────────────────────────────────
TEST_CASE("Chronon3d Suite: Cinematic Bloom Tests") {
    auto renderer = make_renderer();

    Composition comp(CompositionSpec{.width = 512, .height = 128}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("l", [](LayerBuilder& l) {
            // Dark gray box below threshold
            l.rect("c1", {.size = {60, 60}, .color = Color{0.5f, 0.0f, 0.0f, 1.0f}, .pos = {-158, -30, 0}});
            // Super bright red box (HDR) above threshold
            l.rect("c2", {.size = {60, 60}, .color = Color{5.0f, 0.0f, 0.0f, 1.0f}, .pos = {98, -30, 0}});
            
            // Bloom: Threshold 1.0, radius 16, intensity 2.0
            l.bloom(1.0f, 16.0f, 2.0f);
        });
        return s.build();
    });
    comp.camera.transform.position.x = 256.0f;
    comp.camera.transform.position.y = 64.0f;
    comp.camera.transform.position.z = -comp.camera.focal_length(512.0f);

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);

    // Box 1 (brightness 0.5): should NOT spread red bloom around it
    // Check pixel at x=70 (outside box 1, which bounds 98..158)
    Color outside_b1 = fb->get_pixel(70, 64);
    CHECK(outside_b1.r < 0.01f);

    // Box 2 (brightness 5.0): SHOULD spread massive red bloom around it
    // Check pixel at x=320 (outside box 2, which bounds 354..414)
    Color outside_b2 = fb->get_pixel(320, 64);
    CHECK(outside_b2.r > 0.05f); // Bloomed!
}

// ── 6. COLOR PIPELINE TESTS ──────────────────────────────────────────────────
TEST_CASE("Chronon3d Suite: Color Pipeline Tests") {
    SUBCASE("Linear vs sRGB Mixing") {
        // Red = (1.0, 0, 0), Blue = (0, 0, 1.0)
        // 50% Mix in sRGB = sRGB(0.5, 0.0, 0.5)
        Color mix_srgb = Color{0.5f, 0.0f, 0.5f}.to_linear();
        
        // 50% Mix in Linear = (0.5, 0.0, 0.5) linear
        Color mix_linear = Color{0.5f, 0.0f, 0.5f};
        
        // They must be different!
        CHECK(mix_srgb.r != doctest::Approx(mix_linear.r));
        CHECK(mix_srgb.r == doctest::Approx(0.214f).epsilon(0.01f));
    }

    SUBCASE("Gamma Conversion") {
        // sRGB 0.5 => linear ~0.214
        CHECK(Color{0.5f, 0.0f, 0.0f}.to_linear().r == doctest::Approx(0.214f).epsilon(0.01f));
        // linear 0.214 => sRGB ~0.5
        CHECK(Color{0.214f, 0.0f, 0.0f}.to_srgb().r == doctest::Approx(0.5f).epsilon(0.01f));
    }

    SUBCASE("Exposure stops") {
        Color lin{0.5f, 0.5f, 0.5f};
        // Exposure +1 stop = multiply by 2.0
        Color exp_plus = lin * 2.0f;
        CHECK(exp_plus.r == doctest::Approx(1.0f));
        
        // Exposure -1 stop = multiply by 0.5
        Color exp_minus = lin * 0.5f;
        CHECK(exp_minus.r == doctest::Approx(0.25f));
    }

    SUBCASE("Contrast") {
        // contrast > 1.0 increases distance from 0.5
        auto adj = [](f32 v, f32 contrast) {
            return std::clamp((v - 0.5f) * contrast + 0.5f, 0.0f, 1.0f);
        };
        CHECK(adj(0.6f, 1.5f) > 0.6f);
        // contrast < 1.0 pulls closer to 0.5
        CHECK(adj(0.6f, 0.5f) < 0.6f);
    }

    SUBCASE("Reinhard Tone Mapping") {
        Color hdr{4.0f, 4.0f, 4.0f};
        // Reinhard: color / (color + 1)
        Color tone_mapped = hdr * (1.0f / (1.0f + 4.0f));
        CHECK(tone_mapped.r == doctest::Approx(0.8f));
        CHECK(tone_mapped.r < 1.0f); // Successfully compressed below 1.0!
    }

    SUBCASE("LUT Invert") {
        auto lut_invert = [](Color c) {
            // Rosso -> Cyan (0, 1, 1)
            // Verde -> Magenta (1, 0, 1)
            // Blu -> Giallo (1, 1, 0)
            return Color{1.0f - c.r, 1.0f - c.g, 1.0f - c.b, c.a};
        };
        Color red = Color::red();
        Color inverted = lut_invert(red);
        CHECK(inverted.r == 0.0f);
        CHECK(inverted.g == 1.0f);
        CHECK(inverted.b == 1.0f); // Cyan!
    }
}

// ── 7. ANIMATION DETERMINISM TESTS ───────────────────────────────────────────
TEST_CASE("Chronon3d Suite: Animation Tests") {
    auto renderer = make_renderer();

    Composition comp(CompositionSpec{.width = 256, .height = 256}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        // Linear animated rect position based on frame number (relative to canvas center 128, 128)
        float x_offset = static_cast<float>(ctx.frame);
        s.rect("animated_box", {
            .size = {50, 50},
            .color = Color::white(),
            .pos = {x_offset, 0.0f, 0}
        });
        return s.build();
    });

    // Render frame 50 twice and verify determinism
    auto fb_run1 = renderer.render_frame(comp, 50);
    auto fb_run2 = renderer.render_frame(comp, 50);
    REQUIRE(fb_run1 != nullptr);
    REQUIRE(fb_run2 != nullptr);

    CHECK(framebuffer_hash(*fb_run1) == framebuffer_hash(*fb_run2));
}

// ── 9b. IMAGEPROOFS GOLDEN REFERENCE ───────────────────────────────────────────
TEST_CASE("Chronon3d Suite: ImageProofs Golden Reference") {
    Composition comp = chronon3d::content::images::image_proofs();

    auto renderer = make_renderer();

    // Render frame 60: fully animated state
    auto fb = renderer.render_frame(comp, 60);
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width() == 1920);
    REQUIRE(fb->height() == 1080);

    verify_golden_or_create(*fb, "image_proofs_golden.png");

    for (int y = 0; y < fb->height(); y += 16) {
        for (int x = 0; x < fb->width(); x += 16) {
            Color c = fb->get_pixel(x, y).to_srgb();
            CHECK_FALSE(std::isnan(c.r));
            CHECK_FALSE(std::isnan(c.g));
            CHECK_FALSE(std::isnan(c.b));
            CHECK(c.r >= -0.01f);
            CHECK(c.r <= 1.01f);
            CHECK(c.g >= -0.01f);
            CHECK(c.g <= 1.01f);
            CHECK(c.b >= -0.01f);
            CHECK(c.b <= 1.01f);
        }
    }
}

// ── 9c. SHAPEPROOFS GOLDEN REFERENCE ───────────────────────────────────────────
TEST_CASE("Chronon3d Suite: ShapeProofs Golden Reference") {
    Composition comp = chronon3d::content::shapes::shape_proofs();

    auto renderer = make_renderer();

    // Render frame 60: fully animated state
    auto fb = renderer.render_frame(comp, 60);
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width() == 1920);
    REQUIRE(fb->height() == 1080);

    verify_golden_or_create(*fb, "shape_proofs_golden.png");

    for (int y = 0; y < fb->height(); y += 16) {
        for (int x = 0; x < fb->width(); x += 16) {
            Color c = fb->get_pixel(x, y).to_srgb();
            CHECK_FALSE(std::isnan(c.r));
            CHECK_FALSE(std::isnan(c.g));
            CHECK_FALSE(std::isnan(c.b));
            CHECK(c.r >= -0.01f);
            CHECK(c.r <= 1.01f);
            CHECK(c.g >= -0.01f);
            CHECK(c.g <= 1.01f);
            CHECK(c.b >= -0.01f);
            CHECK(c.b <= 1.01f);
        }
    }
}

// ── 10. SSAA 2× QUALITY VERIFICATION ───────────────────────────────────────────
TEST_CASE("Chronon3d Suite: SSAA 2x Quality Verification") {
    auto renderer = make_renderer_ssaa(2.0f);

    // 10b. Stroke dash/cap/join at SSAA 2× (edges benefit most from anti-aliasing)
    SUBCASE("Stroke SSAA 2x") {
        Composition comp(CompositionSpec{.width = 512, .height = 512}, [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.line("line_1px", {
                .from = {50 - 256, 50 - 256, 0},
                .to = {450 - 256, 50 - 256, 0},
                .thickness = 1.0f,
                .color = Color::white()
            });
            s.line("line_10px", {
                .from = {50 - 256, 100 - 256, 0},
                .to = {450 - 256, 100 - 256, 0},
                .thickness = 10.0f,
                .color = Color::red()
            });
            s.path("rect_stroke", {
                .commands = {
                    PathCommand::move_to({-100, -50}),
                    PathCommand::line_to({100, -50}),
                    PathCommand::line_to({100, 50}),
                    PathCommand::line_to({-100, 50}),
                },
                .stroke = {.width = 20.0f, .join = LineJoin::Round},
                .fill = Fill::solid_color(Color::transparent()),
                .color = Color{0, 1, 0, 1},
                .pos = {150 - 256, 220 - 256, 0},
                .closed = true
            });
            s.path("trimmed_path", {
                .commands = {
                    PathCommand::move_to({50, 350}),
                    PathCommand::line_to({250, 350})
                },
                .stroke = {.width = 8.0f, .cap = LineCap::Round, .trim_start = 0.1f, .trim_end = 0.9f},
                .color = Color::blue(),
                .pos = {-256, -256, 0}
            });
            s.path("cap_square", {
                .commands = {
                    PathCommand::move_to({50, 420}),
                    PathCommand::line_to({200, 420}),
                    PathCommand::line_to({200, 480})
                },
                .stroke = {.width = 12.0f, .cap = LineCap::Square, .join = LineJoin::Miter},
                .color = Color::white(),
                .pos = {-256, -256, 0}
            });
            s.path("join_bevel", {
                .commands = {
                    PathCommand::move_to({300, 420}),
                    PathCommand::line_to({450, 420}),
                    PathCommand::line_to({450, 480})
                },
                .stroke = {.width = 12.0f, .cap = LineCap::Butt, .join = LineJoin::Bevel},
                .color = Color{1, 1, 0, 1},
                .pos = {-256, -256, 0}
            });
            return s.build();
        });
        auto fb = renderer.render_frame(comp, 0);
        REQUIRE(fb != nullptr);
        verify_golden_or_create(*fb, "stroke-dash-cap-join_ssaa2.png");
    }

    // 10c. Drop shadow at SSAA 2× (shadow edges get anti-aliased)
    SUBCASE("Shadow SSAA 2x") {
        Composition comp(CompositionSpec{.width = 512, .height = 512}, [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.rect("square", {
                .size = {150, 150},
                .color = Color::white(),
                .pos = {-75, -75, 0}
            });
            s.with_shadow({
                .enabled = true,
                .offset = {30.0f, 30.0f},
                .color = Color{0.0f, 0.0f, 0.0f, 0.8f},
                .radius = 20.0f
            });
            return s.build();
        });
        auto fb = renderer.render_frame(comp, 0);
        REQUIRE(fb != nullptr);
        verify_golden_or_create(*fb, "shadow-drop-inner-contact_ssaa2.png");
    }

    // 10d. Cinematic bloom at SSAA 2× (bloom quality at higher resolution)
    SUBCASE("Bloom SSAA 2x") {
        Composition comp(CompositionSpec{.width = 512, .height = 128}, [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("l", [](LayerBuilder& l) {
                l.rect("c1", {.size = {60, 60}, .color = Color{0.5f, 0.0f, 0.0f, 1.0f}, .pos = {-158, -30, 0}});
                l.rect("c2", {.size = {60, 60}, .color = Color{5.0f, 0.0f, 0.0f, 1.0f}, .pos = {98, -30, 0}});
                l.bloom(1.0f, 16.0f, 2.0f);
            });
            return s.build();
        });
        comp.camera.transform.position.x = 256.0f;
        comp.camera.transform.position.y = 64.0f;
        comp.camera.transform.position.z = -comp.camera.focal_length(512.0f);

        auto fb = renderer.render_frame(comp, 0);
        REQUIRE(fb != nullptr);
        verify_golden_or_create(*fb, "bloom-threshold-knee_ssaa2.png");
    }
}


// ── 8. COMBINED STRESS TEST ──────────────────────────────────────────────────
TEST_CASE("Chronon3d Suite: Final Combined Stress Test") {
    auto renderer = make_renderer();

    Composition comp(CompositionSpec{.width = 512, .height = 512}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        float t = static_cast<float>(ctx.frame) / 100.0f; // animation progress 0.0 to 1.0
        
        s.layer("main_layer", [t](LayerBuilder& l) {
            // 1. Dark grid background
            l.grid_background("bg", {
                .size = {512, 512},
                .bg_color = Color{0.05f, 0.05f, 0.08f, 1.0f},
                .grid_color = Color{0.25f, 0.52f, 1.0f, 0.1f}
            });
            
            // 2. Card with radial gradient and dynamic position (offset from canvas center 256, 256)
            std::vector<graphics::GradientStop> stops = {
                {0.0f, Color{0.1f, 0.4f, 0.8f, 1.0f}},
                {1.0f, Color{0.01f, 0.05f, 0.2f, 1.0f}}
            };
            l.rounded_rect("card", {
                .size = {300, 300},
                .radius = 16.0f,
                .pos = {t * 20.0f - 150.0f, -150.0f, 0.0f},
                .fill = FillStyle::radial({0.5f, 0.5f}, 0.5f, stops)
            });
            
            // 3. Trimmed dashed outline shape
            float radius = 80.0f;
            float c = 0.5522847f * radius;
            std::vector<PathCommand> outline_cmds = {
                PathCommand::move_to({0.0f, -radius}),
                PathCommand::cubic_to({c, -radius}, {radius, -c}, {radius, 0.0f}),
                PathCommand::cubic_to({radius, c}, {c, radius}, {0.0f, radius}),
                PathCommand::cubic_to({-c, radius}, {-radius, c}, {-radius, 0.0f}),
                PathCommand::cubic_to({-radius, -c}, {-c, -radius}, {0.0f, -radius}),
                PathCommand::close()
            };
            l.path("outline", {
                .commands = outline_cmds,
                .stroke = {.width = 6.0f, .cap = LineCap::Round, .trim_start = t * 0.1f, .trim_end = 0.9f - t * 0.1f},
                .fill = Fill::solid_color(Color::transparent()),
                .color = Color{0.25f, 0.52f, 1.0f, 0.8f},
                .pos = {0, 0, 0},
                .closed = true
            });
            
            // 4. HDR element with cinematic bloom
            l.circle("hdr_core", {
                .radius = 25.0f,
                .color = Color{6.0f, 1.5f, 0.5f, 1.0f}, // Warm HDR glow
                .pos = {-25, -25, 0}
            });
            
            // Apply cumulative effects
            l.drop_shadow({15.0f, 15.0f}, Color{0, 0, 0, 0.6f}, 12.0f);
            l.bloom(1.2f, 20.0f, 1.8f);
            l.contrast(1.1f);
        });
        
        return s.build();
    });

    // Render multiple frames to ensure no crash, NaN, or out-of-range colors
    for (int frame : {0, 50, 100}) {
        auto fb = renderer.render_frame(comp, frame);
        REQUIRE(fb != nullptr);
        
        // Assert no NaNs and that all final sRGB colors are safe and in bounds
        for (int y = 0; y < fb->height(); y += 8) { // downsampled sweep for speed
            for (int x = 0; x < fb->width(); x += 8) {
                Color c = fb->get_pixel(x, y).to_srgb();
                CHECK_FALSE(std::isnan(c.r));
                CHECK_FALSE(std::isnan(c.g));
                CHECK_FALSE(std::isnan(c.b));
                CHECK_FALSE(std::isnan(c.a));
                CHECK(c.r >= -0.01f);
                CHECK(c.r <= 1.01f);
                CHECK(c.g >= -0.01f);
                CHECK(c.g <= 1.01f);
                CHECK(c.b >= -0.01f);
                CHECK(c.b <= 1.01f);
            }
        }
    }
}

#undef verify_golden_or_create
