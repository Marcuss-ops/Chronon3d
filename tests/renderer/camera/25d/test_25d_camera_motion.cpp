#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <filesystem>
#include <cmath>

using namespace chronon3d;
namespace cm = camera_motion;

// ── Math tests ─────────────────────────────────────────────────────────────────

TEST_CASE("camera_motion: smoothstep clamps and curves") {
    CHECK(cm::smoothstep(-1.0f) == doctest::Approx(0.0f));
    CHECK(cm::smoothstep(2.0f)  == doctest::Approx(1.0f));
    CHECK(cm::smoothstep(0.0f)  == doctest::Approx(0.0f));
    CHECK(cm::smoothstep(1.0f)  == doctest::Approx(1.0f));
    // Mid-point is 0.5 (symmetric)
    CHECK(cm::smoothstep(0.5f)  == doctest::Approx(0.5f));
}

TEST_CASE("camera_motion dolly interpolates camera z") {
    auto c0 = cm::dolly(0.0f, {.from_z = -1200.0f, .to_z = -800.0f});
    auto c1 = cm::dolly(1.0f, {.from_z = -1200.0f, .to_z = -800.0f});
    CHECK(c0.position.z == doctest::Approx(-1200.0f));
    CHECK(c1.position.z == doctest::Approx(-800.0f));
    CHECK(c0.enabled);
    CHECK(c0.point_of_interest_enabled);
}

TEST_CASE("camera_motion pan interpolates camera x") {
    auto c0 = cm::pan(0.0f, {.from_x = -100.0f, .to_x = 100.0f, .z = -1000.0f});
    auto c1 = cm::pan(1.0f, {.from_x = -100.0f, .to_x = 100.0f, .z = -1000.0f});
    CHECK(c0.position.x == doctest::Approx(-100.0f));
    CHECK(c1.position.x == doctest::Approx(100.0f));
    CHECK(c0.position.z == doctest::Approx(-1000.0f));
    CHECK(c0.point_of_interest_enabled);
}

TEST_CASE("camera_motion orbit enables point of interest") {
    auto c = cm::orbit(0.5f, {.radius=200.0f, .z=-1000.0f, .target={0,0,0}});
    CHECK(c.enabled);
    CHECK(c.point_of_interest_enabled);
    CHECK(c.point_of_interest.x == doctest::Approx(0.0f));
    CHECK(c.point_of_interest.y == doctest::Approx(0.0f));
    CHECK(c.point_of_interest.z == doctest::Approx(0.0f));
    // Position must lie at distance ≈ radius from x/z origin
    const float dist_xz = std::sqrt(c.position.x * c.position.x + (c.position.z + 1000.0f) * (c.position.z + 1000.0f));
    CHECK(dist_xz == doctest::Approx(200.0f).epsilon(0.01f));
}

TEST_CASE("camera_motion orbit endpoints at correct angles") {
    const float pi = 3.14159265f;
    auto c_start = cm::orbit(0.0f, {.radius=100.0f, .z=-1000.0f, .start_angle_deg=-90.0f, .end_angle_deg=90.0f});
    auto c_end   = cm::orbit(1.0f, {.radius=100.0f, .z=-1000.0f, .start_angle_deg=-90.0f, .end_angle_deg=90.0f});
    // -90° → position.x = sin(-π/2)*100 = -100
    CHECK(c_start.position.x == doctest::Approx(-100.0f).epsilon(0.01f));
    // +90° → position.x = sin(+π/2)*100 = +100
    CHECK(c_end.position.x   == doctest::Approx( 100.0f).epsilon(0.01f));
}

TEST_CASE("camera_motion tilt_roll sets rotation without POI") {
    auto c = cm::tilt_roll(0.0f, {.tilt_deg=8.0f, .pan_deg=-5.0f, .roll_deg=3.0f});
    CHECK(c.rotation.x == doctest::Approx(8.0f));
    CHECK(c.rotation.y == doctest::Approx(-5.0f));
    CHECK(c.rotation.z == doctest::Approx(3.0f));
    CHECK(!c.point_of_interest_enabled);
}

TEST_CASE("camera_motion push_in_tilt interpolates z and tilt") {
    auto c0 = cm::push_in_tilt(0.0f, {.from_z=-1200.0f, .to_z=-850.0f, .from_tilt=-4.0f, .to_tilt=4.0f});
    auto c1 = cm::push_in_tilt(1.0f, {.from_z=-1200.0f, .to_z=-850.0f, .from_tilt=-4.0f, .to_tilt=4.0f});
    CHECK(c0.position.z == doctest::Approx(-1200.0f));
    CHECK(c1.position.z == doctest::Approx(-850.0f));
    CHECK(c0.rotation.x == doctest::Approx(-4.0f));
    CHECK(c1.rotation.x == doctest::Approx(4.0f));
}

TEST_CASE("camera_motion convenience: dolly_in/out are inverses") {
    auto in_start = cm::dolly_in(0.0f);
    auto out_end  = cm::dolly_out(1.0f);
    CHECK(in_start.position.z == doctest::Approx(out_end.position.z));
}

