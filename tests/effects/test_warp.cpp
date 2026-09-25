// =============================================================================
// test_warp.cpp — Warp family (CornerPin, Twirl, Bulge, MeshWarp)
//
// The four effects resample in place through an inverse map, so the identity
// configuration must reproduce the input pixel-exactly and a constant
// displacement must translate content without smearing beyond bilinear.
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/backends/software/builtin_processors.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/effects/effect_execution_context.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include "src/backends/software/effects/render_effects_processor.hpp"

#include <cmath>
#include <stdexcept>

using namespace chronon3d;
using namespace chronon3d::renderer;

namespace {

Framebuffer make_gradient(int w, int h) {
    Framebuffer fb(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            fb.set_pixel(x, y, Color{(static_cast<float>(x) + 1.0f) / static_cast<float>(w),
                                     (static_cast<float>(y) + 1.0f) / static_cast<float>(h),
                                     0.5f, 1.0f});
    return fb;
}

Framebuffer run_effect(const Framebuffer& input, const EffectParams& params) {
    SoftwareRegistry registry;
    register_builtin_processors(registry);
    Framebuffer fb = input;
    EffectStack stack;
    stack.emplace_back(params);
    effects::EffectExecutionContext context;
    apply_effect_stack(fb, stack, context, registry);
    return fb;
}

bool colors_close(const Color& a, const Color& b, float eps = 1.0e-3f) {
    return std::abs(a.r - b.r) <= eps && std::abs(a.g - b.g) <= eps &&
           std::abs(a.b - b.b) <= eps && std::abs(a.a - b.a) <= eps;
}

} // namespace

TEST_CASE("Warp family: identity configurations reproduce the input") {
    const Framebuffer input = make_gradient(8, 8);

    const auto corner = run_effect(input, CornerPinParams{});
    const auto twirl = run_effect(input, TwirlParams{.angle_degrees = 0.0f});
    const auto bulge = run_effect(input, BulgeParams{.amount = 0.0f});
    MeshWarpParams mesh;
    mesh.offsets.assign(4, Vec2{0.0f, 0.0f});
    const auto warped = run_effect(input, mesh);

    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const Color expected = input.get_pixel(x, y);
            CHECK(colors_close(corner.get_pixel(x, y), expected));
            CHECK(colors_close(twirl.get_pixel(x, y), expected));
            CHECK(colors_close(bulge.get_pixel(x, y), expected));
            CHECK(colors_close(warped.get_pixel(x, y), expected));
        }
    }
}

TEST_CASE("Corner pin translates content when the quad is shifted") {
    const Framebuffer input = make_gradient(8, 8);
    CornerPinParams shift;
    shift.top_left = {0.5f, 0.0f};
    shift.top_right = {1.5f, 0.0f};
    shift.bottom_right = {1.5f, 1.0f};
    shift.bottom_left = {0.5f, 1.0f};
    const Framebuffer out = run_effect(input, shift);
    // Content moves right by half the width: out(x + 4, y) == input(x, y).
    CHECK(colors_close(out.get_pixel(5, 2), input.get_pixel(1, 2)));
    CHECK(colors_close(out.get_pixel(7, 6), input.get_pixel(3, 6)));
    // The vacated left half samples outside the quad → transparent.
    CHECK(out.get_pixel(1, 2).a == doctest::Approx(0.0f));
}

TEST_CASE("Twirl rotates around the center and leaves the far field alone") {
    const Framebuffer input = make_gradient(16, 16);
    TwirlParams params;
    params.center = {0.5f, 0.5f};
    params.angle_degrees = 180.0f;
    params.radius = 0.25f;
    const Framebuffer out = run_effect(input, params);
    // Outside the radius the warp is the identity.
    CHECK(colors_close(out.get_pixel(0, 0), input.get_pixel(0, 0)));
    CHECK(colors_close(out.get_pixel(15, 15), input.get_pixel(15, 15)));
    // Inside the radius the sampled color differs from the input.
    bool changed = false;
    for (int y = 6; y < 10 && !changed; ++y)
        for (int x = 6; x < 10 && !changed; ++x)
            changed = !colors_close(out.get_pixel(x, y), input.get_pixel(x, y), 0.02f);
    CHECK(changed);
}

TEST_CASE("Bulge magnifies the centre and pinches with negative amount") {
    const Framebuffer input = make_gradient(16, 16);
    BulgeParams bulge;
    bulge.center = {0.5f, 0.5f};
    bulge.amount = 1.0f;
    bulge.radius = 0.5f;
    const Framebuffer out = run_effect(input, bulge);
    // Magnification pulls content inward: the gradient value at (10, 8) must
    // now come from closer to the centre, i.e. a strictly smaller red value.
    CHECK(out.get_pixel(10, 8).r < input.get_pixel(10, 8).r);
    // Outside the radius the warp is the identity.
    CHECK(colors_close(out.get_pixel(0, 0), input.get_pixel(0, 0)));

    BulgeParams pinch;
    pinch.center = {0.5f, 0.5f};
    pinch.amount = -1.0f;
    pinch.radius = 0.5f;
    const Framebuffer pinched = run_effect(input, pinch);
    // Pinching pushes content outward: a strictly larger red value at (10, 8).
    CHECK(pinched.get_pixel(10, 8).r > input.get_pixel(10, 8).r);
}

TEST_CASE("Mesh warp shifts content by a constant displacement") {
    const Framebuffer input = make_gradient(8, 8);
    MeshWarpParams params;
    params.offsets.assign(4, Vec2{0.25f, 0.0f});
    const Framebuffer out = run_effect(input, params);
    // Positive displacement moves content right by 0.25 * 8 = 2 px.
    CHECK(colors_close(out.get_pixel(4, 3), input.get_pixel(2, 3)));
    CHECK(colors_close(out.get_pixel(7, 5), input.get_pixel(5, 5)));
    // Rows with no vertical displacement keep their gradient.
    CHECK(colors_close(out.get_pixel(2, 6), input.get_pixel(0, 6)));
}

TEST_CASE("Warp family rejects invalid parameters fail-closed") {
    const Framebuffer input = make_gradient(4, 4);

    TwirlParams twirl;
    twirl.radius = -1.0f;
    CHECK_THROWS(run_effect(input, twirl));

    BulgeParams bulge;
    bulge.amount = 2.0f;
    CHECK_THROWS(run_effect(input, bulge));

    MeshWarpParams mesh;
    mesh.cols = 3;
    mesh.offsets.assign(4, Vec2{0.0f, 0.0f}); // 3 * 2 expected, 2 * 2 given
    CHECK_THROWS(run_effect(input, mesh));

    CornerPinParams corner;
    corner.top_left = {std::nanf(""), 0.0f};
    CHECK_THROWS(run_effect(input, corner));
}
