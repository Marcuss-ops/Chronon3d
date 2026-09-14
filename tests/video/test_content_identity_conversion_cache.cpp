// ---------------------------------------------------------------------------
// Content-identity contract for the converted-frame caches.
//
// Regression lock for the production freeze: the RGBA→YUV conversion was
// skipped whenever `Framebuffer::key_digest()` repeated. That digest is the
// producing node's cache-KEY digest, so every frame of the same node carried
// the same value and the encoder re-sent the first frame's YUV planes for the
// whole export. The cache is now keyed on `Framebuffer::content_digest()` — an
// explicit, producer-asserted pixel identity that defaults to 0 = unknown, and
// an unknown identity is always a conversion.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/media/frame_conversion/conversion_cache_key.hpp>
#include <chronon3d/media/frame_conversion/frame_conversion_service.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace chronon3d::video;
using chronon3d::Color;
using chronon3d::Framebuffer;

namespace {

constexpr int kWidth  = 8;
constexpr int kHeight = 8;
// YUV420P tight packing: luma + quarter-resolution chroma × 2.
constexpr std::size_t kTightBytes =
    static_cast<std::size_t>(kWidth) * kHeight
    + static_cast<std::size_t>(kWidth / 2) * (kHeight / 2) * 2;

ConversionOptions yuv420_options() {
    ConversionOptions opts;
    opts.width      = kWidth;
    opts.height     = kHeight;
    opts.format     = EncoderPixelFormat::YUV420P;
    opts.matrix     = YuvMatrix::BT709;
    opts.range      = ColorRange::Limited;
    opts.apply_gamma = true;
    opts.use_cache   = true;
    return opts;
}

} // namespace

// ---------------------------------------------------------------------------
//  Key-level identity
// ---------------------------------------------------------------------------

TEST_CASE("ConversionCacheKey: asserted content identity + params is reusable") {
    Framebuffer fb(kWidth, kHeight);
    fb.set_content_digest(0xABCDEF01ULL);

    const auto a = make_conversion_cache_key(fb, kWidth, kHeight, 0, true, 0);
    const auto b = make_conversion_cache_key(fb, kWidth, kHeight, 0, true, 0);

    CHECK(a.usable());
    CHECK(a == b);
}

TEST_CASE("ConversionCacheKey: an unset content digest is never reusable") {
    // The production failure: a node-derived cache-key digest was present, so
    // the old code treated every frame as identical. The key must stay
    // unusable while the content identity is unknown, whatever key_digest says.
    Framebuffer fb(kWidth, kHeight);
    fb.set_key_digest(0xDEADBEEFULL);

    CHECK_FALSE(has_content_identity(fb));

    const auto key = make_conversion_cache_key(fb, kWidth, kHeight, 0, true, 0);
    CHECK_FALSE(key.usable());
}

TEST_CASE("ConversionCacheKey: the node cache-key digest is never consulted") {
    Framebuffer first(kWidth, kHeight);
    Framebuffer second(kWidth, kHeight);
    first.set_key_digest(0x1111ULL);
    second.set_key_digest(0x1111ULL);   // same producing node ⇒ same key digest
    first.set_content_digest(0x1000ULL);
    second.set_content_digest(0x2000ULL); // …but different pixels

    const auto a = make_conversion_cache_key(first, kWidth, kHeight, 0, true, 0);
    const auto b = make_conversion_cache_key(second, kWidth, kHeight, 0, true, 0);

    REQUIRE(a.usable());
    REQUIRE(b.usable());
    CHECK(a != b);
}

TEST_CASE("ConversionCacheKey: every conversion parameter participates in the key") {
    Framebuffer fb(kWidth, kHeight);
    fb.set_content_digest(0x5A5AULL);

    const auto base = make_conversion_cache_key(fb, kWidth, kHeight, 0, true, 0);

    CHECK(base != make_conversion_cache_key(fb, kWidth - 2, kHeight, 0, true, 0));
    CHECK(base != make_conversion_cache_key(fb, kWidth, kHeight - 2, 0, true, 0));
    CHECK(base != make_conversion_cache_key(fb, kWidth, kHeight, 1, true, 0));
    CHECK(base != make_conversion_cache_key(fb, kWidth, kHeight, 0, false, 0));
    CHECK(base != make_conversion_cache_key(fb, kWidth, kHeight, 0, true, 1));
}

