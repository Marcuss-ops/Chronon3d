#include <chronon3d/simd/kernels.hpp>
#include <chronon3d/math/color.hpp>


#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace chronon3d;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

struct PixelDiff {
    float max_diff{0.0f};
    float avg_diff{0.0f};
    int   mismatches{0};
};

static PixelDiff compare_buffers(const Color* a, const Color* b, int count) {
    PixelDiff diff;
    double sum = 0.0;
    for (int i = 0; i < count; ++i) {
        const float dr = std::abs(a[i].r - b[i].r);
        const float dg = std::abs(a[i].g - b[i].g);
        const float db = std::abs(a[i].b - b[i].b);
        const float da = std::abs(a[i].a - b[i].a);
        const float d = std::max({dr, dg, db, da});
        diff.max_diff = std::max(diff.max_diff, d);
        sum += d;
        if (d > 1e-6f) ++diff.mismatches;
    }
    diff.avg_diff = static_cast<float>(sum / static_cast<double>(count));
    return diff;
}

// ---------------------------------------------------------------------------
// composite_normal_premul
// ---------------------------------------------------------------------------

TEST_CASE("simd::composite_normal_premul fully opaque src replaces dst") {
    constexpr int N = 64;
    Color dst[N], src[N], ref[N];

    for (int i = 0; i < N; ++i) {
        dst[i] = Color{0.5f, 0.3f, 0.1f, 1.0f};
        src[i] = Color{0.8f, 0.2f, 0.4f, 1.0f}; // fully opaque
        ref[i] = src[i];
    }

    simd::composite_normal_premul(dst, src, N);

    auto diff = compare_buffers(dst, ref, N);
    CHECK(diff.max_diff < 1e-5f);
    CHECK(diff.mismatches == 0);
}

TEST_CASE("simd::composite_normal_premul transparent src leaves dst unchanged") {
    constexpr int N = 64;
    Color dst[N], src[N], ref[N];

    for (int i = 0; i < N; ++i) {
        dst[i] = Color{0.5f, 0.3f, 0.1f, 1.0f};
        src[i] = Color{0.0f, 0.0f, 0.0f, 0.0f};
        ref[i] = dst[i];
    }

    simd::composite_normal_premul(dst, src, N);

    auto diff = compare_buffers(dst, ref, N);
    CHECK(diff.max_diff < 1e-5f);
    CHECK(diff.mismatches == 0);
}

TEST_CASE("simd::composite_normal_premul partial alpha blends correctly") {
    constexpr int N = 16;
    Color dst[N], src[N], ref[N];

    for (int i = 0; i < N; ++i) {
        // dst = solid red
        dst[i] = Color{1.0f, 0.0f, 0.0f, 1.0f};
        // src = blue at 50% opacity (premultiplied)
        src[i] = Color{0.0f, 0.0f, 0.5f, 0.5f};
        // Reference: over composite with premultiplied alpha
        const float inv_a = 1.0f - src[i].a;
        ref[i] = Color{
            src[i].r + dst[i].r * inv_a,   // 0 + 1 * 0.5 = 0.5
            src[i].g + dst[i].g * inv_a,   // 0 + 0 * 0.5 = 0
            src[i].b + dst[i].b * inv_a,   // 0.5 + 0 * 0.5 = 0.5
            src[i].a + dst[i].a * inv_a    // 0.5 + 1 * 0.5 = 1
        };
    }

    simd::composite_normal_premul(dst, src, N);

    auto diff = compare_buffers(dst, ref, N);
    CHECK(diff.max_diff < 1e-5f);
    CHECK(diff.mismatches == 0);
}

TEST_CASE("simd::composite_normal_premul large buffer is deterministic") {
    constexpr int N = 10000;
    std::vector<Color> dst(N);
    std::vector<Color> src(N);
    std::vector<Color> ref(N);

    for (int i = 0; i < N; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(N);
        dst[i] = Color{t, 1.0f - t, t * 0.5f, 0.0f};
        float a = 0.5f + t * 0.5f;
        src[i] = Color{t * 0.3f * a, t * 0.7f * a, (1.0f - t) * a, a};
        
        const float inv_a = 1.0f - a;
        ref[i] = Color{
            src[i].r + dst[i].r * inv_a,
            src[i].g + dst[i].g * inv_a,
            src[i].b + dst[i].b * inv_a,
            src[i].a + dst[i].a * inv_a
        };
    }

    // Run twice — should produce identical results
    std::vector<Color> copy = dst;
    simd::composite_normal_premul(dst.data(), src.data(), N);
    simd::composite_normal_premul(copy.data(), src.data(), N);

    auto diff = compare_buffers(dst.data(), copy.data(), N);
    CHECK(diff.max_diff < 1e-6f);
    CHECK(diff.mismatches == 0);

    // Also check correctness against reference
    auto diff2 = compare_buffers(dst.data(), ref.data(), N);
    CHECK(diff2.max_diff < 1e-5f);
}

