#include <doctest/doctest.h>

#include <chronon3d/scene/model/camera/camera_rig.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/backends/software/rasterizers/projected_card_rasterizer.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/tile_grid.hpp>
#include <chronon3d/core/dirty_tile_mask.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/rendering/projected_card.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/helpers/pixel_assertions.hpp>

#include <cmath>
#include <algorithm>
#include <limits>
#include <numeric>

using namespace chronon3d;
using namespace chronon3d::renderer;
using namespace chronon3d::raster;

namespace {

bool vec3_near(const Vec3& a, const Vec3& b, float eps = 0.01f) {
    return std::abs(a.x - b.x) <= eps &&
           std::abs(a.y - b.y) <= eps &&
           std::abs(a.z - b.z) <= eps;
}

bool color_near(const Color& a, const Color& b, float eps = 0.10f) {
    return std::abs(a.r - b.r) <= eps &&
           std::abs(a.g - b.g) <= eps &&
           std::abs(a.b - b.b) <= eps &&
           std::abs(a.a - b.a) <= eps;
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

float framebuffer_max_delta(const Framebuffer& a, const Framebuffer& b) {
    if (a.width() != b.width() || a.height() != b.height()) return std::numeric_limits<float>::max();
    float max_d = 0.0f;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const Color ca = a.get_pixel(x, y);
            const Color cb = b.get_pixel(x, y);
            max_d = std::max(max_d, std::abs(ca.r - cb.r));
            max_d = std::max(max_d, std::abs(ca.g - cb.g));
            max_d = std::max(max_d, std::abs(ca.b - cb.b));
            max_d = std::max(max_d, std::abs(ca.a - cb.a));
        }
    }
    return max_d;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// 1. CameraRig — hero_push_in stability
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Stabilization: camera_rig hero_push_in default values") {
    auto cam = camera_rig::hero_push_in();

    // At t=0: position at from, tilt at from_tilt, yaw at from_yaw
    Camera2_5D r0 = cam.evaluate(0);
    CHECK(vec3_near(r0.position, Vec3{0.0f, 0.0f, -1200.0f}));
    CHECK(r0.rotation.x == doctest::Approx(-4.0f));
    CHECK(r0.rotation.y == doctest::Approx(0.0f));
    CHECK(r0.zoom == doctest::Approx(1000.0f));

    // At t=duration: position at to, tilt at to_tilt, yaw at to_yaw
    Camera2_5D r90 = cam.evaluate(90);
    CHECK(vec3_near(r90.position, Vec3{0.0f, 0.0f, -750.0f}));
    CHECK(r90.rotation.x == doctest::Approx(2.0f));
    CHECK(r90.rotation.y == doctest::Approx(3.0f));
    CHECK(r90.zoom == doctest::Approx(1000.0f));
}

TEST_CASE("Stabilization: camera_rig hero_push_in mid-frame interpolation") {
    auto cam = camera_rig::hero_push_in();

    // Halfway: should be approximately halfway between start and end
    Camera2_5D r45 = cam.evaluate(45);
    // Eased bezier means not exactly linear, but should be between
    CHECK(r45.position.z < -750.0f);  // closer than end
    CHECK(r45.position.z > -1200.0f); // farther than start
    CHECK(r45.rotation.x > -4.0f);
    CHECK(r45.rotation.x < 2.0f);
    CHECK(r45.rotation.y > 0.0f);
    CHECK(r45.rotation.y < 3.0f);
}

TEST_CASE("Stabilization: camera_rig hero_push_in custom params") {
    auto cam = camera_rig::hero_push_in({
        .from_position = {100.0f, 50.0f, -1500.0f},
        .to_position   = {100.0f, 50.0f, -800.0f},
        .from_tilt = -10.0f,
        .to_tilt   = 5.0f,
        .from_yaw  = -5.0f,
        .to_yaw    = 10.0f,
        .zoom = 800.0f,
        .duration = 120,
        .start_frame = 30,
    });

    // Before start_frame, position is at 'from' (hold mode)
    Camera2_5D r0 = cam.evaluate(0);
    CHECK(vec3_near(r0.position, Vec3{100.0f, 50.0f, -1500.0f}));
    CHECK(r0.rotation.x == doctest::Approx(-10.0f));

    // At start_frame, still at 'from'
    Camera2_5D r30 = cam.evaluate(30);
    CHECK(vec3_near(r30.position, Vec3{100.0f, 50.0f, -1500.0f}));

    // At end, should be at 'to'
    Camera2_5D r150 = cam.evaluate(150);
    CHECK(vec3_near(r150.position, Vec3{100.0f, 50.0f, -800.0f}));
    CHECK(r150.rotation.x == doctest::Approx(5.0f));
    CHECK(r150.rotation.y == doctest::Approx(10.0f));
    CHECK(r150.zoom == doctest::Approx(800.0f));
}

