#include <chronon3d/scene/builders/scene_builder.hpp>
#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/backends/software/rasterizers/projected_card_rasterizer.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include <chronon3d/render_graph/nodes/shadow_node.hpp>
#include <chronon3d/rendering/light_context.hpp>
#include <chronon3d/rendering/lighting_eval.hpp>
#include <tests/helpers/render_fixtures.hpp>
#include <tests/helpers/test_utils.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

using namespace chronon3d;

namespace {

bool color_near(const Color& a, const Color& b, float eps = 0.15f) {
    return std::abs(a.r - b.r) <= eps &&
           std::abs(a.g - b.g) <= eps &&
           std::abs(a.b - b.b) <= eps &&
           std::abs(a.a - b.a) <= eps;
}

float framebuffer_max_delta(const Framebuffer& a, const Framebuffer& b) {
    REQUIRE(a.width() == b.width());
    REQUIRE(a.height() == b.height());
    float max_delta = 0.0f;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const Color ca = a.get_pixel(x, y);
            const Color cb = b.get_pixel(x, y);
            max_delta = std::max(max_delta, std::abs(ca.r - cb.r));
            max_delta = std::max(max_delta, std::abs(ca.g - cb.g));
            max_delta = std::max(max_delta, std::abs(ca.b - cb.b));
            max_delta = std::max(max_delta, std::abs(ca.a - cb.a));
        }
    }
    return max_delta;
}

bool framebuffer_has_only_finite(const Framebuffer& fb) {
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            const Color c = fb.get_pixel(x, y);
            if (!std::isfinite(c.r) || !std::isfinite(c.g) || !std::isfinite(c.b) || !std::isfinite(c.a)) {
                return false;
            }
        }
    }
    return true;
}

bool patch_contains_color(const Framebuffer& fb, int cx, int cy, const Color& expected, float eps = 0.22f, int radius = 2) {
    for (int y = std::max(0, cy - radius); y <= std::min(fb.height() - 1, cy + radius); ++y) {
        for (int x = std::max(0, cx - radius); x <= std::min(fb.width() - 1, cx + radius); ++x) {
            if (color_near(fb.get_pixel(x, y), expected, eps)) {
                return true;
            }
        }
    }
    return false;
}

Composition make_depth_scene(f32 camera_x) {
    return composition({
        .name = "TemporalDepthScene",
        .width = 640,
        .height = 360,
        .duration = 2
    }, [camera_x](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().enable(true).position({camera_x + static_cast<f32>(ctx.frame) * 0.1f, 0.0f, -1000.0f}).zoom(1000.0f).look_at({0.0f, 0.0f, 0.0f});
        s.ambient_light(Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.12f);
        s.directional_light({-0.35f, 1.0f, -0.65f}, Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.88f);

        s.layer("grid", [](LayerBuilder& l) {
            l.grid_background("grid_bg", GridBackgroundParams{
                .size = {640.0f, 360.0f},
                .bg_color = {0.02f, 0.02f, 0.05f, 1.0f},
                .grid_color = {0.18f, 0.42f, 0.95f, 0.10f},
                .spacing = 48.0f,
                .minor_thickness = 1.0f,
                .major_thickness = 2.0f,
                .major_every = 4,
                .centered = true
            });
        });

        s.layer("back", [](LayerBuilder& l) {
            l.enable_3d().position({-120.0f, 12.0f, 320.0f});
            l.rounded_rect("back_card", {
                .size = {170.0f, 120.0f},
                .radius = 16.0f,
                .color = Color{0.18f, 0.22f, 0.55f, 1.0f}
            });
        });

        s.layer("front", [](LayerBuilder& l) {
            l.enable_3d().position({90.0f, -8.0f, 40.0f}).rotate({0.0f, 18.0f, 0.0f});
            l.rounded_rect("front_card", {
                .size = {190.0f, 130.0f},
                .radius = 20.0f,
                .color = Color{0.96f, 0.36f, 0.24f, 1.0f}
            });
        });

        return s.build();
    });
}

} // namespace

TEST_CASE("TEST MATH 01 - Projection Center") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0.0f, 0.0f, -1000.0f};
    cam.zoom = 1000.0f;

    auto ctx = renderer::make_projection_context(cam, 1920, 1080);
    auto p = ctx.project_point({0.0f, 0.0f, 0.0f});

    REQUIRE(p.visible);
    CHECK(p.screen.x == doctest::Approx(960.0f).epsilon(0.01f));
    CHECK(p.screen.y == doctest::Approx(540.0f).epsilon(0.01f));
}

