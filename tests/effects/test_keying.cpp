#include <doctest/doctest.h>

#include <chronon3d/backends/software/builtin_processors.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/effects/effect_execution_context.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include "src/backends/software/effects/render_effects_processor.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string_view>

using namespace chronon3d;
using namespace chronon3d::renderer;

namespace {
void write_raw_rgba8(const Framebuffer& fb, const char* path) {
    if (!path || !*path) return;
    std::ofstream raw(path, std::ios::binary | std::ios::trunc);
    REQUIRE(raw.good());
    for (int y = 0; y < fb.height(); ++y) for (int x = 0; x < fb.width(); ++x) {
        const auto pixel = fb.get_pixel(x, y);
        const std::uint8_t rgba[] = {Color::linear_to_srgb8(pixel.r), Color::linear_to_srgb8(pixel.g),
            Color::linear_to_srgb8(pixel.b), static_cast<std::uint8_t>(pixel.a * 255.0f + 0.5f)};
        raw.write(reinterpret_cast<const char*>(rgba), sizeof(rgba));
    }
    REQUIRE(raw.good());
}
}

TEST_CASE("Keying removes chroma and luma while preserving premultiplied alpha") {
    SoftwareRegistry registry;
    register_builtin_processors(registry);
    Framebuffer fb(4, 1);
    fb.set_pixel(0, 0, Color{0.0f, 1.0f, 0.0f, 1.0f});
    fb.set_pixel(1, 0, Color{0.5f, 0.0f, 0.0f, 0.5f});
    fb.set_pixel(2, 0, Color{0.0f, 0.0f, 0.0f, 1.0f});
    fb.set_pixel(3, 0, Color{1.0f, 1.0f, 1.0f, 1.0f});
    EffectStack stack;
    stack.emplace_back(KeyingParams{.mode = KeyingMode::Chroma,
        .key_color = Color{0.0f, 1.0f, 0.0f, 1.0f},
        .threshold = 0.01f, .softness = 0.1f, .spill = 0.0f});
    effects::EffectExecutionContext context;
    apply_effect_stack(fb, stack, context, registry);
    CHECK(fb.get_pixel(0, 0).a == doctest::Approx(0.0f));
    CHECK(fb.get_pixel(1, 0).a == doctest::Approx(0.5f));
    CHECK(fb.get_pixel(1, 0).r == doctest::Approx(0.5f));

    EffectStack luma;
    luma.emplace_back(KeyingParams{.mode = KeyingMode::Luma,
        .threshold = 0.2f, .softness = 0.1f, .spill = 0.0f});
    apply_effect_stack(fb, luma, context, registry);
    CHECK(fb.get_pixel(2, 0).a == doctest::Approx(0.0f));
    CHECK(fb.get_pixel(3, 0).a == doctest::Approx(1.0f));
}

TEST_CASE("KeyingFramehashCanary") {
    constexpr int width = 32, height = 32;
    SoftwareRegistry registry;
    register_builtin_processors(registry);
    EffectStack stack;
    stack.emplace_back(KeyingParams{.mode = KeyingMode::Chroma,
        .key_color = Color{0.0f, 0.75f, 0.0f, 1.0f},
        .threshold = 0.07f, .softness = 0.15f, .spill = 0.6f});
    Framebuffer fb(width, height);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        if (x < 11) fb.set_pixel(x, y, Color{0.0f, 0.75f, 0.0f, 1.0f});
        else if (x < 17) fb.set_pixel(x, y, Color{0.0f, 0.9f, 0.1f, 1.0f});
        else fb.set_pixel(x, y, Color{0.8f, 0.15f, 0.08f, 1.0f});
    }
    effects::EffectExecutionContext context;
    apply_effect_stack(fb, stack, context, registry);
    write_raw_rgba8(fb, std::getenv("CHRONON_KEYING_RAW"));
    CHECK(fb.get_pixel(3, 16).a == doctest::Approx(0.0f));
    CHECK(fb.get_pixel(26, 16).a == doctest::Approx(1.0f));
}
