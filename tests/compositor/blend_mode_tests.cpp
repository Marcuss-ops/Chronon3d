#include <doctest/doctest.h>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/backends/software/software_compositor.hpp>
#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/core/trace.hpp>

using namespace chronon3d;
using namespace chronon3d::compositor;

TEST_CASE("Test 11.1 — Normal blend mode math") {
    // Normal blend: src over dst
    Color src{1.0f, 0.0f, 0.0f, 0.5f}; // 50% opacity red
    Color dst{0.0f, 0.0f, 1.0f, 1.0f}; // opaque blue

    Color res = blend(src, dst, BlendMode::Normal);
    // Red over Blue at 50% opacity should blend colors
    CHECK(res.r == doctest::Approx(0.5f));
    CHECK(res.b == doctest::Approx(0.5f));
    CHECK(res.a == doctest::Approx(1.0f));
}

TEST_CASE("Test 11.2 — Add blend mode math") {
    Color src{0.5f, 0.0f, 0.0f, 0.5f};
    Color dst{0.3f, 0.0f, 0.0f, 0.5f};

    Color res = blend(src, dst, BlendMode::Add);
    CHECK(res.r == doctest::Approx(0.8f));
    CHECK(res.a == doctest::Approx(1.0f)); // 0.5 + 0.5 clamped to 1.0
}

TEST_CASE("Test 11.3 — Multiply blend mode math") {
    Color src{0.8f, 0.8f, 0.8f, 1.0f};
    Color dst{0.5f, 0.5f, 0.5f, 1.0f};

    Color res = blend(src, dst, BlendMode::Multiply);
    CHECK(res.r == doctest::Approx(0.4f));
    CHECK(res.g == doctest::Approx(0.4f));
}

TEST_CASE("Test 11.4 — Screen blend mode math") {
    Color src{0.8f, 0.8f, 0.8f, 1.0f};
    Color dst{0.5f, 0.5f, 0.5f, 1.0f};

    // 1 - (1 - 0.8) * (1 - 0.5) = 1 - 0.2 * 0.5 = 1 - 0.1 = 0.9
    Color res = blend(src, dst, BlendMode::Screen);
    CHECK(res.r == doctest::Approx(0.9f));
}

TEST_CASE("Test 11.5 — Overlay blend mode math") {
    Color src{0.3f, 0.8f, 0.0f, 1.0f};
    Color dst{0.4f, 0.6f, 0.0f, 1.0f};

    // overlay logic:
    // for r: dst < 0.5 -> 2.0 * s * d = 2.0 * 0.3 * 0.4 = 0.24
    // for g: dst >= 0.5 -> 1.0 - 2.0 * (1 - 0.8) * (1 - 0.6) = 1.0 - 2.0 * 0.2 * 0.4 = 1.0 - 0.16 = 0.84
    Color res = blend(src, dst, BlendMode::Overlay);
    CHECK(res.r == doctest::Approx(0.24f));
    CHECK(res.g == doctest::Approx(0.84f));
}

TEST_CASE("Test 11.6 — Blend mode guard: opacity 0 leaves background unchanged") {
    Color src{1.0f, 0.0f, 0.0f, 0.0f}; // fully transparent red
    Color dst{0.0f, 1.0f, 0.0f, 1.0f}; // green

    for (auto mode : {BlendMode::Normal, BlendMode::Add, BlendMode::Multiply, BlendMode::Screen, BlendMode::Overlay}) {
        Color res = blend(src, dst, mode);
        // Green color should remain unchanged
        CHECK(res.g == doctest::Approx(1.0f));
        CHECK(res.r == doctest::Approx(0.0f));
        CHECK(res.a == doctest::Approx(1.0f));
    }
}

TEST_CASE("Test 11.7 — Blend mode guard: opacity 1 covers background under Normal mode") {
    Color src{1.0f, 0.0f, 0.0f, 1.0f}; // opaque red
    Color dst{0.0f, 1.0f, 0.0f, 1.0f}; // green

    Color res = blend(src, dst, BlendMode::Normal);
    CHECK(res.r == doctest::Approx(1.0f));
    CHECK(res.g == doctest::Approx(0.0f));
    CHECK(res.a == doctest::Approx(1.0f));
}

// ---------------------------------------------------------------------------
// Telemetry / counter tests for SIMD compositing path
// ---------------------------------------------------------------------------

TEST_CASE("compositor_normal_uses_simd_counter") {
    RenderCounters counters;
    profiling::g_current_counters = &counters;

    Framebuffer dst(1920, 1080);
    Framebuffer src(1920, 1080);

    dst.clear(Color{0, 0, 0, 1});
    src.clear(Color{1, 0, 0, 0.5f});

    SoftwareCompositor::composite_layer(dst, src, BlendMode::Normal);

    REQUIRE(counters.simd_lerp_calls.load() > 0);
    REQUIRE(counters.pixels_touched.load() == 0); // not set by compositor directly

    profiling::g_current_counters = nullptr;
}

TEST_CASE("compositor_normal_simd_matches_scalar") {
    Framebuffer src(1920, 1080);
    Framebuffer dst_simd(1920, 1080);
    Framebuffer dst_scalar(1920, 1080);

    // Fill src with varied semi-transparent premultiplied colors
    for (int y = 0; y < 1080; ++y) {
        for (int x = 0; x < 1920; ++x) {
            float u = static_cast<float>(x) / 1919.0f;
            float v = static_cast<float>(y) / 1079.0f;
            float a = 0.5f + u * 0.5f;
            src.set_pixel(x, y, Color{u * a, v * a, (1.0f - u * v) * a, a});
            dst_simd.set_pixel(x, y, Color{0.1f, 0.2f, 0.3f, 1.0f});
            dst_scalar.set_pixel(x, y, Color{0.1f, 0.2f, 0.3f, 1.0f});
        }
    }

    // SIMD path
    SoftwareCompositor::composite_layer(dst_simd, src, BlendMode::Normal);

    // Scalar path — apply premultiplied OVER blend formula
    const i32 w = 1920, h = 1080;
    for (i32 y = 0; y < h; ++y) {
        for (i32 x = 0; x < w; ++x) {
            Color s = src.get_pixel(x, y);
            if (s.a <= 0.0f) continue;
            Color d = dst_scalar.get_pixel(x, y);
            const float inv_a = 1.0f - s.a;
            dst_scalar.set_pixel(x, y, Color{
                s.r + d.r * inv_a,
                s.g + d.g * inv_a,
                s.b + d.b * inv_a,
                s.a + d.a * inv_a
            });
        }
    }

    // Compare every pixel
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Color simd = dst_simd.get_pixel(x, y);
            Color scalar = dst_scalar.get_pixel(x, y);
            CHECK(simd.r == doctest::Approx(scalar.r).epsilon(0.0001f));
            CHECK(simd.g == doctest::Approx(scalar.g).epsilon(0.0001f));
            CHECK(simd.b == doctest::Approx(scalar.b).epsilon(0.0001f));
            CHECK(simd.a == doctest::Approx(scalar.a).epsilon(0.0001f));
        }
    }
}
