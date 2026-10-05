// Single-glow API and pipeline contract tests.
#include <doctest/doctest.h>

#include <chronon3d/effects/glow_pipeline.hpp>
#include <chronon3d/effects/spatial_bounds.hpp>
#include <chronon3d/backends/software/software_session_resources.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/profiling/profiling_context.hpp>
#include "effects/render_effects_processor.hpp"
#include <tbb/global_control.h>
#include <cmath>

using namespace chronon3d;

TEST_CASE("GlowPipeline converts the single radius, intensity and color") {
    GlowParams params{
        .radius = 24.0f,
        .intensity = 0.35f,
        .color = Color{0.5f, 0.6f, 0.7f, 1.0f},
    };

    const auto pipeline = GlowPipeline::from(params);
    CHECK(pipeline.color.r == doctest::Approx(0.5f));
    CHECK(pipeline.color.g == doctest::Approx(0.6f));
    CHECK(pipeline.color.b == doctest::Approx(0.7f));
    CHECK(pipeline.radius == doctest::Approx(24.0f));
    CHECK(pipeline.intensity == doctest::Approx(0.35f));
}

TEST_CASE("Glow radius zero and intensity zero are exact software no-ops") {
    for (const auto params : {GlowParams{.radius = 0.0f, .intensity = 0.8f},
                              GlowParams{.radius = 12.0f, .intensity = 0.0f}}) {
        Framebuffer fb{11, 7};
        for (int y = 0; y < fb.height(); ++y)
            for (int x = 0; x < fb.width(); ++x)
                fb.pixels_row(y)[x] = Color{
                    0.25f, 0.1f, 0.0f, (x + y) % 2 == 0 ? 0.25f : 0.0f};
        const Framebuffer original{fb};
        const auto pipeline = GlowPipeline::from(params);
        renderer::run_glow_pipeline(fb, pipeline, std::nullopt);
        for (int y = 0; y < fb.height(); ++y)
            for (int x = 0; x < fb.width(); ++x) {
                const auto a = fb.pixels_row(y)[x];
                const auto b = original.pixels_row(y)[x];
                CHECK(a.r == b.r);
                CHECK(a.g == b.g);
                CHECK(a.b == b.b);
                CHECK(a.a == b.a);
            }
    }
}

TEST_CASE("Glow radius uses continuous subpixel sampling") {
    Framebuffer radius_four{33, 33};
    radius_four.clear(Color::transparent());
    radius_four.pixels_row(16)[16] = Color{0.5f, 0.5f, 0.5f, 0.5f};
    Framebuffer radius_four_half{radius_four};
    auto a = GlowPipeline::from(GlowParams{
        .radius = 4.0f, .intensity = 0.8f,
        .color = Color{1.0f, 1.0f, 1.0f, 1.0f}});
    auto b = a;
    b.radius = 4.5f;
    renderer::run_glow_pipeline(radius_four, a, std::nullopt);
    renderer::run_glow_pipeline(radius_four_half, b, std::nullopt);
    CHECK(std::abs(radius_four.pixels_row(16)[18].a -
                   radius_four_half.pixels_row(16)[18].a) > 1.0e-6f);
}

TEST_CASE("glow_effect_extent follows the single radius") {
    GlowParams params;
    params.radius = 20.0f;
    CHECK(glow_effect_extent(params) == doctest::Approx(84.0f));

    const auto pipeline = GlowPipeline::from(params);
    CHECK(glow_effect_extent(pipeline) == doctest::Approx(84.0f));
}

