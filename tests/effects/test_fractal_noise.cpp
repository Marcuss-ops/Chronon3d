#include <doctest/doctest.h>
#include <algorithm>
#include <chronon3d/effects/effect_catalog_data.hpp>
#include "src/backends/software/processors/effects/generate/fractal_noise.hpp"
#include "src/backends/software/processors/effects/generate/checkerboard.hpp"
#include "src/backends/software/processors/effects/generate/four_color_gradient.hpp"
#include "src/backends/software/processors/effects/generate/light_rays.hpp"
#include <chronon3d/backends/software/builtin_processors.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include "src/backends/software/effects/render_effects_processor.hpp"
#include <array>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::renderer;

TEST_CASE("Fractal noise is deterministic for the same seed") {
    FractalNoiseParams params;
    params.seed = 731u;
    params.octaves = 5;
    params.frequency = 0.09f;
    Framebuffer a(32, 24), b(32, 24);
    apply_fractal_noise(a, params, 0.0f);
    apply_fractal_noise(b, params, 0.0f);
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            CHECK(a.get_pixel(x, y) == b.get_pixel(x, y));
}

TEST_CASE("Fractal noise evolution changes the generated frame") {
    FractalNoiseParams params;
    params.seed = 731u;
    params.frequency = 0.08f;
    params.evolution = 2.0f;
    Framebuffer a(24, 24), b(24, 24);
    apply_fractal_noise(a, params, 0.0f);
    apply_fractal_noise(b, params, 1.0f);
    bool differs = false;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            differs |= a.get_pixel(x, y) != b.get_pixel(x, y);
    CHECK(differs);
}

TEST_CASE("Fractal noise generates opaque pixels and honors clipping") {
    FractalNoiseParams params;
    Framebuffer fb(8, 8);
    apply_fractal_noise(fb, params, 0.0f, raster::BBox{2, 2, 6, 6});
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const Color pixel = fb.get_pixel(x, y);
            if (x >= 2 && x < 6 && y >= 2 && y < 6) {
                CHECK(pixel.a == 1.0f);
                CHECK(pixel.r == pixel.g);
                CHECK(pixel.g == pixel.b);
            } else {
                CHECK(pixel == Color::transparent());
            }
        }
    }
}

TEST_CASE("Fractal noise is present in the effect catalog") {
    const auto entries = effects::builtin_effect_catalog();
    const auto found = std::ranges::find_if(entries, [](const auto& entry) {
        return entry.id == effects::ids::GenerateFractalNoise;
    });
    REQUIRE(found != entries.end());
    CHECK(found->type == effects::EffectType::FractalNoise);
    CHECK(found->alpha_policy == effects::AlphaPolicy::Generate);
    CHECK(found->bounds_policy == effects::BoundsPolicy::FullFrame);
}

TEST_CASE("Checkerboard generates the expected alternating cells") {
    CheckerboardParams params;
    params.size = 2.0f;
    params.color_a = Color{0.2f, 0.1f, 0.0f, 1.0f};
    params.color_b = Color{0.0f, 0.3f, 0.8f, 1.0f};
    Framebuffer fb(4, 4);
    apply_checkerboard(fb, params);
    CHECK(fb.get_pixel(0, 0) == params.color_a);
    CHECK(fb.get_pixel(2, 0) == params.color_b);
    CHECK(fb.get_pixel(0, 2) == params.color_b);
    CHECK(fb.get_pixel(2, 2) == params.color_a);
}

TEST_CASE("Checkerboard feather smooths edges and clipping preserves surroundings") {
    CheckerboardParams params;
    params.size = 4.0f;
    params.feather = 2.0f;
    Framebuffer fb(8, 4);
    apply_checkerboard(fb, params, raster::BBox{2, 0, 6, 4});
    CHECK(fb.get_pixel(0, 0) == Color::transparent());
    CHECK(fb.get_pixel(7, 0) == Color::transparent());
    CHECK(fb.get_pixel(3, 0).r > 0.0f);
    CHECK(fb.get_pixel(3, 0).r < 1.0f);
}

