#include <doctest/doctest.h>
#include <chronon3d/render_graph/post_processing_system.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <cmath>

using namespace chronon3d;

static Framebuffer make_filled_fb(i32 W, i32 H, const Color& c) {
    Framebuffer fb(W, H);
    for (i32 y = 0; y < H; ++y) {
        Color* row = fb.pixels_row(y);
        for (i32 x = 0; x < W; ++x) row[x] = c;
    }
    return fb;
}

// ── PostProcessingSystem Tests ───────────────────────────────────────────────

TEST_CASE("PostProcessingSystem: default state") {
    PostProcessingSystem pp;
    CHECK(pp.depth_of_field_enabled());
    CHECK_FALSE(pp.motion_blur_enabled());
}

TEST_CASE("PostProcessingSystem: enable / disable passes") {
    PostProcessingSystem pp;
    pp.enable_motion_blur(true);
    pp.enable_depth_of_field(false);
    CHECK(pp.motion_blur_enabled());
    CHECK_FALSE(pp.depth_of_field_enabled());
}

TEST_CASE("PostProcessingSystem: DOF disabled → passthrough") {
    PostProcessingSystem pp;
    pp.enable_depth_of_field(false);
    pp.set_dof(DepthOfFieldSettings{ .enabled = true, .aperture = 1.0f, .max_blur = 32.0f });

    Framebuffer a = make_filled_fb(16, 16, Color{0.5f, 0.5f, 0.5f, 1.0f});
    Framebuffer out(16, 16);
    pp.apply(a, out);

    for (i32 y = 0; y < 16; ++y) {
        for (i32 x = 0; x < 16; ++x) {
            const Color c = out.get_pixel(x, y);
            CHECK(c.r == doctest::Approx(0.5f).epsilon(0.001f));
        }
    }
}

TEST_CASE("PostProcessingSystem: DOF enabled with zero radius → passthrough") {
    PostProcessingSystem pp;
    pp.set_dof(DepthOfFieldSettings{ .enabled = true, .focus_z = 0, .aperture = 0, .max_blur = 0 });

    Framebuffer a = make_filled_fb(16, 16, Color{0.8f, 0.8f, 0.8f, 1.0f});
    Framebuffer out(16, 16);
    pp.apply(a, out);

    for (i32 y = 0; y < 16; ++y) {
        for (i32 x = 0; x < 16; ++x) {
            const Color c = out.get_pixel(x, y);
            CHECK(c.r == doctest::Approx(0.8f).epsilon(0.001f));
        }
    }
}

TEST_CASE("PostProcessingSystem: DOF with non-zero blur softens a high-contrast edge") {
    PostProcessingSystem pp;
    pp.enable_motion_blur(false);
    // focus_z=0, layer at z=100 → dz=100 → r=min(100*0.5, 6) = 6 pixels
    pp.set_dof(DepthOfFieldSettings{ .enabled = true, .focus_z = 0.0f, .aperture = 0.5f, .max_blur = 6.0f });

    // Left half black, right half white
    Framebuffer a(16, 16);
    for (i32 y = 0; y < 16; ++y) {
        Color* row = a.pixels_row(y);
        for (i32 x = 0; x < 16; ++x) {
            row[x] = (x < 8) ? Color{0, 0, 0, 1} : Color{1, 1, 1, 1};
        }
    }

    Framebuffer out(16, 16);
    pp.apply(a, out, Camera2_5D{}, /*layer_world_z=*/100.0f);

    // Pixels near the boundary (x=7..8) should be blended, not pure black/white
    const Color c7 = out.get_pixel(7, 8);
    const Color c8 = out.get_pixel(8, 8);
    CHECK(c7.r > 0.0f);
    CHECK(c7.r < 1.0f);
    CHECK(c8.r > 0.0f);
    CHECK(c8.r < 1.0f);
}

TEST_CASE("PostProcessingSystem: motion blur can be enabled") {
    PostProcessingSystem pp;
    pp.enable_motion_blur(true);
    pp.set_motion_blur_samples(4);
    pp.set_motion_blur_strength(0.5f);

    Framebuffer a = make_filled_fb(16, 16, Color{0.4f, 0.4f, 0.4f, 1.0f});
    Framebuffer out(16, 16);
    pp.apply(a, out);

    // First call to motion blur is pass-through
    CHECK(out.get_pixel(8, 8).r == doctest::Approx(0.4f).epsilon(0.02f));
}

TEST_CASE("PostProcessingSystem: DOF + motion blur in pipeline") {
    PostProcessingSystem pp;
    pp.enable_depth_of_field(true);
    pp.enable_motion_blur(true);
    pp.set_dof(DepthOfFieldSettings{ .enabled = true, .aperture = 0.1f, .max_blur = 2.0f });
    pp.set_motion_blur_samples(2).set_motion_blur_strength(1.0f);

    Framebuffer a = make_filled_fb(16, 16, Color{0.6f, 0.6f, 0.6f, 1.0f});
    Framebuffer b = make_filled_fb(16, 16, Color{0.6f, 0.6f, 0.6f, 1.0f});
    Framebuffer out(16, 16);

    pp.apply(a, out);
    pp.apply(b, out);

    // Both frames are static grey → output should be near 0.6
    CHECK(out.get_pixel(8, 8).r == doctest::Approx(0.6f).epsilon(0.05f));
}

TEST_CASE("PostProcessingSystem: reset clears internal state") {
    PostProcessingSystem pp;
    pp.enable_motion_blur(true);
    Framebuffer a = make_filled_fb(16, 16, Color{0.5f, 0.5f, 0.5f, 1.0f});
    Framebuffer out(16, 16);
    pp.apply(a, out);

    pp.reset();
    // After reset, motion blur should be a pass-through
    Framebuffer b = make_filled_fb(16, 16, Color{0.7f, 0.7f, 0.7f, 1.0f});
    pp.apply(b, out);
    CHECK(out.get_pixel(8, 8).r == doctest::Approx(0.7f).epsilon(0.02f));
}

TEST_CASE("PostProcessingSystem: configuration accessors") {
    PostProcessingSystem pp;
    pp.set_motion_blur_block_size(16)
      .set_motion_blur_search_radius(8)
      .set_motion_blur_samples(12)
      .set_motion_blur_strength(0.75f)
      .set_motion_blur_max_velocity(100.0f);

    const auto& s = pp.motion_blur().settings();
    CHECK(s.block_size == 16);
    CHECK(s.search_radius == 8);
    CHECK(s.samples == 12);
    CHECK(s.strength == doctest::Approx(0.75f));
    CHECK(s.max_velocity == doctest::Approx(100.0f));
}