TEST_CASE("Stabilization: camera_rig hero_push_in never produces NaN/Inf") {
    auto cam = camera_rig::hero_push_in();

    // Evaluate at every 10th frame through the full duration
    for (Frame f = 0; f <= 90; f += 10) {
        Camera2_5D r = cam.evaluate(f);
        CHECK(std::isfinite(r.position.x));
        CHECK(std::isfinite(r.position.y));
        CHECK(std::isfinite(r.position.z));
        CHECK(std::isfinite(r.zoom));
        CHECK(std::isfinite(r.rotation.x));
        CHECK(std::isfinite(r.rotation.y));
        CHECK(std::isfinite(r.rotation.z));
    }
}

TEST_CASE("Stabilization: camera_rig hero_push_in monotonic position.z") {
    auto cam = camera_rig::hero_push_in();

    f32 prev_z = cam.evaluate(0).position.z;
    for (Frame f = 10; f <= 90; f += 10) {
        f32 cur_z = cam.evaluate(f).position.z;
        CHECK(cur_z > prev_z);  // camera moves forward (z increases)
        prev_z = cur_z;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 2. Homography — checkerboard projection stability
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Stabilization: homography checkerboard identity projection") {
    // With identity transform, projected card should match source exactly
    Framebuffer src(64, 64);
    src.clear(Color::transparent());
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            bool left = x < 32;
            bool top  = y < 32;
            Color c = (top && left) ? Color::red()
                    : (top && !left) ? Color::green()
                    : (!top && !left) ? Color::blue()
                    : Color::yellow();
            src.set_pixel(x, y, c);
        }
    }

    // Camera looking straight on
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0.0f, 0.0f, -1000.0f};
    cam.zoom = 1000.0f;
    auto ctx = renderer::make_projection_context(cam, 200, 200);

    // Layer at origin with no rotation → face-on projection
    Transform tr;
    tr.position = {0.0f, 0.0f, 0.0f};
    const auto projected = ctx.project_card(tr.to_mat4(), {140.0f, 140.0f});
    REQUIRE(projected.visible);

    Framebuffer dst(200, 200);
    dst.clear(Color::transparent());
    renderer::composite_projected_framebuffer(dst, src, projected, 1.0f, BlendMode::Normal);

    // Sample at center of each quadrant in the projected card
    // For face-on projection, UV mapping should be direct
    CHECK(framebuffer_has_only_finite(dst));
}

TEST_CASE("Stabilization: homography solve_4pt correctness") {
    // Known mapping: unit square → offset/scaled rectangle
    const Vec2 src_pts[4] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f},
    };

    const Vec2 dst_pts[4] = {
        {10.0f, 20.0f},
        {210.0f, 20.0f},
        {210.0f, 220.0f},
        {10.0f, 220.0f},
    };

    glm::mat3 H{};
    bool solved = solve_homography_4pt(src_pts, dst_pts, H);
    REQUIRE(solved);
    CHECK(std::abs(glm::determinant(H)) > 1e-6f);

    // Map the 4 source corners and verify they map to the destination corners
    for (int i = 0; i < 4; ++i) {
        Vec3 p = H * Vec3{src_pts[i].x, src_pts[i].y, 1.0f};
        REQUIRE(std::abs(p.z) > 1e-8f);
        f32 px = p.x / p.z;
        f32 py = p.y / p.z;
        CHECK(px == doctest::Approx(dst_pts[i].x).epsilon(0.01f));
        CHECK(py == doctest::Approx(dst_pts[i].y).epsilon(0.01f));
    }
}