TEST_CASE("Checkerboard is present in the effect catalog") {
    const auto entries = effects::builtin_effect_catalog();
    const auto found = std::ranges::find_if(entries, [](const auto& entry) {
        return entry.id == effects::ids::GenerateCheckerboard;
    });
    REQUIRE(found != entries.end());
    CHECK(found->type == effects::EffectType::Checkerboard);
    CHECK(found->alpha_policy == effects::AlphaPolicy::Generate);
    CHECK(found->bounds_policy == effects::BoundsPolicy::FullFrame);
}

TEST_CASE("Four-color gradient honors normalized color control points") {
    FourColorGradientParams params;
    params.points = {Vec2{0.25f, 0.25f}, Vec2{0.75f, 0.25f},
                     Vec2{0.25f, 0.75f}, Vec2{0.75f, 0.75f}};
    params.colors = {Color{1.0f, 0.0f, 0.0f, 1.0f}, Color{0.0f, 1.0f, 0.0f, 1.0f},
                     Color{0.0f, 0.0f, 1.0f, 1.0f}, Color{1.0f, 1.0f, 0.0f, 1.0f}};
    params.blend_smoothness = 0.0f;
    Framebuffer fb(2, 2);
    apply_four_color_gradient(fb, params);
    CHECK(fb.get_pixel(0, 0) == params.colors[0]);
    CHECK(fb.get_pixel(1, 0) == params.colors[1]);
    CHECK(fb.get_pixel(0, 1) == params.colors[2]);
    CHECK(fb.get_pixel(1, 1) == params.colors[3]);
}

TEST_CASE("Four-color gradient generates content only inside clip") {
    FourColorGradientParams params;
    Framebuffer fb(4, 4);
    apply_four_color_gradient(fb, params, raster::BBox{1, 1, 3, 3});
    CHECK(fb.get_pixel(0, 0) == Color::transparent());
    CHECK(fb.get_pixel(1, 1).a == doctest::Approx(1.0f));
    CHECK(fb.get_pixel(2, 2).a == doctest::Approx(1.0f));
    CHECK(fb.get_pixel(3, 3) == Color::transparent());
}

TEST_CASE("Four-color gradient is present in the effect catalog") {
    const auto entries = effects::builtin_effect_catalog();
    const auto found = std::ranges::find_if(entries, [](const auto& entry) {
        return entry.id == effects::ids::GenerateFourColorGradient;
    });
    REQUIRE(found != entries.end());
    CHECK(found->type == effects::EffectType::FourColorGradient);
    CHECK(found->alpha_policy == effects::AlphaPolicy::Generate);
    CHECK(found->bounds_policy == effects::BoundsPolicy::FullFrame);
}

TEST_CASE("Light rays deterministically extend bright samples from their origin") {
    LightRaysParams params;
    params.origin = Vec2{0.125f, 0.125f};
    params.length = 8.0f;
    params.density = 1.0f;
    params.decay = 1.0f;
    params.color = Color{1.0f, 0.2f, 0.0f, 1.0f};
    Framebuffer first(8, 8), second(8, 8);
    first.set_pixel(0, 0, Color::white());
    second.set_pixel(0, 0, Color::white());
    EffectScratchResources scratch_a, scratch_b;
    apply_light_rays(first, params, scratch_a);
    apply_light_rays(second, params, scratch_b);
    CHECK(first.get_pixel(3, 0).r > 0.0f);
    for (int y = 0; y < first.height(); ++y)
        for (int x = 0; x < first.width(); ++x)
            CHECK(first.get_pixel(x, y) == second.get_pixel(x, y));
}

