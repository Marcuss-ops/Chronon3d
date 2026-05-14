#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

using namespace chronon3d;

static const Color k_bg        = Color{0.07f, 0.07f, 0.10f, 1.0f};
static const Color k_front     = Color{0.96f, 0.95f, 0.91f, 1.0f};
static const Color k_side      = Color{0.48f, 0.40f, 0.28f, 1.0f};
static const char* k_bold      = "assets/fonts/Inter-Bold.ttf";
static const char* k_regular   = "assets/fonts/Inter-Regular.ttf";

// Common 3/4-view camera: slightly above-left-front
static void setup_camera(SceneBuilder& s) {
    s.camera_2_5d({
        .enabled                  = true,
        .position                 = {-80.0f, 140.0f, -820.0f},
        .point_of_interest        = {0.0f, 0.0f, 0.0f},
        .point_of_interest_enabled = true,
        .projection_mode          = Camera2_5DProjectionMode::Fov,
        .fov_deg                  = 36.0f
    });
}

static void bg(SceneBuilder& s) {
    s.layer("bg", [](LayerBuilder& l) {
        l.rect("fill", {.size = {9999, 9999}, .color = k_bg});
    });
}

// ── 01  front face sharp, centered, not deformed ─────────────────────────────
Composition ExtrudedText01FrontClean() {
    return composition({ .name = "ExtrudedText01FrontClean", .width = 600, .height = 600, .duration = 1 },
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera_2_5d({ .enabled = true, .position = {0, 0, -820.0f},
                        .point_of_interest = {0, 0, 0}, .point_of_interest_enabled = true,
                        .projection_mode = Camera2_5DProjectionMode::Fov, .fov_deg = 36.0f });
        bg(s);
        s.layer("text", [](LayerBuilder& l) {
            l.enable_3d();
            FakeExtrudedTextParams tp;
            tp.text          = "CHRONON3D";
            tp.pos           = {-380, 0, 0};
            tp.font_path     = k_bold;
            tp.font_size     = 100.0f;
            tp.depth         = 1;
            tp.extrude_z_step = 0.0f;
            tp.front_color   = k_front;
            tp.side_color    = k_side;
            tp.align         = TextAlign::Left;
            l.fake_extruded_text("t", tp);
        });
        return s.build();
    });
}

// ── 02  side extrusion depth visible, follows glyph shapes ───────────────────
Composition ExtrudedText02ExtrusionDepth() {
    return composition({ .name = "ExtrudedText02ExtrusionDepth", .width = 600, .height = 600, .duration = 1 },
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        setup_camera(s);
        bg(s);
        s.layer("text", [](LayerBuilder& l) {
            l.enable_3d();
            l.rotate({8.0f, -28.0f, 0.0f});
            FakeExtrudedTextParams tp;
            tp.text           = "DEPTH";
            tp.pos            = {-280, 0, 0};
            tp.font_path      = k_bold;
            tp.font_size      = 160.0f;
            tp.depth          = 20;
            tp.extrude_z_step = 3.5f;
            tp.front_color    = k_front;
            tp.side_color     = k_side;
            tp.align          = TextAlign::Left;
            l.fake_extruded_text("t", tp);
        });
        return s.build();
    });
}

// ── 03  glyph holes remain open (B, O, A, R, D) ──────────────────────────────
Composition ExtrudedText03GlyphHoles() {
    return composition({ .name = "ExtrudedText03GlyphHoles", .width = 600, .height = 600, .duration = 1 },
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        setup_camera(s);
        bg(s);
        s.layer("text", [](LayerBuilder& l) {
            l.enable_3d();
            l.rotate({8.0f, -25.0f, 0.0f});
            FakeExtrudedTextParams tp;
            tp.text           = "BOARD";
            tp.pos            = {-320, 0, 0};
            tp.font_path      = k_bold;
            tp.font_size      = 160.0f;
            tp.depth          = 18;
            tp.extrude_z_step = 3.0f;
            tp.front_color    = k_front;
            tp.side_color     = k_side;
            tp.align          = TextAlign::Left;
            l.fake_extruded_text("t", tp);
        });
        return s.build();
    });
}

// ── 04  bevel edges follow contours, no broken geometry ──────────────────────
Composition ExtrudedText04BevelEdges() {
    return composition({ .name = "ExtrudedText04BevelEdges", .width = 600, .height = 600, .duration = 1 },
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        setup_camera(s);
        bg(s);
        s.layer("text", [](LayerBuilder& l) {
            l.enable_3d();
            l.rotate({8.0f, -28.0f, 0.0f});
            FakeExtrudedTextParams tp;
            tp.text           = "BEVEL";
            tp.pos            = {-290, 0, 0};
            tp.font_path      = k_bold;
            tp.font_size      = 160.0f;
            tp.depth          = 18;
            tp.extrude_z_step = 3.0f;
            tp.bevel_size     = 5.0f;
            tp.front_color    = k_front;
            tp.side_color     = k_side;
            tp.align          = TextAlign::Left;
            l.fake_extruded_text("t", tp);
        });
        return s.build();
    });
}