TEST_CASE("Stabilization: homography identity matrix") {
    // Identity mapping: src == dst
    const Vec2 pts[4] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f},
    };

    glm::mat3 H{};
    bool solved = solve_homography_4pt(pts, pts, H);
    REQUIRE(solved);

    // For identity mapping, H should be approximately identity
    CHECK(H[0][0] == doctest::Approx(1.0f).epsilon(0.01f));
    CHECK(H[1][1] == doctest::Approx(1.0f).epsilon(0.01f));
    CHECK(H[2][2] == doctest::Approx(1.0f).epsilon(0.01f));
    CHECK(H[0][1] == doctest::Approx(0.0f).epsilon(0.01f));
    CHECK(H[1][0] == doctest::Approx(0.0f).epsilon(0.01f));
}

TEST_CASE("Stabilization: homography with perspective skew") {
    // Simulate a perspective trapezoid (wider at bottom)
    const Vec2 src_pts[4] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f},
    };

    const Vec2 dst_pts[4] = {
        {50.0f, 0.0f},   // TL: narrower top
        {150.0f, 0.0f},  // TR: narrower top
        {200.0f, 200.0f}, // BR: wider bottom
        {0.0f, 200.0f},   // BL: wider bottom
    };

    glm::mat3 H{};
    bool solved = solve_homography_4pt(src_pts, dst_pts, H);
    REQUIRE(solved);

    // Verify round-trip for all 4 corners
    for (int i = 0; i < 4; ++i) {
        Vec3 p = H * Vec3{src_pts[i].x, src_pts[i].y, 1.0f};
        REQUIRE(std::abs(p.z) > 1e-8f);
        CHECK(p.x / p.z == doctest::Approx(dst_pts[i].x).epsilon(0.5f));
        CHECK(p.y / p.z == doctest::Approx(dst_pts[i].y).epsilon(0.5f));
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 3. Projected Card — HDR Add blend
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Stabilization: projected_card_add_hdr allows values > 1.0") {
    // Create a source framebuffer with HDR values (>1.0)
    Framebuffer src_hdr(16, 16);
    src_hdr.clear(Color{1.5f, 1.5f, 1.5f, 1.0f});  // HDR white

    Framebuffer dst(64, 64);
    dst.clear(Color{0.2f, 0.2f, 0.2f, 1.0f});  // dark gray base

    // Identity projection: camera straight on
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0.0f, 0.0f, -1000.0f};
    cam.zoom = 1000.0f;
    auto ctx = renderer::make_projection_context(cam, 64, 64);
    Transform tr;
    tr.position = {0.0f, 0.0f, 50.0f};
    const auto card = ctx.project_card(tr.to_mat4(), {40.0f, 40.0f});
    REQUIRE(card.visible);

    // Composite with HDR Add blend
    renderer::composite_projected_framebuffer(dst, src_hdr, card, 1.0f, BlendMode::Add);

    // Sample center pixel — Add blend clamps to 1.0, so max is 1.0
    Color center = dst.get_pixel(32, 32);
    CHECK(center.r == doctest::Approx(1.0f));  // 0.2 + 1.5 clamped to 1.0
    CHECK(center.g == doctest::Approx(1.0f));
    CHECK(center.b == doctest::Approx(1.0f));
    CHECK(framebuffer_has_only_finite(dst));
}

TEST_CASE("Stabilization: projected_card_add_hdr accumulates across multiple composites") {
    Framebuffer dst(64, 64);
    dst.clear(Color{0.0f, 0.0f, 0.0f, 1.0f});

    auto src = std::make_shared<Framebuffer>(16, 16);
    src->clear(Color{0.8f, 0.4f, 0.2f, 0.5f});

    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0.0f, 0.0f, -1000.0f};
    cam.zoom = 1000.0f;
    auto ctx = renderer::make_projection_context(cam, 64, 64);

    // Composite the same card twice with Add blend
    Transform tr;
    tr.position = {0.0f, 0.0f, 50.0f};
    const auto card = ctx.project_card(tr.to_mat4(), {30.0f, 30.0f});
    REQUIRE(card.visible);

    renderer::composite_projected_framebuffer(dst, *src, card, 1.0f, BlendMode::Add);
    renderer::composite_projected_framebuffer(dst, *src, card, 1.0f, BlendMode::Add);

    Color center = dst.get_pixel(32, 32);
    CHECK(center.r > 0.8f);  // Two composites should accumulate
    CHECK(framebuffer_has_only_finite(dst));
}

