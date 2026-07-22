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

#include <vector>
#include <string>
#include <cmath>
#include <tbb/global_control.h>
using namespace chronon3d;

using namespace chronon3d::test;

namespace {

// Simple static scene that should be perfectly deterministic
Composition make_static_test_comp() {
    return composition(
        {.name = "DeterminismHarness", .width = 320, .height = 180, .duration = 10},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.rect("bg", {.size = {320, 180}, .color = Color{0.1f, 0.15f, 0.2f, 1.0f}, .pos = {0, 0, 0}});
            s.rect("red_box", {.size = {60, 60}, .color = Color::red(), .pos = {-80, -40, 0}});
            s.rect("green_box", {.size = {60, 60}, .color = Color::green(), .pos = {0, -40, 0}});
            s.rect("blue_box", {.size = {60, 60}, .color = Color::blue(), .pos = {80, -40, 0}});
            return s.build();
        }
    );
}

// Animated scene (should still be deterministic for same frame)
Composition make_animated_test_comp() {
    return composition(
        {.name = "DeterminismHarnessAnimated", .width = 320, .height = 180, .duration = 60},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            float x = static_cast<float>(ctx.frame()) * 2.0f;
            s.rect("bg", {.size = {320, 180}, .color = Color{0.05f, 0.05f, 0.05f, 1.0f}, .pos = {0, 0, 0}});
            s.rect("moving_box", {.size = {40, 40}, .color = Color::white(), .pos = {x - 160.0f, 0, 0}});
            return s.build();
        }
    );
}

} // anonymous namespace

// ── 1. REPEATED RENDER DETERMINISM ───────────────────────────────────────────
TEST_CASE("Determinism harness: static scene — 20 consecutive renders identical") {
    auto comp = make_static_test_comp();
    auto renderer = test::make_renderer();

    std::vector<u64> hashes;
    hashes.reserve(20);

    for (int i = 0; i < 20; ++i) {
        auto fb = renderer.render(comp, 0);
        REQUIRE(fb != nullptr);
        hashes.push_back(framebuffer_hash(*fb));
    }

    for (size_t i = 1; i < hashes.size(); ++i) {
        CHECK(hashes[i] == hashes[0]);
    }
}

TEST_CASE("Determinism harness: animated scene — same frame repeated identical") {
    auto comp = make_animated_test_comp();
    auto renderer = test::make_renderer();

    const Frame test_frame{25};
    std::vector<u64> hashes;
    hashes.reserve(10);

    for (int i = 0; i < 10; ++i) {
        auto fb = renderer.render(comp, test_frame);
        REQUIRE(fb != nullptr);
        hashes.push_back(framebuffer_hash(*fb));
    }

    for (size_t i = 1; i < hashes.size(); ++i) {
        CHECK(hashes[i] == hashes[0]);
    }
}

// ── 2. COLD CACHE vs WARM CACHE ────────────────────────────────────────────
TEST_CASE("Determinism harness: cold cache vs warm cache yields identical pixels") {
    auto comp = make_static_test_comp();

    // Cold run: fresh renderer
    auto renderer1 = test::make_renderer();
    auto fb_cold = renderer1.render(comp, 0);
    REQUIRE(fb_cold != nullptr);
    const u64 hash_cold = framebuffer_hash(*fb_cold);

    // Warm run: same renderer again (cache populated)
    auto fb_warm = renderer1.render(comp, 0);
    REQUIRE(fb_warm != nullptr);
    const u64 hash_warm = framebuffer_hash(*fb_warm);

    CHECK(hash_cold == hash_warm);
}

// ── 3. NEW RENDERER INSTANCE vs SAME INSTANCE ───────────────────────────────
TEST_CASE("Determinism harness: new renderer instance yields identical pixels") {
    auto comp = make_static_test_comp();

    auto renderer1 = test::make_renderer();
    auto fb1 = renderer1.render(comp, 0);
    REQUIRE(fb1 != nullptr);

    auto renderer2 = test::make_renderer();
    auto fb2 = renderer2.render(comp, 0);
    REQUIRE(fb2 != nullptr);

    const u64 h1 = framebuffer_hash(*fb1);
    const u64 h2 = framebuffer_hash(*fb2);

    CHECK(h1 == h2);
}

// ── 4. SEMANTIC COMPARISON UTILITIES ────────────────────────────────────────
TEST_CASE("Determinism harness: semantic comparison on identical frames") {
    auto comp = make_static_test_comp();
    auto renderer = test::make_renderer();

    auto fb1 = renderer.render(comp, 0);
    auto fb2 = renderer.render(comp, 0);
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);

    auto diff = compare_framebuffers_semantic(*fb1, *fb2);
    CHECK(diff.max_channel_error == 0.0f);
    CHECK(diff.mean_channel_error == 0.0f);
    CHECK(diff.mismatched_pixels == 0);
    CHECK(diff.psnr >= 100.0f); // identical
}

