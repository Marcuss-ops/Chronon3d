// ============================================================================
// test_per_pixel_dof.cpp — Tests for per-pixel depth-of-field blur
//
// Tests:
//   1. Disc blur kernel correctness (apply_per_pixel_dof)
//   2. Depth-based blur: far pixels get more blur than near
//   3. Depth buffer tracking during compositing (CompositeNode)
//   4. Edge cases: disabled DOF, zero radius, mismatched buffers, etc.
// ============================================================================

#include <doctest/doctest.h>

#include <chronon3d/backends/software/utils/effects/per_pixel_dof.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/model/camera/dof.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

#include <vector>
#include <cmath>
#include <algorithm>
#include <tests/helpers/test_utils.hpp>
using namespace chronon3d;


static const LensModel kDefaultLens;

// ── Helper: create a uniform depth buffer ──────────────────────────────────
static std::vector<float> make_depth_buffer(i32 w, i32 h, float world_z) {
    return std::vector<float>(static_cast<size_t>(w) * h, world_z);
}

// ── Helper: create a two-zone depth buffer ─────────────────────────────────
// Left half = near_z, right half = far_z
static std::vector<float> make_two_zone_depth(i32 w, i32 h, float near_z, float far_z) {
    std::vector<float> depth(static_cast<size_t>(w) * h);
    for (i32 y = 0; y < h; ++y) {
        for (i32 x = 0; x < w; ++x) {
            depth[static_cast<size_t>(y) * w + x] = (x < w / 2) ? near_z : far_z;
        }
    }
    return depth;
}

// ── Helper: measure variance in a region (blur indicator) ──────────────────
// Higher variance = sharper edges = less blur
// Lower variance = more blurred = more blur
static float region_variance(const Framebuffer& fb, i32 x0, i32 y0, i32 x1, i32 y1) {
    float sum = 0.0f, sum_sq = 0.0f;
    int count = 0;
    for (i32 y = y0; y < y1; ++y) {
        for (i32 x = x0; x < x1; ++x) {
            Color c = fb.get_pixel(x, y);
            // Use luminance as a single metric
            float lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
            sum += lum;
            sum_sq += lum * lum;
            ++count;
        }
    }
    if (count == 0) return 0.0f;
    float mean = sum / static_cast<float>(count);
    return (sum_sq / static_cast<float>(count)) - mean * mean;
}

// ============================================================================
// Section 1: apply_per_pixel_dof() — Kernel correctness
// ============================================================================

TEST_CASE("PerPixelDOF: disabled DOF is no-op") {
    const i32 w = 32, h = 32;
    Framebuffer fb(w, h);
    fb.clear(Color{1.0f, 0.0f, 0.0f, 1.0f});

    auto depth = make_depth_buffer(w, h, -500.0f);
    DepthOfFieldSettings dof{.enabled = false, .focus_z = 0.0f, .aperture = 0.02f, .max_blur = 10.0f};

    renderer::apply_per_pixel_dof(fb, depth, dof, kDefaultLens);

    // Pixel should be unchanged
    Color c = fb.get_pixel(16, 16);
    CHECK(c.r == doctest::Approx(1.0f));
    CHECK(c.g == doctest::Approx(0.0f));
}

TEST_CASE("PerPixelDOF: mismatched depth buffer size is no-op") {
    const i32 w = 32, h = 32;
    Framebuffer fb(w, h);
    fb.clear(Color{0.0f, 1.0f, 0.0f, 1.0f});

    std::vector<float> depth(10, -500.0f); // wrong size
    DepthOfFieldSettings dof{.enabled = true, .focus_z = 0.0f, .aperture = 0.02f, .max_blur = 10.0f};

    renderer::apply_per_pixel_dof(fb, depth, dof, kDefaultLens);

    Color c = fb.get_pixel(16, 16);
    CHECK(c.g == doctest::Approx(1.0f)); // unchanged
}

TEST_CASE("PerPixelDOF: zero blur radius (at focus) is no-op") {
    const i32 w = 32, h = 32;
    Framebuffer fb(w, h);
    fb.clear(Color{0.0f, 0.0f, 1.0f, 1.0f});

    // All pixels at focus_z → blur radius = 0
    auto depth = make_depth_buffer(w, h, 0.0f);
    DepthOfFieldSettings dof{.enabled = true, .focus_z = 0.0f, .aperture = 0.02f, .max_blur = 10.0f};

    renderer::apply_per_pixel_dof(fb, depth, dof, kDefaultLens);

    Color c = fb.get_pixel(16, 16);
    CHECK(c.b == doctest::Approx(1.0f)); // unchanged
}

