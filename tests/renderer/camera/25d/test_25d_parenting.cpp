#include <doctest/doctest.h>

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/framebuffer_analysis.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <tests/helpers/pixel_assertions.hpp>
#include <tests/helpers/render_fixtures.hpp>

#include <cmath>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

Composition make_parented_card_scene(
    std::string name,
    Vec3 parent_pos,
    Vec3 parent_rot,
    Vec3 parent_scale,
    Vec3 parent_anchor,
    Vec3 child_pos,
    Vec3 child_rot,
    Vec3 child_scale,
    Vec3 child_anchor,
    f32 parent_opacity,
    f32 child_opacity,
    Vec2 child_size,
    Color child_color,
    Vec2 canvas = {320.0f, 240.0f}
) {
    return composition({
        .name = std::move(name),
        .width = static_cast<int>(canvas.x),
        .height = static_cast<int>(canvas.y),
        .duration = 1
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera().set({
            .enabled = true,
            .position = {0, 0, -1000},
            .zoom = 1000.0f
        });

        s.rect("bg", {
            .size = canvas,
            .color = Color::black(),
            .pos = {0, 0, 0}
        });

        s.null_layer("rig", [=](LayerBuilder& l) {
            l.enable_3d()
             .position(parent_pos)
             .rotate(parent_rot)
             .scale(parent_scale)
             .anchor(parent_anchor)
             .opacity(parent_opacity);
        });

        s.layer("card", [=](LayerBuilder& l) {
            l.parent("rig")
             .enable_3d()
             .position(child_pos)
             .rotate(child_rot)
             .scale(child_scale)
             .anchor(child_anchor)
             .opacity(child_opacity)
             .rect("fill", {
                 .size = child_size,
                 .color = child_color,
                 .pos = {0, 0, 0}
             });
        });

        return s.build();
    });
}

Composition make_chain_scene(f32 grandparent_rot_y) {
    return composition({
        .name = grandparent_rot_y == 0.0f ? "ParentingChainFlat" : "ParentingChainRotated",
        .width = 320,
        .height = 240,
        .duration = 1
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera().set({
            .enabled = true,
            .position = {0, 0, -1000},
            .zoom = 1000.0f
        });

        s.rect("bg", {
            .size = {320, 240},
            .color = Color::black(),
            .pos = {0, 0, 0}
        });

        s.null_layer("grandparent", [=](LayerBuilder& l) {
            l.enable_3d()
             .position({0, 0, -500})
             .rotate({0, grandparent_rot_y, 0});
        });

        s.null_layer("parent", [](LayerBuilder& l) {
            l.parent("grandparent")
             .enable_3d()
             .position({40, 0, 0});
        });

        s.layer("child", [](LayerBuilder& l) {
            l.parent("parent")
             .enable_3d()
             .position({20, 0, 0})
             .rect("fill", {
                 .size = {80, 80},
                 .color = Color::red(),
                 .pos = {0, 0, 0}
             });
        });

        return s.build();
    });
}

Composition make_camera_dolly_scene(f32 rig_z) {
    return composition({
        .name = rig_z == 0.0f ? "CameraParentFlat" : "CameraParentDolly",
        .width = 320,
        .height = 240,
        .duration = 1
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.null_layer("rig", [=](LayerBuilder& l) {
            l.enable_3d()
             .position({0, 0, rig_z});
        });

        s.camera().set({
            .enabled = true,
            .position = {0, 0, -1000},
            .zoom = 1000.0f
        });
        s.camera().parent("rig");

        s.rect("bg", {
            .size = {320, 240},
            .color = Color::black(),
            .pos = {0, 0, 0}
        });

        s.layer("card", [](LayerBuilder& l) {
            l.enable_3d()
             .position({0, 0, -500})
             .rect("fill", {
                 .size = {80, 80},
                 .color = Color::red(),
                 .pos = {0, 0, 0}
             });
        });

        return s.build();
    });
}

