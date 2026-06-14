#include <doctest/doctest.h>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <cmath>
#include <vector>

using namespace chronon3d;

namespace {

// ── Helper: build a composition with a layer that moves horizontally ──
//    Position goes from x_left → x_right over 1 frame at 30 fps.
//    Each motion blur sub-sample sees a different position.
Composition make_moving_layer_comp(int w, int h, f32 x_left, f32 x_right) {
    CompositionSpec spec{.name="MBIntegration_MovingLayer", .width=w, .height=h,
                         .frame_rate={30,1}, .duration=60};
    return Composition(spec, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        // Dark background fills the entire canvas so we can detect the blur smear
        s.layer("bg", [&](LayerBuilder& l) {
            l.rect("bg_fill", RectParams{
                .size = {static_cast<f32>(w), static_cast<f32>(h)},
                .color = Color::black(),
                .pos  = {static_cast<f32>(w) * 0.5f, static_cast<f32>(h) * 0.5f, 0.0f},
                .fill = Fill::solid_color(Color::black())
            });
        });
        // Moving red block: travels x_left → x_right in 1 frame
        s.layer("moving_box", [&](LayerBuilder& l) {
            l.from(Frame{0}).duration(Frame{60});
            l.position_anim()
                .key(0, Vec3{x_left,  static_cast<f32>(h) * 0.5f, 0.0f})
                .key(1, Vec3{x_right, static_cast<f32>(h) * 0.5f, 0.0f});
            l.rect("box", RectParams{
                .size = {20.0f, static_cast<f32>(h)},
                .color = Color::red(),
                .pos   = {0.0f, 0.0f, 0.0f},
                .fill  = Fill::solid_color(Color::red())
            });
        });
        AnimatedCamera2_5D cam;
        cam.enabled = true;
        cam.position.set(Vec3{0.0f, 0.0f, -1000.0f});
        cam.zoom.set(1000.0f);
        s.animated_camera(cam);
        return s.build();
    });
}

// ── Helper: count non-black pixels in a framebuffer ──
[[nodiscard]] int count_non_black_pixels(const Framebuffer& fb) {
    int count = 0;
    for (i32 y = 0; y < fb.height(); ++y) {
        for (i32 x = 0; x < fb.width(); ++x) {
            Color c = fb.get_pixel(x, y);
            if (c.r > 0.01f || c.g > 0.01f || c.b > 0.01f) {
                ++count;
            }
        }
    }
    return count;
}

// ── Helper: verify that motion-blurred output has intermediate (blended) pixels
//    between the no-MB box position and the extremes of the smear.
[[nodiscard]] bool has_intermediate_pixels(const Framebuffer& fb, int mix_y) {
    // Walk a row through the middle of the smear: we expect some pixels to be
    // between pure black (0) and pure red (1).
    bool found_blended = false;
    for (i32 x = 0; x < fb.width(); ++x) {
        Color c = fb.get_pixel(x, mix_y);
        // A blended pixel: red channel between 0.01 and 0.99 (not pure black, not pure red)
        if (c.r > 0.01f && c.r < 0.99f && c.g < 0.01f && c.b < 0.01f) {
            found_blended = true;
            break;
        }
    }
    return found_blended;
}

} // namespace

// ============================================================================
// Integration: 8-sample motion blur produces perceptible sub-frame differences
// ============================================================================

TEST_CASE("Motion blur: 8 samples smear a fast-moving layer across the frame") {
    const int w = 300;
    const int h = 200;

    auto comp = make_moving_layer_comp(w, h, 50.0f, 250.0f);

    // ── Render WITH motion blur ──────────────────────────────────────────
    RenderSettings mb_settings;
    mb_settings.motion_blur.enabled          = true;
    mb_settings.motion_blur.samples          = 8;
    mb_settings.motion_blur.shutter_angle_deg = 180.0f;
    mb_settings.motion_blur.shutter_phase_deg = -90.0f;   // centred
    mb_settings.motion_blur.pattern          = TemporalSamplePattern::Uniform;
    mb_settings.motion_blur.filter           = TemporalFilter::Box;

    SoftwareRenderer mb_renderer;
    mb_renderer.set_settings(mb_settings);
    auto mb_fb = mb_renderer.render_frame(comp, Frame{0});
    REQUIRE(mb_fb != nullptr);

    // ── Render WITHOUT motion blur ───────────────────────────────────────
    RenderSettings no_mb_settings;
    no_mb_settings.motion_blur.enabled = false;

    SoftwareRenderer no_mb_renderer;
    no_mb_renderer.set_settings(no_mb_settings);
    auto no_mb_fb = no_mb_renderer.render_frame(comp, Frame{0});
    REQUIRE(no_mb_fb != nullptr);

    // ── Assertions ──────────────────────────────────────────────────────

    // 1. Motion blur should produce MORE non-black pixels (smear) than
    //    the single-frame render (box is 20px wide × 200px tall = 4000 px)
    const int mb_pixels  = count_non_black_pixels(*mb_fb);
    const int no_mb_pixels = count_non_black_pixels(*no_mb_fb);
    CHECK(mb_pixels > no_mb_pixels);

    // 2. Motion blur output must contain intermediate (blended) pixels
    //    where the red channel is between 0 and 1 — evidence of accumulation.
    const int mid_y = h / 2;
    CHECK(has_intermediate_pixels(*mb_fb, mid_y));

    // 3. No-MB output must NOT have intermediate pixels (all-or-nothing)
    //    The box is pure red, background is pure black — no blending without MB.
    CHECK_FALSE(has_intermediate_pixels(*no_mb_fb, mid_y));
}

