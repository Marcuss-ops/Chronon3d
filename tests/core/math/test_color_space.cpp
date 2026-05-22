#include <doctest/doctest.h>
#include <chronon3d/math/color_space.hpp>
#include <chronon3d/math/color.hpp>
#include <optional>
#include <algorithm>

using namespace chronon3d;

TEST_CASE("Color: safe hex parsing") {
    SUBCASE("try_from_hex") {
        auto c1 = Color::try_from_hex("#ff0000");
        REQUIRE(c1.has_value());
        CHECK(c1->r == 1.0f);
        CHECK(c1->g == 0.0f);
        CHECK(c1->b == 0.0f);
        CHECK(c1->a == 1.0f);

        auto c2 = Color::try_from_hex("#00ff007f"); // semi-transparent green
        REQUIRE(c2.has_value());
        CHECK(c2->r == 0.0f);
        CHECK(c2->g == 1.0f);
        CHECK(c2->b == 0.0f);
        CHECK(c2->a == doctest::Approx(0.5f).epsilon(0.01));

        CHECK_FALSE(Color::try_from_hex("red").has_value());
        CHECK_FALSE(Color::try_from_hex("#ff").has_value());
        CHECK_FALSE(Color::try_from_hex("#GGGGGG").has_value());
    }
}

// ---------------------------------------------------------------------------
// sRGB <-> linear round-trip
// ---------------------------------------------------------------------------
TEST_CASE("color_space: sRGB 0.0 and 1.0 are fixed points") {
    CHECK(srgb_to_linear(0.0f) == doctest::Approx(0.0f));
    CHECK(srgb_to_linear(1.0f) == doctest::Approx(1.0f));
    CHECK(linear_to_srgb(0.0f) == doctest::Approx(0.0f));
    CHECK(linear_to_srgb(1.0f) == doctest::Approx(1.0f));
}

TEST_CASE("color_space: round-trip sRGB -> linear -> sRGB") {
    for (float v : {0.0f, 0.1f, 0.5f, 0.9f, 1.0f}) {
        CHECK(linear_to_srgb(srgb_to_linear(v)) == doctest::Approx(v).epsilon(0.001f));
    }
}

TEST_CASE("color_space: mid-grey 0.5 sRGB is darker than 0.5 linear") {
    // sRGB 0.5 ≈ linear 0.214
    CHECK(srgb_to_linear(0.5f) == doctest::Approx(0.214f).epsilon(0.005f));
}

// ---------------------------------------------------------------------------
// premultiply / unpremultiply
// ---------------------------------------------------------------------------
TEST_CASE("color_space: premultiply scales RGB by alpha") {
    Color c{0.8f, 0.4f, 0.2f, 0.5f};
    Color p = premultiply(c);
    CHECK(p.r == doctest::Approx(0.4f));
    CHECK(p.g == doctest::Approx(0.2f));
    CHECK(p.b == doctest::Approx(0.1f));
    CHECK(p.a == doctest::Approx(0.5f));
}

TEST_CASE("color_space: unpremultiply inverts premultiply") {
    Color c{0.8f, 0.4f, 0.2f, 0.5f};
    Color round = unpremultiply(premultiply(c));
    CHECK(round.r == doctest::Approx(c.r).epsilon(0.001f));
    CHECK(round.g == doctest::Approx(c.g).epsilon(0.001f));
    CHECK(round.b == doctest::Approx(c.b).epsilon(0.001f));
}

TEST_CASE("color_space: premultiply with alpha=0 gives black transparent") {
    Color c = premultiply(Color{1,1,1,0});
    CHECK(c.r == doctest::Approx(0.0f));
    CHECK(c.a == doctest::Approx(0.0f));
}

TEST_CASE("color_space: unpremultiply with alpha=0 gives transparent") {
    Color c = unpremultiply(Color{0.5f, 0.5f, 0.5f, 0.0f});
    CHECK(c.a == doctest::Approx(0.0f));
}

TEST_CASE("color_space: premultiply alpha=1 is identity on RGB") {
    Color c{0.6f, 0.3f, 0.9f, 1.0f};
    Color p = premultiply(c);
    CHECK(p.r == doctest::Approx(c.r));
    CHECK(p.g == doctest::Approx(c.g));
    CHECK(p.b == doctest::Approx(c.b));
}

// ---------------------------------------------------------------------------
// Halo test: blur-like scenario should not create dark fringe
// (linear premultiplied avoids the classic alpha bleed artefact)
// ---------------------------------------------------------------------------
TEST_CASE("color_space: no dark halo -- premultiplied white + transparent average") {
    // A white pixel next to a transparent pixel: averaging in premultiplied space
    // gives a semi-transparent white, NOT a dark grey.
    Color white{1,1,1,1};
    Color trans{0,0,0,0};

    Color wp = premultiply(white);  // {1,1,1,1}
    Color tp = premultiply(trans);  // {0,0,0,0}

    // Average in premultiplied space
    Color avg_p = (wp + tp) * 0.5f;
    Color avg = unpremultiply(avg_p);

    // Should be white at 50% opacity, NOT grey at 50% opacity
    CHECK(avg.a == doctest::Approx(0.5f));
    CHECK(avg.r == doctest::Approx(1.0f));
    CHECK(avg.g == doctest::Approx(1.0f));
    CHECK(avg.b == doctest::Approx(1.0f));
}

// ---------------------------------------------------------------------------
// Pipeline helpers
// ---------------------------------------------------------------------------
TEST_CASE("color_space: source_to_linear converts sRGB") {
    Color srgb{0.5f, 0.5f, 0.5f, 1.0f};
    Color lin = source_to_linear(srgb);
    CHECK(lin.r == doctest::Approx(srgb_to_linear(0.5f)));
}

TEST_CASE("color_space: linear_to_output converts back and clamps") {
    Color lin{1.2f, 0.5f, -0.1f, 1.0f};
    Color out = linear_to_output(lin);
    CHECK(out.r == doctest::Approx(1.0f));
    CHECK(out.g == doctest::Approx(linear_to_srgb(0.5f)).epsilon(0.001f));
    CHECK(out.b == doctest::Approx(0.0f));
}
