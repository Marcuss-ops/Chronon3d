#include <doctest/doctest.h>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/dirty_fallback_reason.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <cmath>

using namespace chronon3d;

namespace {

bool tile_fb_pixel_match(const Framebuffer& a, const Framebuffer& b,
                         int& mismatches, float epsilon = 1e-4f) {
    if (a.width() != b.width() || a.height() != b.height()) return false;
    bool ok = true;
    mismatches = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            Color ca = a.get_pixel(x, y);
            Color cb = b.get_pixel(x, y);
            if (std::abs(ca.r - cb.r) > epsilon ||
                std::abs(ca.g - cb.g) > epsilon ||
                std::abs(ca.b - cb.b) > epsilon ||
                std::abs(ca.a - cb.a) > epsilon) {
                ok = false;
                ++mismatches;
            }
        }
    }
    return ok;
}

// ── Tiny moving circle scene for equivalence tests ──────────────────────
Composition make_moving_circle_comp(int width, int height, int duration) {
    return Composition(CompositionSpec{
        .name = "MovingCircleTile", .width = width, .height = height, .duration = duration
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        // Static background
        s.rect("bg", {
            .size = {200.0f, 150.0f},
            .color = Color{0.1f, 0.15f, 0.2f, 1.0f},
            .pos = {100.0f, 75.0f, 0}
        });
        // Small animated circle
        float x = 40.0f + static_cast<float>(ctx.frame.frame) * 4.0f;
        s.circle("ball", {
            .radius = 12.0f,
            .color = Color::red(),
            .pos = {x, 75.0f, 0}
        });
        return s.build();
    });
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// Test 1 — Smoke test: render with tiles enabled, verify no crash
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("Dirty Tiles: Smoke test — renders without crash with tile settings") {
    const int W = 200, H = 150;
    Composition comp = make_moving_circle_comp(W, H, 5);

    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    settings.dirty.tile_size = 32;
    settings.dirty.use_bitmask = true;
    settings.dirty.enabled = true;
    renderer.set_settings(settings);

    // Render several frames — must not crash
    for (Frame f = 0; f < 5; ++f) {
        auto fb = renderer.render_frame(comp, f);
        REQUIRE(fb != nullptr);
        CHECK(fb->width() == W);
        CHECK(fb->height() == H);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Test 2 — Pixel-perfect equivalence: tiles enabled vs disabled
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("Dirty Tiles: Pixel-perfect equivalence with moving element") {
    const int W = 160, H = 120;
    const int kFrames = 12;
    Composition comp = make_moving_circle_comp(W, H, kFrames);

    // Baseline: no dirty rects, no tiles
    SoftwareRenderer baseline;
    {
        RenderSettings s;
        s.use_modular_graph = true;
        s.dirty.enabled = false;
        baseline.set_settings(s);
    }

    // Optimized: dirty rects + tiles
    SoftwareRenderer opt;
    {
        RenderSettings s;
        s.use_modular_graph = true;
        s.dirty.enabled = true;
        s.dirty.use_bitmask = true;
        s.dirty.tile_size = 32;
        opt.set_settings(s);
    }

    int total_mismatches = 0;
    for (Frame f = 0; f < kFrames; ++f) {
        auto fb_b = baseline.render_frame(comp, f);
        auto fb_o = opt.render_frame(comp, f);
        REQUIRE(fb_b != nullptr);
        REQUIRE(fb_o != nullptr);

        int mism = 0;
        CHECK(tile_fb_pixel_match(*fb_b, *fb_o, mism));
        total_mismatches += mism;
    }
    CHECK(total_mismatches == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Test 3 — Dirty rect still works with tiles enabled
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("Dirty Tiles: Dirty rect counters still active with tiles on") {
    const int W = 200, H = 150;
    Composition comp = make_moving_circle_comp(W, H, 5);

    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    settings.dirty.enabled = true;
    settings.dirty.use_bitmask = true;
    settings.dirty.tile_size = 32;
    renderer.set_settings(settings);

    renderer.render_frame(comp, 0);
    renderer.counters()->reset();
    renderer.render_frame(comp, 1);

    const auto* counters = renderer.counters();
    uint64_t dirty_px = counters->dirty_pixels.load();
    uint64_t dirty_count = counters->dirty_rect_count.load();
    uint64_t full_px = static_cast<uint64_t>(W) * H;

    // The existing dirty rect counters should still be populated
    CHECK(dirty_count > 0);
    CHECK(dirty_px > 0);
    CHECK(dirty_px < full_px);  // small moving object = partial frame
}

// ═════════════════════════════════════════════════════════════════════════════
// Test 4 — First frame (no prev framebuffer) uses full frame
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("Dirty Tiles: First frame renders full frame (no prev fb)") {
    const int W = 160, H = 120;
    Composition comp = make_moving_circle_comp(W, H, 3);

    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    settings.dirty.enabled = true;
    settings.dirty.use_bitmask = true;
    settings.dirty.tile_size = 32;
    renderer.set_settings(settings);

    // Frame 0 has no previous framebuffer → full render
    auto fb0 = renderer.render_frame(comp, 0);
    REQUIRE(fb0 != nullptr);

    // Verify the output is correct (non-empty)
    int opaque_pixels = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (fb0->get_pixel(x, y).a > 0.01f) ++opaque_pixels;
    CHECK(opaque_pixels > 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Test 5 — Multiple tile sizes all produce correct output
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("Dirty Tiles: Correct output across different tile sizes") {
    const int W = 128, H = 128;
    Composition comp = make_moving_circle_comp(W, H, 3);

    std::vector<int> tile_sizes = {16, 32, 64};

    for (int tsize : tile_sizes) {
        // Baseline
        SoftwareRenderer baseline;
        {
            RenderSettings s;
            s.use_modular_graph = true;
            s.dirty.enabled = false;
            baseline.set_settings(s);
        }

        // With tiles
        SoftwareRenderer opt;
        {
            RenderSettings s;
            s.use_modular_graph = true;
            s.dirty.enabled = true;
            s.dirty.use_bitmask = true;
            s.dirty.tile_size = tsize;
            opt.set_settings(s);
        }

        for (Frame f = 0; f < 3; ++f) {
            auto fb_b = baseline.render_frame(comp, f);
            auto fb_o = opt.render_frame(comp, f);
            REQUIRE(fb_b != nullptr);
            REQUIRE(fb_o != nullptr);

            int mism = 0;
            INFO("tile_size=", tsize, " frame=", static_cast<int>(f));
            CHECK(tile_fb_pixel_match(*fb_b, *fb_o, mism));
            CHECK(mism == 0);
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Test 6 — Two objects far apart: verify correct output
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("Dirty Tiles: Two distant moving objects render correctly") {
    const int W = 200, H = 150;

    Composition comp(CompositionSpec{
        .name = "TwoDistantObjects", .width = W, .height = H, .duration = 6
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);

        // Static background
        s.rect("bg", {
            .size = {static_cast<float>(W), static_cast<float>(H)},
            .color = Color{0.08f, 0.12f, 0.18f, 1.0f},
            .pos = {static_cast<float>(W) * 0.5f, static_cast<float>(H) * 0.5f, 0}
        });

        // Object A: top-left
        float ax = 25.0f + static_cast<float>(ctx.frame.frame) * 3.0f;
        s.circle("objA", {
            .radius = 10.0f,
            .color = Color::red(),
            .pos = {ax, 35.0f, 0}
        });

        // Object B: bottom-right
        float bx = 175.0f - static_cast<float>(ctx.frame.frame) * 3.0f;
        s.circle("objB", {
            .radius = 10.0f,
            .color = Color::green(),
            .pos = {bx, 115.0f, 0}
        });

        return s.build();
    });

    // Baseline vs optimized
    SoftwareRenderer baseline;
    {
        RenderSettings s;
        s.use_modular_graph = true;
        s.dirty.enabled = false;
        baseline.set_settings(s);
    }

    SoftwareRenderer opt;
    {
        RenderSettings s;
        s.use_modular_graph = true;
        s.dirty.enabled = true;
        s.dirty.use_bitmask = true;
        s.dirty.tile_size = 32;
        opt.set_settings(s);
    }

    int total_mism = 0;
    for (Frame f = 0; f < 6; ++f) {
        auto fb_b = baseline.render_frame(comp, f);
        auto fb_o = opt.render_frame(comp, f);
        REQUIRE(fb_b != nullptr);
        REQUIRE(fb_o != nullptr);

        int mism = 0;
        CHECK(tile_fb_pixel_match(*fb_b, *fb_o, mism));
        total_mism += mism;
    }
    CHECK(total_mism == 0);
}