TEST_CASE("camera_motion convenience: dramatic_push matches push_in_tilt") {
    auto d = cm::dramatic_push(0.5f);
    auto p = cm::push_in_tilt(0.5f, {.from_z=-1300.0f, .to_z=-760.0f, .from_tilt=-5.0f, .to_tilt=5.0f});
    CHECK(d.position.z == doctest::Approx(p.position.z));
    CHECK(d.rotation.x == doctest::Approx(p.rotation.x));
}

// ── Visual / graphical tests ──────────────────────────────────────────────────

namespace {

std::unique_ptr<Framebuffer> render25d(const Composition& comp) {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    return renderer.render_frame(comp, 0);
}

// Build a minimal 2-layer scene with near (red) and far (blue) rects,
// given a pre-built Camera2_5D.
Composition make_parallax_scene(const Camera2_5D& cam) {
    return composition({
        .name = "ParallaxPreset", .width = 400, .height = 300, .duration = 1
    }, [cam](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().set(cam);
        s.layer("near", [](LayerBuilder& l) {
            l.enable_3d().position({0, -50.0f, -400.0f});
            l.rect("r", {.size={40,40}, .color=Color{1,0,0,1}, .pos={0,0,0}});
        });
        s.layer("far", [](LayerBuilder& l) {
            l.enable_3d().position({0, 50.0f, 600.0f});
            l.rect("b", {.size={40,40}, .color=Color{0,0,1,1}, .pos={0,0,0}});
        });
        return s.build();
    });
}

float color_centroid_x(const Framebuffer& fb, bool is_red) {
    float sum = 0; int n = 0;
    for (int y = 0; y < fb.height(); ++y)
        for (int x = 0; x < fb.width(); ++x) {
            auto p = fb.get_pixel(x, y);
            bool match = is_red
                ? (p.r > 0.4f && p.g < 0.3f && p.b < 0.3f && p.a > 0.05f)
                : (p.b > 0.4f && p.r < 0.3f && p.g < 0.3f && p.a > 0.05f);
            if (match) { sum += x; ++n; }
        }
    return n > 0 ? sum / n : -1.0f;
}

int count_red(const Framebuffer& fb) {
    int n = 0;
    for (int y = 0; y < fb.height(); ++y)
        for (int x = 0; x < fb.width(); ++x) {
            auto p = fb.get_pixel(x, y);
            if (p.r > 0.4f && p.g < 0.3f && p.b < 0.3f && p.a > 0.05f) ++n;
        }
    return n;
}

} // namespace

// ── parallax_sweep creates visible parallax ────────────────────────────────────

TEST_CASE("camera_motion preset pan: parallax_sweep creates parallax") {
    auto cam_left  = cm::parallax_sweep(0.0f, 80.0f, -1000.0f);
    auto cam_right = cm::parallax_sweep(1.0f, 80.0f, -1000.0f);

    auto fb_left  = render25d(make_parallax_scene(cam_left));
    auto fb_right = render25d(make_parallax_scene(cam_right));
    REQUIRE(fb_left  != nullptr);
    REQUIRE(fb_right != nullptr);

    float red_l  = color_centroid_x(*fb_left,  true);
    float red_r  = color_centroid_x(*fb_right, true);
    float blue_l = color_centroid_x(*fb_left,  false);
    float blue_r = color_centroid_x(*fb_right, false);

    // Both layers must be visible in both frames.
    CHECK(red_l  >= 0); CHECK(red_r  >= 0);
    CHECK(blue_l >= 0); CHECK(blue_r >= 0);

    // Near (red) shifts more than far (blue) when camera pans.
    float delta_near = std::abs(red_r  - red_l);
    float delta_far  = std::abs(blue_r - blue_l);
    CHECK(delta_near > delta_far);

    std::filesystem::create_directories("output/debug/25d");
    save_png(*fb_left,  "output/debug/25d/preset_pan_left.png");
    save_png(*fb_right, "output/debug/25d/preset_pan_right.png");
}

// ── dolly_in changes apparent scale ───────────────────────────────────────────

TEST_CASE("camera_motion preset dolly: dolly_in increases apparent size") {
    const int W = 400, H = 300;

    auto make = [&](float t) {
        auto cam = cm::dolly_in(t);
        return composition({
            .name="DollyPreset", .width=W, .height=H, .duration=1
        }, [cam](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera().set(cam);
            s.layer("rect", [](LayerBuilder& l) {
                l.enable_3d().position({0, 0, 0.0f});
                l.rect("r", {.size={80,80}, .color=Color{1,0,0,1}, .pos={0,0,0}});
            });
            return s.build();
        });
    };

    auto fb_far  = render25d(make(0.0f));
    auto fb_near = render25d(make(1.0f));
    REQUIRE(fb_far  != nullptr);
    REQUIRE(fb_near != nullptr);

    int px_far  = count_red(*fb_far);
    int px_near = count_red(*fb_near);
    CHECK(px_far  > 0);
    CHECK(px_near > 0);
    CHECK(px_near > px_far); // closer → more pixels

    std::filesystem::create_directories("output/debug/25d");
    save_png(*fb_far,  "output/debug/25d/preset_dolly_far.png");
    save_png(*fb_near, "output/debug/25d/preset_dolly_near.png");
}

