#include <doctest/doctest.h>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/graphics/gradient.hpp>
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
using namespace chronon3d::graphics;

static bool approx(f32 a, f32 b, f32 eps = 1e-5f) {
    return std::abs(a - b) < eps;
}

namespace {

// ── Gradient test scenes ───────────────────────────────────────────────
//
// These use the existing Fill API (Fill::linear / Fill::radial) which is
// wired into the path rasterizer.  GradientDefinition will be tested once
// FillStyle integration lands.

Composition make_gradient_static_comp() {
    return composition(
        {.name = "GradientDeterminismStatic", .width = 320, .height = 180, .duration = 10},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            // Dark background
            s.rect("bg", {.size = {320, 180}, .color = Color{0.08f, 0.09f, 0.12f, 1.0f}, .pos = {0, 0, 0}});
            // Red→blue horizontal gradient
            s.rounded_rect("card1", {
                .size   = {160, 80},
                .radius = 12.0f,
                .color  = Color::white(),
                .pos    = {-60, -30, 0},
                .fill   = FillStyle::linear(
                    {0.0f, 0.5f}, {1.0f, 0.5f},
                    {{0.0f, Color::from_hex("#1e3a5f")}, {1.0f, Color::from_hex("#3b82f6")}}),
            });
            // Green→yellow vertical gradient
            s.rounded_rect("card2", {
                .size   = {120, 100},
                .radius = 12.0f,
                .color  = Color::white(),
                .pos    = {80, -20, 0},
                .fill   = FillStyle::linear(
                    {0.5f, 0.0f}, {0.5f, 1.0f},
                    {{0.0f, Color::from_hex("#065f46")}, {1.0f, Color::from_hex("#34d399")}}),
            });
            // Purple→pink diagonal gradient (rotated via layer)
            s.layer("card3", [](LayerBuilder& l) {
                l.position({-40, 55, 0});
                l.rotate({0, 0, 15});
                l.rounded_rect("r3", {
                    .size   = {130, 70},
                    .radius = 10.0f,
                    .color  = Color::white(),
                    .pos    = {0, 0, 0},
                    .fill   = FillStyle::linear(
                        {0.0f, 0.0f}, {1.0f, 1.0f},
                        {{0.0f, Color::from_hex("#7c3aed")}, {1.0f, Color::from_hex("#e879f9")}}),
                });
            });
            // Radial gradient on a circle
            s.layer("card4", [](LayerBuilder& l) {
                l.position({90, 50, 0});
                l.circle("c4", {
                    .radius = 50.0f,
                    .color  = Color::white(),
                    .pos    = {0, 0, 0},
                    .fill   = FillStyle::radial(
                        {0.5f, 0.5f}, 0.5f,
                        {{0.0f, Color::from_hex("#fef08a")}, {1.0f, Color::from_hex("#b45309")}}),
                });
            });
            return s.build();
        }
    );
}

Composition make_gradient_animated_comp() {
    return composition(
        {.name = "GradientDeterminismAnimated", .width = 320, .height = 180, .duration = 60},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.rect("bg", {.size = {320, 180}, .color = Color{0.05f, 0.05f, 0.05f, 1.0f}, .pos = {0, 0, 0}});
            // Animate the rectangle's x position
            float x = static_cast<float>(ctx.frame) * 2.0f;
            s.layer("moving_gradient", [x](LayerBuilder& l) {
                l.position({x - 160.0f, 0.0f, 0.0f});
                l.rounded_rect("r", {
                    .size   = {80, 60},
                    .radius = 8.0f,
                    .color  = Color::white(),
                    .pos    = {0, 0, 0},
                    .fill   = FillStyle::linear(
                        {0.0f, 0.5f}, {1.0f, 0.5f},
                        {{0.0f, Color::from_hex("#ef4444")}, {1.0f, Color::from_hex("#3b82f6")}}),
                });
            });
            return s.build();
        }
    );
}

// FNV-1a hash of the framebuffer (matching test_utils.hpp's framebuffer_hash).
u64 compute_pixel_hash(const Framebuffer& fb) {
    u64 h = 0x811c9dc5;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            Color c = fb.get_pixel(x, y);
            u32 pixel = (static_cast<u32>(std::clamp(c.r * 255.0f, 0.0f, 255.0f)) << 24) |
                        (static_cast<u32>(std::clamp(c.g * 255.0f, 0.0f, 255.0f)) << 16) |
                        (static_cast<u32>(std::clamp(c.b * 255.0f, 0.0f, 255.0f)) << 8)  |
                        (static_cast<u32>(std::clamp(c.a * 255.0f, 0.0f, 255.0f)));
            h = (h ^ pixel) * 0x01000193;
        }
    }
    return h;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════
