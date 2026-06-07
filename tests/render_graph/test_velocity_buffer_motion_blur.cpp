#include <doctest/doctest.h>
#include <chronon3d/render_graph/velocity_buffer_motion_blur.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <cmath>

using namespace chronon3d;

// Helper: build a 16x16 framebuffer filled with `c`.
static Framebuffer make_filled_fb(i32 W, i32 H, const Color& c) {
    Framebuffer fb(W, H);
    for (i32 y = 0; y < H; ++y) {
        Color* row = fb.pixels_row(y);
        for (i32 x = 0; x < W; ++x) row[x] = c;
    }
    return fb;
}

// Helper: make a 16x16 framebuffer with a white square in the middle.
static Framebuffer make_moving_square_fb(i32 W, i32 H, i32 square_x) {
    Framebuffer fb(W, H);
    for (i32 y = 0; y < H; ++y) {
        Color* row = fb.pixels_row(y);
        for (i32 x = 0; x < W; ++x) {
            const bool inside = (x >= square_x && x < square_x + 4 &&
                                 y >= 6 && y < 10);
            row[x] = inside ? Color{1, 1, 1, 1} : Color{0, 0, 0, 0};
        }
    }
    return fb;
}

// ── VelocityBufferMotionBlur Tests ───────────────────────────────────────────

TEST_CASE("VelocityBufferMotionBlur: first call is pass-through + history") {
    VelocityBufferMotionBlur mb;
    CHECK_FALSE(mb.has_history());

    Framebuffer a = make_filled_fb(16, 16, Color{0.5f, 0.5f, 0.5f, 1.0f});
    Framebuffer out(16, 16);
    mb.apply(a, out);

    CHECK(mb.has_history());

    // First call: out should equal a
    for (i32 y = 0; y < 16; ++y) {
        for (i32 x = 0; x < 16; ++x) {
            CHECK(out.get_pixel(x, y).r == doctest::Approx(0.5f));
        }
    }
}

TEST_CASE("VelocityBufferMotionBlur: reset clears history") {
    VelocityBufferMotionBlur mb;
    Framebuffer a = make_filled_fb(16, 16, Color{0.5f, 0.5f, 0.5f, 1.0f});
    Framebuffer out(16, 16);
    mb.apply(a, out);
    CHECK(mb.has_history());

    mb.reset();
    CHECK_FALSE(mb.has_history());
}

TEST_CASE("VelocityBufferMotionBlur: static frames produce near-zero blur") {
    VelocityBufferMotionBlur mb;
    mb.set_block_size(8).set_search_radius(8).set_samples(4);

    Framebuffer a = make_filled_fb(16, 16, Color{0.7f, 0.7f, 0.7f, 1.0f});
    Framebuffer out1(16, 16);
    mb.apply(a, out1);

    // Second frame identical: block matching should find zero offset.
    Framebuffer out2(16, 16);
    mb.apply(a, out2);

    // Pixels should be unchanged (zero velocity = pass through).
    for (i32 y = 0; y < 16; ++y) {
        for (i32 x = 0; x < 16; ++x) {
            const Color c = out2.get_pixel(x, y);
            CHECK(c.r == doctest::Approx(0.7f));
            CHECK(c.g == doctest::Approx(0.7f));
            CHECK(c.b == doctest::Approx(0.7f));
        }
    }
}

TEST_CASE("VelocityBufferMotionBlur: moving object produces a smear") {
    VelocityBufferMotionBlur mb;
    mb.set_block_size(4).set_search_radius(8).set_samples(4).set_strength(1.0f);

    // Frame 1: white square at x=4
    Framebuffer a = make_moving_square_fb(16, 16, 4);
    Framebuffer out1(16, 16);
    mb.apply(a, out1);

    // Frame 2: white square moved to x=6
    Framebuffer b = make_moving_square_fb(16, 16, 6);
    Framebuffer out2(16, 16);
    mb.apply(b, out2);

    // The motion vector is approximately (2, 0) (square moved +2 in x).
    // The smeared output should still have a bright region but slightly wider.
    i32 bright_count = 0;
    for (i32 y = 0; y < 16; ++y) {
        for (i32 x = 0; x < 16; ++x) {
            const Color c = out2.get_pixel(x, y);
            if (c.r > 0.05f) ++bright_count;
        }
    }
    // The smear should produce a few extra "bright" pixels compared to the
    // unblurred 4x4=16 pixel block.
    CHECK(bright_count >= 16);
}

TEST_CASE("VelocityBufferMotionBlur: settings accessors") {
    VelocityBufferMotionBlur mb;
    mb.set_block_size(16).set_search_radius(32).set_samples(12)
      .set_strength(2.0f).set_motion_threshold(0.05f).set_max_velocity(48.0f);

    const auto& s = mb.settings();
    CHECK(s.block_size == 16);
    CHECK(s.search_radius == 32);
    CHECK(s.samples == 12);
    CHECK(s.strength == doctest::Approx(2.0f));
    CHECK(s.motion_threshold == doctest::Approx(0.05f));
    CHECK(s.max_velocity == doctest::Approx(48.0f));
}

TEST_CASE("VelocityBufferMotionBlur: apply_inplace works") {
    VelocityBufferMotionBlur mb;
    Framebuffer a = make_filled_fb(16, 16, Color{0.3f, 0.3f, 0.3f, 1.0f});
    Framebuffer out(16, 16);
    mb.apply(a, out);

    Framebuffer b = make_filled_fb(16, 16, Color{0.3f, 0.3f, 0.3f, 1.0f});
    mb.apply_inplace(b);

    // After in-place apply, the result is in `b`
    CHECK(b.get_pixel(8, 8).r == doctest::Approx(0.3f).epsilon(0.02f));
}

TEST_CASE("VelocityBufferMotionBlur: clamps strength to [0, 4]") {
    VelocityBufferMotionBlur mb;
    mb.set_strength(-1.0f);
    CHECK(mb.settings().strength == doctest::Approx(0.0f));
    mb.set_strength(10.0f);
    CHECK(mb.settings().strength == doctest::Approx(4.0f));
}

TEST_CASE("VelocityBufferMotionBlur: history resizes on size change") {
    VelocityBufferMotionBlur mb;
    Framebuffer a = make_filled_fb(16, 16, Color{0.5f, 0.5f, 0.5f, 1.0f});
    Framebuffer out(16, 16);
    mb.apply(a, out);
    CHECK(mb.has_history());
    CHECK(mb.previous_frame()->width() == 16);

    // Apply with a different size: history resizes internally.
    Framebuffer c = make_filled_fb(32, 32, Color{0.5f, 0.5f, 0.5f, 1.0f});
    Framebuffer out2(32, 32);
    mb.apply(c, out2);
    CHECK(mb.has_history());
    CHECK(mb.previous_frame()->width() == 32);
}
