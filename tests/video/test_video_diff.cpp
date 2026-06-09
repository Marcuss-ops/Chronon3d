#include <doctest/doctest.h>
#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <cmath>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::video;

static Framebuffer make_fb(int w, int h, Color c) {
    Framebuffer fb(w, h);
    fb.clear(c);
    return fb;
}

TEST_CASE("frame_converter: successive YUV420P conversions of identical input produce identical output") {
    constexpr int w = 32, h = 32;
    auto fb = make_fb(w, h, Color{0.3f, 0.6f, 0.9f, 1.0f});

    const size_t y_sz  = w * h;
    const size_t uv_sz = (w / 2) * (h / 2);

    std::vector<uint8_t> y1(y_sz), u1(uv_sz), v1(uv_sz);
    std::vector<uint8_t> y2(y_sz), u2(uv_sz), v2(uv_sz);

    auto r1 = convert_frame_tight(fb, y1.data(), u1.data(), v1.data(), nullptr,
                                  w, h, EncoderPixelFormat::YUV420P, true);
    auto r2 = convert_frame_tight(fb, y2.data(), u2.data(), v2.data(), nullptr,
                                  w, h, EncoderPixelFormat::YUV420P, true);

    REQUIRE(r1.success);
    REQUIRE(r2.success);
    REQUIRE(r1.conversion_ns > 0);
    REQUIRE(r2.conversion_ns > 0);

    for (size_t i = 0; i < y_sz; ++i)
        CHECK(y1[i] == y2[i]);
    for (size_t i = 0; i < uv_sz; ++i) {
        CHECK(u1[i] == u2[i]);
        CHECK(v1[i] == v2[i]);
    }
}

TEST_CASE("frame_converter: YUV420P Y plane matches NV12 Y plane for various colors") {
    constexpr int w = 8, h = 8;
    const Color colors[] = {
        Color{1.0f, 0.0f, 0.0f, 1.0f},
        Color{0.0f, 1.0f, 0.0f, 1.0f},
        Color{0.0f, 0.0f, 1.0f, 1.0f},
        Color{1.0f, 1.0f, 1.0f, 1.0f},
        Color{0.0f, 0.0f, 0.0f, 1.0f},
        Color{0.5f, 0.3f, 0.7f, 1.0f},
    };

    const size_t y_sz  = w * h;
    const size_t uv_sz = y_sz / 2;

    for (const auto& c : colors) {
        auto fb = make_fb(w, h, c);

        std::vector<uint8_t> y_yuv(y_sz), u(uv_sz), v(uv_sz);
        std::vector<uint8_t> y_nv(y_sz), uv(uv_sz);

        auto r_yuv = convert_frame_tight(fb, y_yuv.data(), u.data(), v.data(), nullptr,
                                         w, h, EncoderPixelFormat::YUV420P, true);
        auto r_nv  = convert_frame_tight(fb, y_nv.data(), nullptr, nullptr, uv.data(),
                                         w, h, EncoderPixelFormat::NV12, true);

        REQUIRE(r_yuv.success);
        REQUIRE(r_nv.success);

        for (size_t i = 0; i < y_sz; ++i)
            CHECK(std::abs(static_cast<int>(y_yuv[i]) - static_cast<int>(y_nv[i])) <= 1);
    }
}

TEST_CASE("frame_converter: YUV420P with gamma=on vs gamma=off differ") {
    constexpr int w = 8, h = 8;
    auto fb = make_fb(w, h, Color{0.2f, 0.5f, 0.8f, 1.0f});

    const size_t y_sz  = w * h;
    const size_t uv_sz = (w / 2) * (h / 2);

    std::vector<uint8_t> yg(y_sz), ug(uv_sz), vg(uv_sz);
    std::vector<uint8_t> yl(y_sz), ul(uv_sz), vl(uv_sz);

    auto r_gamma = convert_frame_tight(fb, yg.data(), ug.data(), vg.data(), nullptr,
                                       w, h, EncoderPixelFormat::YUV420P, true);
    auto r_linear = convert_frame_tight(fb, yl.data(), ul.data(), vl.data(), nullptr,
                                        w, h, EncoderPixelFormat::YUV420P, false);

    REQUIRE(r_gamma.success);
    REQUIRE(r_linear.success);

    bool any_diff = false;
    for (size_t i = 0; i < y_sz; ++i) {
        if (yg[i] != yl[i]) { any_diff = true; break; }
    }
    CHECK(any_diff);
}
