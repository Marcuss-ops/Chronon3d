// bl2d_sampling.cpp - Bilinear sampling operations for blend2d_bridge

#include "bl2d_sampling.hpp"
#include <cmath>
#include <algorithm>

namespace chronon3d::blend2d_bridge {

void sample_bilinear_prgb32(const uint32_t* base, int stride, int sw, int sh, float lx, float ly, float& r, float& g, float& b, float& a) {
    const float u = lx - 0.5f;
    const float v = ly - 0.5f;
    int x0 = static_cast<int>(std::floor(u));
    int y0 = static_cast<int>(std::floor(v));
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    const float tx = u - static_cast<float>(x0);
    const float ty = v - static_cast<float>(y0);

    x0 = std::clamp(x0, 0, sw - 1);
    y0 = std::clamp(y0, 0, sh - 1);
    x1 = std::clamp(x1, 0, sw - 1);
    y1 = std::clamp(y1, 0, sh - 1);

    constexpr float inv255 = 1.0f / 255.0f;

    auto unpack = [&](uint32_t p, float& cr, float& cg, float& cb, float& ca) {
        ca = ((p >> 24) & 0xFF) * inv255;
        cr = ((p >> 16) & 0xFF) * inv255;
        cg = ((p >> 8) & 0xFF) * inv255;
        cb = (p & 0xFF) * inv255;
    };

    float r00, g00, b00, a00; unpack(base[y0 * stride + x0], r00, g00, b00, a00);
    float r10, g10, b10, a10; unpack(base[y0 * stride + x1], r10, g10, b10, a10);
    float r01, g01, b01, a01; unpack(base[y1 * stride + x0], r01, g01, b01, a01);
    float r11, g11, b11, a11; unpack(base[y1 * stride + x1], r11, g11, b11, a11);

    const float w0 = (1.0f - tx) * (1.0f - ty);
    const float w1 = tx * (1.0f - ty);
    const float w2 = (1.0f - tx) * ty;
    const float w3 = tx * ty;

    r = r00 * w0 + r10 * w1 + r01 * w2 + r11 * w3;
    g = g00 * w0 + g10 * w1 + g01 * w2 + g11 * w3;
    b = b00 * w0 + b10 * w1 + b01 * w2 + b11 * w3;
    a = a00 * w0 + a10 * w1 + a01 * w2 + a11 * w3;
}

void sample_bilinear_float(const Color* base, int stride, int sw, int sh, float lx, float ly, Color& result) {
    const float u = lx - 0.5f;
    const float v = ly - 0.5f;
    int x0 = static_cast<int>(std::floor(u));
    int y0 = static_cast<int>(std::floor(v));
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    const float tx = u - static_cast<float>(x0);
    const float ty = v - static_cast<float>(y0);

    x0 = std::clamp(x0, 0, sw - 1);
    y0 = std::clamp(y0, 0, sh - 1);
    x1 = std::clamp(x1, 0, sw - 1);
    y1 = std::clamp(y1, 0, sh - 1);

    const Color& c00 = base[y0 * stride + x0];
    const Color& c10 = base[y0 * stride + x1];
    const Color& c01 = base[y1 * stride + x0];
    const Color& c11 = base[y1 * stride + x1];

    const float w0 = (1.0f - tx) * (1.0f - ty);
    const float w1 = tx * (1.0f - ty);
    const float w2 = (1.0f - tx) * ty;
    const float w3 = tx * ty;

    result.r = c00.r * w0 + c10.r * w1 + c01.r * w2 + c11.r * w3;
    result.g = c00.g * w0 + c10.g * w1 + c01.g * w2 + c11.g * w3;
    result.b = c00.b * w0 + c10.b * w1 + c01.b * w2 + c11.b * w3;
    result.a = c00.a * w0 + c10.a * w1 + c01.a * w2 + c11.a * w3;
}

} // namespace chronon3d::blend2d_bridge
