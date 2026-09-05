#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/backends/vulkan/cuda_yuv_conversion.hpp>

using chronon3d::backends::vulkan::make_yuv_to_rgb_params;
using chronon3d::runtime::ColorMatrix;
using chronon3d::runtime::ColorMetadata;
using chronon3d::runtime::ColorRange;
using chronon3d::runtime::ColorPrimaries;
using chronon3d::runtime::PixelFormat;
using chronon3d::runtime::TransferFunction;

// ColorMetadata is now an alias of runtime::FrameFormat: build values via
// make_frame_format instead of the retired aggregate brace-init.
const auto make_metadata = [](ColorMatrix matrix, ColorRange range,
                              ColorPrimaries primaries,
                              TransferFunction transfer) {
    return chronon3d::runtime::make_frame_format(
        PixelFormat::Nv12,
        ColorMetadata{matrix, range, transfer, primaries,
                      chronon3d::runtime::ChromaLocation::Left});
};
const auto limited_bt709 =
    make_metadata(ColorMatrix::Bt709, ColorRange::Limited,
                  ColorPrimaries::Bt709, TransferFunction::Bt1886);
const auto full_bt709 =
    make_metadata(ColorMatrix::Bt709, ColorRange::Full,
                  ColorPrimaries::Bt709, TransferFunction::Bt1886);
const auto limited_bt601 =
    make_metadata(ColorMatrix::Bt601, ColorRange::Limited,
                  ColorPrimaries::Bt709, TransferFunction::Bt1886);
const auto limited_bt2020 =
    make_metadata(ColorMatrix::Bt2020Ncl, ColorRange::Limited,
                  ColorPrimaries::Bt2020, TransferFunction::Bt1886);

TEST_CASE("YUV conversion parameters cover all supported matrices") {
    for (const auto metadata : {limited_bt601, limited_bt709, limited_bt2020}) {
        const auto params = make_yuv_to_rgb_params(
            metadata, PixelFormat::Nv12);
        CHECK(params.y_offset == doctest::Approx(16.0f));
        CHECK(params.uv_offset == doctest::Approx(128.0f));
        CHECK(params.y_scale == doctest::Approx(1.0f / 219.0f));
        CHECK(params.uv_scale == doctest::Approx(1.0f / 224.0f));
    }
}

TEST_CASE("YUV conversion parameters distinguish full and limited range") {
    const auto limited_params = make_yuv_to_rgb_params(limited_bt709, PixelFormat::Nv12);
    const auto full_params = make_yuv_to_rgb_params(full_bt709, PixelFormat::Nv12);

    CHECK(limited_params.y_offset == doctest::Approx(16.0f));
    CHECK(full_params.y_offset == doctest::Approx(0.0f));
    CHECK(limited_params.y_scale == doctest::Approx(1.0f / 219.0f));
    CHECK(full_params.y_scale == doctest::Approx(1.0f / 255.0f));
    CHECK(limited_params.uv_scale == doctest::Approx(1.0f / 224.0f));
    CHECK(full_params.uv_scale == doctest::Approx(1.0f / 224.0f));
}

TEST_CASE("P010 parameters use the ten-bit storage domain") {
    const auto params = make_yuv_to_rgb_params(limited_bt2020, PixelFormat::P010);

    CHECK(params.storage_shift == 6u);
    CHECK(params.y_offset == doctest::Approx(64.0f));
    CHECK(params.uv_offset == doctest::Approx(512.0f));
    CHECK(params.y_scale == doctest::Approx(1.0f / 876.0f));
    CHECK(params.uv_scale == doctest::Approx(1.0f / 896.0f));
}

TEST_CASE("P010 full range scales ten-bit code values") {
    const auto params = make_yuv_to_rgb_params(full_bt709, PixelFormat::P010);

    CHECK(params.y_offset == doctest::Approx(0.0f));
    CHECK(params.y_scale == doctest::Approx(1.0f / 1020.0f));
    CHECK(params.uv_offset == doctest::Approx(512.0f));
    CHECK(params.uv_scale == doctest::Approx(1.0f / 896.0f));
}