TEST_CASE("PerPixelDOF: empty framebuffer (all transparent) survives blur") {
    const i32 w = 32, h = 32;
    Framebuffer fb(w, h);
    fb.clear(Color::transparent());

    auto depth = make_depth_buffer(w, h, -1000.0f);
    DepthOfFieldSettings dof{.enabled = true, .focus_z = 0.0f, .aperture = 0.02f, .max_blur = 20.0f};

    renderer::apply_per_pixel_dof(fb, depth, dof, kDefaultLens);

    // Should still be transparent (no crash, no garbage)
    Color c = fb.get_pixel(16, 16);
    CHECK(c.a == doctest::Approx(0.0f));
}

TEST_CASE("PerPixelDOF: unset depth pixels are left unblurred") {
    const i32 w = 32, h = 32;
    Framebuffer fb(w, h);

    // Fill with a sharp red square in the center
    fb.clear(Color::transparent());
    for (i32 y = 10; y < 22; ++y) {
        for (i32 x = 10; x < 22; ++x) {
            fb.set_pixel(x, y, Color{1.0f, 0.0f, 0.0f, 1.0f});
        }
    }

    // Depth buffer: all sentinel (unset)
    std::vector<float> depth(static_cast<size_t>(w) * h, 1e18f);
    DepthOfFieldSettings dof{.enabled = true, .focus_z = 0.0f, .aperture = 0.02f, .max_blur = 10.0f};

    renderer::apply_per_pixel_dof(fb, depth, dof, kDefaultLens);

    // Center pixel should still be sharp red (no blur applied)
    Color c = fb.get_pixel(15, 15);
    CHECK(c.r == doctest::Approx(1.0f));
    CHECK(c.a == doctest::Approx(1.0f));
}

// ============================================================================
// Section 2: Depth-based blur — far pixels get more blur than near
// ============================================================================

TEST_CASE("PerPixelDOF: far pixels blur more than near pixels") {
    const i32 w = 80, h = 80;
    Framebuffer fb(w, h);
    fb.clear(Color::transparent());

    // Draw two identical sharp white squares: left (near), right (far)
    for (i32 y = 30; y < 50; ++y) {
        for (i32 x = 5; x < 25; ++x) {
            fb.set_pixel(x, y, Color{1.0f, 1.0f, 1.0f, 1.0f});
        }
        for (i32 x = 50; x < 70; ++x) {
            fb.set_pixel(x, y, Color{1.0f, 1.0f, 1.0f, 1.0f});
        }
    }

    // Near layer at z=-300 (blur≈6), far layer at z=-1500 (blur≈24)
    auto depth = make_two_zone_depth(w, h, -300.0f, -1500.0f);
    DepthOfFieldSettings dof{.enabled = true, .focus_z = 0.0f, .aperture = 0.02f, .max_blur = 24.0f};

    renderer::apply_per_pixel_dof(fb, depth, dof, kDefaultLens);

    // Measure average alpha in a strip just outside each square's right edge.
    // Near square right edge at x=25, far square right edge at x=70.
    // A larger blur radius spills more colour (and alpha) into the transparent
    // region outside the square.
    auto measure_avg_alpha = [&](i32 sx0, i32 sy0, i32 sx1, i32 sy1) -> float {
        float sum = 0.0f;
        int count = 0;
        for (i32 y = sy0; y < sy1; ++y) {
            for (i32 x = sx0; x < sx1; ++x) {
                sum += fb.get_pixel(x, y).a;
                ++count;
            }
        }
        return count > 0 ? sum / static_cast<float>(count) : 0.0f;
    };

    // Strip just outside right edge of each square (was transparent before blur)
    float near_spill = measure_avg_alpha(26, 34, 32, 46); // 1-7px outside near square
    float far_spill  = measure_avg_alpha(71, 34, 77, 46); // 1-7px outside far square

    // Far blur (24px radius) spills more alpha into neighboring transparent
    // region than near blur (6px radius).
    CHECK(far_spill > near_spill);
}