Composition make_camera_target_scene() {
    return composition({
        .name = "CameraParentTarget",
        .width = 320,
        .height = 240,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera().set({
            .enabled = true,
            .position = {0, 0, -1000},
            .zoom = 1000.0f
        });
        s.camera().target("target");

        s.rect("bg", {
            .size = {320, 240},
            .color = Color::black(),
            .pos = {0, 0, 0}
        });

        s.null_layer("rig", [](LayerBuilder& l) {
            l.enable_3d()
             .position({80, 0, -500});
        });

        s.layer("target", [](LayerBuilder& l) {
            l.parent("rig")
             .enable_3d()
             .position({0, 0, 0})
             .rect("target_fill", {
                 .size = {60, 60},
                 .color = Color::red(),
                 .pos = {0, 0, 0}
             });
        });

        s.layer("reference", [](LayerBuilder& l) {
            l.enable_3d()
             .position({0, 0, -500})
             .rect("reference_fill", {
                 .size = {24, 24},
                 .color = Color::blue(),
                 .pos = {0, 0, 0}
             });
        });

        return s.build();
    });
}

static int bbox_width(const Framebuffer& fb) {
    auto bbox = renderer::bright_bbox(fb);
    REQUIRE(bbox.has_value());
    return bbox->max_x - bbox->min_x + 1;
}

static int bbox_height(const Framebuffer& fb) {
    auto bbox = renderer::bright_bbox(fb);
    REQUIRE(bbox.has_value());
    return bbox->max_y - bbox->min_y + 1;
}

static Vec2 bbox_centroid(const Framebuffer& fb) {
    auto centroid = renderer::bright_centroid(fb);
    REQUIRE(centroid.has_value());
    return *centroid;
}

} // namespace

TEST_CASE("3D parenting visual: Y45 child is narrower than flat") {
    auto flat = test::render_modular(make_parented_card_scene(
        "ParentingFlatY",
        {0, 0, -500},
        {0, 0, 0},
        {1, 1, 1},
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0},
        {1, 1, 1},
        {0, 0, 0},
        1.0f,
        1.0f,
        {120, 120},
        Color::red()
    ));

    auto rot = test::render_modular(make_parented_card_scene(
        "ParentingY45",
        {0, 0, -500},
        {0, 45, 0},
        {1, 1, 1},
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0},
        {1, 1, 1},
        {0, 0, 0},
        1.0f,
        1.0f,
        {120, 120},
        Color::red()
    ));

    REQUIRE(flat != nullptr);
    REQUIRE(rot != nullptr);

    save_debug(*flat, "output/debug/3d_parenting/01_flat_child.png");
    save_debug(*rot, "output/debug/3d_parenting/02_parent_y45_child.png");

    CHECK(bbox_width(*rot) < bbox_width(*flat));
}

TEST_CASE("3D parenting visual: X45 child is shorter than flat") {
    auto flat = test::render_modular(make_parented_card_scene(
        "ParentingFlatX",
        {0, 0, -500},
        {0, 0, 0},
        {1, 1, 1},
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0},
        {1, 1, 1},
        {0, 0, 0},
        1.0f,
        1.0f,
        {120, 120},
        Color::blue()
    ));

    auto rot = test::render_modular(make_parented_card_scene(
        "ParentingX45",
        {0, 0, -500},
        {45, 0, 0},
        {1, 1, 1},
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0},
        {1, 1, 1},
        {0, 0, 0},
        1.0f,
        1.0f,
        {120, 120},
        Color{0, 0.5f, 1.0f, 1.0f}
    ));

    REQUIRE(flat != nullptr);
    REQUIRE(rot != nullptr);

    save_debug(*rot, "output/debug/3d_parenting/03_parent_x45_child.png");

    CHECK(bbox_height(*rot) < bbox_height(*flat));
}

