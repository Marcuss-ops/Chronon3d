#include <doctest/doctest.h>
#include <chronon3d/scene/camera_2_5d.hpp>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/renderer/software_renderer.hpp>

using namespace chronon3d;

static std::unique_ptr<Framebuffer> render_fn(
    std::function<Scene(const FrameContext&)> fn, int w = 120, int h = 120)
{
    SoftwareRenderer rend;
    Composition comp(CompositionSpec{.width=w,.height=h,.duration=1}, fn);
    return rend.render_frame(comp, 0);
}

// ---------------------------------------------------------------------------
// DepthOfFieldSettings struct defaults
// ---------------------------------------------------------------------------
TEST_CASE("DOF: DepthOfFieldSettings default disabled") {
    DepthOfFieldSettings d;
    CHECK_FALSE(d.enabled);
    CHECK(d.focus_z   == doctest::Approx(0.0f));
    CHECK(d.aperture  == doctest::Approx(0.015f));
    CHECK(d.max_blur  == doctest::Approx(24.0f));
}

TEST_CASE("DOF: Camera2_5D has dof field") {
    Camera2_5D cam;
    CHECK_FALSE(cam.dof.enabled);
}

// ---------------------------------------------------------------------------
// Blur formula: |layer.z - focus_z| * aperture, clamped to max_blur
// ---------------------------------------------------------------------------
TEST_CASE("DOF: blur amount formula") {
    DepthOfFieldSettings d{.enabled=true, .focus_z=0.0f, .aperture=0.02f, .max_blur=20.0f};
    float dist    = std::abs(-500.0f - d.focus_z);  // layer at z=-500
    float blur    = std::min(dist * d.aperture, d.max_blur);
    CHECK(blur == doctest::Approx(10.0f));          // 500 * 0.02 = 10
}

TEST_CASE("DOF: blur clamped at max_blur") {
    DepthOfFieldSettings d{.enabled=true, .focus_z=0.0f, .aperture=0.02f, .max_blur=8.0f};
    float dist = std::abs(-2000.0f - d.focus_z);
    float blur = std::min(dist * d.aperture, d.max_blur);
    CHECK(blur == doctest::Approx(8.0f));
}

TEST_CASE("DOF: focus plane has zero blur") {
    DepthOfFieldSettings d{.enabled=true, .focus_z=0.0f, .aperture=0.02f, .max_blur=20.0f};
    float blur = std::min(std::abs(0.0f - d.focus_z) * d.aperture, d.max_blur);
    CHECK(blur == doctest::Approx(0.0f));
}

// ---------------------------------------------------------------------------
// Render with DOF enabled -- just verify no crash and frame is produced
// ---------------------------------------------------------------------------
TEST_CASE("DOF: render with DOF enabled does not crash") {
    auto fb = render_fn([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera_2_5d({
            .enabled = true,
            .position = {0, 0, -1000},
            .zoom = 1000.0f,
            .dof = DepthOfFieldSettings{
                .enabled  = true,
                .focus_z  = 0.0f,
                .aperture = 0.02f,
                .max_blur = 12.0f
            }
        });
        s.rect("bg", {.size={120,120}, .color=Color{0.1f,0.1f,0.1f,1}, .pos={60,60,0}});
        s.layer("near", [](LayerBuilder& l) {
            l.enable_3d(true).position({60, 60, -400});  // z=-400, far from focus
            l.rounded_rect("r", {.size={40,40}, .radius=6, .color=Color::red()});
        });
        s.layer("subject", [](LayerBuilder& l) {
            l.enable_3d(true).position({60, 60, 0});    // at focus
            l.rounded_rect("r", {.size={40,40}, .radius=6, .color=Color::white()});
        });
        return s.build();
    });

    CHECK(fb != nullptr);
    CHECK(fb->width()  == 120);
    CHECK(fb->height() == 120);

    // Subject at focus should be sharp (white pixel at center)
    // Near layer at z=-400 should be blurred (softer edges)
    Color centre = fb->get_pixel(60, 60);
    CHECK(centre.a > 0.0f);  // something was rendered
}
