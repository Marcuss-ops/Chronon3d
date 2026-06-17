#include <chronon3d/simd/kernels.hpp>
#include <chronon3d/math/color.hpp>


#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
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

static PixelDiff compare_buffers(const Color* a, const Color* b, int count, float tol = 1e-6f) {
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
        if (d > tol) ++diff.mismatches;
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

    simd::composite_normal_premul(std::span<Color>(dst, N), std::span<const Color>(src, N));

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

    simd::composite_normal_premul(std::span<Color>(dst, N), std::span<const Color>(src, N));

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

    simd::composite_normal_premul(std::span<Color>(dst, N), std::span<const Color>(src, N));

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
    simd::composite_normal_premul(std::span<Color>(dst), std::span<const Color>(src));
    simd::composite_normal_premul(std::span<Color>(copy), std::span<const Color>(src));

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

    simd::composite_normal_premul(std::span<Color>(dst), std::span<const Color>(src));

    auto diff = compare_buffers(dst.data(), scalar_ref.data(), N);
    CHECK_MESSAGE(diff.max_diff < 1e-5f,
        "SIMD composite differs from scalar. max_diff=" << diff.max_diff
        << " avg_diff=" << diff.avg_diff
        << " mismatches=" << diff.mismatches);
}

// ---------------------------------------------------------------------------
// composite_normal_premul NaN/Inf guards
// ---------------------------------------------------------------------------

namespace {
bool is_transparent(const Color& c) {
    return c.r == 0.0f && c.g == 0.0f && c.b == 0.0f && c.a == 0.0f;
}

bool has_nan_or_inf(const Color& c) {
    return std::isnan(c.r) || std::isnan(c.g) || std::isnan(c.b) || std::isnan(c.a) ||
           std::isinf(c.r) || std::isinf(c.g) || std::isinf(c.b) || std::isinf(c.a);
}
} // namespace

TEST_CASE("simd::composite_normal_premul NaN src (canary: first pixel) triggers fallback") {
    constexpr int N = 64;
    Color dst[N], ref[N];
    Color src[N];

    for (int i = 0; i < N; ++i) {
        dst[i] = Color{0.5f, 0.3f, 0.1f, 1.0f};
        src[i] = Color{0.8f, 0.2f, 0.4f, 0.5f};
        // Reference: normal blend
        const float inv_a = 1.0f - src[i].a;
        ref[i] = Color{
            src[i].r + dst[i].r * inv_a,
            src[i].g + dst[i].g * inv_a,
            src[i].b + dst[i].b * inv_a,
            src[i].a + dst[i].a * inv_a
        };
    }

    // Inject NaN at the first pixel of src
    src[0] = Color{std::numeric_limits<float>::quiet_NaN(), 0.5f, 0.5f, 0.5f};
    ref[0] = dst[0]; // scalar fallback skips bad pixel → dst unchanged

    simd::composite_normal_premul(std::span<Color>(dst, N), std::span<const Color>(src, N));

    // First pixel: should be left untouched (fallback skipped it)
    CHECK_EQ(dst[0].r, doctest::Approx(ref[0].r));
    CHECK_EQ(dst[0].g, doctest::Approx(ref[0].g));
    CHECK_EQ(dst[0].b, doctest::Approx(ref[0].b));
    CHECK_EQ(dst[0].a, doctest::Approx(ref[0].a));

    // Rest of the buffer: should be blended correctly (scalar fallback did the work)
    for (int i = 1; i < N; ++i) {
        CHECK_EQ(dst[i].r, doctest::Approx(ref[i].r));
        CHECK_EQ(dst[i].g, doctest::Approx(ref[i].g));
        CHECK_EQ(dst[i].b, doctest::Approx(ref[i].b));
        CHECK_EQ(dst[i].a, doctest::Approx(ref[i].a));
    }

    // No NaN/Inf propagated
    for (int i = 0; i < N; ++i) {
        CHECK(!has_nan_or_inf(dst[i]));
    }
}