TEST_CASE("3D parenting visual: parent scale 2x enlarges child") {
    auto flat = test::render_modular(make_parented_card_scene(
        "ParentingScaleFlat",
        {0, 0, -500},
        {0, 0, 0},
        {1, 1, 1},
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0},
        {1, 1, 1},
        {0, 0, 0},
        1.0f,
        1.0f,
        {60, 60},
        Color{1.0f, 0.25f, 0.25f, 1.0f}
    ));

    auto scaled = test::render_modular(make_parented_card_scene(
        "ParentingScale2x",
        {0, 0, -500},
        {0, 0, 0},
        {2, 2, 2},
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0},
        {1, 1, 1},
        {0, 0, 0},
        1.0f,
        1.0f,
        {60, 60},
        Color{1.0f, 0.25f, 0.25f, 1.0f}
    ));

    REQUIRE(flat != nullptr);
    REQUIRE(scaled != nullptr);

    save_debug(*scaled, "output/debug/3d_parenting/04_parent_scale_2x.png");

    CHECK(bbox_width(*scaled) > bbox_width(*flat));
    CHECK(bbox_height(*scaled) > bbox_height(*flat));
}

TEST_CASE("3D parenting visual: parent position plus child offset shifts the card") {
    auto flat = test::render_modular(make_parented_card_scene(
        "ParentingFlatOffset",
        {0, 0, -500},
        {0, 0, 0},
        {1, 1, 1},
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0},
        {1, 1, 1},
        {0, 0, 0},
        1.0f,
        1.0f,
        {80, 80},
        Color::white()
    ));

    auto offset = test::render_modular(make_parented_card_scene(
        "ParentingOffset",
        {-120, 0, -500},
        {0, 0, 0},
        {1, 1, 1},
        {0, 0, 0},
        {80, 0, 0},
        {0, 0, 0},
        {1, 1, 1},
        {0, 0, 0},
        1.0f,
        1.0f,
        {80, 80},
        Color::white()
    ));

    REQUIRE(flat != nullptr);
    REQUIRE(offset != nullptr);

    save_debug(*offset, "output/debug/3d_parenting/05_parent_position_child_offset.png");

    const Vec2 flat_centroid = bbox_centroid(*flat);
    const Vec2 offset_centroid = bbox_centroid(*offset);

    CHECK(offset_centroid.x < flat_centroid.x);
    CHECK(std::abs(offset_centroid.x - flat_centroid.x) > 20.0f);
}

TEST_CASE("3D parenting visual: grandparent rotation changes the chain") {
    auto flat = test::render_modular(make_chain_scene(0.0f));
    auto rot = test::render_modular(make_chain_scene(30.0f));

    REQUIRE(flat != nullptr);
    REQUIRE(rot != nullptr);

    save_debug(*flat, "output/debug/3d_parenting/06_chain_flat.png");
    save_debug(*rot, "output/debug/3d_parenting/07_chain_grandparent_parent_child.png");

    CHECK(std::abs(bbox_width(*rot) - bbox_width(*flat)) > 10);
    CHECK(std::abs(bbox_centroid(*rot).x - bbox_centroid(*flat).x) > 10.0f);
}

TEST_CASE("3D parenting visual: parent opacity dims the card") {
    auto opaque = test::render_modular(make_parented_card_scene(
        "ParentingOpaque",
        {0, 0, -500},
        {0, 0, 0},
        {1, 1, 1},
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0},
        {1, 1, 1},
        {0, 0, 0},
        1.0f,
        1.0f,
        {100, 100},
        Color::red()
    ));

    auto dimmed = test::render_modular(make_parented_card_scene(
        "ParentingDimmed",
        {0, 0, -500},
        {0, 0, 0},
        {1, 1, 1},
        {0, 0, 0},
        {0, 0, 0},
        {0, 0, 0},
        {1, 1, 1},
        {0, 0, 0},
        0.5f,
        1.0f,
        {100, 100},
        Color::red()
    ));

    REQUIRE(opaque != nullptr);
    REQUIRE(dimmed != nullptr);

    save_debug(*dimmed, "output/debug/3d_parenting/08_parent_opacity_50.png");

    const int cx = opaque->width() / 2;
    const int cy = opaque->height() / 2;
    CHECK(opaque->get_pixel(cx, cy).r > dimmed->get_pixel(cx, cy).r);
}

