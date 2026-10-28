#include <doctest/doctest.h>

#include <chronon3d/effects/color_pipeline.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/backends/software/builtin_processors.hpp>
#include <chronon3d/backends/software/software_session_resources.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/effects/effect_execution_context.hpp>
#include "src/backends/software/effects/render_effects_processor.hpp"

#include <array>
#include <limits>
#include <stdexcept>

using namespace chronon3d;
using namespace chronon3d::renderer;

namespace {

// A red->black ramp with a hard white top stop: luminance 1 maps to white,
// everything below maps toward black through the evenly spaced stops.
ColorMapParams ramp_params() {
    ColorMapParams params;
    params.stops = {0.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 0.0f,
                    1.0f, 1.0f, 1.0f};
    return params;
}

} // namespace

TEST_CASE("ColorMap: default stops map luminance through a grey ramp") {
    Framebuffer fb(1, 1);
    fb.pixels_row(0)[0] = {0.5f, 0.25f, 0.125f, 0.5f}; // premultiplied

    ColorPipeline pipeline;
    pipeline.add_stage(ColorMapStage{ColorMapParams{}.stops});
    pipeline.apply(fb);

    const float luma = 0.2126f * 1.0f + 0.7152f * 0.5f + 0.0722f * 0.25f;
    const auto out = fb.get_pixel(0, 0);
    CHECK(out.r == doctest::Approx(luma * 0.5f).epsilon(1.0e-5));
    CHECK(out.g == doctest::Approx(luma * 0.5f).epsilon(1.0e-5));
    CHECK(out.b == doctest::Approx(luma * 0.5f).epsilon(1.0e-5));
    CHECK(out.a == doctest::Approx(0.5f));
}

TEST_CASE("ColorMap: transparent pixels stay transparent") {
    Framebuffer fb(1, 1);
    fb.pixels_row(0)[0] = {0.0f, 0.0f, 0.0f, 0.0f};

    ColorPipeline pipeline;
    pipeline.add_stage(ColorMapStage{ramp_params().stops});
    pipeline.apply(fb);

    const auto out = fb.get_pixel(0, 0);
    CHECK(out.r == 0.0f);
    CHECK(out.g == 0.0f);
    CHECK(out.b == 0.0f);
    CHECK(out.a == 0.0f);
}

TEST_CASE("ColorMap: software processor matches the fused color pipeline") {
    ColorMapParams params = ramp_params();
    constexpr int width = 4;
    constexpr int height = 2;

    Framebuffer fused(width, height);
    Framebuffer processed(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Color pixel{static_cast<float>(x) / (width - 1),
                              static_cast<float>(y) / (height - 1),
                              0.25f, 1.0f};
            fused.set_pixel(x, y, pixel);
            processed.set_pixel(x, y, pixel);
        }
    }

    ColorPipeline pipeline;
    pipeline.add_stage(ColorMapStage{params.stops});
    pipeline.apply(fused);

    SoftwareRegistry registry;
    register_builtin_processors(registry);
    EffectStack stack;
    stack.push_back(EffectInstance{params});
    effects::EffectExecutionContext context;
    apply_effect_stack(processed, stack, context, registry);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto expected = fused.get_pixel(x, y);
            const auto actual = processed.get_pixel(x, y);
            CHECK(actual.r == doctest::Approx(expected.r).epsilon(1.0e-5));
            CHECK(actual.g == doctest::Approx(expected.g).epsilon(1.0e-5));
            CHECK(actual.b == doctest::Approx(expected.b).epsilon(1.0e-5));
            CHECK(actual.a == doctest::Approx(expected.a).epsilon(1.0e-5));
        }
    }
}

TEST_CASE("ColorMap: amount blends the mapped colour with the source") {
    Framebuffer fused(1, 1);
    fused.pixels_row(0)[0] = {0.4f, 0.2f, 0.1f, 0.5f};
    ColorPipeline pipeline;
    pipeline.add_stage(ColorMapStage{ramp_params().stops, 0.5f});
    pipeline.apply(fused);

    // Straight (0.8, 0.4, 0.2) maps to black, so half the blend keeps half.
    const auto out = fused.get_pixel(0, 0);
    CHECK(out.r == doctest::Approx(0.2f).epsilon(1.0e-5));
    CHECK(out.g == doctest::Approx(0.1f).epsilon(1.0e-5));
    CHECK(out.b == doctest::Approx(0.05f).epsilon(1.0e-5));
    CHECK(out.a == doctest::Approx(0.5f));

    Framebuffer processed(1, 1);
    processed.set_pixel(0, 0, {0.4f, 0.2f, 0.1f, 0.5f});
    SoftwareRegistry registry;
    register_builtin_processors(registry);
    ColorMapParams params = ramp_params();
    params.amount = 0.5f;
    EffectStack stack;
    stack.push_back(EffectInstance{params});
    effects::EffectExecutionContext context;
    apply_effect_stack(processed, stack, context, registry);

    const auto software = processed.get_pixel(0, 0);
    CHECK(software.r == doctest::Approx(out.r).epsilon(1.0e-5));
    CHECK(software.g == doctest::Approx(out.g).epsilon(1.0e-5));
    CHECK(software.b == doctest::Approx(out.b).epsilon(1.0e-5));
    CHECK(software.a == doctest::Approx(out.a));
}

TEST_CASE("ColorMap: parameters reject non-finite and out-of-range values") {
    ColorMapParams params;
    params.stops[4] = 1.5f;
    CHECK_THROWS_AS(params.validate(), std::invalid_argument);

    params.stops[4] = 0.5f;
    CHECK_NOTHROW(params.validate());

    params.stops[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_AS(params.validate(), std::invalid_argument);

    params.stops[0] = 0.0f;
    params.amount = 1.5f;
    CHECK_THROWS_AS(params.validate(), std::invalid_argument);
    params.amount = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_AS(params.validate(), std::invalid_argument);
}