TEST_CASE("Light rays honor clipping and are present in the effect catalog") {
    LightRaysParams params;
    params.origin = Vec2{0.125f, 0.125f};
    params.length = 8.0f;
    params.density = 1.0f;
    Framebuffer fb(8, 8);
    fb.set_pixel(0, 0, Color::white());
    EffectScratchResources scratch;
    apply_light_rays(fb, params, scratch, raster::BBox{2, 0, 6, 8});
    CHECK(fb.get_pixel(1, 0) == Color::transparent());
    const auto entries = effects::builtin_effect_catalog();
    const auto found = std::ranges::find_if(entries, [](const auto& entry) {
        return entry.id == effects::ids::GenerateLightRays;
    });
    REQUIRE(found != entries.end());
    CHECK(found->type == effects::EffectType::LightRays);
    CHECK(found->bounds_policy == effects::BoundsPolicy::FullFrame);
    CHECK_FALSE(found->fusible_color);
}

TEST_CASE("Tier 2 generators dispatch through the registered effect processors") {
    SoftwareRegistry registry;
    register_builtin_processors(registry);
    EffectScratchResources scratch;
    effects::EffectExecutionContext context;
    context.effect_scratch = &scratch;

    const auto exercise = [&](auto params) {
        EffectStack stack;
        stack.push_back(EffectInstance{params});
        Framebuffer fb(16, 16);
        fb.clear(Color::white());
        apply_effect_stack(fb, stack, context, registry);
        bool generated = false;
        for (int y = 0; y < fb.height(); ++y)
            for (int x = 0; x < fb.width(); ++x)
                generated |= fb.get_pixel(x, y).a > 0.0f;
        CHECK(generated);
    };

    exercise(FractalNoiseParams{});
    exercise(CheckerboardParams{});
    exercise(FourColorGradientParams{});
    LightRaysParams rays;
    rays.origin = Vec2{0.5f, 0.5f};
    rays.length = 16.0f;
    exercise(rays);
}

TEST_CASE("Tier2GeneratorFramehashCanary") {
    constexpr int width = 32;
    constexpr int height = 32;
    SoftwareRegistry registry;
    register_builtin_processors(registry);
    EffectScratchResources scratch;
    effects::EffectExecutionContext context;
    context.effect_scratch = &scratch;
    const char* raw_path = std::getenv("CHRONON_TIER2_GENERATORS_RAW");
    std::ofstream raw;
    if (raw_path && *raw_path) {
        raw.open(raw_path, std::ios::binary | std::ios::trunc);
        REQUIRE(raw.good());
    }

    const auto render_frame = [&](EffectInstance effect, float time, bool seed_light_source) {
        Framebuffer fb(width, height);
        fb.clear(Color::transparent());
        if (seed_light_source) fb.set_pixel(width / 2, height / 2, Color::white());
        EffectStack stack;
        stack.push_back(std::move(effect));
        context.time_seconds = time;
        apply_effect_stack(fb, stack, context, registry);
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width * height * 4));
        std::size_t offset = 0;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const Color pixel = fb.get_pixel(x, y);
                rgba[offset++] = Color::linear_to_srgb8(pixel.r);
                rgba[offset++] = Color::linear_to_srgb8(pixel.g);
                rgba[offset++] = Color::linear_to_srgb8(pixel.b);
                rgba[offset++] = static_cast<std::uint8_t>(
                    std::clamp(pixel.a, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        }
        if (raw.is_open()) raw.write(reinterpret_cast<const char*>(rgba.data()),
                                     static_cast<std::streamsize>(rgba.size()));
        CHECK(fb.get_pixel(width / 2, height / 2).a > 0.0f);
    };

    FractalNoiseParams fractal;
    fractal.seed = 20260924u;
    fractal.frequency = 0.07f;
    fractal.evolution = 2.0f;
    render_frame(EffectInstance{fractal}, 0.0f, false);
    render_frame(EffectInstance{fractal}, 0.5f, false);
    render_frame(EffectInstance{CheckerboardParams{.size = 7.0f, .feather = 1.5f}}, 0.0f, false);
    render_frame(EffectInstance{FourColorGradientParams{}}, 0.0f, false);
    LightRaysParams rays;
    rays.length = 22.0f;
    rays.density = 1.5f;
    render_frame(EffectInstance{rays}, 0.0f, true);
    if (raw.is_open()) CHECK(raw.good());
}