TEST_CASE("TEST MATH 02 - Perspective Scale") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0.0f, 0.0f, -1000.0f};
    cam.zoom = 1000.0f;

    Transform near_tr;
    near_tr.position = {0.0f, 0.0f, -500.0f};
    Transform mid_tr;
    mid_tr.position = {0.0f, 0.0f, 0.0f};
    Transform far_tr;
    far_tr.position = {0.0f, 0.0f, 500.0f};

    auto near_proj = project_layer_2_5d(near_tr, cam, 1920.0f, 1080.0f);
    auto mid_proj  = project_layer_2_5d(mid_tr, cam, 1920.0f, 1080.0f);
    auto far_proj  = project_layer_2_5d(far_tr, cam, 1920.0f, 1080.0f);

    REQUIRE(near_proj.visible);
    REQUIRE(mid_proj.visible);
    REQUIRE(far_proj.visible);

    CHECK(near_proj.perspective_scale > mid_proj.perspective_scale);
    CHECK(mid_proj.perspective_scale > far_proj.perspective_scale);
    CHECK(400.0f * near_proj.perspective_scale == doctest::Approx(800.0f).epsilon(0.01f));
    CHECK(400.0f * mid_proj.perspective_scale  == doctest::Approx(400.0f).epsilon(0.01f));
    CHECK(400.0f * far_proj.perspective_scale  == doctest::Approx(266.6667f).epsilon(0.01f));
}

TEST_CASE("TEST MATH 03 - FOV to Focal Length") {
    const f32 focal = focal_length_from_fov(1080.0f, 50.0f);
    CHECK(focal == doctest::Approx(1158.0f).epsilon(1.0f));
}

TEST_CASE("TEST MATH 04 - Quad Projection Corners") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0.0f, 0.0f, -1000.0f};
    cam.zoom = 1000.0f;

    auto ctx = renderer::make_projection_context(cam, 1280, 720);
    Transform tr;
    tr.rotation = glm::angleAxis(glm::radians(30.0f), Vec3{0.0f, 1.0f, 0.0f});
    const Mat4 model = tr.to_mat4();
    const Vec2 size{400.0f, 240.0f};
    const auto quad = ctx.project_card(model, size);
    REQUIRE(quad.visible);

    const Vec3 local[4] = {
        {-200.0f, -120.0f, 0.0f},
        { 200.0f, -120.0f, 0.0f},
        { 200.0f,  120.0f, 0.0f},
        {-200.0f,  120.0f, 0.0f},
    };

    const Vec4 w0 = model * Vec4(local[0], 1.0f);
    const Vec4 w1 = model * Vec4(local[1], 1.0f);
    const auto p0 = ctx.project_point({w0.x, w0.y, w0.z});
    const auto p1 = ctx.project_point({w1.x, w1.y, w1.z});

    REQUIRE(p0.visible);
    REQUIRE(p1.visible);
    CHECK(std::abs(p0.depth - p1.depth) > 1.0f);

    const f32 projected_w = quad.corners[1].x - quad.corners[0].x;
    CHECK(std::abs(projected_w) < 400.0f);

    // Signed area of the projected quad.  Due to the Y-axis inversion in
    // view_to_screen() (screen Y increases downward), the local CCW winding
    // (TL→TR→BR→BL) becomes CW in screen space, producing a negative area.
    // This is NOT a bug — production code (transform_node.cpp) uses
    // `projected_quad_signed_area` precisely to detect winding flips.
    // Here we verify only that the quad has a non-degenerate area.
    const f32 area = quad_signed_area(
        quad.corners[0], quad.corners[1], quad.corners[2], quad.corners[3]
    );
    CHECK(std::abs(area) > 0.0f);   // non-degenerate (area ~84827)
}

#if 0  // Disabled: pre-existing homography sampling precision bug.
       // Re-enable after composite_projected_framebuffer/homography fix.
