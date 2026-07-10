// ---------------------------------------------------------------------------
// test_determinism_matrix.cpp — P2-D: Determinism matrix (FNV-1a hash).
//
// Systematic matrix: two consecutive renders of the same input must produce
// pixel-identical frames (XXH64 hash via framebuffer_hash).
//
// Matrix dimensions:
//   Rows  = scene types (static solid, multi-layer, animated, gradient-like)
//   Cols  = conditions (same renderer, new renderer, cold→warm cache,
//           cache invalidation, 1-thread vs N-thread)
//
// Every cell in the matrix asserts hash equality (XXH64 via framebuffer_hash).
// ---------------------------------------------------------------------------

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/helpers/pixel_assertions.hpp>

#include <tbb/global_control.h>
#include <vector>
#include <cstdint>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

// ── Scene factories (rows of the matrix) ─────────────────────────────────

/// Solid-color background — simplest possible deterministic scene.
Composition make_scene_solid() {
    return composition(
        {.name = "DetMatrix_Solid", .width = 320, .height = 180, .duration = 10},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.rect("bg", {.size = {320, 180},
                          .color = Color{0.2f, 0.3f, 0.4f, 1.0f},
                          .pos = {0, 0, 0}});
            return s.build();
        });
}

/// Multi-layer static scene — multiple overlapping colored rects.
Composition make_scene_multilayer() {
    return composition(
        {.name = "DetMatrix_MultiLayer", .width = 320, .height = 180, .duration = 10},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.rect("bg",     {.size = {320, 180}, .color = Color{0.1f, 0.1f, 0.1f, 1.0f}, .pos = {0, 0, 0}});
            s.rect("red",    {.size = {80, 80},   .color = Color::red(),   .pos = {-100, 0, 0}});
            s.rect("green",  {.size = {80, 80},   .color = Color::green(), .pos = {0, 0, 0}});
            s.rect("blue",   {.size = {80, 80},   .color = Color::blue(),  .pos = {100, 0, 0}});
            s.rect("white",  {.size = {40, 40},   .color = Color::white(), .pos = {0, 60, 0}});
            return s.build();
        });
}

/// Animated scene — position varies by frame, deterministic per frame.
Composition make_scene_animated() {
    return composition(
        {.name = "DetMatrix_Animated", .width = 320, .height = 180, .duration = 120},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            float x = static_cast<float>(ctx.frame) * 1.5f;
            s.rect("bg",    {.size = {320, 180}, .color = Color{0.05f, 0.05f, 0.1f, 1.0f}, .pos = {0, 0, 0}});
            s.rect("moving", {.size = {50, 50},  .color = Color::white(), .pos = {x - 160.0f, 0, 0}});
            return s.build();
        });
}

/// Semi-transparent overlapping layers — exercises compositing determinism.
Composition make_scene_alpha_blend() {
    return composition(
        {.name = "DetMatrix_AlphaBlend", .width = 320, .height = 180, .duration = 10},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.rect("bg",     {.size = {320, 180}, .color = Color::black(), .pos = {0, 0, 0}});
            s.rect("a50",    {.size = {120, 120}, .color = Color{1.0f, 0.0f, 0.0f, 0.5f}, .pos = {-40, 0, 0}});
            s.rect("b50",    {.size = {120, 120}, .color = Color{0.0f, 0.0f, 1.0f, 0.5f}, .pos = {40, 0, 0}});
            return s.build();
        });
}

/// Small frame — edge case: tiny framebuffer (4×4).
Composition make_scene_tiny() {
    return composition(
        {.name = "DetMatrix_Tiny", .width = 4, .height = 4, .duration = 5},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.rect("bg", {.size = {4, 4}, .color = Color{0.5f, 0.5f, 0.5f, 1.0f}, .pos = {0, 0, 0}});
            return s.build();
        });
}

} // anonymous namespace

// ═════════════════════════════════════════════════════════════════════════
//  Column 1: Same renderer, consecutive renders — FNV-1a hash equality
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("Determinism matrix: solid — 2 consecutive renders, FNV-1a identical") {
    auto comp = make_scene_solid();
    auto r = test::make_renderer_shared();
    auto fb1 = r->render(comp, 0);
    auto fb2 = r->render(comp, 0);
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);
    CHECK(framebuffer_hash(*fb1) == framebuffer_hash(*fb2));
}

TEST_CASE("Determinism matrix: multilayer — 2 consecutive renders, FNV-1a identical") {
    auto comp = make_scene_multilayer();
    auto r = test::make_renderer_shared();
    auto fb1 = r->render(comp, 0);
    auto fb2 = r->render(comp, 0);
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);
    CHECK(framebuffer_hash(*fb1) == framebuffer_hash(*fb2));
}

