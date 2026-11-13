// ---------------------------------------------------------------------------
// test_ae_gap_effects.cpp — AE gap closure effects
// ---------------------------------------------------------------------------
//
// Covered:
//   * Inner Glow produces an inset edge band and leaves the center intact.
//   * Glow Advanced composites a thresholded halo behind its source.
//   * Optics Compensation with FOV 0 is identity; reverse=True changes corners.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <chronon3d/backends/software/builtin_processors.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/backends/software/software_session_resources.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/effects/effect_execution_context.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include "src/backends/software/effects/render_effects_processor.hpp"

#include <algorithm>
#include <cmath>

using namespace chronon3d;
using namespace chronon3d::renderer;

namespace {

EffectScratchResources g_scratch;

template <class Params>
void apply_one(Framebuffer& fb, const Params& params) {
    SoftwareRegistry registry;
    register_builtin_processors(registry);
    EffectStack stack;
    stack.push_back(EffectInstance{params});
    effects::EffectExecutionContext context;
    context.effect_scratch = &g_scratch;
    apply_effect_stack(fb, stack, context, registry);
}

} // namespace

TEST_CASE("Inner Glow paints an inset edge band without changing the center") {
    Framebuffer fb(9, 9);
    fb.clear(Color::transparent());
    for (int y = 1; y <= 7; ++y)
        for (int x = 1; x <= 7; ++x)
            fb.set_pixel(x, y, Color{0.25f, 0.05f, 0.05f, 1.0f});

    InnerGlowParams params;
    params.size = 2.0f;
    params.choke = 0.0f;
    params.intensity = 1.0f;
    params.color_r = 1.0f;
    params.color_g = 0.0f;
    params.color_b = 0.0f;
    apply_one(fb, params);

    const auto edge = fb.get_pixel(1, 4);
    const auto center = fb.get_pixel(4, 4);
    CHECK(edge.r > 0.25f);
    CHECK(edge.a == doctest::Approx(1.0f));
    CHECK(center.r == doctest::Approx(0.25f).epsilon(1.0e-5));
    CHECK(center.a == doctest::Approx(1.0f));
}

TEST_CASE("Glow Advanced composites a thresholded A/B halo behind the source") {
    Framebuffer fb(9, 9);
    fb.clear(Color::transparent());
    fb.set_pixel(4, 4, Color{1.0f, 1.0f, 1.0f, 1.0f});

    GlowAdvancedParams params;
    params.threshold = 0.0f;
    params.radius = 2.0f;
    params.intensity = 1.0f;
    params.color_r = 1.0f;
    params.color_g = 0.0f;
    params.color_b = 0.0f;
    params.color_b_r = 0.0f;
    params.color_b_g = 0.0f;
    params.color_b_b = 1.0f;
    params.falloff = 1.5f;
    apply_one(fb, params);

    const auto source = fb.get_pixel(4, 4);
    const auto halo = fb.get_pixel(3, 4);
    CHECK(source.r == doctest::Approx(1.0f));
    CHECK(source.a == doctest::Approx(1.0f));
    CHECK(halo.a > 0.0f);
    CHECK(halo.b > 0.0f);
}

TEST_CASE("Light Sweep brightens a moving band and preserves alpha") {
    Framebuffer fb(9, 9);
    fb.clear(Color::transparent());
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 9; ++x)
            fb.set_pixel(x, y, Color{0.1f, 0.1f, 0.1f, 1.0f});

    LightSweepParams params;
    params.center_x = 0.5f;
    params.center_y = 0.5f;
    params.angle_degrees = 0.0f;
    params.width = 2.0f;
    params.softness = 0.0f;
    params.intensity = 1.0f;
    params.edge_intensity = 0.0f;
    apply_one(fb, params);

    CHECK(fb.get_pixel(4, 4).r > 0.9f);
    CHECK(fb.get_pixel(4, 4).a == doctest::Approx(1.0f));
    CHECK(fb.get_pixel(0, 4).r == doctest::Approx(0.1f));
    CHECK(fb.get_pixel(0, 4).a == doctest::Approx(1.0f));

    Framebuffer transparent(5, 5);
    transparent.clear(Color::transparent());
    apply_one(transparent, params);
    CHECK(transparent.get_pixel(2, 2).a == doctest::Approx(0.0f));
    CHECK(transparent.get_pixel(2, 2).r == doctest::Approx(0.0f));
}