TEST_CASE("PerPixelDOF: blur radius increases with depth distance from focus") {
    DepthOfFieldSettings dof{.enabled = true, .focus_z = 0.0f, .aperture = 0.01f, .max_blur = 50.0f};

    float blur_near   = compute_dof_blur_radius(dof, kDefaultLens, -200.0f);
    float blur_mid    = compute_dof_blur_radius(dof, kDefaultLens, -500.0f);
    float blur_far    = compute_dof_blur_radius(dof, kDefaultLens, -1000.0f);

    CHECK(blur_near < blur_mid);
    CHECK(blur_mid  < blur_far);
    CHECK(blur_near == doctest::Approx(2.0f));   // 200 * 0.01
    CHECK(blur_mid  == doctest::Approx(5.0f));   // 500 * 0.01
    CHECK(blur_far  == doctest::Approx(10.0f));  // 1000 * 0.01
}

TEST_CASE("PerPixelDOF: symmetric depth produces equal blur") {
    DepthOfFieldSettings dof{.enabled = true, .focus_z = 0.0f, .aperture = 0.02f, .max_blur = 20.0f};

    float blur_neg = compute_dof_blur_radius(dof, kDefaultLens, -500.0f);
    float blur_pos = compute_dof_blur_radius(dof, kDefaultLens, 500.0f);

    CHECK(blur_neg == doctest::Approx(blur_pos));
}

TEST_CASE("PerPixelDOF: large blur spreads color beyond original bounds") {
    const i32 w = 32, h = 32;
    Framebuffer fb(w, h);
    fb.clear(Color::transparent());

    // Single white pixel at center
    fb.set_pixel(16, 16, Color{1.0f, 1.0f, 1.0f, 1.0f});

    // Large blur radius
    auto depth = make_depth_buffer(w, h, -2000.0f);
    DepthOfFieldSettings dof{.enabled = true, .focus_z = 0.0f, .aperture = 0.02f, .max_blur = 8.0f};

    renderer::apply_per_pixel_dof(fb, depth, dof, kDefaultLens);

    // A pixel nearby should now have some alpha (color spread)
    Color neighbor = fb.get_pixel(18, 16);
    CHECK(neighbor.a > 0.0f);
}

TEST_CASE("PerPixelDOF: focus plane pixels remain sharp while defocused blur") {
    const i32 w = 64, h = 32;
    Framebuffer fb(w, h);
    fb.clear(Color::transparent());

    // Left half: white square at focus (z=0)
    // Right half: white square defocused (z=-1000)
    for (i32 y = 8; y < 24; ++y) {
        for (i32 x = 8; x < 24; ++x) {
            fb.set_pixel(x, y, Color{1.0f, 1.0f, 1.0f, 1.0f});
        }
        for (i32 x = 40; x < 56; ++x) {
            fb.set_pixel(x, y, Color{1.0f, 1.0f, 1.0f, 1.0f});
        }
    }

    auto depth = make_two_zone_depth(w, h, 0.0f, -1000.0f);
    DepthOfFieldSettings dof{.enabled = true, .focus_z = 0.0f, .aperture = 0.02f, .max_blur = 20.0f};

    renderer::apply_per_pixel_dof(fb, depth, dof, kDefaultLens);

    // Focus region: should remain fully opaque white
    Color focus_c = fb.get_pixel(16, 16);
    CHECK(focus_c.r == doctest::Approx(1.0f));
    CHECK(focus_c.a == doctest::Approx(1.0f));

    // Defocused region: should be blurred (lower variance)
    float focus_var = region_variance(fb, 10, 10, 22, 22);
    float defocus_var = region_variance(fb, 42, 10, 54, 22);
    CHECK(focus_var >= defocus_var);
}

// ============================================================================
// Section 3: Edge cases
// ============================================================================

TEST_CASE("PerPixelDOF: 1x1 framebuffer does not crash") {
    Framebuffer fb(1, 1);
    fb.clear(Color{0.5f, 0.5f, 0.5f, 1.0f});

    std::vector<float> depth{1, -500.0f};
    DepthOfFieldSettings dof{.enabled = true, .focus_z = 0.0f, .aperture = 0.02f, .max_blur = 10.0f};

    renderer::apply_per_pixel_dof(fb, depth, dof, kDefaultLens);

    Color c = fb.get_pixel(0, 0);
    CHECK(c.a > 0.0f); // still rendered
}