TEST_CASE("Glow reuses its session kernel and clears scratch after a shrinking ROI") {
    auto pool = std::make_shared<cache::FramebufferPool>(16U * 1024U * 1024U);
    profiling::ProfilingGuard profiling_guard(nullptr, pool.get());
    Framebuffer frame{64, 48};
    frame.clear(Color::transparent());
    frame.pixels_row(24)[32] = Color{0.8f, 0.0f, 0.0f, 0.8f};
    EffectScratchResources scratch;
    auto& kernel = scratch.glow_kernel_scratch();
    const auto large = GlowPipeline::from(GlowParams{
        .radius = 16.0f, .intensity = 0.5f,
        .color = Color{0.2f, 0.8f, 1.0f, 1.0f}});
    renderer::run_glow_pipeline(frame, large, raster::BBox{28, 20, 36, 28}, false,
                                nullptr, &kernel);
    REQUIRE(kernel.size() == 33);
    const auto capacity_after_warmup = kernel.capacity();
    const auto allocations_after_warmup = pool->stats().total_allocations;

    Framebuffer repeat{64, 48};
    repeat.clear(Color::transparent());
    repeat.pixels_row(24)[32] = Color{0.8f, 0.0f, 0.0f, 0.8f};
    EffectScratchResources repeat_scratch;
    {
        tbb::global_control serial_workers{
            tbb::global_control::max_allowed_parallelism, 1};
        renderer::run_glow_pipeline(
            repeat, large, raster::BBox{28, 20, 36, 28}, false, nullptr,
            &repeat_scratch.glow_kernel_scratch());
    }
    for (int y = 0; y < frame.height(); ++y) {
        for (int x = 0; x < frame.width(); ++x) {
            const auto a = frame.pixels_row(y)[x];
            const auto b = repeat.pixels_row(y)[x];
            CHECK(a.r == b.r);
            CHECK(a.g == b.g);
            CHECK(a.b == b.b);
            CHECK(a.a == b.a);
        }
    }
    CHECK(pool->stats().total_allocations == allocations_after_warmup);
    CHECK(pool->stats().total_reuses > 0);

    // Reuse the same scratch with a smaller kernel and then an empty frame.
    frame.clear(Color::transparent());
    frame.pixels_row(24)[32] = Color{0.8f, 0.0f, 0.0f, 0.8f};
    const auto small = GlowPipeline::from(GlowParams{
        .radius = 4.0f, .intensity = 0.5f,
        .color = Color{0.2f, 0.8f, 1.0f, 1.0f}});
    renderer::run_glow_pipeline(frame, small, raster::BBox{30, 22, 34, 26}, false,
                                nullptr, &kernel);
    CHECK(kernel.size() == 9);
    CHECK(kernel.capacity() == capacity_after_warmup);

    frame.clear(Color::transparent());
    renderer::run_glow_pipeline(frame, small, raster::BBox{30, 22, 34, 26}, false,
                                nullptr, &kernel);
    for (int y = 0; y < frame.height(); ++y)
        for (int x = 0; x < frame.width(); ++x)
            CHECK(frame.pixels_row(y)[x].a == 0.0f);
}

TEST_CASE("effect_spatial_halo matches software blur strategies and glow bounds") {
    const auto blur_halo = effects::effect_spatial_halo(BlurParams{.radius = 10.0f});
    CHECK(blur_halo.left == doctest::Approx(26.0f));
    CHECK(blur_halo.top == doctest::Approx(26.0f));
    CHECK(blur_halo.right == doctest::Approx(26.0f));
    CHECK(blur_halo.bottom == doctest::Approx(26.0f));

    // These radii select the half-resolution, quarter-resolution, and nested
    // quarter-resolution blur branches in apply_blur(). The halo must include
    // the kernel support and resampling fringe at every level.
    CHECK(effects::spatial_halo_extent(
              effects::effect_spatial_halo(BlurParams{.radius = 20.0f})) >= 22.0f);
    CHECK(effects::spatial_halo_extent(
              effects::effect_spatial_halo(BlurParams{.radius = 30.0f})) >= 34.0f);
    CHECK(effects::spatial_halo_extent(
              effects::effect_spatial_halo(BlurParams{.radius = 100.0f})) >= 110.0f);

    // The renderer clamps pass count to one; the spatial contract must do so
    // as well instead of returning a zero halo for caller values <= 0.
    const auto zero_pass_halo = effects::blur_spatial_halo(8.0f, 0);
    const auto one_pass_halo = effects::blur_spatial_halo(8.0f, 1);
    CHECK(zero_pass_halo.left == doctest::Approx(one_pass_halo.left));
    CHECK(zero_pass_halo.right == doctest::Approx(one_pass_halo.right));
    CHECK(effects::spatial_halo_extent(
              effects::blur_spatial_halo(8.0f, -1)) > 0.0f);

    const auto glow_halo = effects::effect_spatial_halo(GlowParams{.radius = 20.0f});
    CHECK(glow_halo.left == doctest::Approx(84.0f));
    CHECK(glow_halo.top == doctest::Approx(84.0f));
    CHECK(glow_halo.right == doctest::Approx(84.0f));
    CHECK(glow_halo.bottom == doctest::Approx(84.0f));
}