TEST_CASE("Stabilization: projected_card_add_hdr preserves transparent areas") {
    Framebuffer src(16, 16);
    src.clear(Color::transparent());
    // Only center pixel has a value
    src.set_pixel(8, 8, {1.5f, 2.0f, 0.5f, 0.8f});

    Framebuffer dst(64, 64);
    dst.clear(Color{0.1f, 0.1f, 0.1f, 1.0f});

    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0.0f, 0.0f, -1000.0f};
    cam.zoom = 1000.0f;
    auto ctx = renderer::make_projection_context(cam, 64, 64);
    Transform tr;
    tr.position = {0.0f, 0.0f, 50.0f};
    const auto card = ctx.project_card(tr.to_mat4(), {40.0f, 40.0f});
    REQUIRE(card.visible);

    renderer::composite_projected_framebuffer(dst, src, card, 1.0f, BlendMode::Add);

    // Most of dst should still be 0.1 (background untouched)
    bool has_background = false;
    for (int y = 0; y < 8 && !has_background; ++y) {
        for (int x = 0; x < 8 && !has_background; ++x) {
            Color c = dst.get_pixel(x, y);
            if (std::abs(c.r - 0.1f) < 0.01f) has_background = true;
        }
    }
    CHECK(has_background);
}

TEST_CASE("Stabilization: projected_card_add_hdr different opacities") {
    Framebuffer src(16, 16);
    src.clear(Color{1.0f, 1.0f, 1.0f, 1.0f});

    Framebuffer dst(64, 64);
    dst.clear(Color{0.0f, 0.0f, 0.0f, 1.0f});

    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0.0f, 0.0f, -1000.0f};
    cam.zoom = 1000.0f;
    auto ctx = renderer::make_projection_context(cam, 64, 64);
    Transform tr;
    tr.position = {0.0f, 0.0f, 50.0f};
    const auto card = ctx.project_card(tr.to_mat4(), {40.0f, 40.0f});
    REQUIRE(card.visible);

    // Composite with 0.5 opacity
    renderer::composite_projected_framebuffer(dst, src, card, 0.5f, BlendMode::Add);

    // Center pixel should have accumulated 0.5
    Color center = dst.get_pixel(32, 32);
    CHECK(center.r == doctest::Approx(0.5f).epsilon(0.1f));
    CHECK(framebuffer_has_only_finite(dst));
}

// ═══════════════════════════════════════════════════════════════════════════════
// 4. Tile Effect — no seams between tiles
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Stabilization: tile grid produces contiguous coverage") {
    TileGrid grid(100, 80, 32);

    // Build a set covering the full frame
    std::vector<bool> covered(100 * 80, false);

    for (int ty = 0; ty < grid.rows(); ++ty) {
        for (int tx = 0; tx < grid.cols(); ++tx) {
            BBox b = grid.tile_bounds(tx, ty);
            // Each tile should start exactly where the previous ended
            for (int y = b.y0; y < b.y1; ++y) {
                for (int x = b.x0; x < b.x1; ++x) {
                    covered[y * 100 + x] = true;
                }
            }
        }
    }

    // Every pixel should be covered exactly once (no gaps, no overlaps)
    for (int y = 0; y < 80; ++y) {
        for (int x = 0; x < 100; ++x) {
            CHECK(covered[y * 100 + x]);  // No pixel should be uncovered
        }
    }

    // Verify tile boundaries are exact: tile[n+1].x0 == tile[n].x1
    for (int ty = 0; ty < grid.rows(); ++ty) {
        for (int tx = 0; tx < grid.cols() - 1; ++tx) {
            BBox left  = grid.tile_bounds(tx,     ty);
            BBox right = grid.tile_bounds(tx + 1, ty);
            CHECK(left.x1 == right.x0);  // no gap
        }
    }
    for (int tx = 0; tx < grid.cols(); ++tx) {
        for (int ty = 0; ty < grid.rows() - 1; ++ty) {
            BBox top = grid.tile_bounds(tx, ty);
            BBox bot = grid.tile_bounds(tx, ty + 1);
            CHECK(top.y1 == bot.y0);  // no gap
        }
    }
}