// ── 05  accented glyphs positioned correctly and extrude cleanly ─────────────
Composition ExtrudedText05AccentsExtruded() {
    return composition({ .name = "ExtrudedText05AccentsExtruded", .width = 600, .height = 600, .duration = 1 },
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        setup_camera(s);
        bg(s);
        s.layer("text", [](LayerBuilder& l) {
            l.enable_3d();
            l.rotate({8.0f, -22.0f, 0.0f});
            FakeExtrudedTextParams tp;
            tp.text           = "ÈLITE ÀREA";  // ÈLITE ÀREA
            tp.pos            = {-340, 0, 0};
            tp.font_path      = k_bold;
            tp.font_size      = 110.0f;
            tp.depth          = 16;
            tp.extrude_z_step = 3.0f;
            tp.front_color    = k_front;
            tp.side_color     = k_side;
            tp.align          = TextAlign::Left;
            l.fake_extruded_text("t", tp);
        });
        return s.build();
    });
}

// ── 06  front text correctly occludes back text (depth sorting) ───────────────
Composition ExtrudedText06DepthSorting() {
    return composition({ .name = "ExtrudedText06DepthSorting", .width = 600, .height = 600, .duration = 1 },
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera_2_5d({ .enabled = true, .position = {0, 100, -820.0f},
                        .point_of_interest = {0, 0, 0}, .point_of_interest_enabled = true,
                        .projection_mode = Camera2_5DProjectionMode::Fov, .fov_deg = 42.0f });
        bg(s);

        // "BACK" sits further away (positive Z = away from camera)
        s.layer("back_text", [](LayerBuilder& l) {
            l.enable_3d();
            l.rotate({12.0f, 0.0f, 0.0f});
            FakeExtrudedTextParams tp;
            tp.text           = "BACK";
            tp.pos            = {-220, 50, 120};
            tp.font_path      = k_bold;
            tp.font_size      = 200.0f;
            tp.depth          = 22;
            tp.extrude_z_step = 3.0f;
            tp.front_color    = Color{0.70f, 0.70f, 0.70f, 1.0f};
            tp.side_color     = Color{0.35f, 0.30f, 0.22f, 1.0f};
            tp.align          = TextAlign::Left;
            l.fake_extruded_text("t", tp);
        });

        // "FRONT" closer to camera (Z=0), should occlude BACK
        s.layer("front_text", [](LayerBuilder& l) {
            l.enable_3d();
            l.rotate({12.0f, 0.0f, 0.0f});
            FakeExtrudedTextParams tp;
            tp.text           = "FRONT";
            tp.pos            = {-270, -60, 0};
            tp.font_path      = k_bold;
            tp.font_size      = 200.0f;
            tp.depth          = 22;
            tp.extrude_z_step = 3.0f;
            tp.front_color    = k_front;
            tp.side_color     = k_side;
            tp.align          = TextAlign::Left;
            l.fake_extruded_text("t", tp);
        });
        return s.build();
    });
}

// ── 07  3D tilt and perspective visible, readability stable ──────────────────
Composition ExtrudedText07CameraTilt() {
    return composition({ .name = "ExtrudedText07CameraTilt", .width = 600, .height = 600, .duration = 1 },
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera_2_5d({ .enabled = true, .position = {0, 80, -820.0f},
                        .point_of_interest = {0, 0, 0}, .point_of_interest_enabled = true,
                        .projection_mode = Camera2_5DProjectionMode::Fov, .fov_deg = 36.0f });
        bg(s);
        s.layer("text", [](LayerBuilder& l) {
            l.enable_3d();
            l.rotate({22.0f, -35.0f, -8.0f});
            FakeExtrudedTextParams tp;
            tp.text           = "TILT";
            tp.pos            = {-210, 0, 0};
            tp.font_path      = k_bold;
            tp.font_size      = 210.0f;
            tp.depth          = 22;
            tp.extrude_z_step = 3.5f;
            tp.bevel_size     = 4.0f;
            tp.front_color    = k_front;
            tp.side_color     = k_side;
            tp.align          = TextAlign::Left;
            l.fake_extruded_text("t", tp);
        });
        return s.build();
    });
}