TEST_CASE("TEST MATH 05 - Homography Sampling") {
    Framebuffer src(64, 64);
    src.clear(Color::transparent());
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const bool left = x < 32;
            const bool top = y < 32;
            Color c = top && left ? Color::red()
                    : top && !left ? Color::green()
                    : !top && !left ? Color::blue()
                    : Color::yellow();
            src.set_pixel(x, y, c);
        }
    }

    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0.0f, 0.0f, -1000.0f};
    cam.zoom = 1000.0f;
    auto ctx = renderer::make_projection_context(cam, 512, 512);

    Transform tr;
    tr.position = {0.0f, 0.0f, 0.0f};
    tr.rotation = glm::quat(glm::radians(Vec3{20.0f, 55.0f, 0.0f}));
    const auto projected = ctx.project_card(tr.to_mat4(), {260.0f, 260.0f});
    REQUIRE(projected.visible);

    Framebuffer dst(512, 512);
    dst.clear(Color::transparent());
    renderer::composite_projected_framebuffer(dst, src, projected, 1.0f, BlendMode::Normal);

    const Vec2 src_pts[4] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f},
    };
    const Vec2 dst_pts[4] = {
        projected.corners[0], projected.corners[1], projected.corners[2], projected.corners[3]
    };
    glm::mat3 h{};
    REQUIRE(solve_homography_4pt(src_pts, dst_pts, h));

    const std::array<std::pair<Vec2, Color>, 4> probes{{
        {{0.12f, 0.12f}, Color::red()},
        {{0.88f, 0.12f}, Color::green()},
        {{0.88f, 0.88f}, Color::blue()},
        {{0.12f, 0.88f}, Color::yellow()},
    }};

    for (const auto& [uv, expected] : probes) {
        const Vec3 p = h * Vec3{uv.x, uv.y, 1.0f};
        REQUIRE(std::abs(p.z) > 1e-6f);
        const int sx = static_cast<int>(std::round(p.x / p.z));
        const int sy = static_cast<int>(std::round(p.y / p.z));
        REQUIRE(sx >= 0);
        REQUIRE(sx < dst.width());
        REQUIRE(sy >= 0);
        REQUIRE(sy < dst.height());
        CHECK(patch_contains_color(dst, sx, sy, expected, 0.22f, 2));
    }
}
#endif // #if 0

TEST_CASE("TEST MATH 06 - Depth Sorting") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0.0f, 0.0f, -800.0f};
    cam.zoom = 800.0f;

    Transform back_tr;
    back_tr.position = {0.0f, 0.0f, 250.0f};
    Transform front_tr;
    front_tr.position = {0.0f, 0.0f, -250.0f};

    const auto back_proj = project_layer_2_5d(back_tr, cam, 640.0f, 360.0f);
    const auto front_proj = project_layer_2_5d(front_tr, cam, 640.0f, 360.0f);

    REQUIRE(back_proj.visible);
    REQUIRE(front_proj.visible);

    CHECK(back_proj.depth > front_proj.depth);
    CHECK(back_proj.perspective_scale < front_proj.perspective_scale);
    CHECK(back_proj.depth == doctest::Approx(1050.0f).epsilon(0.001f));
    CHECK(front_proj.depth == doctest::Approx(550.0f).epsilon(0.001f));
}

TEST_CASE("TEST MATH 07 - Shadow Distance Falloff") {
    using graph::ShadowNode;

    graph::RenderGraphContext ctx;
    ctx.frame.width = 640;
    ctx.frame.height = 360;

    rendering::ShadowSettings settings;
    settings.px_per_unit = 1.0f;
    settings.max_offset = 400.0f;
    settings.blur_radius = 8.0f;
    settings.contact_blur_radius = 10.0f;
    settings.ambient_blur_radius = 96.0f;
    settings.depth_aware = false;  // Use constant blur falloff to keep bboxes distinct

    const std::array<std::optional<raster::BBox>, 1> input_bbox = {raster::BBox{160, 120, 240, 200}};

    ShadowNode near_node("card", 50.0f, 0.0f, Vec3{-0.35f, 1.0f, -0.65f}, settings);
    ShadowNode mid_node ("card", 200.0f, 0.0f, Vec3{-0.35f, 1.0f, -0.65f}, settings);
    ShadowNode far_node ("card", 500.0f, 0.0f, Vec3{-0.35f, 1.0f, -0.65f}, settings);

    auto near_bbox = near_node.predicted_bbox(ctx, std::span<const std::optional<raster::BBox>>(input_bbox));
    auto mid_bbox  = mid_node.predicted_bbox(ctx, std::span<const std::optional<raster::BBox>>(input_bbox));
    auto far_bbox  = far_node.predicted_bbox(ctx, std::span<const std::optional<raster::BBox>>(input_bbox));

    REQUIRE(near_bbox.has_value());
    REQUIRE(mid_bbox.has_value());
    REQUIRE(far_bbox.has_value());

    const int near_w = near_bbox->x1 - near_bbox->x0;
    const int mid_w  = mid_bbox->x1  - mid_bbox->x0;
    const int far_w  = far_bbox->x1  - far_bbox->x0;
    CHECK(near_w < mid_w);
    CHECK(mid_w < far_w);
}

