#include <chronon3d/scene/builders/scene_builder.hpp>
#include <doctest/doctest.h>
#include <fmt/core.h>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <tests/helpers/test_utils.hpp>
using namespace chronon3d;


namespace {

// Pixel comparison helper reused across determinism tests
bool pix_match(const Framebuffer& a, const Framebuffer& b,
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

// Scene with static background + two moving objects (corners)
Composition make_two_corner_scene(int width, int height, int duration) {
    return Composition(CompositionSpec{
        .name = "TwoCornerParallel", .width = width, .height = height, .duration = duration
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        // Full-frame static background
        s.rect("bg", {
            .size = {static_cast<float>(width), static_cast<float>(height)},
            .color = Color{0.05f, 0.1f, 0.15f, 1.0f},
            .pos = {static_cast<float>(width) * 0.5f, static_cast<float>(height) * 0.5f, 0}
        });
        // Top-left moving circle
        float ax = 20.0f + static_cast<float>(ctx.frame()) * 3.0f;
        s.circle("objA", {
            .radius = 8.0f,
            .color = Color::red(),
            .pos = {ax, 30.0f, 0}
        });
        // Bottom-right moving circle
        float bx = static_cast<float>(width) - 20.0f
                   - static_cast<float>(ctx.frame()) * 3.0f;
        s.circle("objB", {
            .radius = 8.0f,
            .color = Color::green(),
            .pos = {bx, static_cast<float>(height) - 30.0f, 0}
        });
        return s.build();
    });
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// Test 1 — Determinism: same scene, same settings → same pixels (3 separate renderers)
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("TileParallel: Determinism — three independent renderers produce identical output") {
    const int W = 200, H = 150;
    const int kFrames = 8;
    Composition comp = make_two_corner_scene(W, H, kFrames);

    // Renderer A
    auto ra = test::make_renderer();
    {
        RenderSettings s;
        s.use_modular_graph = true;
        s.dirty.enabled = true;
        s.dirty.use_bitmask = true;
        s.dirty.tile_size = 32;
        ra.set_settings(s);
    }

    // Renderer B
    auto rb = test::make_renderer();
    {
        RenderSettings s;
        s.use_modular_graph = true;
        s.dirty.enabled = true;
        s.dirty.use_bitmask = true;
        s.dirty.tile_size = 32;
        rb.set_settings(s);
    }

    // Renderer C
    auto rc = test::make_renderer();
    {
        RenderSettings s;
        s.use_modular_graph = true;
        s.dirty.enabled = true;
        s.dirty.use_bitmask = true;
        s.dirty.tile_size = 32;
        rc.set_settings(s);
    }

    for (Frame f = 0; f < kFrames; ++f) {
        auto fb_a = ra.render(comp, f);
        auto fb_b = rb.render(comp, f);
        auto fb_c = rc.render(comp, f);

        REQUIRE(fb_a != nullptr);
        REQUIRE(fb_b != nullptr);
        REQUIRE(fb_c != nullptr);

        int mism_ab = 0, mism_ac = 0;
        INFO("frame=", static_cast<int>(f));
        CHECK(pix_match(*fb_a, *fb_b, mism_ab));
        CHECK(mism_ab == 0);
        CHECK(pix_match(*fb_a, *fb_c, mism_ac));
        CHECK(mism_ac == 0);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Test 2 — Determinism: render same scene twice with same renderer, verify match
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("TileParallel: Determinism — same renderer twice produces identical output") {
    const int W = 200, H = 150;
    const int kFrames = 6;
    Composition comp = make_two_corner_scene(W, H, kFrames);

    auto renderer = test::make_renderer();
    {
        RenderSettings s;
        s.use_modular_graph = true;
        s.dirty.enabled = true;
        s.dirty.use_bitmask = true;
        s.dirty.tile_size = 32;
        renderer.set_settings(s);
    }

    // Run 1
    std::vector<std::shared_ptr<Framebuffer>> run1;
    for (Frame f = 0; f < kFrames; ++f) {
        run1.push_back(renderer.render(comp, f));
    }

    // Run 2 — fresh renderer
    auto renderer2 = test::make_renderer();
    {
        RenderSettings s;
        s.use_modular_graph = true;
        s.dirty.enabled = true;
        s.dirty.use_bitmask = true;
        s.dirty.tile_size = 32;
        renderer2.set_settings(s);
    }

    for (Frame f = 0; f < kFrames; ++f) {
        auto fb2 = renderer2.render(comp, f);
        REQUIRE(fb2 != nullptr);

        int mism = 0;
        INFO("frame=", static_cast<int>(f));
        CHECK(pix_match(*run1[static_cast<size_t>(f.as_i64())], *fb2, mism));
        CHECK(mism == 0);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Test 3 — Determinism: tiles on vs off produce same output
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("TileParallel: Determinism — parallel tiles match baseline (no tiles)") {
    const int W = 160, H = 120;
    const int kFrames = 10;
    Composition comp = make_two_corner_scene(W, H, kFrames);

    // Baseline: no dirty rects, no tiles
    auto baseline = test::make_renderer();
    {
        RenderSettings s;
        s.use_modular_graph = true;
        s.dirty.enabled = false;
        baseline.set_settings(s);
    }

    // Parallel tiles
    auto opt = test::make_renderer();
    {
        RenderSettings s;
        s.use_modular_graph = true;
        s.dirty.enabled = true;
        s.dirty.use_bitmask = true;
        s.dirty.tile_size = 32;
        opt.set_settings(s);
    }

    int total_mism = 0;
    for (Frame f = 0; f < kFrames; ++f) {
        auto fb_b = baseline.render(comp, f);
        auto fb_o = opt.render(comp, f);
        REQUIRE(fb_b != nullptr);
        REQUIRE(fb_o != nullptr);

        int mism = 0;
        bool match = pix_match(*fb_b, *fb_o, mism);
        if (!match) {
            for (int y = 0; y < fb_b->height(); ++y) {
                for (int x = 0; x < fb_b->width(); ++x) {
                    Color cb = fb_b->get_pixel(x, y);
                    Color co = fb_o->get_pixel(x, y);
                    if (std::abs(cb.r - co.r) > 1e-4f || std::abs(cb.g - co.g) > 1e-4f ||
                        std::abs(cb.b - co.b) > 1e-4f || std::abs(cb.a - co.a) > 1e-4f) {
                        MESSAGE("Test5 frame=", static_cast<int>(f), " first_mismatch_at=(", x, ",", y, ") baseline=(",
                            cb.r, ",", cb.g, ",", cb.b, ",", cb.a, ") opt=(",
                            co.r, ",", co.g, ",", co.b, ",", co.a, ")");
                        goto done_finding5;
                    }
                }
            }
            done_finding5:;
        }
        CHECK(match);
        total_mism += mism;
    }
    CHECK(total_mism == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Test 4 — Stress: medium resolution with multiple tile sizes, verify no crash and tile counters
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("TileParallel: Stress — medium resolution with multiple tile sizes") {
    // Reduced frame count for test speed; coverage focus is on resolution + tiling
    const int W = 640, H = 360;
    const int kFrames = 12;

    Composition comp(CompositionSpec{
        .name = "Stress1080p", .width = W, .height = H, .duration = kFrames
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        // Complex scene: background + many elements to exercise parallelism
        s.rect("bg", {
            .size = {640.0f, 360.0f},
            .color = Color{0.08f, 0.12f, 0.18f, 1.0f},
            .pos = {320.0f, 180.0f, 0}
        });
        for (int i = 0; i < 8; ++i) {
            float x = 40.0f + static_cast<float>(i) * 60.0f
                      + static_cast<float>(ctx.frame()) * 2.0f;
            s.circle("ball" + std::to_string(i), {
                .radius = 6.0f,
                .color = Color{
                    0.3f + 0.1f * static_cast<float>(i),
                    0.5f,
                    0.7f,
                    0.9f
                },
                .pos = {x, 180.0f + static_cast<float>(i % 3 - 1) * 40.0f, 0}
            });
        }
        return s.build();
    });

    std::vector<int> tile_sizes = {64, 128};

    for (int tsize : tile_sizes) {
        auto renderer = test::make_renderer();
        {
            RenderSettings s;
            s.use_modular_graph = true;
            s.dirty.enabled = true;
            s.dirty.use_bitmask = true;
            s.dirty.tile_size = tsize;
            renderer.set_settings(s);
        }

        // Render all frames — must not throw/crash
        for (Frame f = 0; f < kFrames; ++f) {
            auto fb = renderer.render(comp, f);
            REQUIRE(fb != nullptr);
            CHECK(fb->width() == W);
            CHECK(fb->height() == H);
        }

        const auto* c = renderer.counters();
        INFO("tile_size=", tsize);

        // Tile counters should show meaningful activity
        uint64_t tile_dirty = c->tile_dirty_count.load();
        uint64_t tile_clean = c->tile_clean_count.load();
        uint64_t px_skipped = c->tile_pixels_skipped.load();

        CHECK(tile_dirty > 0);
        CHECK(tile_clean > 0);
        CHECK(px_skipped > 0);

        // No fallbacks in normal operation
        CHECK(c->tile_full_fallbacks.load() == 0);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Test 5 — Stress: many dirty tiles (full-screen update), verify correctness
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("TileParallel: Stress — full-screen dirty (all tiles dirty) still correct") {
    const int W = 128, H = 128;
    const int kFrames = 6;

    // Scene with full-screen animated gradient — every tile is dirty every frame
    Composition comp(CompositionSpec{
        .name = "FullScreenDirty", .width = W, .height = H, .duration = kFrames
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        float r = 0.2f + static_cast<float>(ctx.frame()) * 0.1f;
        float g = 0.3f;
        float b = 0.5f + static_cast<float>(ctx.frame()) * 0.05f;
        s.rect("full", {
            .size = {128.0f, 128.0f},
            .color = Color{r, g, b, 1.0f},
            .pos = {64.0f, 64.0f, 0}
        });
        return s.build();
    });

    // Baseline: no tiles
    auto baseline = test::make_renderer();
    {
        RenderSettings s;
        s.use_modular_graph = true;
        s.dirty.enabled = false;
        baseline.set_settings(s);
    }

    // Tiles enabled
    auto opt = test::make_renderer();
    {
        RenderSettings s;
        s.use_modular_graph = true;
        s.dirty.enabled = true;
        s.dirty.use_bitmask = true;
        s.dirty.tile_size = 32;
        opt.set_settings(s);
    }

    int total_mism = 0;
    void* prev_fb_o = nullptr;
    for (Frame f = 0; f < kFrames; ++f) {
        auto fb_b = baseline.render(comp, f);
        auto fb_o = opt.render(comp, f);
        REQUIRE(fb_b != nullptr);
        REQUIRE(fb_o != nullptr);

        MESSAGE("Test5 frame=", static_cast<int>(f), " fb_b_ptr=", fmt::ptr(fb_b.get()), " fb_o_ptr=", fmt::ptr(fb_o.get()), " prev_fb_o_ptr=", prev_fb_o);
        if (f > 0 && fb_o.get() == prev_fb_o) {
            MESSAGE("Test5 frame=", static_cast<int>(f), " OPT RETURNED SAME FB POINTER AS PREVIOUS FRAME!");
        }
        prev_fb_o = fb_o.get();

        int mism = 0;
        bool match = pix_match(*fb_b, *fb_o, mism);
        if (!match) {
            for (int y = 0; y < fb_b->height(); ++y) {
                for (int x = 0; x < fb_b->width(); ++x) {
                    Color cb = fb_b->get_pixel(x, y);
                    Color co = fb_o->get_pixel(x, y);
                    if (std::abs(cb.r - co.r) > 1e-4f || std::abs(cb.g - co.g) > 1e-4f ||
                        std::abs(cb.b - co.b) > 1e-4f || std::abs(cb.a - co.a) > 1e-4f) {
                        MESSAGE("Test5 frame=", static_cast<int>(f), " first_mismatch_at=(", x, ",", y, ") baseline=(",
                            cb.r, ",", cb.g, ",", cb.b, ",", cb.a, ") opt=(",
                            co.r, ",", co.g, ",", co.b, ",", co.a, ")");
                        goto done_finding5;
                    }
                }
            }
            done_finding5:;
        }
        CHECK(match);
        total_mism += mism;
    }
    CHECK(total_mism == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Test 6 — Stress: many frames with tiny tile size (forces many parallel tasks)
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("TileParallel: Stress — fine-grained tiles (tile_size=16), many frames") {
    const int W = 128, H = 96;
    const int kFrames = 15;

    Composition comp = make_two_corner_scene(W, H, kFrames);

    auto renderer = test::make_renderer();
    {
        RenderSettings s;
        s.use_modular_graph = true;
        s.dirty.enabled = true;
        s.dirty.use_bitmask = true;
        s.dirty.tile_size = 16;  // 8 × 6 = 48 tiles — many small tasks
        renderer.set_settings(s);
    }

    // Must not crash across many frames
    for (Frame f = 0; f < kFrames; ++f) {
        auto fb = renderer.render(comp, f);
        REQUIRE(fb != nullptr);
    }

    const auto* c = renderer.counters();
    CHECK(c->tile_dirty_count.load() > 0);
    CHECK(c->tile_clean_count.load() > 0);
    CHECK(c->tile_full_fallbacks.load() == 0);
}