// ── 08  near-camera text clips cleanly without unstable geometry ─────────────
Composition ExtrudedText08NearClipping() {
    return composition({ .name = "ExtrudedText08NearClipping", .width = 600, .height = 600, .duration = 1 },
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        // Camera very close — some of the geometry will be inside the near plane
        s.camera_2_5d({ .enabled = true, .position = {0, 0, -160.0f},
                        .point_of_interest = {0, 0, 0}, .point_of_interest_enabled = true,
                        .projection_mode = Camera2_5DProjectionMode::Fov, .fov_deg = 55.0f });
        bg(s);
        s.layer("text", [](LayerBuilder& l) {
            l.enable_3d();
            FakeExtrudedTextParams tp;
            tp.text           = "CLOSE";
            tp.pos            = {-310, 0, 0};
            tp.font_path      = k_bold;
            tp.font_size      = 210.0f;
            tp.depth          = 18;
            tp.extrude_z_step = 3.0f;
            tp.front_color    = k_front;
            tp.side_color     = k_side;
            tp.align          = TextAlign::Left;
            l.fake_extruded_text("t", tp);
        });
        return s.build();
    });
}

// ── 09  spacing and extrusion consistent across a long word ──────────────────
Composition ExtrudedText09LongWordSpacing() {
    return composition({ .name = "ExtrudedText09LongWordSpacing", .width = 600, .height = 600, .duration = 1 },
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera_2_5d({ .enabled = true, .position = {0, 0, -820.0f},
                        .point_of_interest = {0, 0, 0}, .point_of_interest_enabled = true,
                        .projection_mode = Camera2_5DProjectionMode::Fov, .fov_deg = 36.0f });
        bg(s);
        s.layer("text", [](LayerBuilder& l) {
            l.enable_3d();
            FakeExtrudedTextParams tp;
            tp.text           = "CHRONONMOTIONENGINE";
            tp.pos            = {-550, 0, 0};
            tp.font_path      = k_bold;
            tp.font_size      = 52.0f;
            tp.depth          = 1;
            tp.extrude_z_step = 0.0f;
            tp.front_color    = k_front;
            tp.side_color     = k_side;
            tp.align          = TextAlign::Left;
            l.fake_extruded_text("t", tp);
        });
        return s.build();
    });
}

// ── 10  thin and bold glyph weights both extrude cleanly ─────────────────────
Composition ExtrudedText10ThinVsBold() {
    return composition({ .name = "ExtrudedText10ThinVsBold", .width = 600, .height = 600, .duration = 1 },
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        setup_camera(s);
        bg(s);

        // THIN (regular weight) — top half
        s.layer("thin", [](LayerBuilder& l) {
            l.enable_3d();
            l.rotate({8.0f, -22.0f, 0.0f});
            FakeExtrudedTextParams tp;
            tp.text           = "THIN";
            tp.pos            = {-190, -100, 0};
            tp.font_path      = k_regular;
            tp.font_size      = 200.0f;
            tp.depth          = 16;
            tp.extrude_z_step = 3.0f;
            tp.front_color    = k_front;
            tp.side_color     = k_side;
            tp.align          = TextAlign::Left;
            l.fake_extruded_text("t", tp);
        });

        // BOLD — bottom half
        s.layer("bold", [](LayerBuilder& l) {
            l.enable_3d();
            l.rotate({8.0f, -22.0f, 0.0f});
            FakeExtrudedTextParams tp;
            tp.text           = "BOLD";
            tp.pos            = {-210, 110, 0};
            tp.font_path      = k_bold;
            tp.font_size      = 200.0f;
            tp.depth          = 16;
            tp.extrude_z_step = 3.0f;
            tp.front_color    = k_front;
            tp.side_color     = k_side;
            tp.align          = TextAlign::Left;
            l.fake_extruded_text("t", tp);
        });
        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("ExtrudedText01FrontClean",       ExtrudedText01FrontClean)
CHRONON_REGISTER_COMPOSITION("ExtrudedText02ExtrusionDepth",   ExtrudedText02ExtrusionDepth)
CHRONON_REGISTER_COMPOSITION("ExtrudedText03GlyphHoles",       ExtrudedText03GlyphHoles)
CHRONON_REGISTER_COMPOSITION("ExtrudedText04BevelEdges",       ExtrudedText04BevelEdges)
CHRONON_REGISTER_COMPOSITION("ExtrudedText05AccentsExtruded",  ExtrudedText05AccentsExtruded)
CHRONON_REGISTER_COMPOSITION("ExtrudedText06DepthSorting",     ExtrudedText06DepthSorting)
CHRONON_REGISTER_COMPOSITION("ExtrudedText07CameraTilt",       ExtrudedText07CameraTilt)
CHRONON_REGISTER_COMPOSITION("ExtrudedText08NearClipping",     ExtrudedText08NearClipping)
CHRONON_REGISTER_COMPOSITION("ExtrudedText09LongWordSpacing",  ExtrudedText09LongWordSpacing)
CHRONON_REGISTER_COMPOSITION("ExtrudedText10ThinVsBold",       ExtrudedText10ThinVsBold)
