// test_camera25d_graph.cpp
// Unit tests for Camera2_5D projection in the modular graph (--graph) path.
// Each test verifies a specific 2.5D property: z-sort, depth scale, parallax,
// and invariance of 2D layers under camera movement.

#include <doctest/doctest.h>
#include <chronon3d/renderer/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>

using namespace chronon3d;

namespace {

SoftwareRenderer make_graph_renderer() {
    SoftwareRenderer r;
    RenderSettings s;
    s.use_modular_graph = true;
    r.set_settings(s);
    return r;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────
// Z-sort: near red should composite on top of far blue.
// Layers are inserted out of depth order (near first, far second).
// After projection + sort the far layer must be composited first (below).
// ─────────────────────────────────────────────────────────────────────────
TEST_CASE("Camera2_5D Graph: z-sort — near layer composites on top of far layer") {
    Composition comp = composition({
        .name     = "ZSortTest",
        .width    = 200,
        .height   = 200,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera_2_5d({
            .enabled  = true,
            .position = {0, 0, -1000},
            .zoom     = 1000.0f
        });

        // Background fill
        s.rect("bg", {
            .size = {200, 200},
            .color = Color{0, 0, 0, 1},
            .pos   = {100, 100, 0}
        });

        // NEAR red square (z = -300) — inserted first but should render last (on top).
        s.layer("near-red", [](LayerBuilder& l) {
            l.enable_3d().position({0, 0, -300});
            l.rect("r", {
                 .size  = {60, 60},
                 .color = Color{1, 0, 0, 1},
                 .pos   = {0, 0, 0}
             });
        });

        // FAR blue square (z = +600) — inserted second but should render first (below).
        s.layer("far-blue", [](LayerBuilder& l) {
            l.enable_3d().position({0, 0, 600});
            l.rect("b", {
                 .size  = {60, 60},
                 .color = Color{0, 0, 1, 1},
                 .pos   = {0, 0, 0}
            });
        });

        return s.build();
    });

    auto r = make_graph_renderer();
    auto fb = r.render_frame(comp, 0);
    REQUIRE(fb != nullptr);

    // Center pixel (100, 100) must be red (near), not blue (far).
    Color c = fb->get_pixel(100, 100);
    CHECK(c.r > 0.5f);
    CHECK(c.b < 0.3f);
}

// ─────────────────────────────────────────────────────────────────────────
// Depth scale: a layer at z=+600 must appear smaller than one at z=0.
// With zoom=1000, depth=1600 → scale≈0.625; depth=1000 → scale=1.0.
// The far card's projected size should be smaller, so fewer pixels are filled.
// ─────────────────────────────────────────────────────────────────────────
TEST_CASE("Camera2_5D Graph: depth scale — far layer is visually smaller") {
    // Two compositions: one with Subject (z=0), one with Background (z=+1200).
    auto make_comp = [](f32 layer_z, const std::string& name) {
        return composition({
            .name     = name,
            .width    = 300,
            .height   = 300,
            .duration = 1
        }, [layer_z](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera_2_5d({
                .enabled  = true,
                .position = {0, 0, -1000},
                .zoom     = 1000.0f
            });
            s.layer("card", [layer_z](LayerBuilder& l) {
                l.enable_3d().position({150, 150, layer_z});
                l.rect("r", {
                    .size  = {100, 100},
                    .color = Color{1, 1, 1, 1},
                    .pos   = {0, 0, 0}
                });
            });
            return s.build();
        });
    };

    SoftwareRenderer r = make_graph_renderer();

    auto fb_subject = r.render_frame(make_comp(0.0f,    "SubjectScale"), 0);
    auto fb_far     = r.render_frame(make_comp(1200.0f, "FarScale"), 0);
    REQUIRE(fb_subject != nullptr);
    REQUIRE(fb_far != nullptr);

    // Count lit pixels in each framebuffer.
    auto count_lit = [](const Framebuffer& fb) {
        int count = 0;
        for (int y = 0; y < fb.height(); ++y)
            for (int x = 0; x < fb.width(); ++x)
                if (fb.get_pixel(x, y).a > 0.5f) ++count;
        return count;
    };

    int lit_subject = count_lit(*fb_subject);
    int lit_far     = count_lit(*fb_far);

    // Far layer must cover fewer pixels than subject layer.
    CHECK(lit_far < lit_subject);
    // Far layer (depth=2200, scale≈0.45) should be roughly 45%² ≈ 20% of subject area.
    // We just check it's meaningfully smaller (< 70% of subject area).
    CHECK(lit_far < static_cast<int>(lit_subject * 0.70f));
}

// ─────────────────────────────────────────────────────────────────────────
// Parallax: camera panned right displaces a near layer more than a far layer.
// ─────────────────────────────────────────────────────────────────────────
TEST_CASE("Camera2_5D Graph: parallax — near layer shifts more than far layer") {
    // Helper: render a single 3D layer at given world Z and camera X.
    // Returns the X-centroid of all lit pixels in the 300x300 canvas.
    auto render_centroid_x = [](f32 layer_z, f32 cam_x) -> f32 {
        auto comp = composition({
            .name     = "ParallaxTest",
            .width    = 300,
            .height   = 300,
            .duration = 1
        }, [layer_z, cam_x](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera_2_5d({
                .enabled  = true,
                .position = {cam_x, 0, -1000},
                .zoom     = 1000.0f
            });
            // Layer always at world X=150, so it starts centered.
            s.layer("item", [layer_z](LayerBuilder& l) {
                l.enable_3d().position({150, 150, layer_z});
                l.rect("r", {
                    .size  = {40, 40},
                    .color = Color{1, 1, 1, 1},
                    .pos   = {0, 0, 0}
                });
            });
            return s.build();
        });

        SoftwareRenderer r;
        RenderSettings rs;
        rs.use_modular_graph = true;
        r.set_settings(rs);
        auto fb = r.render_frame(comp, 0);

        f32 sum_x = 0.0f;
        int count  = 0;
        for (int y = 0; y < 300; ++y)
            for (int x = 0; x < 300; ++x)
                if (fb->get_pixel(x, y).a > 0.5f) { sum_x += x; ++count; }
        return count > 0 ? sum_x / count : 150.0f;
    };

    // Camera panned 150px to the right.
    const f32 cx_near_at_0    = render_centroid_x(-400.0f,   0.0f);
    const f32 cx_near_panned  = render_centroid_x(-400.0f, 150.0f);
    const f32 cx_far_at_0     = render_centroid_x(+800.0f,   0.0f);
    const f32 cx_far_panned   = render_centroid_x(+800.0f, 150.0f);

    const f32 shift_near = cx_near_at_0 - cx_near_panned; // positive = moved left in screen
    const f32 shift_far  = cx_far_at_0  - cx_far_panned;

    // Both should shift in the same direction.
    CHECK(shift_near > 0.0f);
    CHECK(shift_far  > 0.0f);

    // Near layer (depth=600) should shift more than far layer (depth=1800).
    // Ratio ≈ 1800/600 = 3×.
    CHECK(shift_near > shift_far * 1.5f);
}

// ─────────────────────────────────────────────────────────────────────────
// 2D layer invariance: a layer without enable_3d() must not shift when
// the camera pans.
// ─────────────────────────────────────────────────────────────────────────
TEST_CASE("Camera2_5D Graph: 2D layer is unaffected by camera pan") {
    auto render_2d_centroid = [](f32 cam_x) -> f32 {
        auto comp = composition({
            .name     = "InvarianceTest",
            .width    = 300,
            .height   = 300,
            .duration = 1
        }, [cam_x](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera_2_5d({
                .enabled  = true,
                .position = {cam_x, 0, -1000},
                .zoom     = 1000.0f
            });
            // 2D layer — no enable_3d()
            s.layer("hud", [](LayerBuilder& l) {
                l.position({150, 150, 0});
                l.rect("box", {
                    .size  = {40, 40},
                    .color = Color{1, 1, 0, 1},
                    .pos   = {0, 0, 0}
                });
            });
            return s.build();
        });

        SoftwareRenderer r;
        RenderSettings rs;
        rs.use_modular_graph = true;
        r.set_settings(rs);
        auto fb = r.render_frame(comp, 0);

        f32 sum_x = 0.0f;
        int count  = 0;
        for (int y = 0; y < 300; ++y)
            for (int x = 0; x < 300; ++x)
                if (fb->get_pixel(x, y).a > 0.5f) { sum_x += x; ++count; }
        return count > 0 ? sum_x / count : 150.0f;
    };

    const f32 cx_0   = render_2d_centroid(0.0f);
    const f32 cx_pan = render_2d_centroid(200.0f);

    // 2D HUD must remain at the same screen position.
    CHECK(std::abs(cx_0 - cx_pan) < 2.0f);  // ≤ 2px tolerance
}

// ─────────────────────────────────────────────────────────────────────────
// FOV mode: using camera_fov() should produce the same result as using
// a zoom derived from the same FOV angle.
// At FOV=50° and height=300, focal ≈ (150)/tan(25°) ≈ 321.7 pixels.
// ─────────────────────────────────────────────────────────────────────────
TEST_CASE("Camera2_5D Graph: FOV projection mode matches equivalent zoom") {
    const f32 h       = 300.0f;
    const f32 fov_deg = 50.0f;
    const f32 expected_focal = focal_length_from_fov(h, fov_deg);

    // Render with FOV mode.
    auto comp_fov = composition({
        .name = "FovMode", .width = 300, .height = 300, .duration = 1
    }, [fov_deg](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.enable_camera_2_5d()
         .camera_position({0, 0, -1000})
         .camera_fov(fov_deg);
        s.layer("item", [](LayerBuilder& l) {
            l.enable_3d().position({150, 150, 0});
            l.rect("r", { .size = {60, 60}, .color = Color{1, 1, 1, 1}, .pos = {0, 0, 0} });
        });
        return s.build();
    });

    // Render with equivalent Zoom mode.
    auto comp_zoom = composition({
        .name = "ZoomMode", .width = 300, .height = 300, .duration = 1
    }, [expected_focal](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera_2_5d({
            .enabled  = true,
            .position = {0, 0, -1000},
            .zoom     = expected_focal
        });
        s.layer("item", [](LayerBuilder& l) {
            l.enable_3d().position({150, 150, 0});
            l.rect("r", { .size = {60, 60}, .color = Color{1, 1, 1, 1}, .pos = {0, 0, 0} });
        });
        return s.build();
    });

    SoftwareRenderer r = make_graph_renderer();
    auto fb_fov  = r.render_frame(comp_fov,  0);
    auto fb_zoom = r.render_frame(comp_zoom, 0);
    REQUIRE(fb_fov != nullptr);
    REQUIRE(fb_zoom != nullptr);

    // Both renders should produce very similar output.
    // Compare lit pixel counts.
    auto count_lit = [](const Framebuffer& fb) {
        int n = 0;
        for (int y = 0; y < fb.height(); ++y)
            for (int x = 0; x < fb.width(); ++x)
                if (fb.get_pixel(x, y).a > 0.5f) ++n;
        return n;
    };

    const int lit_fov  = count_lit(*fb_fov);
    const int lit_zoom = count_lit(*fb_zoom);

    // Allow ≤ 5% difference (rounding from floating-point focal length).
    const f32 diff = std::abs(static_cast<f32>(lit_fov - lit_zoom));
    const f32 max_allowed = static_cast<f32>(std::max(lit_fov, lit_zoom)) * 0.05f;
    CHECK(diff <= max_allowed + 1.0f);
}