TEST_CASE("PerPixelDOF: clip rectangle limits blur region") {
    const i32 w = 32, h = 32;
    Framebuffer fb(w, h);
    fb.clear(Color{1.0f, 0.0f, 0.0f, 1.0f});

    auto depth = make_depth_buffer(w, h, -1000.0f);
    DepthOfFieldSettings dof{.enabled = true, .focus_z = 0.0f, .aperture = 0.02f, .max_blur = 10.0f};

    // Only blur the left half
    raster::BBox clip{0, 0, 16, 32};
    renderer::apply_per_pixel_dof(fb, depth, dof, kDefaultLens, clip);

    // Right half should be untouched (still solid red)
    Color right = fb.get_pixel(24, 16);
    CHECK(right.r == doctest::Approx(1.0f));
    CHECK(right.g == doctest::Approx(0.0f));
}

TEST_CASE("PerPixelDOF: max_blur clamps the blur radius") {
    DepthOfFieldSettings dof{.enabled = true, .focus_z = 0.0f, .aperture = 1.0f, .max_blur = 5.0f};

    float blur = compute_dof_blur_radius(dof, kDefaultLens, -1000.0f);
    CHECK(blur == doctest::Approx(5.0f)); // clamped, not 1000
}

TEST_CASE("PerPixelDOF: max_r < 0.5 returns early (no visible blur)") {
    const i32 w = 32, h = 32;
    Framebuffer fb(w, h);
    fb.clear(Color{0.0f, 1.0f, 0.0f, 1.0f});

    // Very small aperture → blur radius < 0.5
    auto depth = make_depth_buffer(w, h, -10.0f);
    DepthOfFieldSettings dof{.enabled = true, .focus_z = 0.0f, .aperture = 0.001f, .max_blur = 20.0f};

    renderer::apply_per_pixel_dof(fb, depth, dof, kDefaultLens);

    // Should be unchanged (early return)
    Color c = fb.get_pixel(16, 16);
    CHECK(c.g == doctest::Approx(1.0f));
}

// ============================================================================
// Section 4: Integration — full render with per-pixel DOF
// ============================================================================

TEST_CASE("PerPixelDOF: end-to-end render with DOF does not crash") {
    auto rend = test::make_renderer();
    RenderSettings settings;
    settings.use_modular_graph = true;
    rend.set_settings(settings);

    Composition comp(CompositionSpec{.width = 80, .height = 80, .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.camera().set({
                .enabled = true,
                .position = {0, 0, -1000},
                .zoom = 1000.0f,
                .dof = DepthOfFieldSettings{
                    .enabled  = true,
                    .focus_z  = 0.0f,
                    .aperture = 0.02f,
                    .max_blur = 12.0f
                }
            });
            s.rect("bg", {.size = {80, 80}, .color = Color{0.1f, 0.1f, 0.1f, 1}, .pos = {40, 40, 0}});
            s.layer("near", [](LayerBuilder& l) {
                l.enable_3d(true).position({20, 40, -400});
                l.rounded_rect("r", {.size = {20, 20}, .radius = 4, .color = Color::red()});
            });
            s.layer("focus", [](LayerBuilder& l) {
                l.enable_3d(true).position({40, 40, 0});
                l.rounded_rect("r", {.size = {20, 20}, .radius = 4, .color = Color::white()});
            });
            s.layer("far", [](LayerBuilder& l) {
                l.enable_3d(true).position({60, 40, -1500});
                l.rounded_rect("r", {.size = {20, 20}, .radius = 4, .color = Color::blue()});
            });
            return s.build();
        });

    auto fb = rend.render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 80);
    CHECK(fb->height() == 80);

    // Focus layer should be sharpest (highest variance)
    // Near and far should be more blurred
    float focus_var = region_variance(*fb, 35, 35, 45, 45);
    float near_var  = region_variance(*fb, 15, 35, 25, 45);
    float far_var   = region_variance(*fb, 55, 35, 65, 45);

    // Focus region should be at least as sharp as defocused regions
    CHECK(focus_var >= near_var * 0.5f);
    CHECK(focus_var >= far_var * 0.5f);
}
