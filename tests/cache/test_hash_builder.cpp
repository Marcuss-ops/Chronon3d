// =============================================================================
// test_hash_builder.cpp — Golden digest tests for HashBuilder + cache keys.
//
// Ensures digests are deterministic across runs and that any intentional
// change to cache keys is visible in git.
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/core/hash/hash_builder.hpp>
#include <chronon3d/cache/frame_cache.hpp>
#include <chronon3d/cache/video_frame_cache.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/media/frame_conversion/converted_frame_cache.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>

using namespace chronon3d;
using namespace chronon3d::core::hash;
using namespace chronon3d::cache;
using namespace chronon3d::video;
using namespace chronon3d::graph;

TEST_CASE("HashBuilder: basic determinism") {
    // Same inputs → same digest.
    auto h1 = HashBuilder{}.add("hello").add(42).finish();
    auto h2 = HashBuilder{}.add("hello").add(42).finish();
    CHECK(h1 == h2);
}

TEST_CASE("HashBuilder: different inputs → different digests") {
    auto h1 = HashBuilder{}.add("hello").add(42).finish();
    auto h2 = HashBuilder{}.add("world").add(42).finish();
    CHECK(h1 != h2);
}

TEST_CASE("HashBuilder: order matters") {
    auto h1 = HashBuilder{}.add("a").add("b").finish();
    auto h2 = HashBuilder{}.add("b").add("a").finish();
    CHECK(h1 != h2);
}

// ── Golden digest values ──────────────────────────────────────────────────

TEST_CASE("FrameCacheKey: golden digest") {
    FrameCacheKey key;
    key.composition_id = "test_comp";
    key.frame          = Frame{5};
    key.width          = 1920;
    key.height         = 1080;
    key.scene_hash     = 0xABCD1234;
    key.render_hash    = 0x5678EF90;
    key.temporal_key   = TemporalSampleKey{Frame{5}, 0, 1};

    // V1 fixture: freeze the legacy digest before introducing semantic key versions.
    constexpr u64 expected = 15271311225107256366ULL;
    CHECK(key.digest_v1() == expected);
    CHECK(key.digest() == key.digest_v2());
    CHECK(key.digest_v2() != key.digest_v1());
    CHECK(key.digest_v2() == 8530631660911191565ULL); // V2 fixture: schema change requires a version bump
}

TEST_CASE("VideoFrameKey: golden digest") {
    VideoFrameKey key;
    key.composition_id = "test_comp";
    key.frame_index    = 10;
    key.width          = 1920;
    key.height         = 1080;
    key.format         = runtime::make_frame_format(runtime::PixelFormat::Yuv420P);
    key.scene_hash     = 0xDEADBEEF;
    key.render_hash    = 0xCAFEBABE;

    // V1 fixture: changing this sequence requires an explicit key-version bump.
    constexpr u64 expected = 4747266337232658455ULL;
    CHECK(key.digest_v1() == expected);
    CHECK(key.digest() == key.digest_v2());
    CHECK(key.digest_v2() != key.digest_v1());
    CHECK(key.digest_v2() == 17712888411037905226ULL); // V2 fixture: schema change requires a version bump
}

TEST_CASE("NodeCacheKey: golden digest") {
    NodeCacheKey key;
    key.scope        = "test_scope";
    key.frame        = Frame{10};
    key.width        = 640;
    key.height       = 480;
    key.params_hash  = 0x11111111;
    key.source_hash  = 0x22222222;
    key.input_hash   = 0x33333333;
    key.temporal_key = TemporalSampleKey{Frame{10}, 100, 2};
    key.tile_x       = 0;
    key.tile_y       = 0;
    key.tile_size    = 64;
    key.tile_hash    = 0;

    // V1 fixture: changing this sequence requires an explicit key-version bump.
    constexpr u64 expected = 13175591628716667620ULL;
    CHECK(key.digest() == expected);
}

TEST_CASE("FrameCacheKey V2 digest includes every semantic field") {
    FrameCacheKey base;
    base.composition_id = "test_comp";
    base.frame = Frame{5};
    base.width = 1920;
    base.height = 1080;
    base.scene_hash = 0xABCD1234;
    base.render_hash = 0x5678EF90;
    base.temporal_key = TemporalSampleKey{Frame{5}, 0, 1};
    const auto original = base.digest();

    const auto changed = [&](auto mutate) {
        auto key = base;
        mutate(key);
        CHECK(key.digest() != original);
    };
    changed([](auto& k) { k.composition_id += "x"; });
    changed([](auto& k) { k.frame = Frame{6}; });
    changed([](auto& k) { ++k.width; });
    changed([](auto& k) { ++k.height; });
    changed([](auto& k) { ++k.scene_hash; });
    changed([](auto& k) { ++k.render_hash; });
    changed([](auto& k) { k.temporal_key.frame = Frame{6}; });
    changed([](auto& k) { ++k.temporal_key.subframe_tick; });
    changed([](auto& k) { ++k.temporal_key.version; });
}