TEST_CASE("PR2-Unit-ShadowBounds: halo matches contact and ambient shadow maps") {
    DropShadowParams params;
    params.offset = {0.0f, 4.0f};
    params.radius = 12.0f;

    const auto halo = effects::drop_shadow_spatial_halo(params);
    CHECK(halo.left == doctest::Approx(44.0f));
    CHECK(halo.top == doctest::Approx(44.0f));
    CHECK(halo.right == doctest::Approx(44.0f));
    // The ambient map rounds the 1.65x contact offset to an integer pixel,
    // so the lower halo is blur padding plus seven pixels.
    CHECK(halo.bottom == doctest::Approx(51.0f));
    CHECK(effects::drop_shadow_blur_padding(params) == doctest::Approx(44.0f));

    // Both maps are included in the shared ROI even for atypical negative
    // radius values where contact blur can be the wider kernel.
    params.radius = -10.0f;
    const auto negative_radius_halo = effects::effect_spatial_halo(params);
    CHECK(effects::spatial_halo_extent(negative_radius_halo) >=
          effects::spatial_halo_extent(effects::blur_spatial_halo(1.0f, 1)));

    params.radius = 12.0f;
    params.offset = {-7.0f, -11.0f};
    const auto negative_offset_halo = effects::effect_spatial_halo(params);
    CHECK(negative_offset_halo.left == doctest::Approx(56.0f));
    CHECK(negative_offset_halo.top == doctest::Approx(62.0f));
    CHECK(negative_offset_halo.right == doctest::Approx(44.0f));
    CHECK(negative_offset_halo.bottom == doctest::Approx(44.0f));

    // Match the implementation's integer pixel offsets: the 0.6px authored
    // offset rounds to one contact pixel and two ambient pixels.
    params.offset = {0.6f, -0.4f};
    const auto fractional_offset_halo = effects::effect_spatial_halo(params);
    CHECK(fractional_offset_halo.right == doctest::Approx(46.0f));
    CHECK(fractional_offset_halo.left == doctest::Approx(44.0f));
    CHECK(fractional_offset_halo.top == doctest::Approx(44.0f));
    CHECK(fractional_offset_halo.bottom == doctest::Approx(44.0f));
}