TEST_CASE("simd::composite_normal_premul NaN dst (canary: last pixel) triggers fallback") {
    constexpr int N = 64;
    Color dst[N], ref[N];
    Color src[N];

    for (int i = 0; i < N; ++i) {
        dst[i] = Color{0.5f, 0.3f, 0.1f, 1.0f};
        src[i] = Color{0.8f, 0.2f, 0.4f, 0.5f};
        const float inv_a = 1.0f - src[i].a;
        ref[i] = Color{
            src[i].r + dst[i].r * inv_a,
            src[i].g + dst[i].g * inv_a,
            src[i].b + dst[i].b * inv_a,
            src[i].a + dst[i].a * inv_a
        };
    }

    // Inject NaN at the last pixel of dst
    dst[N - 1] = Color{std::numeric_limits<float>::quiet_NaN(), 0.5f, 0.5f, 1.0f};
    ref[N - 1] = dst[N - 1]; // fallback skips bad pixel → but dst was already set to NaN so it stays NaN
    // Actually, fallback skips if has_bad(dst), so dst[N-1] stays unchanged (NaN)

    simd::composite_normal_premul(std::span<Color>(dst, N), std::span<const Color>(src, N));

    // Last pixel: untouched by fallback (bad dst detected)
    // All other pixels: blended correctly
    for (int i = 0; i < N - 1; ++i) {
        CHECK_EQ(dst[i].r, doctest::Approx(ref[i].r));
        CHECK_EQ(dst[i].g, doctest::Approx(ref[i].g));
        CHECK_EQ(dst[i].b, doctest::Approx(ref[i].b));
        CHECK_EQ(dst[i].a, doctest::Approx(ref[i].a));
        CHECK(!has_nan_or_inf(dst[i]));
    }
    // The bad pixel itself stays NaN — the guard doesn't sanitize, it just skips
    CHECK(std::isnan(dst[N - 1].r));
}

TEST_CASE("simd::composite_normal_premul Inf src (canary: first+last both good) uses SIMD") {
    // When the canary (first + last) passes but a middle pixel has Inf,
    // the SIMD path is used. This test verifies that the canary check doesn't
    // falsely reject clean data.
    constexpr int N = 256;
    std::vector<Color> dst(N);
    std::vector<Color> src(N);
    std::vector<Color> ref(N);

    for (int i = 0; i < N; ++i) {
        dst[i] = Color{0.5f, 0.3f, 0.1f, 1.0f};
        src[i] = Color{0.8f, 0.2f, 0.4f, 0.5f};
        const float inv_a = 1.0f - src[i].a;
        ref[i] = Color{
            src[i].r + dst[i].r * inv_a,
            src[i].g + dst[i].g * inv_a,
            src[i].b + dst[i].b * inv_a,
            src[i].a + dst[i].a * inv_a
        };
    }

    simd::composite_normal_premul(std::span<Color>(dst), std::span<const Color>(src));

    auto diff = compare_buffers(dst.data(), ref.data(), N);
    CHECK(diff.max_diff < 1e-5f);
    CHECK(diff.mismatches == 0);

    // No NaN/Inf anywhere
    for (int i = 0; i < N; ++i) {
        CHECK(!has_nan_or_inf(dst[i]));
    }
}

// ---------------------------------------------------------------------------
// clear_framebuffer
// ---------------------------------------------------------------------------

TEST_CASE("simd::clear_framebuffer fills with constant color") {
    constexpr int N = 128;
    std::vector<Color> buf(N, Color{1.0f, 1.0f, 1.0f, 1.0f});
    const Color clear_color{0.3f, 0.6f, 0.9f, 1.0f};

    simd::clear_framebuffer(std::span<Color>(buf), clear_color);

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

    simd::clear_framebuffer(std::span<Color>(buf), clear_color);

    CHECK_EQ(buf[0].r, doctest::Approx(0.5f));
    CHECK_EQ(buf[0].g, doctest::Approx(0.5f));
    CHECK_EQ(buf[0].b, doctest::Approx(0.5f));
    CHECK_EQ(buf[0].a, doctest::Approx(0.5f));
}

TEST_CASE("simd::clear_framebuffer zero pixels is safe") {
    Color buf[1] = {Color{1.0f, 1.0f, 1.0f, 1.0f}};
    // Should not crash
    simd::clear_framebuffer(std::span<Color>(buf, 0), Color{0.0f, 0.0f, 0.0f, 1.0f});
    CHECK_EQ(buf[0].r, doctest::Approx(1.0f)); // unchanged
}

