#include <doctest/doctest.h>

#include "../../src/backends/software/processors/effects/distort/roughen_edges_kernel.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using chronon3d::effects::distort::RoughenEdgesKernelParams;
using chronon3d::effects::distort::roughen_edges_rgba;

// 32x32 buffer: an opaque 12x12 square at (10..21, 10..21) on an empty field.
std::vector<float> make_square(int width, int height) {
    std::vector<float> rgba(static_cast<std::size_t>(width) * height * 4, 0.0f);
    for (int y = 10; y <= 21; ++y) {
        for (int x = 10; x <= 21; ++x) {
            const auto i = (static_cast<std::size_t>(y) * width + x) * 4;
            rgba[i + 0] = 0.8f; rgba[i + 1] = 0.4f; rgba[i + 2] = 0.2f; rgba[i + 3] = 1.0f;
        }
    }
    return rgba;
}

bool is_boundary(const std::vector<float>& rgba, int width, int height, int x, int y) {
    const auto alpha = [&](int xx, int yy) {
        xx = xx < 0 ? 0 : (xx >= width ? width - 1 : xx);
        yy = yy < 0 ? 0 : (yy >= height ? height - 1 : yy);
        return rgba[(static_cast<std::size_t>(yy) * width + xx) * 4 + 3];
    };
    const float a = alpha(x, y);
    return std::abs(alpha(x - 1, y) - a) > 1.0e-4f || std::abs(alpha(x + 1, y) - a) > 1.0e-4f ||
           std::abs(alpha(x, y - 1) - a) > 1.0e-4f || std::abs(alpha(x, y + 1) - a) > 1.0e-4f;
}

RoughenEdgesKernelParams params(float amount, std::uint32_t seed = 7u) {
    RoughenEdgesKernelParams p;
    p.amount = amount;
    p.scale = 4.0f;
    p.octaves = 2u;
    p.seed = seed;
    return p;
}

} // namespace

TEST_SUITE("RoughenEdgesKernel") {

TEST_CASE("zero amount is the identity") {
    constexpr int w = 32, h = 32;
    auto rgba = make_square(w, h);
    const auto original = rgba;
    roughen_edges_rgba(rgba, w, h, params(0.0f));
    CHECK(rgba == original);
}

TEST_CASE("interior and empty pixels are untouched; only the boundary band changes") {
    constexpr int w = 32, h = 32;
    const auto original = make_square(w, h);
    auto rgba = original;
    roughen_edges_rgba(rgba, w, h, params(3.0f));

    std::size_t changed = 0;
    bool interior_clean = true;
    bool changed_only_boundary = true;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto i = (static_cast<std::size_t>(y) * w + x) * 4;
            const bool differs = rgba[i] != original[i] || rgba[i + 3] != original[i + 3];
            if (differs) {
                ++changed;
                if (!is_boundary(original, w, h, x, y)) changed_only_boundary = false;
            }
            // Pixels whose 4-neighbourhood is entirely opaque or entirely empty.
            if (!is_boundary(original, w, h, x, y) && differs) interior_clean = false;
        }
    }
    CHECK(changed > 0);
    CHECK(changed_only_boundary);
    CHECK(interior_clean);
}

TEST_CASE("the result is deterministic for identical parameters") {
    constexpr int w = 32, h = 32;
    auto a = make_square(w, h);
    auto b = make_square(w, h);
    roughen_edges_rgba(a, w, h, params(3.0f, 42u));
    roughen_edges_rgba(b, w, h, params(3.0f, 42u));
    CHECK(a == b);
}

TEST_CASE("a different seed produces a different boundary") {
    constexpr int w = 32, h = 32;
    auto a = make_square(w, h);
    auto b = make_square(w, h);
    roughen_edges_rgba(a, w, h, params(3.0f, 1u));
    roughen_edges_rgba(b, w, h, params(3.0f, 2u));
    CHECK(a != b);
}

TEST_CASE("undersized buffers are rejected without writing") {
    std::vector<float> small(4, 0.5f);
    const auto before = small;
    roughen_edges_rgba(small, 32, 32, params(3.0f));
    CHECK(small == before);
}

} // TEST_SUITE