TEST_CASE("PhaseB CPU blur, shadow, and bloom preserve neutral paths, alpha, and deterministic pixels") {
    const auto fill_pattern = [](Framebuffer& fb) {
        fb.clear(Color::transparent());
        for (int y = 3; y < std::min(9, fb.height() - 2); ++y)
            for (int x = 4; x < std::min(11, fb.width() - 2); ++x)
                fb.pixels_row(y)[x] = Color{0.3f, 0.1f, 0.05f, 0.4f};
        fb.pixels_row(fb.height() / 2)[fb.width() / 2] = Color{0.8f, 0.8f, 0.8f, 1.0f};
    };
    const auto exact_pixels = [](const Framebuffer& a, const Framebuffer& b) {
        REQUIRE(a.width() == b.width());
        REQUIRE(a.height() == b.height());
        for (int y = 0; y < a.height(); ++y)
            for (int x = 0; x < a.width(); ++x) {
                CHECK(a.pixels_row(y)[x].r == b.pixels_row(y)[x].r);
                CHECK(a.pixels_row(y)[x].g == b.pixels_row(y)[x].g);
                CHECK(a.pixels_row(y)[x].b == b.pixels_row(y)[x].b);
                CHECK(a.pixels_row(y)[x].a == b.pixels_row(y)[x].a);
            }
    };
    const auto check_premultiplied = [](const Framebuffer& fb, const char* effect) {
        for (int y = 0; y < fb.height(); ++y)
            for (int x = 0; x < fb.width(); ++x) {
                const auto c = fb.pixels_row(y)[x];
                CHECK_MESSAGE(c.a >= -1.0e-7f, effect);
                CHECK_MESSAGE(c.a <= 1.0f + 1.0e-6f, effect);
                CHECK_MESSAGE(c.r <= c.a + 1.0e-6f, effect);
                CHECK_MESSAGE(c.g <= c.a + 1.0e-6f, effect);
                CHECK_MESSAGE(c.b <= c.a + 1.0e-6f, effect);
            }
    };

    for (const auto [width, height] : {std::pair{64, 40}, std::pair{40, 64}}) {
        Framebuffer source{width, height};
        fill_pattern(source);

        Framebuffer blur_neutral{source};
        renderer::apply_blur(blur_neutral, 0.0f, std::nullopt, 1);
        exact_pixels(source, blur_neutral);
        Framebuffer blur_a{source}, blur_b{source};
        renderer::apply_blur(blur_a, 4.0f, std::nullopt, 1);
        renderer::apply_blur(blur_b, 4.0f, std::nullopt, 1);
        exact_pixels(blur_a, blur_b);
        check_premultiplied(blur_a, "after Gaussian blur");

        DropShadowParams shadow;
        shadow.offset = {3.0f, -2.0f};
        shadow.color = Color{0.8f, 0.1f, 0.0f, 0.0f};
        Framebuffer shadow_neutral{source};
        renderer::apply_shadow_effect(shadow_neutral, shadow, std::nullopt);
        exact_pixels(source, shadow_neutral);
        shadow.color.a = 0.6f;
        shadow.radius = 2.0f;
        Framebuffer shadow_a{source}, shadow_b{source};
        renderer::apply_shadow_effect(shadow_a, shadow, std::nullopt);
        renderer::apply_shadow_effect(shadow_b, shadow, std::nullopt);
        exact_pixels(shadow_a, shadow_b);
        check_premultiplied(shadow_a, "after drop shadow");

        BloomParams bloom;
        bloom.intensity = 0.0f;
        Framebuffer bloom_neutral{source};
        renderer::run_bloom_effect(bloom_neutral, bloom, std::nullopt);
        exact_pixels(source, bloom_neutral);
        bloom = BloomParams{.threshold = 0.5f, .radius = 5.0f, .intensity = 0.8f};
        Framebuffer bloom_a{source}, bloom_b{source};
        renderer::run_bloom_effect(bloom_a, bloom, std::nullopt);
        renderer::run_bloom_effect(bloom_b, bloom, std::nullopt);
        exact_pixels(bloom_a, bloom_b);
        check_premultiplied(bloom_a, "after bloom");
    }
}

