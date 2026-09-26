// Single-glow API and pipeline contract tests.
#include <doctest/doctest.h>

#include <chronon3d/effects/glow_pipeline.hpp>
#include <chronon3d/effects/spatial_bounds.hpp>
#include <chronon3d/backends/software/software_session_resources.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/profiling/profiling_context.hpp>
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