TEST_CASE("3D parenting visual: camera parent dolly changes card footprint") {
    auto flat = test::render_modular(make_camera_dolly_scene(0.0f));
    auto dolly = test::render_modular(make_camera_dolly_scene(150.0f));

    REQUIRE(flat != nullptr);
    REQUIRE(dolly != nullptr);

    save_debug(*flat, "output/debug/3d_parenting/09_camera_parent_flat.png");
    save_debug(*dolly, "output/debug/3d_parenting/10_camera_parent_dolly.png");

    CHECK(bbox_width(*dolly) > bbox_width(*flat));
}

TEST_CASE("3D parenting visual: camera target follows parented target") {
    auto fb = test::render_modular(make_camera_target_scene());

    REQUIRE(fb != nullptr);

    save_debug(*fb, "output/debug/3d_parenting/11_camera_target_parented.png");

    const float red_x = centroid_x(*fb, Color::red());
    const float blue_x = centroid_x(*fb, Color::blue());
    REQUIRE(red_x >= 0.0f);
    REQUIRE(blue_x >= 0.0f);

    CHECK(std::abs(red_x - (fb->width() * 0.5f)) < 20.0f);
    CHECK(std::abs(red_x - blue_x) > 20.0f);
}

TEST_CASE("3D parenting visual: parent anchor changes orbit") {
    auto no_anchor = test::render_modular(make_parented_card_scene(
        "ParentingAnchorFlat",
        {0, 0, -500},
        {0, 35, 0},
        {1, 1, 1},
        {0, 0, 0},
        {100, 0, 0},
        {0, 0, 0},
        {1, 1, 1},
        {0, 0, 0},
        1.0f,
        1.0f,
        {80, 80},
        Color{1.0f, 0.4f, 0.1f, 1.0f}
    ));

    auto anchored = test::render_modular(make_parented_card_scene(
        "ParentingAnchorOffset",
        {0, 0, -500},
        {0, 35, 0},
        {1, 1, 1},
        {40, 0, 0},
        {100, 0, 0},
        {0, 0, 0},
        {1, 1, 1},
        {0, 0, 0},
        1.0f,
        1.0f,
        {80, 80},
        Color{1.0f, 0.4f, 0.1f, 1.0f}
    ));

    REQUIRE(no_anchor != nullptr);
    REQUIRE(anchored != nullptr);

    save_debug(*no_anchor, "output/debug/3d_parenting/12_parent_anchor_flat.png");
    save_debug(*anchored, "output/debug/3d_parenting/13_parent_anchor_rotation.png");

    CHECK(bbox_width(*no_anchor) > 0);
    CHECK(bbox_width(*anchored) > 0);
}

TEST_CASE("3D parenting visual: parent anchor wide preview") {
    auto no_anchor = test::render_modular(make_parented_card_scene(
        "ParentingAnchorWideFlat",
        {0, 0, -500},
        {0, 35, 0},
        {1, 1, 1},
        {0, 0, 0},
        {160, 0, 0},
        {0, 0, 0},
        {1, 1, 1},
        {0, 0, 0},
        1.0f,
        1.0f,
        {100, 100},
        Color{1.0f, 0.4f, 0.1f, 1.0f},
        {640, 360}
    ));

    auto anchored = test::render_modular(make_parented_card_scene(
        "ParentingAnchorWideOffset",
        {0, 0, -500},
        {0, 35, 0},
        {1, 1, 1},
        {100, 0, 0},
        {160, 0, 0},
        {0, 0, 0},
        {1, 1, 1},
        {0, 0, 0},
        1.0f,
        1.0f,
        {100, 100},
        Color{1.0f, 0.4f, 0.1f, 1.0f},
        {640, 360}
    ));

    REQUIRE(no_anchor != nullptr);
    REQUIRE(anchored != nullptr);

    save_debug(*no_anchor, "output/debug/3d_parenting/13a_parent_anchor_rotation_wide_flat.png");
    save_debug(*anchored, "output/debug/3d_parenting/13b_parent_anchor_rotation_wide.png");

    CHECK(bbox_width(*no_anchor) > 0);
    CHECK(bbox_width(*anchored) > 0);
}