TEST_CASE("PhaseB blur, drop shadow, and bloom reuse warmed framebuffer scratch") {
    auto pool = std::make_shared<cache::FramebufferPool>(16U * 1024U * 1024U);
    profiling::ProfilingGuard profiling_guard(nullptr, pool.get());
    Framebuffer base{64, 48};
    base.clear(Color::transparent());
    for (int y = 18; y < 30; ++y)
        for (int x = 20; x < 36; ++x)
            base.pixels_row(y)[x] = Color{0.6f, 0.2f, 0.1f, 0.75f};

    const auto run_all = [&](Framebuffer& fb) {
        renderer::apply_blur(fb, 4.0f, std::nullopt, 1);
        renderer::apply_shadow_effect(fb, DropShadowParams{
            .offset = {3.0f, 2.0f}, .color = {0.1f, 0.0f, 0.0f, 0.5f}, .radius = 3.0f}, std::nullopt);
        renderer::run_bloom_effect(fb, BloomParams{
            .threshold = 0.5f, .radius = 5.0f, .intensity = 0.6f}, std::nullopt);
    };
    Framebuffer first{base};
    run_all(first);
    const auto allocations_after_warmup = pool->stats().total_allocations;
    const auto reuses_after_warmup = pool->stats().total_reuses;
    Framebuffer second{base};
    run_all(second);
    CHECK(pool->stats().total_allocations == allocations_after_warmup);
    CHECK(pool->stats().total_reuses > reuses_after_warmup);
    for (int y = 0; y < first.height(); ++y)
        for (int x = 0; x < first.width(); ++x) {
            CHECK(first.pixels_row(y)[x].r == second.pixels_row(y)[x].r);
            CHECK(first.pixels_row(y)[x].g == second.pixels_row(y)[x].g);
            CHECK(first.pixels_row(y)[x].b == second.pixels_row(y)[x].b);
            CHECK(first.pixels_row(y)[x].a == second.pixels_row(y)[x].a);
        }
}

TEST_CASE("Bloom V1 threshold boundary and zero strength are exact") {
    Framebuffer source{7, 1};
    source.clear(Color::transparent());
    const float values[] = {0.2f, 0.5f, 0.699f, 0.700f, 0.701f, 0.8f, 1.2f};
    for (int x = 0; x < 7; ++x)
        source.pixels_row(0)[x] = Color{values[x], values[x], values[x], 1.0f};

    Framebuffer identity{source};
    renderer::run_bloom_effect(identity, BloomParams{
        .threshold = 0.7f, .radius = 0.0f, .intensity = 0.0f}, std::nullopt);
    for (int x = 0; x < 7; ++x) {
        CHECK(identity.pixels_row(0)[x].r == source.pixels_row(0)[x].r);
        CHECK(identity.pixels_row(0)[x].a == source.pixels_row(0)[x].a);
    }

    Framebuffer extracted{source};
    renderer::run_bloom_effect(extracted, BloomParams{
        .threshold = 0.7f, .radius = 0.0f, .intensity = 1.0f}, std::nullopt);
    CHECK(extracted.pixels_row(0)[0].r == doctest::Approx(0.2f));
    CHECK(extracted.pixels_row(0)[1].r == doctest::Approx(0.5f));
    CHECK(extracted.pixels_row(0)[2].r == doctest::Approx(0.699f));
    CHECK(extracted.pixels_row(0)[3].r == doctest::Approx(0.700f));
    CHECK(extracted.pixels_row(0)[4].r > 0.701f);
    CHECK(extracted.pixels_row(0)[5].r > 0.8f);
    CHECK(extracted.pixels_row(0)[6].r > 1.2f);
}