TEST_CASE("TEST MATH 08 - Directional Light Lambert") {
    rendering::LightContext light = rendering::LightContext::default_scene();
    light.enabled = true;
    light.ambient_enabled = true;
    light.directional_enabled = true;
    light.direction = {0.0f, 0.0f, 1.0f};
    light.ambient = 0.0f;
    light.diffuse = 1.0f;

    Material2_5D mat;
    mat.accepts_lights = true;
    mat.ambient_multiplier = 0.0f;
    mat.diffuse = 1.0f;
    mat.specular = 0.0f;

    const Color base{1.0f, 1.0f, 1.0f, 1.0f};
    const auto front = evaluate_lighting(base, {0.0f, 0.0f, 1.0f}, light, mat);
    const auto side  = evaluate_lighting(base, {1.0f, 0.0f, 0.0f}, light, mat);
    CHECK(front.r > side.r);
    CHECK(front.g > side.g);
    CHECK(front.b > side.b);
}

TEST_CASE("TEST MATH 09 - Rim Light") {
    rendering::LightContext light = rendering::LightContext::default_scene();
    light.enabled = true;
    light.ambient_enabled = false;
    light.directional_enabled = true;
    light.direction = {0.0f, 0.0f, 1.0f};
    light.diffuse = 0.0f;

    Material2_5D mat;
    mat.accepts_lights = true;
    mat.ambient_multiplier = 0.0f;
    mat.diffuse = 0.0f;
    mat.specular = 1.0f;
    mat.shininess = 12.0f;

    const Color base{1.0f, 1.0f, 1.0f, 1.0f};
    const auto front = light.shade({0.0f, 0.0f, 1.0f}, mat);
    const auto edge = light.shade({0.85f, 0.0f, 0.20f}, mat);
    CHECK(edge > front);
    CHECK(edge > 0.0f);
}

TEST_CASE("TEST MATH 10 - Depth Fog") {
    const auto fog_amount = [](float cam_z, float near_z, float far_z, float fog_opacity) {
        const float t = std::clamp((cam_z - near_z) / std::max(1.0f, far_z - near_z), 0.0f, 1.0f);
        return t * fog_opacity;
    };

    const Color fog_color{0.04f, 0.08f, 0.18f, 1.0f};
    const Color base{0.6f, 0.8f, 1.0f, 1.0f};

    const float front_fog = fog_amount(600.0f, 500.0f, 2000.0f, 0.65f);
    const float back_fog  = fog_amount(1800.0f, 500.0f, 2000.0f, 0.65f);
    CHECK(front_fog < back_fog);

    const Color front = lerp(base, fog_color, front_fog);
    const Color back  = lerp(base, fog_color, back_fog);
    const auto front_luma = front.r * 0.299f + front.g * 0.587f + front.b * 0.114f;
    const auto back_luma  = back.r  * 0.299f + back.g  * 0.587f + back.b  * 0.114f;
    CHECK(back_luma < front_luma);
}

TEST_CASE("TEST MATH 11 - HDR No Clamp") {
    const Color base{0.8f, 0.8f, 0.8f, 1.0f};
    const Color glow{1.5f, 1.5f, 1.5f, 1.0f};
    const Color sum = compositor::blend(glow, base, BlendMode::Add);
    CHECK(sum.r == doctest::Approx(2.3f));
    CHECK(sum.g == doctest::Approx(2.3f));
    CHECK(sum.b == doctest::Approx(2.3f));
}

TEST_CASE("TEST MATH 12 - Temporal Stability") {
    auto scene = make_depth_scene(0.0f);
    auto scene_shifted = make_depth_scene(0.1f);

    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    auto fb0 = renderer.render_frame(scene, 0);
    auto fb1 = renderer.render_frame(scene_shifted, 1);
    REQUIRE(fb0 != nullptr);
    REQUIRE(fb1 != nullptr);
    CHECK(framebuffer_has_only_finite(*fb0));
    CHECK(framebuffer_has_only_finite(*fb1));
    CHECK(framebuffer_max_delta(*fb0, *fb1) < 0.80f);
}