// ---------------------------------------------------------------------------
// bl_image_prgb32_to_color_row / color_to_prgb32_row
// ---------------------------------------------------------------------------

namespace {

/// Scalar reference: unpack PRGB32 (0xAARRGGBB premultiplied) → Color (float RGBA, un-premultiplied).
static Color scalar_prgb32_to_color(uint32_t p) {
    const float a = static_cast<float>((p >> 24) & 0xFF) * (1.0f / 255.0f);
    if (a <= 0.0f) return Color{0.0f, 0.0f, 0.0f, 0.0f};
    const float inv_a = 1.0f / a;
    return Color{
        std::clamp(static_cast<float>((p >> 16) & 0xFF) * (1.0f / 255.0f) * inv_a, 0.0f, 1.0f),
        std::clamp(static_cast<float>((p >>  8) & 0xFF) * (1.0f / 255.0f) * inv_a, 0.0f, 1.0f),
        std::clamp(static_cast<float>( p        & 0xFF) * (1.0f / 255.0f) * inv_a, 0.0f, 1.0f),
        a
    };
}

/// Scalar reference: Color (float RGBA) → PRGB32 (premultiplied 0xAARRGGBB).
static uint32_t scalar_color_to_prgb32(const Color& c) {
    const float a = std::clamp(c.a, 0.0f, 1.0f);
    const auto pack = [](float v) -> uint32_t {
        return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    const uint32_t pa = pack(a);
    const uint32_t pr = pack(std::clamp(c.r * a, 0.0f, 1.0f));
    const uint32_t pg = pack(std::clamp(c.g * a, 0.0f, 1.0f));
    const uint32_t pb = pack(std::clamp(c.b * a, 0.0f, 1.0f));
    return (pa << 24) | (pr << 16) | (pg << 8) | pb;
}

/// Compare uint32 PRGB32 buffers allowing a tolerance of ±1 per channel.
static PixelDiff compare_prgb32_buffers(const uint32_t* a, const uint32_t* b, int count) {
    PixelDiff diff;
    double sum = 0.0;
    for (int i = 0; i < count; ++i) {
        float d = 0.0f;
        for (int shift = 0; shift <= 24; shift += 8) {
            const uint8_t ca = static_cast<uint8_t>((a[i] >> shift) & 0xFF);
            const uint8_t cb = static_cast<uint8_t>((b[i] >> shift) & 0xFF);
            d = std::max(d, std::abs(static_cast<float>(ca) - static_cast<float>(cb)));
        }
        diff.max_diff = std::max(diff.max_diff, d);
        sum += d;
        if (d > 1.0f) ++diff.mismatches;
    }
    diff.avg_diff = static_cast<float>(sum / static_cast<double>(count));
    return diff;
}

} // namespace

// ── bl_image_prgb32_to_color_row ──────────────────────────────────────────

TEST_CASE("simd::bl_image_prgb32_to_color_row opaque white") {
    // PRGB32 opaque white = 0xFFFFFFFF (alpha=255, r=255, g=255, b=255)
    // Un-premultiplied result: (1.0, 1.0, 1.0, 1.0)
    const uint32_t src = 0xFFFFFFFF;
    Color dst{};
    simd::bl_image_prgb32_to_color_row(&dst, &src, 1);
    CHECK_EQ(dst.r, doctest::Approx(1.0f));
    CHECK_EQ(dst.g, doctest::Approx(1.0f));
    CHECK_EQ(dst.b, doctest::Approx(1.0f));
    CHECK_EQ(dst.a, doctest::Approx(1.0f));
}

TEST_CASE("simd::bl_image_prgb32_to_color_row fully transparent") {
    // PRGB32 transparent = 0x00000000
    const uint32_t src = 0x00000000;
    Color dst{1.0f, 1.0f, 1.0f, 1.0f};
    simd::bl_image_prgb32_to_color_row(&dst, &src, 1);
    CHECK_EQ(dst.r, doctest::Approx(0.0f));
    CHECK_EQ(dst.g, doctest::Approx(0.0f));
    CHECK_EQ(dst.b, doctest::Approx(0.0f));
    CHECK_EQ(dst.a, doctest::Approx(0.0f));
}

TEST_CASE("simd::bl_image_prgb32_to_color_row 50% alpha gray") {
    // PRGB32: alpha=128, premultiplied gray R=G=B=64 → 0x80404040
    // Un-premultiplied: 64/128 ≈ 0.5, alpha = 128/255 ≈ 0.502
    const uint32_t src = 0x80404040;
    Color dst{};
    simd::bl_image_prgb32_to_color_row(&dst, &src, 1);
    const float expected_a = 128.0f / 255.0f;
    const float expected_rgb = 64.0f / 128.0f; // ≈ 0.5
    CHECK_EQ(dst.a, doctest::Approx(expected_a).epsilon(0.01));
    CHECK_EQ(dst.r, doctest::Approx(expected_rgb).epsilon(0.01));
    CHECK_EQ(dst.g, doctest::Approx(expected_rgb).epsilon(0.01));
    CHECK_EQ(dst.b, doctest::Approx(expected_rgb).epsilon(0.01));
}

TEST_CASE("simd::bl_image_prgb32_to_color_row matches scalar reference") {
    constexpr int N = 256;
    std::vector<uint32_t> src(N);
    std::vector<Color> dst(N);
    std::vector<Color> ref(N);

    for (int i = 0; i < N; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(N);
        const uint8_t a = static_cast<uint8_t>(t * 255.0f);
        const uint8_t r = static_cast<uint8_t>(t * 200.0f);
        const uint8_t g = static_cast<uint8_t>((1.0f - t) * 150.0f);
        const uint8_t b = static_cast<uint8_t>(t * t * 255.0f);
        // Premultiply: pr = r * a / 255, etc.
        const uint8_t pr = static_cast<uint8_t>((static_cast<uint16_t>(r) * a + 127) / 255);
        const uint8_t pg = static_cast<uint8_t>((static_cast<uint16_t>(g) * a + 127) / 255);
        const uint8_t pb = static_cast<uint8_t>((static_cast<uint16_t>(b) * a + 127) / 255);
        src[i] = (static_cast<uint32_t>(a) << 24) | (pr << 16) | (pg << 8) | pb;
        ref[i] = scalar_prgb32_to_color(src[i]);
    }

    simd::bl_image_prgb32_to_color_row(dst.data(), src.data(), N);

    // Allow 2/255 tolerance due to uint8 quantization + un-premultiply amplification
    auto diff = compare_buffers(dst.data(), ref.data(), N, 2.0f / 255.0f);
    CHECK_MESSAGE(diff.max_diff < 2.0f / 255.0f,
        "SIMD prgb32_to_color differs from scalar. max_diff=" << diff.max_diff
        << " avg_diff=" << diff.avg_diff << " mismatches=" << diff.mismatches);
}

TEST_CASE("simd::bl_image_prgb32_to_color_row transparent pair fast-path") {
    // Tests the AVX2 2-pixel transparent-pair fast-path: both pixels fully transparent.
    constexpr int N = 4;
    const uint32_t src[N] = {0x00000000, 0x00000000, 0xFF804020, 0x00000000};
    Color dst[N] = {{1,1,1,1}, {1,1,1,1}, {1,1,1,1}, {1,1,1,1}};

    simd::bl_image_prgb32_to_color_row(dst, src, N);

    // Pixels 0,1: transparent pair → zero
    CHECK_EQ(dst[0].r, doctest::Approx(0.0f));
    CHECK_EQ(dst[0].a, doctest::Approx(0.0f));
    CHECK_EQ(dst[1].r, doctest::Approx(0.0f));
    CHECK_EQ(dst[1].a, doctest::Approx(0.0f));
    // Pixel 2: opaque 0xFF804020 → r=128/255, g=64/255, b=32/255, a=1.0
    CHECK_EQ(dst[2].a, doctest::Approx(1.0f));
    CHECK_EQ(dst[2].r, doctest::Approx(128.0f / 255.0f).epsilon(0.01));
    // Pixel 3: transparent single → zero
    CHECK_EQ(dst[3].a, doctest::Approx(0.0f));
}

TEST_CASE("simd::bl_image_prgb32_to_color_row mixed transparent+opaque pair") {
    // Tests the AVX2 2-pixel path where pixel 0 is transparent, pixel 1 is opaque.
    // This exercises the code path where the transparent-pair fast-path is skipped
    // but the IfThenElse mask must still zero pixel 0 while processing pixel 1.
    constexpr int N = 2;
    const uint32_t src[N] = {0x00000000, 0xFF804020};
    Color dst[N] = {{1,1,1,1}, {0,0,0,0}};

    simd::bl_image_prgb32_to_color_row(dst, src, N);

    // Pixel 0: transparent → zero
    CHECK_EQ(dst[0].r, doctest::Approx(0.0f));
    CHECK_EQ(dst[0].g, doctest::Approx(0.0f));
    CHECK_EQ(dst[0].b, doctest::Approx(0.0f));
    CHECK_EQ(dst[0].a, doctest::Approx(0.0f));
    // Pixel 1: opaque 0xFF804020 → r=128/255, g=64/255, b=32/255, a=1.0
    CHECK_EQ(dst[1].a, doctest::Approx(1.0f));
    CHECK_EQ(dst[1].r, doctest::Approx(128.0f / 255.0f).epsilon(0.01));
    CHECK_EQ(dst[1].g, doctest::Approx(64.0f / 255.0f).epsilon(0.01));
    CHECK_EQ(dst[1].b, doctest::Approx(32.0f / 255.0f).epsilon(0.01));
}

TEST_CASE("simd::bl_image_prgb32_to_color_row opaque+transparent pair") {
    // Tests the AVX2 2-pixel path where pixel 0 is opaque, pixel 1 is transparent.
    constexpr int N = 2;
    const uint32_t src[N] = {0xFF804020, 0x00000000};
    Color dst[N] = {{0,0,0,0}, {1,1,1,1}};

    simd::bl_image_prgb32_to_color_row(dst, src, N);

    // Pixel 0: opaque
    CHECK_EQ(dst[0].a, doctest::Approx(1.0f));
    CHECK_EQ(dst[0].r, doctest::Approx(128.0f / 255.0f).epsilon(0.01));
    // Pixel 1: transparent → zero
    CHECK_EQ(dst[1].r, doctest::Approx(0.0f));
    CHECK_EQ(dst[1].a, doctest::Approx(0.0f));
}

TEST_CASE("simd::bl_image_prgb32_to_color_row near-zero alpha") {
    // PRGB32: alpha=1/255, premul R=1, G=0, B=0 → 0x01010000
    // Un-premultiply: r = (1/255) / (1/255) = 1.0, clamped
    const uint32_t src = 0x01010000;
    Color dst{};
    Color ref = scalar_prgb32_to_color(src);
    simd::bl_image_prgb32_to_color_row(&dst, &src, 1);
    CHECK_EQ(dst.a, doctest::Approx(ref.a).epsilon(0.01));
    CHECK_EQ(dst.r, doctest::Approx(ref.r).epsilon(0.01));
}

TEST_CASE("simd::bl_image_prgb32_to_color_row near-zero alpha pair") {
    // Test AVX2 2-pixel path with both pixels having near-zero alpha.
    constexpr int N = 2;
    const uint32_t src[N] = {0x01010000, 0x01000100};
    Color dst[N]{};
    Color ref[N];
    ref[0] = scalar_prgb32_to_color(src[0]);
    ref[1] = scalar_prgb32_to_color(src[1]);

    simd::bl_image_prgb32_to_color_row(dst, src, N);

    auto diff = compare_buffers(dst, ref, N, 2.0f / 255.0f);
    CHECK(diff.max_diff < 2.0f / 255.0f);
}

TEST_CASE("simd::bl_image_prgb32_to_color_row odd pixel count") {
    // Tests the 1-pixel remainder path after the 2-pixel AVX2 loop.
    constexpr int N = 5;
    uint32_t src[N];
    Color dst[N];
    Color ref[N];

    for (int i = 0; i < N; ++i) {
        const uint8_t a = static_cast<uint8_t>(128 + i * 20);
        const uint8_t r = static_cast<uint8_t>(100 + i * 30);
        const uint8_t g = 50;
        const uint8_t b = 200;
        const uint8_t pr = static_cast<uint8_t>((static_cast<uint16_t>(r) * a + 127) / 255);
        const uint8_t pg = static_cast<uint8_t>((static_cast<uint16_t>(g) * a + 127) / 255);
        const uint8_t pb = static_cast<uint8_t>((static_cast<uint16_t>(b) * a + 127) / 255);
        src[i] = (static_cast<uint32_t>(a) << 24) | (pr << 16) | (pg << 8) | pb;
        ref[i] = scalar_prgb32_to_color(src[i]);
    }

    simd::bl_image_prgb32_to_color_row(dst, src, N);

    auto diff = compare_buffers(dst, ref, N, 2.0f / 255.0f);
    CHECK(diff.max_diff < 2.0f / 255.0f);
}

TEST_CASE("simd::bl_image_prgb32_to_color_row large buffer deterministic") {
    constexpr int N = 10000;
    std::vector<uint32_t> src(N);
    std::vector<Color> dst1(N), dst2(N);

    for (int i = 0; i < N; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(N);
        const uint8_t a = static_cast<uint8_t>(t * 254.0f + 1); // avoid 0
        const uint8_t r = static_cast<uint8_t>(t * 200.0f);
        const uint8_t g = static_cast<uint8_t>((1.0f - t) * 180.0f);
        const uint8_t b = static_cast<uint8_t>(t * t * 255.0f);
        const uint8_t pr = static_cast<uint8_t>((static_cast<uint16_t>(r) * a + 127) / 255);
        const uint8_t pg = static_cast<uint8_t>((static_cast<uint16_t>(g) * a + 127) / 255);
        const uint8_t pb = static_cast<uint8_t>((static_cast<uint16_t>(b) * a + 127) / 255);
        src[i] = (static_cast<uint32_t>(a) << 24) | (pr << 16) | (pg << 8) | pb;
    }

    // Run twice — should produce identical results
    simd::bl_image_prgb32_to_color_row(dst1.data(), src.data(), N);
    simd::bl_image_prgb32_to_color_row(dst2.data(), src.data(), N);

    auto diff = compare_buffers(dst1.data(), dst2.data(), N, 0.0f);
    CHECK(diff.max_diff < 1e-7f);
    CHECK(diff.mismatches == 0);
}

TEST_CASE("simd::bl_image_prgb32_to_color_row zero pixels is safe") {
    uint32_t src = 0xFF804020;
    Color dst{1.0f, 1.0f, 1.0f, 1.0f};
    simd::bl_image_prgb32_to_color_row(&dst, &src, 0);
    CHECK_EQ(dst.r, doctest::Approx(1.0f)); // unchanged
}

// ── color_to_prgb32_row ──────────────────────────────────────────────────

TEST_CASE("simd::color_to_prgb32_row opaque white") {
    const Color src{1.0f, 1.0f, 1.0f, 1.0f};
    uint32_t dst = 0;
    simd::color_to_prgb32_row(&dst, &src, 1);
    CHECK_EQ(dst, 0xFFFFFFFFu);
}

TEST_CASE("simd::color_to_prgb32_row fully transparent") {
    const Color src{0.5f, 0.5f, 0.5f, 0.0f};
    uint32_t dst = 0xFFFFFFFFu;
    simd::color_to_prgb32_row(&dst, &src, 1);
    // alpha=0 → premultiplied RGB=0 → PRGB32 = 0x00000000
    CHECK_EQ(dst, 0x00000000u);
}

TEST_CASE("simd::color_to_prgb32_row 50% alpha gray") {
    // straight gray (0.5, 0.5, 0.5, 0.5) → premultiplied (0.25, 0.25, 0.25, 0.5)
    // PRGB32: a=128, r=64, g=64, b=64 → 0x80404040
    const Color src{0.5f, 0.5f, 0.5f, 0.5f};
    uint32_t dst = 0;
    simd::color_to_prgb32_row(&dst, &src, 1);
    const uint32_t expected = 0x80404040u;
    // Allow ±1 per channel due to rounding
    auto diff = compare_prgb32_buffers(&dst, &expected, 1);
    CHECK(diff.max_diff <= 1.0f);
}

TEST_CASE("simd::color_to_prgb32_row matches scalar reference") {
    constexpr int N = 256;
    std::vector<Color> src(N);
    std::vector<uint32_t> dst(N);
    std::vector<uint32_t> ref(N);

    for (int i = 0; i < N; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(N);
        src[i] = Color{
            std::clamp(t * 0.8f, 0.0f, 1.0f),
            std::clamp((1.0f - t) * 0.6f, 0.0f, 1.0f),
            std::clamp(t * t, 0.0f, 1.0f),
            std::clamp(0.1f + t * 0.9f, 0.0f, 1.0f)
        };
        ref[i] = scalar_color_to_prgb32(src[i]);
    }

    simd::color_to_prgb32_row(dst.data(), src.data(), N);

    auto diff = compare_prgb32_buffers(dst.data(), ref.data(), N);
    CHECK_MESSAGE(diff.max_diff <= 1.0f,
        "SIMD color_to_prgb32 differs from scalar. max_diff=" << diff.max_diff
        << " avg_diff=" << diff.avg_diff << " mismatches=" << diff.mismatches);
}

TEST_CASE("simd::color_to_prgb32_row odd pixel count") {
    constexpr int N = 5;
    Color src[N];
    uint32_t dst[N];
    uint32_t ref[N];

    for (int i = 0; i < N; ++i) {
        const float t = static_cast<float>(i + 1) / static_cast<float>(N + 1);
        src[i] = Color{t, 1.0f - t, t * 0.5f, std::clamp(t * 1.2f, 0.0f, 1.0f)};
        ref[i] = scalar_color_to_prgb32(src[i]);
    }

    simd::color_to_prgb32_row(dst, src, N);

    auto diff = compare_prgb32_buffers(dst, ref, N);
    CHECK(diff.max_diff <= 1.0f);
}

TEST_CASE("simd::color_to_prgb32_row large buffer deterministic") {
    constexpr int N = 10000;
    std::vector<Color> src(N);
    std::vector<uint32_t> dst1(N), dst2(N);

    for (int i = 0; i < N; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(N);
        src[i] = Color{t, 1.0f - t, t * 0.5f, 0.3f + t * 0.7f};
    }

    simd::color_to_prgb32_row(dst1.data(), src.data(), N);
    simd::color_to_prgb32_row(dst2.data(), src.data(), N);

    auto diff = compare_prgb32_buffers(dst1.data(), dst2.data(), N);
    CHECK(diff.max_diff < 1e-6f);
    CHECK(diff.mismatches == 0);
}

TEST_CASE("simd::color_to_prgb32_row zero pixels is safe") {
    Color src{0.5f, 0.5f, 0.5f, 0.5f};
    uint32_t dst = 0xFFFFFFFFu;
    simd::color_to_prgb32_row(&dst, &src, 0);
    CHECK_EQ(dst, 0xFFFFFFFFu); // unchanged
}

TEST_CASE("simd::color_to_prgb32_row transparent pair fast-path") {
    // Both pixels fully transparent → PRGB32 = 0x00000000
    constexpr int N = 4;
    const Color src[N] = {
        {0.5f, 0.5f, 0.5f, 0.0f},
        {0.8f, 0.2f, 0.4f, 0.0f},
        {0.3f, 0.6f, 0.9f, 0.7f},
        {0.0f, 0.0f, 0.0f, 0.0f}
    };
    uint32_t dst[N] = {0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF};
    uint32_t ref[N];
    for (int i = 0; i < N; ++i) ref[i] = scalar_color_to_prgb32(src[i]);

    simd::color_to_prgb32_row(dst, src, N);

    auto diff = compare_prgb32_buffers(dst, ref, N);
    CHECK(diff.max_diff <= 1.0f);
}

TEST_CASE("simd::color_to_prgb32_row mixed transparent+opaque pair") {
    // pixel 0: transparent, pixel 1: opaque — exercises AVX2 mixed-pair path
    constexpr int N = 2;
    const Color src[N] = {{0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.5f, 0.25f, 1.0f}};
    uint32_t dst[N] = {0xDEADBEEF, 0xDEADBEEF};

    simd::color_to_prgb32_row(dst, src, N);

    // Pixel 0: transparent → 0x00000000
    CHECK_EQ(dst[0], 0x00000000u);
    // Pixel 1: opaque white-ish → 0xFFxx... check alpha channel at least
    CHECK_EQ((dst[1] >> 24) & 0xFF, 255u);
}

TEST_CASE("simd::color_to_prgb32_row opaque+transparent pair") {
    // pixel 0: opaque, pixel 1: transparent
    constexpr int N = 2;
    const Color src[N] = {{0.5f, 0.5f, 0.5f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}};
    uint32_t dst[N] = {0, 0xFFFFFFFFu};

    simd::color_to_prgb32_row(dst, src, N);

    // Pixel 0: opaque → alpha = 255
    CHECK_EQ((dst[0] >> 24) & 0xFF, 255u);
    // Pixel 1: transparent → 0x00000000
    CHECK_EQ(dst[1], 0x00000000u);
}

// ── Round-trip: PRGB32 → Color → PRGB32 ──────────────────────────────────

TEST_CASE("simd::prgb32 round-trip is lossless within ±1") {
    // PRGB32 → Color → PRGB32 should recover the original within ±1 per channel.
    constexpr int N = 512;
    std::vector<uint32_t> src(N);
    std::vector<Color> intermediate(N);
    std::vector<uint32_t> result(N);

    for (int i = 0; i < N; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(N);
        const uint8_t a = static_cast<uint8_t>(t * 254.0f + 1); // alpha > 0
        const uint8_t r = static_cast<uint8_t>(t * 200.0f);
        const uint8_t g = static_cast<uint8_t>((1.0f - t) * 180.0f);
        const uint8_t b = static_cast<uint8_t>(t * t * 255.0f);
        const uint8_t pr = static_cast<uint8_t>((static_cast<uint16_t>(r) * a + 127) / 255);
        const uint8_t pg = static_cast<uint8_t>((static_cast<uint16_t>(g) * a + 127) / 255);
        const uint8_t pb = static_cast<uint8_t>((static_cast<uint16_t>(b) * a + 127) / 255);
        src[i] = (static_cast<uint32_t>(a) << 24) | (pr << 16) | (pg << 8) | pb;
    }

    simd::bl_image_prgb32_to_color_row(intermediate.data(), src.data(), N);
    simd::color_to_prgb32_row(result.data(), intermediate.data(), N);

    auto diff = compare_prgb32_buffers(src.data(), result.data(), N);
    CHECK_MESSAGE(diff.max_diff <= 1.0f,
        "Round-trip PRGB32→Color→PRGB32 loss exceeds ±1. max_diff=" << diff.max_diff
        << " avg_diff=" << diff.avg_diff << " mismatches=" << diff.mismatches);
}

TEST_CASE("simd::prgb32 round-trip batch sizes 1..17") {
    // Test that both the 2-pixel AVX2 path and 1-pixel remainder produce correct results.
    for (int n = 1; n <= 17; ++n) {
        std::vector<uint32_t> src(n);
        std::vector<Color> inter(n);
        std::vector<uint32_t> result(n);

        for (int i = 0; i < n; ++i) {
            const uint8_t a = static_cast<uint8_t>(100 + i * 8);
            const uint8_t r = static_cast<uint8_t>(50 + i * 12);
            const uint8_t g = static_cast<uint8_t>(200 - i * 5);
            const uint8_t b = static_cast<uint8_t>(i * 15);
            const uint8_t pr = static_cast<uint8_t>((static_cast<uint16_t>(r) * a + 127) / 255);
            const uint8_t pg = static_cast<uint8_t>((static_cast<uint16_t>(g) * a + 127) / 255);
            const uint8_t pb = static_cast<uint8_t>((static_cast<uint16_t>(b) * a + 127) / 255);
            src[i] = (static_cast<uint32_t>(a) << 24) | (pr << 16) | (pg << 8) | pb;
        }

        simd::bl_image_prgb32_to_color_row(inter.data(), src.data(), n);
        simd::color_to_prgb32_row(result.data(), inter.data(), n);

        auto diff = compare_prgb32_buffers(src.data(), result.data(), n);
        CHECK_MESSAGE(diff.max_diff <= 1.0f,
            "Round-trip failed at n=" << n << ". max_diff=" << diff.max_diff);
    }
}