TEST_CASE("Determinism harness: semantic comparison on different frames") {
    auto comp = make_animated_test_comp();
    auto renderer = test::make_renderer();

    auto fb1 = renderer.render(comp, Frame{10});
    auto fb2 = renderer.render(comp, Frame{20});
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);

    auto diff = compare_framebuffers_semantic(*fb1, *fb2);
    // Different frames should have some difference
    CHECK(diff.mismatched_pixels > 0);
    CHECK(diff.max_channel_error > 0.01f);
    CHECK(diff.psnr < 80.0f); // lower PSNR = more different
}

TEST_CASE("Determinism harness: SSIM on identical frames") {
    auto comp = make_static_test_comp();
    auto renderer = test::make_renderer();

    auto fb1 = renderer.render(comp, 0);
    auto fb2 = renderer.render(comp, 0);
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);

    float ssim = ssim_luminance(*fb1, *fb2);
    CHECK(ssim >= 0.999f);
}

TEST_CASE("Determinism harness: bounding box and centroid detection") {
    auto comp = make_static_test_comp();
    auto renderer = test::make_renderer();

    auto fb = renderer.render(comp, 0);
    REQUIRE(fb != nullptr);

    // Red box should be visible and detectable
    BBox red_bb = bounding_box(*fb, Color::red(), 0.1f);
    CHECK_FALSE(red_bb.empty);
    CHECK(red_bb.area() > 0);

    float cx = centroid_x(*fb, Color::red(), 0.1f);
    float cy = centroid_y(*fb, Color::red(), 0.1f);
    CHECK(cx > 0.0f);
    CHECK(cy > 0.0f);

    // Count pixels should be positive for each color
    CHECK(count_pixels(*fb, Color::red(), 0.1f) > 0);
    CHECK(count_pixels(*fb, Color::green(), 0.1f) > 0);
    CHECK(count_pixels(*fb, Color::blue(), 0.1f) > 0);
}

// ── 5. THREAD COUNT DETERMINISM ───────────────────────────────────────────
TEST_CASE("Determinism harness: 1-thread vs 4-thread renders identical") {
    auto comp = make_static_test_comp();

    u64 hash_1t = 0;
    {
        tbb::global_control gc(tbb::global_control::max_allowed_parallelism, 1);
        auto renderer = test::make_renderer();
        auto fb = renderer.render(comp, 0);
        REQUIRE(fb != nullptr);
        hash_1t = framebuffer_hash(*fb);
    }

    u64 hash_4t = 0;
    {
        tbb::global_control gc(tbb::global_control::max_allowed_parallelism, 4);
        auto renderer = test::make_renderer();
        auto fb = renderer.render(comp, 0);
        REQUIRE(fb != nullptr);
        hash_4t = framebuffer_hash(*fb);
    }

    CHECK(hash_1t == hash_4t);
}

// ── 6. GRAPH CACHE vs REBUILD ────────────────────────────────────────────
TEST_CASE("Determinism harness: graph cache invalidated then rebuilt yields identical pixels") {
    auto comp = make_static_test_comp();
    auto renderer = test::make_renderer();

    auto fb_cached = renderer.render(comp, 0);
    REQUIRE(fb_cached != nullptr);
    const u64 hash_cached = framebuffer_hash(*fb_cached);

    // Invalidate the graph cache and force a rebuild
    renderer.clear_caches();
    auto fb_rebuilt = renderer.render(comp, 0);
    REQUIRE(fb_rebuilt != nullptr);
    const u64 hash_rebuilt = framebuffer_hash(*fb_rebuilt);

    CHECK(hash_cached == hash_rebuilt);
}

// ── 7. DETERMINISM REPORTING ────────────────────────────────────────────────
TEST_CASE("Determinism harness: non-determinism detection with tolerance") {
    auto comp = make_static_test_comp();

    auto renderer1 = test::make_renderer();
    auto renderer2 = test::make_renderer();

    auto fb1 = renderer1.render(comp, 0);
    auto fb2 = renderer2.render(comp, 0);
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);

    auto diff = compare_framebuffers_semantic(*fb1, *fb2);

    // On a single platform with same backend, these should be identical
    if (diff.mismatched_pixels > 0) {
        MESSAGE("Non-determinism detected: " << diff.mismatched_pixels
                << " mismatched pixels, max_error=" << diff.max_channel_error
                << ", PSNR=" << diff.psnr << " dB");
    }

    // Document tolerance: allow up to 0.1% mismatched pixels with 1/255 max error
    // for cross-run determinism on the same machine.
    const float max_allowed_mismatch_ratio = 0.001f;
    const float max_allowed_channel_error  = 1.0f / 255.0f;

    CHECK(diff.mismatched_pixels <= static_cast<int>(diff.total_pixels * max_allowed_mismatch_ratio));
    CHECK(diff.max_channel_error <= max_allowed_channel_error);
}
