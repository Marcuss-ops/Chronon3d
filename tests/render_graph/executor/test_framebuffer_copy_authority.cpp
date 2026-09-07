#include <doctest/doctest.h>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer_copy.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>

#include <algorithm>
#include <memory>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::graph;

// =============================================================================
// Wave 1A — FramebufferCopyAuthority contract
//
// Framebuffer rows are NOT contiguous at `width * sizeof(Color)`: owned
// storage is cache-line padded and pooled storage is size-class rounded, so
// the logical area must be copied row-by-row using each buffer's own stride.
// A flat [data, data + width*height) copy shifts every row after the first
// whenever either stride exceeds its logical width.
//
// Acceptance:
//   * one full-frame copy implementation (FramebufferCopyAuthority::copy_rows)
//   * unaligned widths, stride > width, per-row distinct patterns
//   * shared acquire == owned acquire == pool acquire_from (pixel-identical)
//   * 0 pixel regressions (aligned stride == width path stays exact)
//   * full_frame_copies accounting unchanged (callers still count per copy)
// =============================================================================

namespace {

Color pattern_color(i32 x, i32 y) {
    // Exact binary fractions only (multiples of 0.25): comparisons stay
    // bit-exact and any row shift changes at least one pixel.
    return Color{static_cast<f32>((x + y) % 4) * 0.25f,
                 static_cast<f32>(y % 4) * 0.25f,
                 0.5f,
                 1.0f};
}

void fill_pattern(Framebuffer& fb) {
    for (i32 y = 0; y < fb.height(); ++y) {
        for (i32 x = 0; x < fb.width(); ++x) {
            fb.set_pixel(x, y, pattern_color(x, y));
        }
    }
}

bool pixels_equal(const Framebuffer& a, const Framebuffer& b) {
    if (a.width() != b.width() || a.height() != b.height()) return false;
    for (i32 y = 0; y < a.height(); ++y) {
        for (i32 x = 0; x < a.width(); ++x) {
            if (!(a.get_pixel(x, y) == b.get_pixel(x, y))) return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("FramebufferCopyAuthority: raw copy_rows honors different row strides") {
    constexpr i32 kWidth = 5;
    constexpr i32 kHeight = 4;
    constexpr i32 kSrcStride = 7;   // stride > width
    constexpr i32 kDstStride = 9;   // different destination stride

    std::vector<Color> src(static_cast<usize>(kSrcStride) * kHeight);
    std::vector<Color> dst(static_cast<usize>(kDstStride) * kHeight, Color::transparent());
    for (i32 y = 0; y < kHeight; ++y) {
        for (i32 x = 0; x < kSrcStride; ++x) {
            src[static_cast<usize>(y) * kSrcStride + x] = pattern_color(x, y);
        }
    }

    FramebufferCopyAuthority::copy_rows(
        dst.data(), kDstStride, src.data(), kSrcStride, kWidth, kHeight);

    // Every logical row must land at its own stride offset.
    for (i32 y = 0; y < kHeight; ++y) {
        for (i32 x = 0; x < kWidth; ++x) {
            CHECK(dst[static_cast<usize>(y) * kDstStride + x] ==
                  src[static_cast<usize>(y) * kSrcStride + x]);
        }
        // The destination gutter (columns kWidth..stride) must stay untouched:
        // a flat width*height copy would have written into it.
        for (i32 x = kWidth; x < kDstStride; ++x) {
            CHECK(dst[static_cast<usize>(y) * kDstStride + x] == Color::transparent());
        }
    }
}

TEST_CASE("FramebufferCopyAuthority: framebuffer copy with stride > width is row-wise exact") {
    // 63 → stride 64, 66 → stride 68 (cache-line alignment adds padding).
    Framebuffer src(63, 7, false);
    Framebuffer dst(66, 7, false);
    REQUIRE(src.stride() == 64);
    REQUIRE(dst.stride() == 68);
    REQUIRE(src.stride() > src.width());
    REQUIRE(dst.stride() > dst.width());

    fill_pattern(src);
    FramebufferCopyAuthority::copy_rows(dst, src);

    for (i32 y = 0; y < 7; ++y) {
        for (i32 x = 0; x < 63; ++x) {
            CHECK(dst.get_pixel(x, y) == src.get_pixel(x, y));
        }
    }
}

TEST_CASE("FramebufferCopyAuthority: differing strides (pool bucket vs cache-line) copy exact pixels") {
    auto pool = FramebufferPool::create_shared(256 * 1024 * 1024);
    REQUIRE(pool != nullptr);

    // Standalone: stride = align_stride_to_cache_line(130) = 132.
    Framebuffer src(130, 4, false);
    // Pooled: bucket round_up_bucket(130) = 144 → stride 144.
    auto dst = pool->acquire_shared(130, 4, false);
    REQUIRE(dst != nullptr);

    REQUIRE(src.stride() == 132);
    REQUIRE(dst->stride() == 144);   // premise: strides really differ
    REQUIRE(dst->data() != src.data());

    fill_pattern(src);
    FramebufferCopyAuthority::copy_rows(*dst, src);

    CHECK(pixels_equal(*dst, src));

    // Gutter columns the copy did not touch must remain pristine
    // (fresh pool allocations start transparent).
    for (i32 y = 0; y < 4; ++y) {
        CHECK(dst->get_pixel(140, y) == Color::transparent());
    }
}

TEST_CASE("RenderGraphContext::acquire_framebuffer pool path copies stride-padded source exactly") {
    // Regression for the flat `std::copy(data, data + w*h, out)` in the
    // pooled branch: with src stride 132 vs dst stride 144, every row after
    // the first used to be shifted (striped output).
    auto pool = FramebufferPool::create_shared(256 * 1024 * 1024);
    RenderGraphContext ctx;
    ctx.services.framebuffer_pool = pool;

    Framebuffer src(130, 4, false);
    REQUIRE(src.stride() == 132);
    fill_pattern(src);

    auto out = ctx.acquire_framebuffer(src);
    REQUIRE(out != nullptr);
    CHECK(out->data() != src.data());
    CHECK(out->stride() == 144);
    CHECK(pixels_equal(*out, src));
}

TEST_CASE("RenderGraphContext::acquire_framebuffer non-pool path copies row-wise (R3)") {
    RenderGraphContext ctx;  // no pool → fresh shared allocation

    Framebuffer src(130, 4, false);
    fill_pattern(src);

    auto out = ctx.acquire_framebuffer(src);
    REQUIRE(out != nullptr);
    CHECK(out->data() != src.data());
    CHECK(pixels_equal(*out, src));
}

TEST_CASE("RenderGraphContext shared and owned full-frame copies agree (single copy authority)") {
    auto pool = FramebufferPool::create_shared(256 * 1024 * 1024);
    RenderGraphContext ctx;
    ctx.services.framebuffer_pool = pool;

    Framebuffer src(130, 4, false);
    fill_pattern(src);

    auto shared = ctx.acquire_framebuffer(src);
    REQUIRE(shared != nullptr);

    auto owned = ctx.acquire_owned_fb(src);
    REQUIRE(owned != nullptr);

    // All three views of the same source must be pixel-identical.
    CHECK(pixels_equal(*shared, src));
    CHECK(pixels_equal(*owned, src));
    CHECK(pixels_equal(*shared, *owned));

    // The no-pool fresh-allocation path must use the same copy authority.
    RenderGraphContext ctx2;  // no pool
    auto fresh = ctx2.acquire_owned_fb(src);
    REQUIRE(fresh != nullptr);
    CHECK(pixels_equal(*fresh, src));
}

TEST_CASE("RenderGraphContext copy stays exact when stride == width (aligned widths)") {
    // 128 is a multiple of 4 → no cache-line padding: stride == width.
    Framebuffer src(128, 4, false);
    REQUIRE(src.stride() == src.width());

    fill_pattern(src);
    RenderGraphContext ctx;
    auto out = ctx.acquire_framebuffer(src);
    REQUIRE(out != nullptr);
    CHECK(pixels_equal(*out, src));
}

TEST_CASE("FramebufferPool::acquire_from is row-wise exact and preserves fb metadata") {
    // acquire_from is the fourth caller of the copy authority (pool adapter
    // over acquire_unique). It must be pixel-identical to the context
    // shared/owned paths and must propagate the full framebuffer contract.
    auto pool = FramebufferPool::create_shared(256 * 1024 * 1024);
    REQUIRE(pool != nullptr);

    Framebuffer src(130, 4, false);   // stride 132
    fill_pattern(src);
    src.set_origin(7, 11);
    src.set_opaque(true);
    src.mark_gpu_authoritative();

    auto copied = pool->acquire_from(src);
    REQUIRE(copied != nullptr);
    REQUIRE(copied->data() != src.data());
    REQUIRE(copied->stride() == 144);  // pool bucket rounding
    CHECK(pixels_equal(*copied, src));
    CHECK(copied->origin_x() == src.origin_x());
    CHECK(copied->origin_y() == src.origin_y());
    CHECK(copied->is_opaque());
    CHECK(!copied->is_cpu_authoritative());
}

TEST_CASE("FramebufferCopyAuthority: self-copy and null/identical-pointer copies are no-ops") {
    Framebuffer fb(63, 5, false);
    fill_pattern(fb);
    const Framebuffer snapshot = fb;

    FramebufferCopyAuthority::copy_rows(fb, fb);              // same allocation
    CHECK(pixels_equal(fb, snapshot));

    Color* p = fb.pixels_row(0);
    FramebufferCopyAuthority::copy_rows(p, fb.stride(), nullptr, fb.stride(), 10, 3);
    FramebufferCopyAuthority::copy_rows(nullptr, fb.stride(), p, fb.stride(), 10, 3);
    FramebufferCopyAuthority::copy_rows(p, fb.stride(), p, fb.stride(), 10, 3);
    FramebufferCopyAuthority::copy_rows(p, fb.stride(), p, fb.stride(), 0, 0);

    CHECK(pixels_equal(fb, snapshot));
}