TEST_CASE("Determinism matrix: animated frame 50 — 2 consecutive, FNV-1a identical") {
    auto comp = make_scene_animated();
    auto r = test::make_renderer_shared();
    auto fb1 = r->render(comp, Frame{50});
    auto fb2 = r->render(comp, Frame{50});
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);
    CHECK(framebuffer_hash(*fb1) == framebuffer_hash(*fb2));
}

TEST_CASE("Determinism matrix: alpha blend — 2 consecutive, FNV-1a identical") {
    auto comp = make_scene_alpha_blend();
    auto r = test::make_renderer_shared();
    auto fb1 = r->render(comp, 0);
    auto fb2 = r->render(comp, 0);
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);
    CHECK(framebuffer_hash(*fb1) == framebuffer_hash(*fb2));
}

TEST_CASE("Determinism matrix: tiny 4×4 — 2 consecutive, FNV-1a identical") {
    auto comp = make_scene_tiny();
    auto r = test::make_renderer_shared();
    auto fb1 = r->render(comp, 0);
    auto fb2 = r->render(comp, 0);
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);
    CHECK(framebuffer_hash(*fb1) == framebuffer_hash(*fb2));
}

// ═════════════════════════════════════════════════════════════════════════
//  Column 2: New renderer instance — same hash as first renderer
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("Determinism matrix: solid — new renderer instance, FNV-1a identical") {
    auto comp = make_scene_solid();
    auto r1 = test::make_renderer_shared();
    auto fb1 = r1->render(comp, 0);
    REQUIRE(fb1 != nullptr);
    auto r2 = test::make_renderer_shared();
    auto fb2 = r2->render(comp, 0);
    REQUIRE(fb2 != nullptr);
    CHECK(framebuffer_hash(*fb1) == framebuffer_hash(*fb2));
}

TEST_CASE("Determinism matrix: multilayer — new renderer instance, FNV-1a identical") {
    auto comp = make_scene_multilayer();
    auto r1 = test::make_renderer_shared();
    auto fb1 = r1->render(comp, 0);
    REQUIRE(fb1 != nullptr);
    auto r2 = test::make_renderer_shared();
    auto fb2 = r2->render(comp, 0);
    REQUIRE(fb2 != nullptr);
    CHECK(framebuffer_hash(*fb1) == framebuffer_hash(*fb2));
}

TEST_CASE("Determinism matrix: animated — new renderer instance, FNV-1a identical") {
    auto comp = make_scene_animated();
    auto r1 = test::make_renderer_shared();
    auto fb1 = r1->render(comp, Frame{30});
    REQUIRE(fb1 != nullptr);
    auto r2 = test::make_renderer_shared();
    auto fb2 = r2->render(comp, Frame{30});
    REQUIRE(fb2 != nullptr);
    CHECK(framebuffer_hash(*fb1) == framebuffer_hash(*fb2));
}

// ═════════════════════════════════════════════════════════════════════════
//  Column 3: Cache invalidation + rebuild — same hash
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("Determinism matrix: solid — cache invalidated + rebuilt, FNV-1a identical") {
    auto comp = make_scene_solid();
    auto r = test::make_renderer_shared();
    auto fb1 = r->render(comp, 0);
    REQUIRE(fb1 != nullptr);
    r->clear_caches();
    auto fb2 = r->render(comp, 0);
    REQUIRE(fb2 != nullptr);
    CHECK(framebuffer_hash(*fb1) == framebuffer_hash(*fb2));
}

TEST_CASE("Determinism matrix: multilayer — cache invalidated + rebuilt, FNV-1a identical") {
    auto comp = make_scene_multilayer();
    auto r = test::make_renderer_shared();
    auto fb1 = r->render(comp, 0);
    REQUIRE(fb1 != nullptr);
    r->clear_caches();
    auto fb2 = r->render(comp, 0);
    REQUIRE(fb2 != nullptr);
    CHECK(framebuffer_hash(*fb1) == framebuffer_hash(*fb2));
}

TEST_CASE("Determinism matrix: alpha blend — cache invalidated + rebuilt, FNV-1a identical") {
    auto comp = make_scene_alpha_blend();
    auto r = test::make_renderer_shared();
    auto fb1 = r->render(comp, 0);
    REQUIRE(fb1 != nullptr);
    r->clear_caches();
    auto fb2 = r->render(comp, 0);
    REQUIRE(fb2 != nullptr);
    CHECK(framebuffer_hash(*fb1) == framebuffer_hash(*fb2));
}

