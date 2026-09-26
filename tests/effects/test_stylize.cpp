#include <doctest/doctest.h>

#include <chronon3d/backends/software/builtin_processors.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/effects/effect_execution_context.hpp>
#include <chronon3d/backends/software/software_session_resources.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include "src/backends/software/effects/render_effects_processor.hpp"

#include <cstdlib>
#include <fstream>

using namespace chronon3d;
using namespace chronon3d::renderer;

namespace {
void write_raw(const Framebuffer& fb, const char* path) {
    if (!path || !*path) return;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    REQUIRE(out.good());
    for (int y = 0; y < fb.height(); ++y) for (int x = 0; x < fb.width(); ++x) {
        auto p = fb.get_pixel(x, y);
        const std::uint8_t rgba[] = {Color::linear_to_srgb8(p.r), Color::linear_to_srgb8(p.g),
            Color::linear_to_srgb8(p.b), static_cast<std::uint8_t>(p.a * 255.0f + 0.5f)};
        out.write(reinterpret_cast<const char*>(rgba), sizeof(rgba));
    }
    REQUIRE(out.good());
}
}

TEST_CASE("Threshold applies a deterministic luma cutoff and preserves alpha") {
    SoftwareRegistry registry;
    register_builtin_processors(registry);
    Framebuffer fb(4, 1);
    fb.set_pixel(0, 0, Color{0.1f, 0.1f, 0.1f, 1.0f});
    fb.set_pixel(1, 0, Color{0.6f, 0.6f, 0.6f, 1.0f});
    fb.set_pixel(2, 0, Color{0.4f, 0.4f, 0.4f, 0.5f});
    fb.set_pixel(3, 0, Color{0.2f, 0.1f, 0.0f, 0.0f});
    EffectStack stack;
    stack.emplace_back(ThresholdParams{.threshold = 0.5f, .softness = 0.0f});
    effects::EffectExecutionContext context;
    apply_effect_stack(fb, stack, context, registry);
    CHECK(fb.get_pixel(0, 0).r == doctest::Approx(0.0f));
    CHECK(fb.get_pixel(1, 0).r == doctest::Approx(1.0f));
    CHECK(fb.get_pixel(2, 0).r == doctest::Approx(0.5f));
    CHECK(fb.get_pixel(2, 0).a == doctest::Approx(0.5f));
    CHECK(fb.get_pixel(3, 0).a == doctest::Approx(0.0f));
}

TEST_CASE("Mosaic samples stable canvas-aligned block centers") {
    SoftwareRegistry registry;
    register_builtin_processors(registry);
    Framebuffer fb(4, 2);
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 4; ++x)
        fb.set_pixel(x, y, Color{static_cast<float>(x) / 3.0f, static_cast<float>(y), 0.0f, 1.0f});
    EffectStack stack;
    stack.emplace_back(MosaicParams{.block_size = 2});
    effects::EffectExecutionContext context;
    apply_effect_stack(fb, stack, context, registry);
    CHECK(fb.get_pixel(0, 0).r == doctest::Approx(fb.get_pixel(1, 1).r));
    CHECK(fb.get_pixel(2, 0).r == doctest::Approx(fb.get_pixel(3, 1).r));
    CHECK(fb.get_pixel(0, 0).r != doctest::Approx(fb.get_pixel(2, 0).r));
}

TEST_CASE("Sharpen has an exact identity mode and honors its difference threshold") {
    SoftwareRegistry registry;
    register_builtin_processors(registry);
    Framebuffer source(5,1);
    source.set_pixel(0,0,Color{0,0,0,1}); source.set_pixel(1,0,Color{0,0,0,1});
    source.set_pixel(2,0,Color{1,1,1,1}); source.set_pixel(3,0,Color{1,1,1,1});
    source.set_pixel(4,0,Color{1,1,1,1});
    const auto run=[&](SharpenParams params) {
        Framebuffer result=source;
        EffectStack stack; stack.emplace_back(params);
        EffectScratchResources scratch;
        effects::EffectExecutionContext context; context.effect_scratch=&scratch;
        apply_effect_stack(result,stack,context,registry);
        return result;
    };
    const auto identity=run(SharpenParams{.amount=0.0f});
    const auto suppressed=run(SharpenParams{.amount=2.0f,.radius=1.0f,.threshold=1.0f});
    const auto sharpened=run(SharpenParams{.amount=1.0f});
    for(int x=0;x<source.width();++x) {
        CHECK(identity.get_pixel(x,0).r==source.get_pixel(x,0).r);
        CHECK(suppressed.get_pixel(x,0).r==source.get_pixel(x,0).r);
        CHECK(identity.get_pixel(x,0).a==source.get_pixel(x,0).a);
    }
    CHECK(sharpened.get_pixel(1,0).r<source.get_pixel(1,0).r);
    CHECK(sharpened.get_pixel(2,0).r>source.get_pixel(2,0).r);
    CHECK(sharpened.get_pixel(2,0).a==source.get_pixel(2,0).a);
}

TEST_CASE("ThresholdFramehashCanary") {
    constexpr int width = 32, height = 32;
    SoftwareRegistry registry;
    register_builtin_processors(registry);
    Framebuffer fb(width, height);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        float value = (static_cast<float>(x) + static_cast<float>(y) * 0.35f) / 42.0f;
        fb.set_pixel(x, y, Color{value, value * 0.8f, value * 0.6f, 1.0f});
    }
    EffectStack stack;
    stack.emplace_back(ThresholdParams{.threshold = 0.43f, .softness = 0.2f});
    effects::EffectExecutionContext context;
    apply_effect_stack(fb, stack, context, registry);
    write_raw(fb, std::getenv("CHRONON_THRESHOLD_RAW"));
}

TEST_CASE("MosaicFramehashCanary") {
    constexpr int width = 32, height = 32;
    SoftwareRegistry registry;
    register_builtin_processors(registry);
    Framebuffer fb(width, height);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
        fb.set_pixel(x, y, Color{static_cast<float>(x) / 31.0f, static_cast<float>(y) / 31.0f,
            static_cast<float>((x * 7 + y * 3) % 32) / 31.0f, 1.0f});
    EffectStack stack;
    stack.emplace_back(MosaicParams{.block_size = 5});
    effects::EffectExecutionContext context;
    apply_effect_stack(fb, stack, context, registry);
    write_raw(fb, std::getenv("CHRONON_MOSAIC_RAW"));
}
