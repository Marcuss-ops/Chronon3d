// ---------------------------------------------------------------------------
// test_sdf_bevel.cpp — SDF bevel, rim and contact-shadow diagnostics.
//
// The materiality pass relies on this effect to give flat vector layers a
// physical edge: a lit bevel on the upper-left, a shaded lower-right, a thin
// rim at the silhouette and a soft contact shadow outside it.  These tests
// keep the effect deterministic, alpha-safe and bounded by its declared halo.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/effects/spatial_bounds.hpp>
#include <chronon3d/math/color.hpp>
#include "src/backends/software/effects/render_effects_processor.hpp"
#include "tests/effects/test_helpers.hpp"

#include <algorithm>
#include <cmath>

using namespace chronon3d;

namespace {

Framebuffer make_image(int width, int height, const Color& background) {
    Framebuffer fb(width, height);
    fb.clear(background);
    return fb;
}

void fill_rect(Framebuffer& fb, int x0, int y0, int x1, int y1,
               const Color& color) {
    for (int y = std::max(0, y0); y < std::min(fb.height(), y1); ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = std::max(0, x0); x < std::min(fb.width(), x1); ++x)
            row[x] = color;
    }
}

float luma(const Color& c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

float mean_luma(const Framebuffer& fb, int x0, int y0, int x1, int y1) {
    double sum = 0.0;
    int count = 0;
    for (int y = std::max(0, y0); y < std::min(fb.height(), y1); ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = std::max(0, x0); x < std::min(fb.width(), x1); ++x) {
            sum += luma(row[x]);
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(sum / count) : 0.0f;
}

SdfBevelParams lit_edge_params() {
    SdfBevelParams p;
    p.bevel_width = 10.0f;
    p.bevel_height = 9.0f;
    p.rim_width = 0.0f;
    p.rim_intensity = 0.0f;
    p.highlight = 0.7f;
    p.shade = 0.55f;
    p.light_angle = -135.0f; // screen-space upper-left
    p.light_elevation = 40.0f;
    p.shadow_opacity = 0.0f;
    return p;
}

} // namespace

TEST_CASE("SdfBevel: empty layers are a no-op") {
    Framebuffer fb = make_image(32, 32, Color{0.0f, 0.0f, 0.0f, 0.0f});
    const Framebuffer before = fb;
    renderer::apply_sdf_bevel_effect(fb, SdfBevelParams{}, 0.0f);

    CHECK(test_fx::framebuffer_max_error(fb, before) <= test_fx::kScalarEpsilon);
}

TEST_CASE("SdfBevel: lit upper-left edge is brighter than the shaded lower-right") {
    Framebuffer fb = make_image(96, 96, Color{0.0f, 0.0f, 0.0f, 0.0f});
    fill_rect(fb, 24, 24, 72, 72, Color{0.55f, 0.55f, 0.55f, 1.0f});
    const Color centre_before = fb.get_pixel(48, 48);

    renderer::apply_sdf_bevel_effect(fb, lit_edge_params(), 0.0f);

    // The flat plateau is not shaded: only the perimeter band carries relief.
    CHECK(fb.get_pixel(48, 48).r == doctest::Approx(centre_before.r));
    CHECK(fb.get_pixel(48, 48).a == doctest::Approx(1.0f));

    const float upper = mean_luma(fb, 28, 24, 68, 30) +
                        mean_luma(fb, 24, 28, 30, 68);
    const float lower = mean_luma(fb, 28, 66, 68, 72) +
                        mean_luma(fb, 66, 28, 72, 68);
    CHECK(upper > lower + 0.05f);
    CHECK(fb.get_pixel(24, 48).a == doctest::Approx(1.0f));
}

TEST_CASE("SdfBevel: contact shadow falls outside the silhouette on the light-opposite side") {
    // The effect runs on a layer framebuffer, so the area around the silhouette
    // is transparent until the projected shadow is composited into it.
    Framebuffer fb = make_image(128, 128, Color{0.0f, 0.0f, 0.0f, 0.0f});
    fill_rect(fb, 40, 40, 88, 88, Color{0.8f, 0.8f, 0.8f, 1.0f});

    SdfBevelParams p;
    p.bevel_width = 6.0f;
    p.bevel_height = 4.0f;
    p.rim_width = 0.0f;
    p.rim_intensity = 0.0f;
    p.highlight = 0.0f;
    p.shade = 0.0f;
    p.light_angle = -135.0f;
    p.light_elevation = 30.0f;
    p.shadow_distance = 22.0f;
    p.shadow_softness = 6.0f;
    p.shadow_opacity = 0.8f;
    p.shadow_color = Color{0.0f, 0.0f, 0.0f, 1.0f};
    renderer::apply_sdf_bevel_effect(fb, p, 0.0f);

    const Color shadow = fb.get_pixel(100, 100);
    const Color opposite = fb.get_pixel(20, 20);
    CHECK(shadow.a > 0.2f);
    CHECK(shadow.r < 0.05f);
    CHECK(opposite.a < 0.05f);
}

TEST_CASE("SdfBevel: rim lies on the silhouette and follows the light") {
    Framebuffer fb = make_image(96, 96, Color{0.0f, 0.0f, 0.0f, 0.0f});
    fill_rect(fb, 24, 24, 72, 72, Color{0.2f, 0.2f, 0.2f, 1.0f});

    SdfBevelParams p;
    p.bevel_width = 8.0f;
    p.bevel_height = 6.0f;
    p.rim_width = 3.0f;
    p.rim_intensity = 0.9f;
    p.rim_color = Color{1.0f, 0.4f, 0.1f, 1.0f};
    p.highlight = 0.0f;
    p.shade = 0.0f;
    p.light_angle = -135.0f;
    p.shadow_opacity = 0.0f;
    renderer::apply_sdf_bevel_effect(fb, p, 0.0f);

    const Color edge = fb.get_pixel(24, 48);
    const Color plateau = fb.get_pixel(48, 48);
    CHECK(edge.r > plateau.r + 0.05f);
    CHECK(edge.r > edge.b);
    CHECK(edge.a == doctest::Approx(1.0f));
}

TEST_CASE("SdfBevel: repeat execution is deterministic") {
    Framebuffer a = make_image(96, 96, Color{0.0f, 0.0f, 0.0f, 0.0f});
    fill_rect(a, 24, 24, 72, 72, Color{0.55f, 0.55f, 0.55f, 1.0f});
    Framebuffer b = a;
    SdfBevelParams p = lit_edge_params();
    p.light_orbit_speed = 12.0f;

    renderer::apply_sdf_bevel_effect(a, p, 1.25f);
    renderer::apply_sdf_bevel_effect(b, p, 1.25f);

    CHECK(test_fx::framebuffer_max_error(a, b) <= test_fx::kScalarEpsilon);
}

TEST_CASE("SdfBevel: spatial halo covers the bevel and the widest shadow reach") {
    SdfBevelParams p;
    p.bevel_width = 12.0f;
    p.rim_width = 4.0f;
    p.shadow_distance = 40.0f;
    p.shadow_softness = 12.0f;
    p.shadow_opacity = 0.7f;

    const auto halo = effects::effect_spatial_halo(p);
    CHECK(halo.left == doctest::Approx(halo.right));
    CHECK(halo.top == doctest::Approx(halo.bottom));
    CHECK(halo.right >= 40.0f * 1.65f);

    SdfBevelParams no_shadow = p;
    no_shadow.shadow_opacity = 0.0f;
    const auto compact = effects::effect_spatial_halo(no_shadow);
    CHECK(compact.right < halo.right);
}