TEST_CASE("Bloom V1 preserves HDR, monotonic strength, center and radial symmetry") {
    const auto make_source = [] {
        Framebuffer source{513, 513};
        source.clear(Color::transparent());
        source.pixels_row(256)[256] = Color{8.0f, 8.0f, 8.0f, 1.0f};
        return source;
    };
    const Framebuffer baseline{make_source()};
    float prior_energy = -1.0f;
    for (const float strength : {0.1f, 0.3f, 0.6f, 1.0f}) {
        Framebuffer result{make_source()};
        renderer::run_bloom_effect(result, BloomParams{
            .threshold = 0.7f, .radius = 8.0f, .intensity = strength}, std::nullopt);
        float energy = 0.0f;
        for (int y = 0; y < result.height(); ++y)
            for (int x = 0; x < result.width(); ++x)
                energy += result.pixels_row(y)[x].r - baseline.pixels_row(y)[x].r;
        CHECK(energy > prior_energy);
        prior_energy = energy;
        CHECK(result.pixels_row(256)[256].r > 8.0f);
        CHECK(result.pixels_row(256)[254].r == doctest::Approx(result.pixels_row(256)[258].r));
        CHECK(result.pixels_row(254)[256].r == doctest::Approx(result.pixels_row(258)[256].r));
        CHECK(result.pixels_row(256)[254].r == doctest::Approx(result.pixels_row(254)[256].r));
    }

    float prior_spread = -1.0f;
    for (int radius : {4, 16, 32, 64, 128}) {
        Framebuffer result{make_source()};
        renderer::run_bloom_effect(result, BloomParams{
            .threshold = 0.7f, .radius = static_cast<float>(radius), .intensity = 0.6f}, std::nullopt);
        CHECK(result.pixels_row(256)[256].r > result.pixels_row(256)[256 + radius / 2].r);
        CHECK(result.pixels_row(256)[256].r > result.pixels_row(256)[256 - radius / 2].r);
        float added_energy = 0.0f;
        float weighted_distance = 0.0f;
        for (int y = 0; y < result.height(); ++y)
            for (int x = 0; x < result.width(); ++x) {
                const auto pixel = result.pixels_row(y)[x];
                const float added = pixel.r - baseline.pixels_row(y)[x].r;
                const float dx = static_cast<float>(x - 256);
                const float dy = static_cast<float>(y - 256);
                added_energy += added;
                weighted_distance += added * (dx * dx + dy * dy);
                CHECK(std::isfinite(pixel.r));
                CHECK(std::isfinite(pixel.g));
                CHECK(std::isfinite(pixel.b));
                CHECK(std::isfinite(pixel.a));
            }
        const float spread = weighted_distance / added_energy;
        CHECK(spread > prior_spread);
        prior_spread = spread;
    }
}

TEST_CASE("Bloom V1 transparent pixels stay colorless and halo remains covered") {
    Framebuffer source{65, 65};
    source.clear(Color::transparent());
    for (int y = 29; y <= 34; ++y)
        for (int x = 29; x <= 34; ++x)
            source.pixels_row(y)[x] = Color{1.0f, 1.0f, 1.0f, 1.0f};
    renderer::run_bloom_effect(source, BloomParams{
        .threshold = 0.5f, .radius = 8.0f, .intensity = 0.5f}, std::nullopt);
    CHECK(source.pixels_row(0)[0].r == 0.0f);
    CHECK(source.pixels_row(0)[0].g == 0.0f);
    CHECK(source.pixels_row(0)[0].b == 0.0f);
    CHECK(source.pixels_row(32)[38].r > 0.0f);
    CHECK(source.pixels_row(32)[38].a > 0.0f);
    CHECK(source.pixels_row(32)[38].r <= source.pixels_row(32)[38].a);
}

TEST_CASE("Bloom V1 extreme HDR inputs never produce NaN or infinity") {
    Framebuffer source{17, 17};
    source.clear(Color::transparent());
    source.pixels_row(8)[8] = Color{1.0e30f, 1.0e30f, 1.0e30f, 1.0f};
    source.pixels_row(8)[9] = Color{1.0e30f, 1.0e30f, 1.0e30f, 1.0f};
    renderer::run_bloom_effect(source, BloomParams{
        .threshold = 0.7f, .radius = 128.0f, .intensity = 4.0f}, std::nullopt);
    for (int y = 0; y < source.height(); ++y)
        for (int x = 0; x < source.width(); ++x) {
            const auto pixel = source.pixels_row(y)[x];
            CHECK(std::isfinite(pixel.r));
            CHECK(std::isfinite(pixel.g));
            CHECK(std::isfinite(pixel.b));
            CHECK(std::isfinite(pixel.a));
        }
}
