#include <doctest/doctest.h>
#include <chronon3d/backends/software/rasterizers/card3d_material_rasterizer.hpp>
#include <chronon3d/rendering/projected_card.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <tests/helpers/test_utils.hpp>
#include <chronon3d/scene/model/core/card3d_material.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <filesystem>
#include <cmath>

using namespace chronon3d;
using namespace chronon3d::renderer;
using chronon3d::test::load_png_as_framebuffer;

namespace {

bool colors_near(const Color& c1, const Color& c2, float tolerance = 0.05f) {
    return std::abs(c1.r - c2.r) <= tolerance &&
           std::abs(c1.g - c2.g) <= tolerance &&
           std::abs(c1.b - c2.b) <= tolerance &&
           std::abs(c1.a - c2.a) <= tolerance;
}

// Build a simple screen-space card (no perspective, flat in screen).
rendering::ProjectedCard make_flat_card(float cx, float cy, float w, float h) {
    rendering::ProjectedCard card;
    const float hw = w * 0.5f;
    const float hh = h * 0.5f;
    const float z = 500.0f;  // depth
    card.corners[0] = {cx - hw, cy - hh, z}; // TL
    card.corners[1] = {cx + hw, cy - hh, z}; // TR
    card.corners[2] = {cx + hw, cy + hh, z}; // BR
    card.corners[3] = {cx - hw, cy + hh, z}; // BL
    card.visible = true;
    card.depth = z;
    return card;
}

} // namespace

TEST_CASE("Card3DMaterial: renders front gradient quad") {
    const int W = 200, H = 200;
    Framebuffer fb(W, H);
    fb.clear(Color{0.05f, 0.05f, 0.10f, 1.0f});

    auto card = make_flat_card(W / 2, H / 2, 120, 80);
    Card3DMaterial mat = Card3DMaterial::glass();

    render_card3d(fb, card, mat, 1.0f);

    Color center = fb.get_pixel(W / 2, H / 2);
    CHECK(center.a > 0.1f);
    CHECK(center.r > 0.3f);
    CHECK(center.g > 0.3f);
    CHECK(center.b > 0.3f);
}

TEST_CASE("Card3DMaterial: side faces are rendered") {
    const int W = 300, H = 300;
    Framebuffer fb(W, H);
    fb.clear(Color::black());

    auto card = make_flat_card(W / 2, H / 2, 100, 100);
    Card3DMaterial mat = Card3DMaterial::dark();
    mat.thickness_px = 30.0f;

    render_card3d(fb, card, mat, 1.0f);

    const int right_x = W / 2 + 50 + 15;
    const int center_y = H / 2;
    Color side_pixel = fb.get_pixel(right_x, center_y);
    CHECK(side_pixel.a > 0.1f);
    CHECK(side_pixel.b > 0.05f);

    const int bottom_y = H / 2 + 50 + 15;
    Color bottom_pixel = fb.get_pixel(W / 2, bottom_y);
    CHECK(bottom_pixel.a > 0.1f);
}

TEST_CASE("Card3DMaterial: presets construct correctly") {
    auto glass = Card3DMaterial::glass();
    CHECK(glass.edge_highlight_intensity == doctest::Approx(0.50f));
    CHECK(glass.rim_light_intensity == doctest::Approx(0.60f));
    CHECK(glass.front_top_color.r == doctest::Approx(0.92f));

    auto dark = Card3DMaterial::dark();
    CHECK(dark.front_top_color.r == doctest::Approx(0.18f));
    CHECK(dark.edge_highlight_color.r == doctest::Approx(0.60f));

    auto neon = Card3DMaterial::neon();
    CHECK(neon.edge_highlight_color.b == doctest::Approx(1.0f));

    auto warm = Card3DMaterial::warm();
    CHECK(warm.front_top_color.r == doctest::Approx(0.95f));

    auto flat = Card3DMaterial::flat();
    CHECK(flat.thickness_px == doctest::Approx(0.0f));
    CHECK_FALSE(flat.cast_shadow);
}

