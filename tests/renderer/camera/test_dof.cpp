#include <doctest/doctest.h>
#include <xxhash.h>

#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/model/camera/dof.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

using namespace chronon3d;

static std::shared_ptr<Framebuffer> render_dof_fn(
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
static const LensModel kDefaultLens;  // 50mm, f/2.8, 36x24mm

TEST_CASE("DOF: blur amount formula") {
    DepthOfFieldSettings d{.enabled=true, .focus_z=0.0f, .aperture=0.02f, .max_blur=20.0f};
    float blur    = compute_dof_blur_radius(d, kDefaultLens, -500.0f);
    CHECK(blur == doctest::Approx(10.0f));          // 500 * 0.02 = 10
}

TEST_CASE("DOF: blur clamped at max_blur") {
    DepthOfFieldSettings d{.enabled=true, .focus_z=0.0f, .aperture=0.02f, .max_blur=8.0f};
    float blur = compute_dof_blur_radius(d, kDefaultLens, -2000.0f);
    CHECK(blur == doctest::Approx(8.0f));
}

TEST_CASE("DOF: focus plane has zero blur") {
    DepthOfFieldSettings d{.enabled=true, .focus_z=0.0f, .aperture=0.02f, .max_blur=20.0f};
    float blur = compute_dof_blur_radius(d, kDefaultLens, 0.0f);
    CHECK(blur == doctest::Approx(0.0f));
}

TEST_CASE("DOF: disabled returns zero blur") {
    DepthOfFieldSettings d{.enabled=false, .focus_z=0.0f, .aperture=1.0f, .max_blur=20.0f};
    CHECK(compute_dof_blur_radius(d, kDefaultLens, 1000.0f) == doctest::Approx(0.0f));
}

// ---------------------------------------------------------------------------
// Render with DOF enabled -- just verify no crash and frame is produced
// ---------------------------------------------------------------------------
TEST_CASE("DOF: render with DOF enabled does not crash") {
    auto fb = render_dof_fn([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().set({
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

// ---------------------------------------------------------------------------
// Physical DoF model — Circle of Confusion
// ---------------------------------------------------------------------------

TEST_CASE("DOF: physical CoC at focus plane is zero") {
    LensModel lens{50.0f, 2.8f, 450.0f, 36.0f, 24.0f, GateFit::Fill};
    DepthOfFieldSettings d{
        .enabled = true,
        .max_blur = 100.0f,
        .focus_distance = 1000.0f,
        .use_physical_model = true
    };
    f32 blur = compute_dof_blur_radius(d, lens, -1000.0f, 1920.0f);
    CHECK(blur == doctest::Approx(0.0f));
}

TEST_CASE("DOF: physical CoC — near has larger CoC than far (asymmetric defocus)") {
    // Thin-lens model: for a fixed focus distance, objects CLOSER than
    // the focus plane produce a larger Circle of Confusion than objects
    // FARTHER away, because of the |S2-S1|/S2 term in the CoC formula.
    LensModel lens{50.0f, 2.8f, 450.0f, 36.0f, 24.0f, GateFit::Fill};
    DepthOfFieldSettings d{
        .enabled = true,
        .max_blur = 100.0f,
        .focus_distance = 1000.0f,
        .use_physical_model = true
    };
    // Object at z=500 (near side): farther from camera, closer to focus.
    f32 blur_near = compute_dof_blur_radius(d, lens, -500.0f, 1920.0f);
    CHECK(blur_near > 0.0f);
    // Object at z=2000 (far side): farther from camera and focus.
    f32 blur_far = compute_dof_blur_radius(d, lens, -2000.0f, 1920.0f);
    CHECK(blur_far > 0.0f);
    // Near object should have LARGER CoC due to asymmetric defocus.
    CHECK(blur_near > blur_far);
}

TEST_CASE("DOF: physical CoC respects max_blur clamp") {
    LensModel lens{50.0f, 1.0f, 450.0f, 36.0f, 24.0f, GateFit::Fill};  // f/1.0
    DepthOfFieldSettings d{
        .enabled = true,
        .max_blur = 8.0f,
        .focus_distance = 1000.0f,
        .use_physical_model = true
    };
    f32 blur = compute_dof_blur_radius(d, lens, -10000.0f, 1920.0f);
    CHECK(blur <= doctest::Approx(8.0f));
}

TEST_CASE("DOF: physical CoC with smaller f-stop (wider aperture) = more blur") {
    LensModel fast{50.0f, 1.4f, 450.0f, 36.0f, 24.0f, GateFit::Fill};
    LensModel slow{50.0f, 16.0f, 450.0f, 36.0f, 24.0f, GateFit::Fill};
    DepthOfFieldSettings d{
        .enabled = true,
        .max_blur = 100.0f,
        .focus_distance = 1000.0f,
        .use_physical_model = true
    };
    f32 blur_fast = compute_dof_blur_radius(d, fast, -2000.0f, 1920.0f);
    f32 blur_slow = compute_dof_blur_radius(d, slow, -2000.0f, 1920.0f);
    CHECK(blur_fast > blur_slow);
}

TEST_CASE("DOF: legacy model unchanged (backward compat)") {
    DepthOfFieldSettings d{.enabled=true, .focus_z=0.0f, .aperture=0.02f, .max_blur=20.0f};
    f32 blur = compute_dof_blur_radius(d, kDefaultLens, -500.0f);
    CHECK(blur == doctest::Approx(10.0f));
}

TEST_CASE("DOF: AnimatedCamera2_5D dof_enabled flag works with constant values") {
    AnimatedCamera2_5D cam;
    cam.dof_enabled = true;
    cam.aperture.set(0.03f);
    cam.max_blur.set(12.0f);
    // Not animated — but dof_enabled should still activate DoF.
    Camera2_5D result = cam.evaluate(Frame{30});
    CHECK(result.dof.enabled == true);
    CHECK(result.dof.aperture == doctest::Approx(0.03f));
    CHECK(result.dof.max_blur == doctest::Approx(12.0f));
}

TEST_CASE("DOF: AnimatedCamera2_5D physical model propagates to Camera2_5D") {
    AnimatedCamera2_5D cam;
    cam.dof_enabled = true;
    cam.use_physical_model = true;
    cam.focal_length.set(85.0f);
    cam.f_stop.set(4.0f);
    cam.focus_distance.set(1500.0f);

    Camera2_5D result = cam.evaluate(Frame{30});
    CHECK(result.dof.use_physical_model == true);
    CHECK(result.lens.focal_length == doctest::Approx(85.0f));
    CHECK(result.lens.f_stop == doctest::Approx(4.0f));
    CHECK(result.dof.focus_distance == doctest::Approx(1500.0f));
}