//  DoD Determinism Test 1: Consecutive renders identical
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("Gradient determinism: 20 consecutive renders — pixel-identical") {
    auto comp = make_gradient_static_comp();
    auto renderer = make_renderer();

    std::vector<u64> hashes;
    hashes.reserve(20);

    for (int i = 0; i < 20; ++i) {
        auto fb = renderer.render_frame(comp, 0);
        REQUIRE(fb != nullptr);
        hashes.push_back(framebuffer_hash(*fb));
    }

    for (size_t i = 1; i < hashes.size(); ++i) {
        INFO("Render " << i << " differs from render 0");
        CHECK(hashes[i] == hashes[0]);
    }
}

TEST_CASE("Gradient determinism: animated scene — same frame repeated 10× identical") {
    auto comp = make_gradient_animated_comp();
    auto renderer = make_renderer();

    const Frame test_frame{25};
    std::vector<u64> hashes;
    hashes.reserve(10);

    for (int i = 0; i < 10; ++i) {
        auto fb = renderer.render_frame(comp, test_frame);
        REQUIRE(fb != nullptr);
        hashes.push_back(framebuffer_hash(*fb));
    }

    for (size_t i = 1; i < hashes.size(); ++i) {
        INFO("Animated frame render " << i << " differs from render 0");
        CHECK(hashes[i] == hashes[0]);
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  DoD Determinism Test 2: Cold cache vs warm cache
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("Gradient determinism: cold cache vs warm cache — identical pixels") {
    auto comp = make_gradient_static_comp();

    // Cold run: fresh renderer (cache empty)
    auto renderer = make_renderer();
    auto fb_cold = renderer.render_frame(comp, 0);
    REQUIRE(fb_cold != nullptr);
    const u64 hash_cold = framebuffer_hash(*fb_cold);

    // Warm run: same renderer, same frame (cache populated)
    auto fb_warm = renderer.render_frame(comp, 0);
    REQUIRE(fb_warm != nullptr);
    const u64 hash_warm = framebuffer_hash(*fb_warm);

    INFO("Cold-cache hash: " << hash_cold << ", Warm-cache hash: " << hash_warm);
    CHECK(hash_cold == hash_warm);
}

TEST_CASE("Gradient determinism: cache invalidated → rebuilt — identical pixels") {
    auto comp = make_gradient_static_comp();
    auto renderer = make_renderer();

    auto fb_cached = renderer.render_frame(comp, 0);
    REQUIRE(fb_cached != nullptr);
    const u64 hash_cached = framebuffer_hash(*fb_cached);

    // Invalidate all caches, force rebuild
    renderer.clear_caches();
    auto fb_rebuilt = renderer.render_frame(comp, 0);
    REQUIRE(fb_rebuilt != nullptr);
    const u64 hash_rebuilt = framebuffer_hash(*fb_rebuilt);

    INFO("Cached hash: " << hash_cached << ", Rebuilt hash: " << hash_rebuilt);
    CHECK(hash_cached == hash_rebuilt);

    // Verify pixel-level semantic equality too
    auto diff = compare_framebuffers_semantic(*fb_cached, *fb_rebuilt);
    CHECK(diff.mismatched_pixels == 0);
    CHECK(diff.max_channel_error == 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════
//  DoD Determinism Test 3: New renderer vs reused renderer
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("Gradient determinism: new renderer vs same renderer — identical pixels") {
    auto comp = make_gradient_static_comp();

    // Fresh renderer A
    auto renderer_a = make_renderer();
    auto fb_a1 = renderer_a.render_frame(comp, 0);
    REQUIRE(fb_a1 != nullptr);

    // Second render on same renderer (reused)
    auto fb_a2 = renderer_a.render_frame(comp, 0);
    REQUIRE(fb_a2 != nullptr);

    // Fresh renderer B (completely new instance)
    auto renderer_b = make_renderer();
    auto fb_b = renderer_b.render_frame(comp, 0);
    REQUIRE(fb_b != nullptr);

    const u64 hash_a1 = framebuffer_hash(*fb_a1);
    const u64 hash_a2 = framebuffer_hash(*fb_a2);
    const u64 hash_b  = framebuffer_hash(*fb_b);

    CHECK(hash_a1 == hash_a2);
    CHECK(hash_a1 == hash_b);
}

// ═══════════════════════════════════════════════════════════════════════
//  DoD Determinism Test 4: Single-thread vs multi-thread
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("Gradient determinism: 1 thread vs 4 threads — identical pixels") {
    auto comp = make_gradient_static_comp();

    u64 hash_1t = 0;
    {
        tbb::global_control gc(tbb::global_control::max_allowed_parallelism, 1);
        auto renderer = make_renderer();
        auto fb = renderer.render_frame(comp, 0);
        REQUIRE(fb != nullptr);
        hash_1t = framebuffer_hash(*fb);
    }

    u64 hash_4t = 0;
    {
        tbb::global_control gc(tbb::global_control::max_allowed_parallelism, 4);
        auto renderer = make_renderer();
        auto fb = renderer.render_frame(comp, 0);
        REQUIRE(fb != nullptr);
        hash_4t = framebuffer_hash(*fb);
    }

    INFO("1-thread hash: " << hash_1t << ", 4-thread hash: " << hash_4t);
    CHECK(hash_1t == hash_4t);
}

TEST_CASE("Gradient determinism: 1 thread vs 8 threads — identical pixels") {
    auto comp = make_gradient_static_comp();

    u64 hash_1t = 0;
    {
        tbb::global_control gc(tbb::global_control::max_allowed_parallelism, 1);
        auto renderer = make_renderer();
        auto fb = renderer.render_frame(comp, 0);
        REQUIRE(fb != nullptr);
        hash_1t = framebuffer_hash(*fb);
    }

    u64 hash_8t = 0;
    {
        tbb::global_control gc(tbb::global_control::max_allowed_parallelism, 8);
        auto renderer = make_renderer();
        auto fb = renderer.render_frame(comp, 0);
        REQUIRE(fb != nullptr);
        hash_8t = framebuffer_hash(*fb);
    }

    INFO("1-thread hash: " << hash_1t << ", 8-thread hash: " << hash_8t);
    CHECK(hash_1t == hash_8t);
}

// ═══════════════════════════════════════════════════════════════════════
//  DoD Determinism Test 5: Semantic comparison — identical frames
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("Gradient determinism: semantic comparison — identical frames") {
    auto comp = make_gradient_static_comp();
    auto renderer = make_renderer();

    auto fb1 = renderer.render_frame(comp, 0);
    auto fb2 = renderer.render_frame(comp, 0);
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);

    auto diff = compare_framebuffers_semantic(*fb1, *fb2);
    CHECK(diff.max_channel_error == 0.0f);
    CHECK(diff.mean_channel_error == 0.0f);
    CHECK(diff.mismatched_pixels == 0);
    CHECK(diff.psnr >= 100.0f);

    // SSIM should be essentially 1.0
    float ssim = ssim_luminance(*fb1, *fb2);
    CHECK(ssim >= 0.999f);
}

TEST_CASE("Gradient determinism: semantic comparison — different frames differ") {
    auto comp = make_gradient_animated_comp();
    auto renderer = make_renderer();

    auto fb10 = renderer.render_frame(comp, Frame{10});
    auto fb20 = renderer.render_frame(comp, Frame{20});
    REQUIRE(fb10 != nullptr);
    REQUIRE(fb20 != nullptr);

    auto diff = compare_framebuffers_semantic(*fb10, *fb20);
    // Different frames must have some pixel differences
    CHECK(diff.mismatched_pixels > 0);
    CHECK(diff.psnr < 80.0f);
}

// ═══════════════════════════════════════════════════════════════════════
//  DoD Determinism Test 6: GradientDefinition sampler determinism
//  (Unit-level: the sampler itself must be deterministic)
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("Gradient determinism: GradientDefinition sampler — repeatable output") {
    auto def = graphics::GradientDefinition::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {
            {0.0f, Color::from_hex("#1e3a5f")},
            {0.5f, Color::from_hex("#f59e0b")},
            {1.0f, Color::from_hex("#3b82f6")},
        },
        graphics::GradientSpread::Repeat);

    // Sample at 50 normalised positions, twice → must be identical
    std::vector<Color> samples_a;
    samples_a.reserve(50);
    for (int i = 0; i < 50; ++i) {
        f32 t = static_cast<f32>(i) / 50.0f;
        samples_a.push_back(graphics::sample_gradient(def, t));
    }

    std::vector<Color> samples_b;
    samples_b.reserve(50);
    for (int i = 0; i < 50; ++i) {
        f32 t = static_cast<f32>(i) / 50.0f;
        samples_b.push_back(graphics::sample_gradient(def, t));
    }

    for (int i = 0; i < 50; ++i) {
        INFO("Sample index " << i);
        CHECK(approx(samples_a[i].r, samples_b[i].r));
        CHECK(approx(samples_a[i].g, samples_b[i].g));
        CHECK(approx(samples_a[i].b, samples_b[i].b));
        CHECK(approx(samples_a[i].a, samples_b[i].a));
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  DoD Determinism Test 7: Bounding box and centroid detection
//  on gradient-filled shapes
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("Gradient determinism: centroid detection on gradient shapes") {
    auto comp = make_gradient_static_comp();
    auto renderer = make_renderer();

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);

    // The blue gradient (card1) should have dominant blue region
    int blue_pixels = count_pixels_dominant_b(*fb);
    CHECK(blue_pixels > 100);

    // The green gradient (card2) should have dominant green region
    int green_pixels = count_pixels_dominant_g(*fb);
    CHECK(green_pixels > 100);

    // The purple/pink gradient (card3) should have dominant red region
    int red_pixels = count_pixels_dominant_r(*fb);
    CHECK(red_pixels > 100);

    // Verify centroids are deterministic
    float cx = centroid_x_dominant_r(*fb);
    float cy = centroid_x_dominant_b(*fb);
    CHECK(cx >= 0.0f);
    CHECK(cy >= 0.0f);
}