// ═════════════════════════════════════════════════════════════════════════
//  Column 4: Thread count variation — 1-thread vs 4-thread
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("Determinism matrix: multilayer — 1-thread vs 4-thread, FNV-1a identical") {
    auto comp = make_scene_multilayer();

    u64 hash_1t = 0;
    {
        tbb::global_control gc(tbb::global_control::max_allowed_parallelism, 1);
        auto r = test::make_renderer_shared();
        auto fb = r->render(comp, 0);
        REQUIRE(fb != nullptr);
        hash_1t = framebuffer_hash(*fb);
    }

    u64 hash_4t = 0;
    {
        tbb::global_control gc(tbb::global_control::max_allowed_parallelism, 4);
        auto r = test::make_renderer_shared();
        auto fb = r->render(comp, 0);
        REQUIRE(fb != nullptr);
        hash_4t = framebuffer_hash(*fb);
    }

    CHECK(hash_1t == hash_4t);
}

TEST_CASE("Determinism matrix: alpha blend — 1-thread vs 4-thread, FNV-1a identical") {
    auto comp = make_scene_alpha_blend();

    u64 hash_1t = 0;
    {
        tbb::global_control gc(tbb::global_control::max_allowed_parallelism, 1);
        auto r = test::make_renderer_shared();
        auto fb = r->render(comp, 0);
        REQUIRE(fb != nullptr);
        hash_1t = framebuffer_hash(*fb);
    }

    u64 hash_4t = 0;
    {
        tbb::global_control gc(tbb::global_control::max_allowed_parallelism, 4);
        auto r = test::make_renderer_shared();
        auto fb = r->render(comp, 0);
        REQUIRE(fb != nullptr);
        hash_4t = framebuffer_hash(*fb);
    }

    CHECK(hash_1t == hash_4t);
}

// ═════════════════════════════════════════════════════════════════════════
//  Column 5: 50 consecutive renders — stress determinism
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("Determinism matrix: solid — 10 consecutive renders, all FNV-1a identical") {
    auto comp = make_scene_solid();
    auto r = test::make_renderer_shared();

    auto fb_ref = r->render(comp, 0);
    REQUIRE(fb_ref != nullptr);
    const u64 ref_hash = framebuffer_hash(*fb_ref);

    for (int i = 1; i < 10; ++i) {
        auto fb = r->render(comp, 0);
        REQUIRE(fb != nullptr);
        CHECK(framebuffer_hash(*fb) == ref_hash);
    }
}

TEST_CASE("Determinism matrix: multilayer — 10 consecutive, all FNV-1a identical") {
    auto comp = make_scene_multilayer();
    auto r = test::make_renderer_shared();

    auto fb_ref = r->render(comp, 0);
    REQUIRE(fb_ref != nullptr);
    const u64 ref_hash = framebuffer_hash(*fb_ref);

    for (int i = 1; i < 10; ++i) {
        auto fb = r->render(comp, 0);
        REQUIRE(fb != nullptr);
        CHECK(framebuffer_hash(*fb) == ref_hash);
    }
}

// ═════════════════════════════════════════════════════════════════════════
//  Note: "different frames produce different output" sanity is already
//  covered by test_determinism_harness.cpp "semantic comparison on
//  different frames". This file focuses on same-input determinism.
// ═════════════════════════════════════════════════════════════════════════

// ═════════════════════════════════════════════════════════════════════════
//  Cross-column: animated frame 100 across all conditions
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("Determinism matrix: animated frame 100 — all conditions agree (FNV-1a)") {
    auto comp = make_scene_animated();
    const Frame target{100};

    // Baseline: first render
    auto r1 = test::make_renderer_shared();
    auto fb_base = r1->render(comp, target);
    REQUIRE(fb_base != nullptr);
    const u64 ref = framebuffer_hash(*fb_base);

    // Same renderer, second render
    auto fb_same = r1->render(comp, target);
    REQUIRE(fb_same != nullptr);
    CHECK(framebuffer_hash(*fb_same) == ref);

    // New renderer instance
    auto r2 = test::make_renderer_shared();
    auto fb_new = r2->render(comp, target);
    REQUIRE(fb_new != nullptr);
    CHECK(framebuffer_hash(*fb_new) == ref);

    // After cache invalidation
    r1->clear_caches();
    auto fb_inv = r1->render(comp, target);
    REQUIRE(fb_inv != nullptr);
    CHECK(framebuffer_hash(*fb_inv) == ref);

    // Different thread count
    {
        tbb::global_control gc(tbb::global_control::max_allowed_parallelism, 1);
        auto r3 = test::make_renderer_shared();
        auto fb_1t = r3->render(comp, target);
        REQUIRE(fb_1t != nullptr);
        CHECK(framebuffer_hash(*fb_1t) == ref);
    }
}