// ---------------------------------------------------------------------------
//  Service-level behaviour (the cache the encoder-adjacent lane uses)
// ---------------------------------------------------------------------------

TEST_CASE("FrameConversionService: a shared node cache-key digest alone never reuses bytes") {
    FrameConversionService svc(1 << 20);
    const auto opts = yuv420_options();

    std::vector<std::uint8_t> dst_a(kTightBytes);
    std::vector<std::uint8_t> dst_b(kTightBytes);

    Framebuffer first(kWidth, kHeight);
    first.clear(Color(0.0f, 0.0f, 0.0f, 1.0f));
    first.set_key_digest(0xBEEFULL); // node cache-key digest, no content identity

    Framebuffer second(kWidth, kHeight);
    second.clear(Color(0.0f, 0.0f, 0.0f, 1.0f));
    second.set_pixel(0, 0, Color(1.0f, 1.0f, 1.0f, 1.0f)); // different pixels
    second.set_key_digest(0xBEEFULL);                      // same producing node

    const auto converted_first  = svc.convert_into(first,  opts, dst_a.data(), dst_a.size());
    const auto converted_second = svc.convert_into(second, opts, dst_b.data(), dst_b.size());

    REQUIRE(converted_first);
    REQUIRE(converted_second);
    CHECK(svc.stats().cache_hits == 0);
    CHECK(svc.stats().conversions == 2);
}

TEST_CASE("FrameConversionService: reused bytes require an asserted content identity") {
    FrameConversionService svc(1 << 20);
    const auto opts = yuv420_options();

    std::vector<std::uint8_t> dst_a(kTightBytes);
    std::vector<std::uint8_t> dst_b(kTightBytes);

    Framebuffer first(kWidth, kHeight);
    first.clear(Color(0.0f, 0.0f, 0.0f, 1.0f));
    first.set_content_digest(0x424242ULL);

    const auto converted_first = svc.convert_into(first, opts, dst_a.data(), dst_a.size());
    REQUIRE(converted_first);
    REQUIRE(converted_first.data.size() == kTightBytes);
    const std::vector<std::uint8_t> bytes_first(
        converted_first.data.begin(), converted_first.data.end());

    Framebuffer same_content(kWidth, kHeight);
    same_content.clear(Color(0.0f, 0.0f, 0.0f, 1.0f));
    same_content.set_content_digest(0x424242ULL);

    const auto converted_second =
        svc.convert_into(same_content, opts, dst_b.data(), dst_b.size());

    REQUIRE(converted_second);
    CHECK(svc.stats().cache_hits == 1);
    CHECK(svc.stats().conversions == 1);
    CHECK(std::equal(bytes_first.begin(), bytes_first.end(), converted_second.data.begin()));
}

TEST_CASE("FrameConversionService: a changed parameter misses even with equal content") {
    FrameConversionService svc(1 << 20);
    auto opts = yuv420_options();

    std::vector<std::uint8_t> dst_a(kTightBytes);
    std::vector<std::uint8_t> dst_b(kTightBytes);

    Framebuffer first(kWidth, kHeight);
    first.clear(Color(0.0f, 0.0f, 0.0f, 1.0f));
    first.set_content_digest(0x777ULL);
    REQUIRE(svc.convert_into(first, opts, dst_a.data(), dst_a.size()));

    Framebuffer same_content(kWidth, kHeight);
    same_content.clear(Color(0.0f, 0.0f, 0.0f, 1.0f));
    same_content.set_content_digest(0x777ULL);

    opts.apply_gamma = false; // same pixels, different conversion contract
    REQUIRE(svc.convert_into(same_content, opts, dst_b.data(), dst_b.size()));

    CHECK(svc.stats().cache_hits == 0);
    CHECK(svc.stats().conversions == 2);
}