TEST_CASE("Card3DMaterial: golden reference render") {
    const int W = 320, H = 240;
    Framebuffer fb(W, H);
    fb.clear(Color{0.02f, 0.02f, 0.06f, 1.0f});

    auto card1 = make_flat_card(100, 100, 80, 60);
    Card3DMaterial mat1 = Card3DMaterial::glass();
    mat1.thickness_px = 12.0f;
    render_card3d(fb, card1, mat1, 0.9f);

    auto card2 = make_flat_card(220, 140, 70, 50);
    Card3DMaterial mat2 = Card3DMaterial::dark();
    mat2.thickness_px = 16.0f;
    render_card3d(fb, card2, mat2, 1.0f);

    const std::filesystem::path golden_dir = "test_renders/golden";
    std::filesystem::create_directories(golden_dir);
    const std::filesystem::path golden_path = golden_dir / "card3d_material_golden.png";

    if (!std::filesystem::exists(golden_path)) {
        REQUIRE(save_png(fb, golden_path.string()));
    }

    auto golden = load_png_as_framebuffer(golden_path.string());
    REQUIRE(golden != nullptr);
    REQUIRE(golden->width() == fb.width());
    REQUIRE(golden->height() == fb.height());

    bool matched = true;
    int diff_count = 0;
    float max_channel_error = 0.0f;
    const float tolerance = 0.035f;

    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            Color rendered_srgb = fb.get_pixel(x, y).to_srgb();
            Color golden_pixel = golden->get_pixel(x, y);

            float dr = std::abs(rendered_srgb.r - golden_pixel.r);
            float dg = std::abs(rendered_srgb.g - golden_pixel.g);
            float db = std::abs(rendered_srgb.b - golden_pixel.b);
            float da = std::abs(rendered_srgb.a - golden_pixel.a);

            float max_err = std::max({dr, dg, db, da});
            max_channel_error = std::max(max_channel_error, max_err);

            if (max_err > tolerance) {
                if (diff_count < 10) {
                    MESSAGE("Diff at (", x, ",", y, "): rendered_srgb=(",
                            rendered_srgb.r, ",", rendered_srgb.g, ",", rendered_srgb.b, ",", rendered_srgb.a,
                            ") golden=(",
                            golden_pixel.r, ",", golden_pixel.g, ",", golden_pixel.b, ",", golden_pixel.a,
                            ") max_err=", max_err);
                }
                diff_count++;
                matched = false;
            }
        }
    }

    CHECK(matched);
    if (!matched) {
        MESSAGE("Total differing pixels: ", diff_count);
        MESSAGE("Max channel error: ", max_channel_error);
    }
}

TEST_CASE("Card3DMaterial: renders at reduced opacity") {
    const int W = 100, H = 100;
    Framebuffer fb(W, H);
    fb.clear(Color::transparent());

    auto card = make_flat_card(W / 2, H / 2, 60, 60);

    Card3DMaterial mat = Card3DMaterial::glass();
    mat.thickness_px = 10.0f;
    mat.edge_highlight_intensity = 0.0f;
    mat.rim_light_intensity = 0.0f;

    render_card3d(fb, card, mat, 0.5f);

    Color center = fb.get_pixel(W / 2, H / 2);
    CHECK(center.a > 0.1f);
    CHECK(center.a < 1.0f);

    Color corner = fb.get_pixel(5, 5);
    CHECK_EQ(corner.a, 0.0f);
}

TEST_CASE("Card3DMaterial: invisible card produces no pixels") {
    const int W = 100, H = 100;
    Framebuffer fb(W, H);
    fb.clear(Color::transparent());

    rendering::ProjectedCard card;
    card.visible = false;
    Card3DMaterial mat = Card3DMaterial::glass();

    REQUIRE_FALSE(card.visible);
    render_card3d(fb, card, mat, 1.0f);

    int lit_pixels = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            Color p = fb.get_pixel(x, y);
            if (p.r > 0.01f || p.g > 0.01f || p.b > 0.01f || p.a > 0.01f) {
                lit_pixels++;
            }
        }
    }
    CHECK_EQ(lit_pixels, 0);
}

// ── Card3DRenderParams-based tests ──────────────────────────────────────────

TEST_CASE("Card3DMaterial: defaults are enabled") {
    Card3DMaterial mat;
    CHECK(mat.enabled);
    CHECK(mat.thickness_px == doctest::Approx(14.0f));
    CHECK(mat.edge_highlight_intensity == doctest::Approx(0.35f));
    CHECK(mat.rim_light_intensity == doctest::Approx(0.45f));
    CHECK(mat.rim_light_power == doctest::Approx(2.2f));
    CHECK(mat.corner_radius == doctest::Approx(28.0f));
    CHECK(mat.cast_shadow);
    CHECK(mat.receive_shadow);
    CHECK(mat.casts_shadows);
    CHECK(mat.accepts_shadows);
}

TEST_CASE("Card3DMaterial: render_card3d_material with glass preset") {
    const int W = 200, H = 200;
    Framebuffer fb(W, H);
    fb.clear(Color{0.05f, 0.05f, 0.10f, 1.0f});

    Card3DMaterial mat = Card3DMaterial::glass();
    Card3DRenderParams params;
    params.position = {40, 60};
    params.size = {120, 80};

    render_card3d_material(fb, mat, params, std::nullopt);

    // Center of the card should have visible front face
    Color center = fb.get_pixel(100, 100);
    CHECK(center.a > 0.1f);
    CHECK(center.r > 0.3f);
}

TEST_CASE("Card3DMaterial: flat preset has no thickness") {
    auto mat = Card3DMaterial::flat();
    CHECK(mat.thickness_px == doctest::Approx(0.0f));
    CHECK(mat.edge_highlight_intensity == doctest::Approx(0.0f));
    CHECK(mat.rim_light_intensity == doctest::Approx(0.0f));
    CHECK_FALSE(mat.cast_shadow);
}