TEST_CASE("simd::composite_normal_premul matches scalar reference") {
    constexpr int N = 256;
    std::vector<Color> dst(N);
    std::vector<Color> src(N);
    std::vector<Color> scalar_ref(N);

    for (int i = 0; i < N; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(N);
        dst[i] = Color{t, 1.0f - t * 0.5f, t * 0.3f, 0.8f};
        float a = t * 0.9f;
        src[i] = Color{t * 0.4f * a, t * 0.6f * a, (1.0f - t * 0.7f) * a, a};
    }

    // Scalar reference
    for (int i = 0; i < N; ++i) {
        const float alpha = src[i].a;
        const float inv_a = 1.0f - alpha;
        scalar_ref[i] = Color{
            src[i].r + dst[i].r * inv_a,
            src[i].g + dst[i].g * inv_a,
            src[i].b + dst[i].b * inv_a,
            src[i].a + dst[i].a * inv_a
        };
    }

    simd::composite_normal_premul(dst.data(), src.data(), N);

    auto diff = compare_buffers(dst.data(), scalar_ref.data(), N);
    CHECK_MESSAGE(diff.max_diff < 1e-5f,
        "SIMD composite differs from scalar. max_diff=" << diff.max_diff
        << " avg_diff=" << diff.avg_diff
        << " mismatches=" << diff.mismatches);
}

// ---------------------------------------------------------------------------
// clear_framebuffer
// ---------------------------------------------------------------------------

TEST_CASE("simd::clear_framebuffer fills with constant color") {
    constexpr int N = 128;
    std::vector<Color> buf(N, Color{1.0f, 1.0f, 1.0f, 1.0f});
    const Color clear_color{0.3f, 0.6f, 0.9f, 1.0f};

    simd::clear_framebuffer(buf.data(), N, clear_color);

    for (int i = 0; i < N; ++i) {
        CHECK_EQ(buf[i].r, doctest::Approx(0.3f));
        CHECK_EQ(buf[i].g, doctest::Approx(0.6f));
        CHECK_EQ(buf[i].b, doctest::Approx(0.9f));
        CHECK_EQ(buf[i].a, doctest::Approx(1.0f));
    }
}

TEST_CASE("simd::clear_framebuffer single pixel") {
    std::vector<Color> buf(1, Color{0.0f, 0.0f, 0.0f, 0.0f});
    const Color clear_color{0.5f, 0.5f, 0.5f, 0.5f};

    simd::clear_framebuffer(buf.data(), 1, clear_color);

    CHECK_EQ(buf[0].r, doctest::Approx(0.5f));
    CHECK_EQ(buf[0].g, doctest::Approx(0.5f));
    CHECK_EQ(buf[0].b, doctest::Approx(0.5f));
    CHECK_EQ(buf[0].a, doctest::Approx(0.5f));
}

TEST_CASE("simd::clear_framebuffer zero pixels is safe") {
    Color buf[1] = {Color{1.0f, 1.0f, 1.0f, 1.0f}};
    // Should not crash
    simd::clear_framebuffer(buf, 0, Color{0.0f, 0.0f, 0.0f, 1.0f});
    CHECK_EQ(buf[0].r, doctest::Approx(1.0f)); // unchanged
}

TEST_CASE("simd::convert_f32_rgba_to_yuv420p_simd_rows works correctly") {
    constexpr int w = 4;
    constexpr int h = 4;
    std::vector<Color> fb(w * h, Color{1.0f, 0.0f, 0.0f, 1.0f}); // solid red
    std::vector<uint8_t> y_plane(w * h, 0);
    std::vector<uint8_t> u_plane((w / 2) * (h / 2), 0);
    std::vector<uint8_t> v_plane((w / 2) * (h / 2), 0);

    simd::convert_f32_rgba_to_yuv420p_simd_rows(
        y_plane.data(), u_plane.data(), v_plane.data(),
        fb.data(), w, h, w, 0, h, true
    );

    for (int i = 0; i < w * h; ++i) {
        printf("Y[%d] = %d\n", i, y_plane[i]);
    }
    for (int i = 0; i < (w / 2) * (h / 2); ++i) {
        printf("U[%d] = %d, V[%d] = %d\n", i, u_plane[i], v_plane[i]);
    }
    
    // Assert Y, U, V are non-zero to verify they are written
    CHECK(y_plane[0] > 0);
    CHECK(u_plane[0] > 0);
    CHECK(v_plane[0] > 0);
}