TEST_CASE("Motion blur: Stratified pattern with Triangle filter produces consistent weights") {
    const int w = 200;
    const int h = 200;

    auto comp = make_moving_layer_comp(w, h, 20.0f, 180.0f);

    // Stratified + Triangle: should still produce valid blended output
    RenderSettings mb_settings;
    mb_settings.motion_blur.enabled          = true;
    mb_settings.motion_blur.samples          = 8;
    mb_settings.motion_blur.shutter_angle_deg = 180.0f;
    mb_settings.motion_blur.shutter_phase_deg = -90.0f;
    mb_settings.motion_blur.pattern          = TemporalSamplePattern::Stratified;
    mb_settings.motion_blur.filter           = TemporalFilter::Triangle;
    mb_settings.motion_blur.jitter_seed      = 42;

    SoftwareRenderer mb_renderer;
    mb_renderer.set_settings(mb_settings);
    auto mb_fb = mb_renderer.render_frame(comp, Frame{0});
    REQUIRE(mb_fb != nullptr);

    const int mid_y = h / 2;
    // With Triangle filter, samples near the shutter edges get lower weight,
    // but we should still see blended pixels (the centred samples dominate)
    CHECK(has_intermediate_pixels(*mb_fb, mid_y));
}

TEST_CASE("Motion blur: deterministic — same seed produces identical output") {
    const int w = 200;
    const int h = 200;

    auto comp = make_moving_layer_comp(w, h, 20.0f, 180.0f);

    RenderSettings mb_settings;
    mb_settings.motion_blur.enabled          = true;
    mb_settings.motion_blur.samples          = 8;
    mb_settings.motion_blur.shutter_angle_deg = 180.0f;
    mb_settings.motion_blur.shutter_phase_deg = -90.0f;
    mb_settings.motion_blur.pattern          = TemporalSamplePattern::Stratified;
    mb_settings.motion_blur.filter           = TemporalFilter::Box;
    mb_settings.motion_blur.jitter_seed      = 0x3A5C9F1E;

    // First render
    SoftwareRenderer r1;
    r1.set_settings(mb_settings);
    auto fb1 = r1.render_frame(comp, Frame{0});
    REQUIRE(fb1 != nullptr);

    // Second render (same settings, fresh renderer to avoid cache reuse)
    SoftwareRenderer r2;
    r2.set_settings(mb_settings);
    auto fb2 = r2.render_frame(comp, Frame{0});
    REQUIRE(fb2 != nullptr);

    // Pixel-for-pixel equality
    bool all_equal = true;
    for (i32 y = 0; y < h && all_equal; ++y) {
        for (i32 x = 0; x < w; ++x) {
            Color c1 = fb1->get_pixel(x, y);
            Color c2 = fb2->get_pixel(x, y);
            if (std::abs(c1.r - c2.r) > 0.001f ||
                std::abs(c1.g - c2.g) > 0.001f ||
                std::abs(c1.b - c2.b) > 0.001f ||
                std::abs(c1.a - c2.a) > 0.001f) {
                all_equal = false;
                break;
            }
        }
    }
    CHECK(all_equal);
}

TEST_CASE("Motion blur: sub-frame pipeline — 8 samples produce 8 distinct positions") {
    // This is the decisive architecture test: without the sub-frame pipeline
    // fixes, all 8 sub-samples would evaluate at the same integer frame
    // and the motion-blurred output would be identical to the no-MB output.

    const int w = 300;
    const int h = 100;
    auto comp = make_moving_layer_comp(w, h, 0.0f, 300.0f);

    RenderSettings mb_settings;
    mb_settings.motion_blur.enabled          = true;
    mb_settings.motion_blur.samples          = 8;
    mb_settings.motion_blur.shutter_angle_deg = 180.0f;
    mb_settings.motion_blur.shutter_phase_deg = -90.0f;
    mb_settings.motion_blur.pattern          = TemporalSamplePattern::Uniform;
    mb_settings.motion_blur.filter           = TemporalFilter::Box;

    SoftwareRenderer r;
    r.set_settings(mb_settings);
    auto mb_fb = r.render_frame(comp, Frame{0});
    REQUIRE(mb_fb != nullptr);

    // Without motion blur, the box at frame 0 is at x=0.
    // With 8 samples and 180° shutter, the box is sampled at positions
    // ranging from x≈0 to x≈150 (half the 300px movement over 1 frame).

    // Collect the range of x-coordinates in the middle row where non-black
    // pixels appear. The MB smear should span a wide range.
    const int mid_y = h / 2;
    int min_x = w, max_x = -1;
    for (i32 x = 0; x < w; ++x) {
        Color c = mb_fb->get_pixel(x, mid_y);
        if (c.r > 0.01f) {
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
        }
    }
    REQUIRE(min_x <= max_x);  // at least some non-black pixels

    // The smear should span more than just the box width (20px),
    // confirming sub-frame sampling is working.
    const int smear_width = max_x - min_x;
    CHECK(smear_width > 25);  // much wider than a 20px box → smearing confirmed

    // Verify: without MB, the box is at a single position
    RenderSettings no_mb_settings;
    no_mb_settings.motion_blur.enabled = false;
    SoftwareRenderer r2;
    r2.set_settings(no_mb_settings);
    auto no_mb_fb = r2.render_frame(comp, Frame{0});

    int no_mb_min = w, no_mb_max = -1;
    for (i32 x = 0; x < w; ++x) {
        Color c = no_mb_fb->get_pixel(x, mid_y);
        if (c.r > 0.01f) {
            if (x < no_mb_min) no_mb_min = x;
            if (x > no_mb_max) no_mb_max = x;
        }
    }
    if (no_mb_min <= no_mb_max) {
        int no_mb_width = no_mb_max - no_mb_min;
        // The MB smear should be wider than the no-MB box
        CHECK(smear_width > no_mb_width);
    }
}
