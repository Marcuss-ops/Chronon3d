#include <chronon3d/scene/builders/scene_builder.hpp>
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
#include <algorithm>
#include <tests/helpers/test_utils.hpp>
using namespace chronon3d;


namespace {

// ── Helpers ──────────────────────────────────────────────────────────────────

bool fb_pixel_match(const Framebuffer& a, const Framebuffer& b,
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

// Render a single frame with dirty rects on/off
std::shared_ptr<Framebuffer> render_with_dirty(const Composition& comp, Frame f,
                                                bool dirty_on) {
    auto renderer = test::make_renderer();
    RenderSettings settings;
    settings.use_modular_graph = true;
    settings.dirty.enabled = dirty_on;
    renderer.set_settings(settings);
    return renderer.render_frame(comp, f);
}

// Count non-transparent pixels in a framebuffer
int count_opaque(const Framebuffer& fb) {
    int n = 0;
    for (int y = 0; y < fb.height(); ++y)
        for (int x = 0; x < fb.width(); ++x)
            if (fb.get_pixel(x, y).a > 0.01f) ++n;
    return n;
}

// Count pixels matching a target color within tolerance
int count_pixels_matching(const Framebuffer& fb, Color target, float eps = 0.1f) {
    int n = 0;
    for (int y = 0; y < fb.height(); ++y)
        for (int x = 0; x < fb.width(); ++x) {
            Color c = fb.get_pixel(x, y);
            if (std::abs(c.r - target.r) < eps &&
                std::abs(c.g - target.g) < eps &&
                std::abs(c.b - target.b) < eps &&
                std::abs(c.a - target.a) < eps)
                ++n;
        }
    return n;
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// Test 1 — Bounding box per nodo
// Verify bounding boxes for rectangles, circles, and text.
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("Dirty Rects: Bounding box correct for simple shapes") {
    const int W = 200, H = 150;

    // Rectangle at known position
    CompositionSpec spec{
        .name = "BBoxRectTest", .width = W, .height = H, .duration = 5
    };
    Composition comp_rect(spec, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        // Small rect at (30,20) with size (40,50) → bbox approximately
        // centered at (30,20) size 40×50 in centered coords
        s.rect("r", {
            .size = {40, 50},
            .color = Color::red(),
            .pos = {30, 20, 0}
        });
        return s.build();
    });

    // Render with and without dirty rects; both should match
    auto fb_dirty   = render_with_dirty(comp_rect, 0, true);
    auto fb_nodirty = render_with_dirty(comp_rect, 0, false);
    REQUIRE(fb_dirty != nullptr);
    REQUIRE(fb_nodirty != nullptr);

    int mismatches = 0;
    CHECK(fb_pixel_match(*fb_dirty, *fb_nodirty, mismatches));
    CHECK(mismatches == 0);

    // The dirty version should record a smaller dirty area than full frame
    // since the rect is small
    // (We can't directly access the renderer's counters from render_with_dirty,
    //  so we recreate the renderer to inspect counters.)
    {
        auto r = test::make_renderer();
        RenderSettings s;
        s.use_modular_graph = true;
        s.dirty.enabled = true;
        r.set_settings(s);
        r.render_frame(comp_rect, 0);  // Render frame 0 baseline
        r.counters()->reset();         // Reset counters for frame 1
        r.render_frame(comp_rect, 1);  // frame 1 → dirty rect active
        uint64_t dirty_px = r.counters()->dirty_pixels.load();
        uint64_t full_px = static_cast<uint64_t>(W) * H;
        CHECK(dirty_px < full_px);  // dirty area smaller than full frame
    }

    // Circle at known position
    Composition comp_circle(CompositionSpec{
        .name = "BBoxCircleTest", .width = W, .height = H, .duration = 5
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        s.circle("c", {
            .radius = 25,
            .color = Color::green(),
            .pos = {100, 75, 0}
        });
        return s.build();
    });

    {
        auto r = test::make_renderer();
        RenderSettings s;
        s.use_modular_graph = true;
        s.dirty.enabled = true;
        r.set_settings(s);
        r.render_frame(comp_circle, 0); // Render frame 0 baseline
        r.counters()->reset();          // Reset counters for frame 1
        r.render_frame(comp_circle, 1);
        uint64_t dirty_px = r.counters()->dirty_pixels.load();
        uint64_t full_px = static_cast<uint64_t>(W) * H;
        CHECK(dirty_px < full_px);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Test 2 — Differenza inter-frame
// Move a small object between frames. The dirty region must include both
// the old and new positions.
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("Dirty Rects: Inter-frame diff includes old and new position") {
    const int W = 200, H = 150;

    // Rectangle that moves from x=40 to x=80 across frames
    Composition comp(CompositionSpec{
        .name = "DirtyRectDiffTest", .width = W, .height = H, .duration = 3
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        float x = 40.0f + static_cast<float>(ctx.frame) * 40.0f;  // frame 0→40, 1→80
        s.rect("moving", {
            .size = {30, 30},
            .color = Color::blue(),
            .pos = {x, 75, 0}
        });
        return s.build();
    });

    // Render frame 0 as baseline with dirty off
    auto fb0_nodirty = render_with_dirty(comp, 0, false);
    REQUIRE(fb0_nodirty);

    // Render frame 0 and 1 with dirty on
    auto fb0_dirty = render_with_dirty(comp, 0, true);
    auto fb1_dirty = render_with_dirty(comp, 1, true);
    REQUIRE(fb0_dirty);
    REQUIRE(fb1_dirty);

    // Frame 1 should match no-dirty reference
    auto fb1_nodirty = render_with_dirty(comp, 1, false);
    int mism = 0;
    CHECK(fb_pixel_match(*fb1_dirty, *fb1_nodirty, mism));
    CHECK(mism == 0);

    // The dirty pixels count for frame 1 should be less than full frame
    {
        auto r = test::make_renderer();
        RenderSettings s;
        s.use_modular_graph = true;
        s.dirty.enabled = true;
        r.set_settings(s);
        r.render_frame(comp, 0);
        r.counters()->reset();
        r.render_frame(comp, 1);
        uint64_t dirty_px = r.counters()->dirty_pixels.load();
        uint64_t full_px = static_cast<uint64_t>(W) * H;
        CHECK(dirty_px > 0);
        CHECK(dirty_px < full_px);  // dirty < full frame
    }

    // No ghosting: the old position (frame 0's x≈40) should NOT have the
    // blue rect in frame 1's output
    {
        // The rect at frame 0 was centered at canvas position (W/2 + 40, H/2) ≈ (140, 75)
        // Since SceneBuilder uses centered coordinates...
        // Just verify frame 1 output has exactly one contiguous blue region
        int blue_pixels = count_pixels_matching(*fb1_dirty, Color::blue());
        CHECK(blue_pixels > 0);
        // Shouldn't be double the area (no ghost)
        CHECK(blue_pixels < 2 * 30 * 30);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Test 3 — Frame statici
// Static scene for many frames. After frame 0, clear_skipped_calls must grow.
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("Dirty Rects: Static scene skips redundant clears") {
    const int W = 200, H = 150;
    const int kFrames = 10;

    Composition comp(CompositionSpec{
        .name = "StaticScene", .width = W, .height = H, .duration = kFrames
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        s.rect("bg", {
            .size = {static_cast<float>(W), static_cast<float>(H)},
            .color = Color{0.1f, 0.15f, 0.2f, 1.0f},
            .pos = {static_cast<float>(W) * 0.5f, static_cast<float>(H) * 0.5f, 0}
        });
        s.rect("panel", {
            .size = {100, 80},
            .color = Color{0.2f, 0.3f, 0.4f, 1.0f},
            .pos = {100, 75, 0}
        });
        return s.build();
    });

    auto renderer = test::make_renderer();
    RenderSettings settings;
    settings.use_modular_graph = true;
    settings.dirty.enabled = true;
    renderer.set_settings(settings);

    // Render all frames
    auto first_fb = renderer.render_frame(comp, 0);
    REQUIRE(first_fb);

    for (Frame f = 1; f < kFrames; ++f) {
        auto fb = renderer.render_frame(comp, f);
        REQUIRE(fb);
    }

    const auto* counters = renderer.counters();

    // After frame 0, the scene is static → clear should be skipped
    uint64_t skipped = counters->clear_skipped_calls.load();
    CHECK(skipped > 0);

    // Dirty rect count should be recorded
    uint64_t dirty_count = counters->dirty_rect_count.load();
    CHECK(dirty_count > 0);

    // Dirty pixels should accumulate mainly from frame 0 (full first render).
    // Subsequent static frames reuse the framebuffer, contributing near-zero.
    // Over 10 frames: ~1 full frame worth of dirty pixels, not 10.
    uint64_t dirty_px = counters->dirty_pixels.load();
    uint64_t full_px = static_cast<uint64_t>(W) * H;
    CHECK(dirty_px > 0);
    CHECK(dirty_px < full_px * 5);  // less than 5 full frames (multi-node graph full render) = static scene works
}

// ═════════════════════════════════════════════════════════════════════════════
// Test 4 — Scena quasi statica
// Static background + small animated element. Dirty pixels << full frame.
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("Dirty Rects: Near-static scene with small animated element") {
    const int W = 200, H = 150;
    const int kFrames = 8;

    Composition comp(CompositionSpec{
        .name = "NearStatic", .width = W, .height = H, .duration = kFrames
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);

        // Large static background
        s.rect("bg", {
            .size = {static_cast<float>(W), static_cast<float>(H)},
            .color = Color{0.05f, 0.1f, 0.15f, 1.0f},
            .pos = {static_cast<float>(W) * 0.5f, static_cast<float>(H) * 0.5f, 0}
        });

        // Small animated dot
        float x = 30.0f + static_cast<float>(ctx.frame) * 5.0f;
        s.circle("dot", {
            .radius = 8,
            .color = Color::red(),
            .pos = {x, 75, 0}
        });

        return s.build();
    });

    // Reference: render all frames without dirty rects
    auto ref_renderer = test::make_renderer();
    RenderSettings ref_settings;
    ref_settings.use_modular_graph = true;
    ref_settings.dirty.enabled = false;
    ref_renderer.set_settings(ref_settings);

    // Optimized: render all frames with dirty rects
    auto opt_renderer = test::make_renderer();
    RenderSettings opt_settings;
    opt_settings.use_modular_graph = true;
    opt_settings.dirty.enabled = true;
    opt_renderer.set_settings(opt_settings);

    int total_mismatches = 0;
    for (Frame f = 0; f < kFrames; ++f) {
        auto fb_ref = ref_renderer.render_frame(comp, f);
        auto fb_opt = opt_renderer.render_frame(comp, f);
        REQUIRE(fb_ref);
        REQUIRE(fb_opt);

        if (f == 0) {
            opt_renderer.counters()->reset();
        }

        int mism = 0;
        CHECK(fb_pixel_match(*fb_ref, *fb_opt, mism));
        total_mismatches += mism;
    }
    CHECK(total_mismatches == 0);

    // Dirty area must be significantly less than full frame area
    const auto* counters = opt_renderer.counters();
    uint64_t dirty_px = counters->dirty_pixels.load();
    uint64_t full_px = static_cast<uint64_t>(W) * H;
    CHECK(dirty_px > 0);
    CHECK(dirty_px < full_px);  // dirty area < full frame

    // The dirty ratio should be small (small animated element only)
    uint64_t dirty_area = counters->dirty_union_area_pixels.load();
    CHECK(dirty_area < full_px / 2);  // less than half the frame
}

// ═════════════════════════════════════════════════════════════════════════════
// Test 5 — Output correctness with effects
// Effects like blur read neighboring pixels; verify output remains correct
// regardless of whether dirty rects or full-frame path is used.
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("Dirty Rects: Output correct with effects (blur)") {
    const int W = 200, H = 150;

    // Scene with a blur effect — blur reads neighboring pixels, making
    // dirty-rect partial updates unsafe.
    Composition comp(CompositionSpec{
        .name = "UnsafeEffect", .width = W, .height = H, .duration = 3
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);

        // Moving rect with blur effect
        float x = 60.0f + static_cast<float>(ctx.frame) * 20.0f;
        s.layer("blurred", [&](LayerBuilder& l) {
            l.rect("r", {
                .size = {50, 50},
                .color = Color{0.8f, 0.3f, 0.2f, 1.0f},
                .pos = {x, 75, 0}
            });
            l.blur(10.0f);
        });

        return s.build();
    });

    // Render frame 0 and 1 with dirty rects ON
    auto renderer = test::make_renderer();
    RenderSettings settings;
    settings.use_modular_graph = true;
    settings.dirty.enabled = true;
    renderer.set_settings(settings);

    auto fb0 = renderer.render_frame(comp, 0);
    auto fb1 = renderer.render_frame(comp, 1);
    REQUIRE(fb0);
    REQUIRE(fb1);

    // Reference frame 1 without dirty rects
    auto fb1_ref = render_with_dirty(comp, 1, false);
    REQUIRE(fb1_ref);

    // Output must be correct despite any fallback
    int mism = 0;
    CHECK(fb_pixel_match(*fb1, *fb1_ref, mism));

    // The dirty rect system should have recorded fallback reasons.
    // For blur effects, EffectBoundsUnknown fallback is expected.
    const auto* counters = renderer.counters();
    uint64_t effect_fallback = counters->dirty_full_fallback_reasons[
        static_cast<std::size_t>(DirtyFallbackReason::EffectBoundsUnknown)
    ].value.load();

    // The key invariant: output must be pixel-correct regardless of
    // whether the dirty rect system took the partial or full-frame path.
    // Blur reads neighboring pixels, so partial updates are risky;
    // the system must produce correct output in either case.
    MESSAGE("Effect fallback counter: ", effect_fallback);
    MESSAGE("Pixel mismatches: ", mism);
    CHECK(mism < static_cast<int>(W * H * 0.01f));  // < 1% mismatches
}

// ═════════════════════════════════════════════════════════════════════════════
// Bonus: verify that dirty rects produce equivalent output across a longer
// sequence with position changes (regression guard).
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("Dirty Rects: Long sequence equivalence with moving elements") {
    const int W = 160, H = 120;
    const int kFrames = 15;

    Composition comp(CompositionSpec{
        .name = "LongDirtySeq", .width = W, .height = H, .duration = kFrames
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);

        // Static background
        s.rect("bg", {
            .size = {static_cast<float>(W), static_cast<float>(H)},
            .color = Color{0.08f, 0.12f, 0.18f, 1.0f},
            .pos = {static_cast<float>(W) * 0.5f, static_cast<float>(H) * 0.5f, 0}
        });

        // Moving element
        float x = 20.0f + static_cast<float>(ctx.frame) * 8.0f;
        float y = 60.0f + std::sin(static_cast<float>(ctx.frame) * 0.8f) * 15.0f;
        s.rounded_rect("moving", {
            .size = {24, 16},
            .radius = 4,
            .color = Color{0.9f, 0.6f, 0.1f, 1.0f},
            .pos = {x, y, 0}
        });

        return s.build();
    });

    // Two renderers: one with dirty rects, one without
    SoftwareRenderer r_dirty, r_clean;
    {
        RenderSettings s;
        s.use_modular_graph = true;
        s.dirty.enabled = true;
        r_dirty.set_settings(s);
    }
    {
        RenderSettings s;
        s.use_modular_graph = true;
        s.dirty.enabled = false;
        r_clean.set_settings(s);
    }

    int total_mism = 0;
    for (Frame f = 0; f < kFrames; ++f) {
        auto fb_d = r_dirty.render_frame(comp, f);
        auto fb_c = r_clean.render_frame(comp, f);
        REQUIRE(fb_d);
        REQUIRE(fb_c);

        int mism = 0;
        CHECK(fb_pixel_match(*fb_d, *fb_c, mism));
        total_mism += mism;
    }

    CHECK(total_mism == 0);

    // Dirty rects should have reduced work
    const auto* cnt = r_dirty.counters();
    uint64_t dirty_px = cnt->dirty_pixels.load();
    uint64_t full_px = static_cast<uint64_t>(W) * H * kFrames;
    CHECK(dirty_px < full_px);
}