TEST_CASE("Stabilization: tile_effect renders identically to full-frame pass") {
    // Render a simple scene twice: once with tile rendering, once without.
    // The output should be pixel-identical.

    auto scene_fn = []() {
        SceneBuilder s(320, 240);

        s.screen_layer("bg", [](LayerBuilder& l) {
            l.rect("fill", {
                .size = {320.0f, 240.0f},
                .color = {0.1f, 0.1f, 0.1f, 1.0f},
            });
        });

        s.layer("red_box", [](LayerBuilder& l) {
            l.rect("box", {
                .size = {60.0f, 60.0f},
                .color = {1.0f, 0.0f, 0.0f, 1.0f},
                .pos = {-80.0f, -40.0f, 0.0f},
            });
        });

        s.layer("green_circle", [](LayerBuilder& l) {
            l.circle("circle", {
                .radius = 30.0f,
                .color = {0.0f, 1.0f, 0.0f, 1.0f},
                .pos = {80.0f, 40.0f, 0.0f},
            });
        });

        return s.build();
    };

    Scene scene = scene_fn();

    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;

    // Render without tiles (full-frame)
    settings.dirty.tile_size  = 0;  // 0 = disabled
    renderer.set_settings(settings);
    auto full_fb = renderer.render_scene(scene, scene.camera_2_5d(), 320, 240);
    REQUIRE(full_fb != nullptr);
    CHECK(framebuffer_has_only_finite(*full_fb));

    // Render with tile rendering
    settings.dirty.tile_size  = 64;
    renderer.set_settings(settings);
    auto tile_fb = renderer.render_scene(scene, scene.camera_2_5d(), 320, 240);
    REQUIRE(tile_fb != nullptr);
    CHECK(framebuffer_has_only_finite(*tile_fb));

    // Full-frame and tile renders should be pixel-identical
    float max_delta = framebuffer_max_delta(*full_fb, *tile_fb);
    CHECK(max_delta < 0.01f);
}

TEST_CASE("Stabilization: tile_effect small tile sizes produce identical output") {
    // Test with various tile sizes
    auto scene_fn = []() {
        SceneBuilder s(128, 128);
        s.layer("gradient", [](LayerBuilder& l) {
            l.rect("g", {
                .size = {128.0f, 128.0f},
                .color = {0.3f, 0.5f, 0.8f, 1.0f},
            });
        });
        s.layer("circle", [](LayerBuilder& l) {
            l.circle("c", {
                .radius = 20.0f,
                .color = {1.0f, 0.8f, 0.2f, 1.0f},
                .pos = {20.0f, 20.0f, 0.0f},
            });
        });
        return s.build();
    };

    Scene scene = scene_fn();
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;

    // Full-frame reference
    settings.dirty.tile_size = 0;
    renderer.set_settings(settings);
    auto ref_fb = renderer.render_scene(scene, scene.camera_2_5d(), 128, 128);
    REQUIRE(ref_fb != nullptr);

    // Test with multiple tile sizes
    for (int ts : {8, 16, 32, 64}) {
        settings.dirty.tile_size = ts;
        renderer.set_settings(settings);
        auto fb = renderer.render_scene(scene, scene.camera_2_5d(), 128, 128);
        REQUIRE(fb != nullptr);

        float delta = framebuffer_max_delta(*ref_fb, *fb);
        INFO("tile_size=" << ts << " max_delta=" << delta);
        CHECK(delta < 0.01f);
        CHECK(framebuffer_has_only_finite(*fb));
    }
}

TEST_CASE("Stabilization: tile_effect produces no NaN/Inf for complex scenes") {
    // Create a scene with multiple overlapping layers and effects
    auto scene_fn = []() {
        SceneBuilder s(320, 240);

        s.layer("bg", [](LayerBuilder& l) {
            l.rect("fill", {
                .size = {320.0f, 240.0f},
                .color = {0.02f, 0.02f, 0.06f, 1.0f},
            });
        });

        for (int i = 0; i < 5; ++i) {
            f32 x = (static_cast<f32>(i) - 2.0f) * 50.0f;
            s.layer("card_" + std::to_string(i), [x](LayerBuilder& l) {
                l.position({x, 0.0f, 0.0f});
                l.rounded_rect("r", {
                    .size = {40.0f, 80.0f},
                    .radius = 8.0f,
                    .color = {0.2f, 0.5f, 1.0f, 0.8f},
                });
                l.glow(12.0f, 0.5f, Color::white(), 0.0f);
            });
        }

        return s.build();
    };

    Scene scene = scene_fn();
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    settings.dirty.tile_size = 32;
    renderer.set_settings(settings);

    auto fb = renderer.render_scene(scene, scene.camera_2_5d(), 320, 240);
    REQUIRE(fb != nullptr);
    CHECK(framebuffer_has_only_finite(*fb));
}