TEST_CASE("CC Cylinder and CC Sphere wrap a layer and clip outside the surface") {
    Framebuffer cylinder(9, 9);
    cylinder.clear(Color::transparent());
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 9; ++x)
            cylinder.set_pixel(x, y, Color{0.2f, 0.3f, 0.4f, 1.0f});
    CylinderWrapParams cparams;
    cparams.width = 0.3f;
    cparams.height = 0.5f;
    apply_one(cylinder, cparams);
    CHECK(cylinder.get_pixel(4, 4).a == doctest::Approx(1.0f));
    CHECK(cylinder.get_pixel(0, 0).a == doctest::Approx(0.0f));

    Framebuffer sphere(9, 9);
    sphere.clear(Color::transparent());
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 9; ++x)
            sphere.set_pixel(x, y, Color{0.4f, 0.2f, 0.1f, 1.0f});
    SphereWrapParams sparams;
    sparams.radius = 0.3f;
    apply_one(sphere, sparams);
    CHECK(sphere.get_pixel(4, 4).a == doctest::Approx(1.0f));
    CHECK(sphere.get_pixel(0, 0).a == doctest::Approx(0.0f));
}

TEST_CASE("Three stacked DropShadow instances keep all authored shadow lobes") {
    Framebuffer fb(61, 61);
    fb.clear(Color::transparent());
    fb.set_pixel(30, 30, Color{1.0f, 1.0f, 1.0f, 1.0f});

    SoftwareRegistry registry;
    register_builtin_processors(registry);
    EffectStack stack;
    stack.push_back(EffectInstance{DropShadowParams{
        .offset = {10.0f, 0.0f}, .color = Color{1.0f, 0.0f, 0.0f, 0.6f}, .radius = 0.0f}});
    stack.push_back(EffectInstance{DropShadowParams{
        .offset = {0.0f, 10.0f}, .color = Color{0.0f, 1.0f, 0.0f, 0.6f}, .radius = 0.0f}});
    stack.push_back(EffectInstance{DropShadowParams{
        .offset = {10.0f, 10.0f}, .color = Color{0.0f, 0.0f, 1.0f, 0.6f}, .radius = 0.0f}});
    effects::EffectExecutionContext context;
    context.effect_scratch = &g_scratch;
    apply_effect_stack(fb, stack, context, registry);

    CHECK(fb.get_pixel(40, 30).a > 0.05f);
    CHECK(fb.get_pixel(30, 40).a > 0.05f);
    CHECK(fb.get_pixel(40, 40).a > 0.05f);
}

TEST_CASE("Optics Compensation is identity at FOV 0 and bends corners with reverse") {
    Framebuffer original(9, 9);
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 9; ++x)
            original.set_pixel(x, y, Color{static_cast<float>(x) / 8.0f,
                                          static_cast<float>(y) / 8.0f,
                                          0.0f, 1.0f});

    Framebuffer identity = original;
    OpticsCompensationParams params;
    params.fov_degrees = 0.0f;
    params.reverse = 0.0f;
    params.scale = 1.0f;
    apply_one(identity, params);
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 9; ++x) {
            const auto expected = original.get_pixel(x, y);
            const auto actual = identity.get_pixel(x, y);
            CHECK(actual.r == doctest::Approx(expected.r).epsilon(1.0e-5));
            CHECK(actual.g == doctest::Approx(expected.g).epsilon(1.0e-5));
        }

    Framebuffer warped = original;
    params.fov_degrees = 60.0f;
    params.reverse = 1.0f;
    apply_one(warped, params);
    const auto before = original.get_pixel(2, 2);
    const auto after = warped.get_pixel(2, 2);
    CHECK(std::abs(after.r - before.r) > 0.01f);
}