// ── orbit_small keeps layer centroid near center ───────────────────────────────

TEST_CASE("camera_motion preset orbit: orbit_small keeps centroid near canvas center") {
    const int W = 400, H = 300;
    const auto make = [&](float t) {
        auto cam = cm::orbit_small(t);
        return composition({
            .name="OrbitPreset", .width=W, .height=H, .duration=1
        }, [cam](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera().set(cam);
            s.layer("rect", [](LayerBuilder& l) {
                l.enable_3d().position({0, 0, 0.0f});
                l.rect("r", {.size={100,100}, .color=Color{1,0,0,1}, .pos={0,0,0}});
            });
            return s.build();
        });
    };

    for (float t : {0.0f, 0.5f, 1.0f}) {
        auto fb = render25d(make(t));
        REQUIRE(fb != nullptr);
        // POI at origin → layer should stay within ±100px of canvas center X.
        float cx = color_centroid_x(*fb, true);
        CHECK(cx >= 0);
        CHECK(std::abs(cx - W * 0.5f) < 120.0f);
    }

    save_png(*render25d(make(0.0f)),   "output/debug/25d/preset_orbit_t0.png");
    save_png(*render25d(make(1.0f)),   "output/debug/25d/preset_orbit_t1.png");
}

// ── roll_reveal rotates the scene ─────────────────────────────────────────────

TEST_CASE("camera_motion preset roll: roll_reveal shifts off-center layer") {
    const int W = 400, H = 300;
    // Roll rotates the scene around the camera Z axis.
    // A rect offset from screen center will appear at a rotated position.
    // We place the rect at canvas-centered x=+100 so roll moves its centroid X.
    const auto make = [&](float t) {
        auto cam = cm::roll_reveal(t, 25.0f);
        return composition({
            .name="RollPreset", .width=W, .height=H, .duration=1
        }, [cam](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera().set(cam);
            // Offset layer so roll has a visible geometric effect.
            s.layer("rect", [](LayerBuilder& l) {
                l.enable_3d().position({100.0f, 0.0f, 0.0f});
                l.rect("r", {.size={60,60}, .color=Color{1,0,0,1}, .pos={0,0,0}});
            });
            return s.build();
        });
    };

    auto fb0 = render25d(make(0.0f));
    auto fb1 = render25d(make(1.0f));
    REQUIRE(fb0 != nullptr);
    REQUIRE(fb1 != nullptr);

    CHECK(count_red(*fb0) > 0);
    CHECK(count_red(*fb1) > 0);

    // With 25° roll, an off-center rect should shift centroid in Y
    // (rotation by 25° maps x-offset to y-offset: Δy = x*sin(25°) ≈ 0.42*x).
    auto cy_of = [&](const Framebuffer& fb) -> float {
        float sum = 0; int n = 0;
        for (int y = 0; y < fb.height(); ++y)
            for (int x = 0; x < fb.width(); ++x) {
                auto p = fb.get_pixel(x, y);
                if (p.r > 0.4f && p.g < 0.3f && p.b < 0.3f && p.a > 0.05f) { sum += y; ++n; }
            }
        return n > 0 ? sum / n : -1.0f;
    };

    float cy0 = cy_of(*fb0);
    float cy1 = cy_of(*fb1);
    CHECK(cy0 >= 0); CHECK(cy1 >= 0);
    // With 0° roll: rect at y=0 → centroid Y near H/2. With 25° roll: it shifts.
    CHECK(std::abs(cy0 - cy1) > 2.0f);

    std::filesystem::create_directories("output/debug/25d");
    save_png(*fb0, "output/debug/25d/preset_roll_0.png");
    save_png(*fb1, "output/debug/25d/preset_roll_25.png");
}

// ── dramatic_push changes scale AND tilt centroid ─────────────────────────────

TEST_CASE("camera_motion preset dramatic_push: scale grows and tilt shifts centroid Y") {
    const int W = 400, H = 300;
    const auto make = [&](float t) {
        auto cam = cm::dramatic_push(t);
        return composition({
            .name="DramaticPush", .width=W, .height=H, .duration=1
        }, [cam](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera().set(cam);
            s.layer("rect", [](LayerBuilder& l) {
                l.enable_3d().position({0, 0, 0.0f});
                l.rect("r", {.size={80,80}, .color=Color{1,0,0,1}, .pos={0,0,0}});
            });
            return s.build();
        });
    };

    auto fb0 = render25d(make(0.0f));
    auto fb1 = render25d(make(1.0f));
    REQUIRE(fb0 != nullptr);
    REQUIRE(fb1 != nullptr);

    // Closer camera → more pixels.
    CHECK(count_red(*fb1) > count_red(*fb0));

    save_png(*fb0, "output/debug/25d/preset_push_t0.png");
    save_png(*fb1, "output/debug/25d/preset_push_t1.png");
}
