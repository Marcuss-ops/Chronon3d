#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/backends/vulkan/cuda_yuv_conversion.hpp>

using chronon3d::backends::vulkan::make_yuv_to_rgb_params;
using chronon3d::runtime::ColorMatrix;
using chronon3d::runtime::ColorMetadata;
using chronon3d::runtime::ColorRange;
using chronon3d::runtime::PixelFormat;

TEST_CASE("YUV conversion parameters cover all supported matrices") {
    for (const auto matrix : {ColorMatrix::Bt601, ColorMatrix::Bt709,
                              ColorMatrix::Bt2020Ncl}) {
        const auto params = make_yuv_to_rgb_params(
            ColorMetadata{matrix, ColorRange::Limited}, PixelFormat::Nv12);
        CHECK(params.y_offset == doctest::Approx(16.0f));
        CHECK(params.uv_offset == doctest::Approx(128.0f));
        CHECK(params.y_scale == doctest::Approx(1.0f / 219.0f));
        CHECK(params.uv_scale == doctest::Approx(1.0f / 224.0f));
    }
}

TEST_CASE("YUV conversion parameters distinguish full and limited range") {
    const ColorMetadata limited{ColorMatrix::Bt709, ColorRange::Limited};
    const ColorMetadata full{ColorMatrix::Bt709, ColorRange::Full};
    const auto limited_params = make_yuv_to_rgb_params(limited, PixelFormat::Nv12);
    const auto full_params = make_yuv_to_rgb_params(full, PixelFormat::Nv12);

    CHECK(limited_params.y_offset == doctest::Approx(16.0f));
    CHECK(full_params.y_offset == doctest::Approx(0.0f));
    CHECK(limited_params.y_scale == doctest::Approx(1.0f / 219.0f));
    CHECK(full_params.y_scale == doctest::Approx(1.0f / 255.0f));
    CHECK(limited_params.uv_scale == doctest::Approx(1.0f / 224.0f));
    CHECK(full_params.uv_scale == doctest::Approx(1.0f / 224.0f));
}

TEST_CASE("P010 parameters use the ten-bit storage domain") {
    const auto params = make_yuv_to_rgb_params(
        ColorMetadata{ColorMatrix::Bt2020Ncl, ColorRange::Limited},
        PixelFormat::P010);

    CHECK(params.storage_shift == 6u);
    CHECK(params.y_offset == doctest::Approx(64.0f));
    CHECK(params.uv_offset == doctest::Approx(512.0f));
    CHECK(params.y_scale == doctest::Approx(1.0f / 876.0f));
    CHECK(params.uv_scale == doctest::Approx(1.0f / 896.0f));
}

TEST_CASE("P010 full range scales ten-bit code values") {
    const auto params = make_yuv_to_rgb_params(
        ColorMetadata{ColorMatrix::Bt709, ColorRange::Full}, PixelFormat::P010);

    CHECK(params.y_offset == doctest::Approx(0.0f));
    CHECK(params.y_scale == doctest::Approx(1.0f / 1020.0f));
    CHECK(params.uv_offset == doctest::Approx(512.0f));
    CHECK(params.uv_scale == doctest::Approx(1.0f / 896.0f));
}
