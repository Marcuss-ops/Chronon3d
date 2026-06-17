#include <doctest/doctest.h>
#include <chronon3d/color/output_transform.hpp>
using namespace chronon3d;

using namespace chronon3d::color;
using chronon3d::Color;
namespace color = chronon3d::color;

// ---------------------------------------------------------------------------
// Float-level helpers
// ---------------------------------------------------------------------------

TEST_CASE("color::linear_srgb_float: 0.0 and 1.0") {
    CHECK(linear_srgb_float(0.0f) == doctest::Approx(0.0f));
    CHECK(linear_srgb_float(1.0f) == doctest::Approx(1.0f));
}

TEST_CASE("color::linear_srgb_float: mid-grey matches float formula") {
    // Linear 0.5 → sRGB = 1.055 * 0.5^(1/2.4) - 0.055 ≈ 0.735
    const float result = linear_srgb_float(0.5f);
    CHECK(result == doctest::Approx(0.735f).epsilon(0.001f));
}

TEST_CASE("color::linear_srgb_float: clamps out-of-range") {
    CHECK(linear_srgb_float(-0.5f) == doctest::Approx(0.0f));
    CHECK(linear_srgb_float(2.0f) == doctest::Approx(1.0f));
}

TEST_CASE("color::linear_to_output_float: sRGB matches linear_srgb_float") {
    for (float v : {0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        CHECK(linear_to_output_float(v, color::ColorSpace::SRGB) ==
              doctest::Approx(linear_srgb_float(v)));
        CHECK(linear_to_output_float(v, color::ColorSpace::Rec709) ==
              doctest::Approx(linear_srgb_float(v)));
    }
}

TEST_CASE("color::linear_to_output_float: LinearSRGB bypasses gamma") {
    CHECK(linear_to_output_float(0.5f, color::ColorSpace::LinearSRGB) == doctest::Approx(0.5f));
    CHECK(linear_to_output_float(-0.5f, color::ColorSpace::LinearSRGB) == doctest::Approx(0.0f));
    CHECK(linear_to_output_float(1.5f, color::ColorSpace::LinearSRGB) == doctest::Approx(1.0f));
}

TEST_CASE("color::linear_to_output_float: respects clamp=false") {
    CHECK(linear_to_output_float(-0.5f, color::ColorSpace::LinearSRGB, false) == doctest::Approx(-0.5f));
    CHECK(linear_to_output_float(1.5f, color::ColorSpace::LinearSRGB, false) == doctest::Approx(1.5f));
}

TEST_CASE("color::linear_to_output_rgb_float: returns expected values") {
    const Color linear{0.5f, 0.3f, 0.1f, 1.0f};
    const auto rgb = linear_to_output_rgb_float(linear, OutputTransformOptions{});
    CHECK(rgb[0] == doctest::Approx(linear_srgb_float(0.5f)));
    CHECK(rgb[1] == doctest::Approx(linear_srgb_float(0.3f)));
    CHECK(rgb[2] == doctest::Approx(linear_srgb_float(0.1f)));
}

TEST_CASE("color::linear_to_output_rgb_float: LinearSRGB bypasses gamma") {
    const Color linear{0.5f, 0.3f, 0.1f, 1.0f};
    OutputTransformOptions opts;
    opts.output = color::ColorSpace::LinearSRGB;
    const auto rgb = linear_to_output_rgb_float(linear, opts);
    CHECK(rgb[0] == doctest::Approx(0.5f));
    CHECK(rgb[1] == doctest::Approx(0.3f));
    CHECK(rgb[2] == doctest::Approx(0.1f));
}

// Float path should be strictly more precise than uint8-round-trip path.
// The float-level values are NOT quantized, so they should differ from
// u8→float→u8 paths for values that land between uint8 quantization bins.
TEST_CASE("color::linear_to_output_rgb_float: no uint8 round-trip") {
    const Color linear{0.5f, 0.3f, 0.1f, 1.0f};
    const auto float_rgb = linear_to_output_rgb_float(linear, OutputTransformOptions{});
    const auto u8_rgb = linear_to_output_rgb8(linear, OutputTransformOptions{});
    // The float value should be closer to the true sRGB OETF than the
    // quantized uint8 round-trip is.
    const float true_r = linear_srgb_float(0.5f);
    const float quantized_r = u8_rgb.r / 255.0f;
    // Float path is bit-exact, uint8 path diverges by ~0.002 at mid-grey
    CHECK(std::abs(true_r - float_rgb[0]) < 1e-6f);
    CHECK(std::abs(true_r - quantized_r) > 0.001f);
}

// ---------------------------------------------------------------------------
// Single-channel uint8 helpers
// ---------------------------------------------------------------------------
TEST_CASE("color::linear_to_srgb8: 0.0 and 1.0") {
    CHECK(linear_to_srgb8(0.0f) == 0);
    CHECK(linear_to_srgb8(1.0f) == 255);
}

TEST_CASE("color::linear_to_srgb8: mid-grey") {
    // Linear 0.5 → sRGB ~0.735 → 188 with round-to-nearest LUT sampling
    const uint8_t result = linear_to_srgb8(0.5f);
    CHECK(result == 188);
}

TEST_CASE("color::linear_to_srgb8: clamps out-of-range") {
    CHECK(linear_to_srgb8(-0.5f) == 0);
    CHECK(linear_to_srgb8(2.0f) == 255);
}

// ---------------------------------------------------------------------------
// Rgb8 output
// ---------------------------------------------------------------------------
TEST_CASE("color::linear_to_output_rgb8: default transform is sRGB") {
    const Color linear{0.5f, 0.3f, 0.1f, 1.0f};
    const auto rgb = linear_to_output_rgb8(linear, OutputTransformOptions{});
    CHECK(rgb.r == linear_to_srgb8(0.5f));
    CHECK(rgb.g == linear_to_srgb8(0.3f));
    CHECK(rgb.b == linear_to_srgb8(0.1f));
}

TEST_CASE("color::linear_to_output_rgb8: Rec709 matches sRGB for V1") {
    const Color linear{0.5f, 0.3f, 0.1f, 1.0f};
    OutputTransformOptions opts;
    opts.output = color::ColorSpace::Rec709;
    const auto rgb = linear_to_output_rgb8(linear, opts);
    CHECK(rgb.r == linear_to_srgb8(0.5f));
    CHECK(rgb.g == linear_to_srgb8(0.3f));
    CHECK(rgb.b == linear_to_srgb8(0.1f));
}

TEST_CASE("color::linear_to_output_rgb8: LinearsRGB bypasses gamma") {
    const Color linear{0.5f, 0.3f, 0.1f, 1.0f};
    OutputTransformOptions opts;
    opts.output = color::ColorSpace::LinearSRGB;
    const auto rgb = linear_to_output_rgb8(linear, opts);
    // 0.5 * 255 = 127.5 → round = 128
    CHECK(rgb.r == 128);
    // 0.3 * 255 = 76.5 → round = 77
    CHECK(rgb.g == 77);
    // 0.1 * 255 = 25.5 → round = 26
    CHECK(rgb.b == 26);
}

TEST_CASE("color::linear_to_output_rgb8: clamps negative values") {
    const Color linear{-0.5f, 2.0f, 0.5f, 1.0f};
    const auto rgb = linear_to_output_rgb8(linear, OutputTransformOptions{});
    CHECK(rgb.r == 0);   // clamped to 0
    CHECK(rgb.g == 255); // clamped to 255
}

TEST_CASE("color::linear_to_output_rgb8: without gamma") {
    const Color linear{0.5f, 0.3f, 0.1f, 1.0f};
    OutputTransformOptions opts;
    opts.apply_gamma = false;
    const auto rgb = linear_to_output_rgb8(linear, opts);
    // Same as LinearSRGB - round to nearest
    CHECK(rgb.r == 128); // 0.5 * 255 = 127.5 → round = 128
    CHECK(rgb.g == 77);  // 0.3 * 255 = 76.5 → round = 77
    CHECK(rgb.b == 26);  // 0.1 * 255 = 25.5 → round = 26
}

// ---------------------------------------------------------------------------
// Convenience wrappers
// ---------------------------------------------------------------------------
TEST_CASE("color::linear_to_srgb_rgb8 convenience") {
    const Color linear{0.5f, 0.3f, 0.1f, 1.0f};
    const auto rgb = linear_to_srgb_rgb8(linear);
    CHECK(rgb.r == linear_to_srgb8(0.5f));
    CHECK(rgb.g == linear_to_srgb8(0.3f));
    CHECK(rgb.b == linear_to_srgb8(0.1f));
}

TEST_CASE("color::linear_to_rec709_rgb8 convenience") {
    const Color linear{0.5f, 0.3f, 0.1f, 1.0f};
    const auto rgb = linear_to_rec709_rgb8(linear);
    CHECK(rgb.r == linear_to_srgb8(0.5f));
    CHECK(rgb.g == linear_to_srgb8(0.3f));
    CHECK(rgb.b == linear_to_srgb8(0.1f));
}

// ---------------------------------------------------------------------------
// Encoder integration: the encoder should produce identical results
// when using the old Color::linear_to_srgb8() and the new color pipeline
// with output=SRGB
// ---------------------------------------------------------------------------
TEST_CASE("output_transform: matches old Color::linear_to_srgb8 for sRGB") {
    const std::array<float, 8> test_values = {0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 1.0f, 1.5f};
    for (float v : test_values) {
        const uint8_t old_way = Color::linear_to_srgb8(v);
        const uint8_t new_way = linear_to_srgb8(v);
        CHECK(old_way == new_way);
    }
}