TEST_CASE("VideoFrameKey V2 digest includes every semantic field") {
    VideoFrameKey base;
    base.composition_id = "test_comp";
    base.frame_index = 10;
    base.width = 1920;
    base.height = 1080;
    base.format = runtime::make_frame_format(runtime::PixelFormat::Yuv420P);
    base.scene_hash = 0xDEADBEEF;
    base.render_hash = 0xCAFEBABE;
    const auto original = base.digest();

    const auto changed = [&](auto mutate) {
        auto key = base;
        mutate(key);
        CHECK(key.digest() != original);
    };
    changed([](auto& k) { k.composition_id += "x"; });
    changed([](auto& k) { ++k.frame_index; });
    changed([](auto& k) { ++k.width; });
    changed([](auto& k) { ++k.height; });
    changed([](auto& k) { k.format.pixel = runtime::PixelFormat::Nv12; });
    changed([](auto& k) { k.format.primaries = runtime::ColorPrimaries::Bt2020; });
    changed([](auto& k) { k.format.transfer = runtime::TransferFunction::Linear; });
    changed([](auto& k) { k.format.matrix = runtime::ColorMatrix::Bt601; });
    changed([](auto& k) { k.format.range = runtime::ColorRange::Full; });
    changed([](auto& k) { k.format.chroma = runtime::ChromaLocation::Center; });
    changed([](auto& k) { k.format.alpha = runtime::AlphaMode::Premultiplied; });
    changed([](auto& k) { ++k.format.pixel_aspect.numerator; });
    changed([](auto& k) { ++k.format.pixel_aspect.denominator; });
    changed([](auto& k) { ++k.scene_hash; });
    changed([](auto& k) { ++k.render_hash; });
}

TEST_CASE("NodeCacheKey V1 digest includes every semantic field") {
    NodeCacheKey base;
    base.scope = "test_scope";
    base.frame = Frame{10};
    base.width = 640;
    base.height = 480;
    base.params_hash = 0x11111111;
    base.source_hash = 0x22222222;
    base.input_hash = 0x33333333;
    base.temporal_key = TemporalSampleKey{Frame{10}, 100, 2};
    base.tile_x = 0;
    base.tile_y = 0;
    base.tile_size = 64;
    const auto original = base.digest();

    const auto changed = [&](auto mutate) {
        auto key = base;
        mutate(key);
        CHECK(key.digest() != original);
    };
    changed([](auto& k) { k.scope += "x"; });
    changed([](auto& k) { k.frame = Frame{11}; });
    changed([](auto& k) { ++k.width; });
    changed([](auto& k) { ++k.height; });
    changed([](auto& k) { ++k.params_hash; });
    changed([](auto& k) { ++k.source_hash; });
    changed([](auto& k) { ++k.input_hash; });
    changed([](auto& k) { ++k.params_version; });
    changed([](auto& k) { ++k.source_version; });
    changed([](auto& k) { ++k.input_version; });
    changed([](auto& k) { k.temporal_key.frame = Frame{11}; });
    changed([](auto& k) { ++k.temporal_key.subframe_tick; });
    changed([](auto& k) { ++k.temporal_key.version; });
    changed([](auto& k) { ++k.tile_x; });
    changed([](auto& k) { ++k.tile_y; });
    changed([](auto& k) { ++k.tile_size; });
    changed([](auto& k) { ++k.tile_hash; });
}

TEST_CASE("ConvertedFrameCacheKey: golden hash") {
    ConvertedFrameCacheKey key;
    key.framebuffer_digest = 0xFEEDFACECAFEBEEFULL;
    key.width              = 1920;
    key.height             = 1080;
    key.format             = EncoderPixelFormat::YUV420P;
    key.matrix             = YuvMatrix::BT709;
    key.range              = ColorRange::Limited;
    key.apply_gamma        = true;

    ConvertedFrameCacheKeyHash hasher;
    const std::size_t original = hasher(key);
    CHECK(original != 0);
    CHECK(hasher(key) == original);
    const auto changed = [&](auto mutate) {
        auto copy = key;
        mutate(copy);
        CHECK(hasher(copy) != original);
    };
    changed([](auto& k) { ++k.framebuffer_digest; });
    changed([](auto& k) { ++k.width; });
    changed([](auto& k) { ++k.height; });
    changed([](auto& k) { k.format = EncoderPixelFormat::NV12; });
    changed([](auto& k) { k.matrix = YuvMatrix::BT601; });
    changed([](auto& k) { k.range = ColorRange::Full; });
    changed([](auto& k) { k.apply_gamma = false; });
    changed([](auto& k) { k.rec709_oetf = true; });
    changed([](auto& k) { k.output_space = color::ColorSpace::Rec709; });
    changed([](auto& k) { k.reference_white_nits += 1.0f; });
}

TEST_CASE("HashBuilder: add_enum works for various enum types") {
    auto h1 = HashBuilder{}.add_enum(runtime::PixelFormat::RGBA8).finish();
    auto h2 = HashBuilder{}.add_enum(runtime::PixelFormat::YUV420P).finish();
    CHECK(h1 != h2);
}

TEST_CASE("HashBuilder: add_bytes produces deterministic output") {
    const int data = 42;
    auto h1 = HashBuilder{}.add_bytes(&data, sizeof(data)).finish();
    auto h2 = HashBuilder{}.add_bytes(&data, sizeof(data)).finish();
    CHECK(h1 == h2);
}

TEST_CASE("HashBuilder: floating point add") {
    auto h1 = HashBuilder{}.add(3.14f).add(2.718).finish();
    auto h2 = HashBuilder{}.add(3.14f).add(2.718).finish();
    CHECK(h1 == h2);
    auto h3 = HashBuilder{}.add(3.15f).add(2.718).finish();
    CHECK(h1 != h3);
}
