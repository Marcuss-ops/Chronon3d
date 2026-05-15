#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/templates/neon_badge_templates.hpp>

using namespace chronon3d;

static constexpr int kSZ = 800;

// ─── Common frame ────────────────────────────────────────────────────────────
// Dark navy background + subtle XY grid + corner brackets + title/check labels.
// 2D screen-space coords: origin = top-left, x right, y down.
// For kSZ=800: center=(400,400), corners at (0,0)..(800,800).
static void add_frame(SceneBuilder& s, const char* title, const char* check) {
    s.layer("_bg", [](LayerBuilder& l) {
        l.rect("r", {.size = {99999, 99999}, .color = Color{0.038f, 0.052f, 0.108f}});
    });
    // Subtle XY grid behind scene (3D layer, shows through Camera2_5D)
    s.layer("_grid", [](LayerBuilder& l) {
        l.enable_3d().position({0, 0, 600});
        GridPlaneParams gp;
        gp.axis    = PlaneAxis::XY;
        gp.extent  = 4000;
        gp.spacing = 80;
        gp.color   = Color{0.13f, 0.17f, 0.27f, 0.30f};
        l.grid_plane("g", gp);
    });
    // Title: top-left, pixel coords
    s.layer("_title", [title](LayerBuilder& l) {
        l.text("t", {
            .content = title,
            .style   = {.font_path = "assets/fonts/Inter-Regular.ttf",
                        .size = 16, .color = Color{0.88f, 0.88f, 0.88f, 1.f},
                        .align = TextAlign::Left},
            .pos = {14.f, 18.f, 0.f}
        });
    });
    // Check text: bottom-center, pixel coords
    s.layer("_check", [check](LayerBuilder& l) {
        l.text("t", {
            .content = check,
            .style   = {.font_path = "assets/fonts/Inter-Regular.ttf",
                        .size = 13, .color = Color{0.48f, 0.52f, 0.58f, 1.f},
                        .align = TextAlign::Center},
            .pos = {400.f, 772.f, 0.f}
        });
    });
    // Corner brackets: pixel coords, margin=8, arm=46
    s.layer("_corners", [](LayerBuilder& l) {
        const Color bc{0.28f, 0.40f, 0.60f, 0.70f};
        const float M = 8.f, S = kSZ - 8.f, A = 46.f;
        l.line("tl_h", {.from={M,M,0},   .to={M+A,M,0},   .thickness=1.5f, .color=bc});
        l.line("tl_v", {.from={M,M,0},   .to={M,M+A,0},   .thickness=1.5f, .color=bc});
        l.line("tr_h", {.from={S-A,M,0}, .to={S,M,0},     .thickness=1.5f, .color=bc});
        l.line("tr_v", {.from={S,M,0},   .to={S,M+A,0},   .thickness=1.5f, .color=bc});
        l.line("bl_h", {.from={M,S,0},   .to={M+A,S,0},   .thickness=1.5f, .color=bc});
        l.line("bl_v", {.from={M,S-A,0}, .to={M,S,0},     .thickness=1.5f, .color=bc});
        l.line("br_h", {.from={S-A,S,0}, .to={S,S,0},     .thickness=1.5f, .color=bc});
        l.line("br_v", {.from={S,S-A,0}, .to={S,S,0},     .thickness=1.5f, .color=bc});
    });
}

// Front-facing camera — straight-on, no perspective distortion
static Camera2_5D cam_front(float zoom = 700.f) {
    return {.enabled = true, .position = {0, 0, -700}, .point_of_interest = {0, 0, 0}, .zoom = zoom};
}

// Slightly elevated camera — shows the top face of extruded glyphs
static Camera2_5D cam_elevated(float zoom = 720.f) {
    return {.enabled = true, .position = {0, 180, -700}, .point_of_interest = {0, 0, 0}, .zoom = zoom};
}

// Default extrusion colours
static constexpr Color kFront{0.96f, 0.94f, 0.90f, 1.f};
static constexpr Color kSide {0.52f, 0.48f, 0.42f, 0.88f};

// ─── 01 Front text — flat face, no visible side ───────────────────────────
static Composition S3D_01_FrontTextClean() {
    return composition({.name="S3D_01_FrontTextClean", .width=kSZ, .height=kSZ, .duration=1},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera_2_5d(cam_front());
        add_frame(s, "01_front_text_clean",
                  "Check: front face is sharp, centered, and not deformed.");
        s.layer("text", [](LayerBuilder& l) {
            l.enable_3d().position({0, 20, 0});
            FakeExtrudedTextParams tp;
            tp.text            = "CHRONON3D";
            tp.font_size       = 90.f;
            tp.depth           = 0;          // flat — no side visible
            tp.extrude_z_step  = 0.f;
            tp.front_color     = kFront;
            tp.side_color      = kSide;
            tp.highlight_opacity = 0.12f;
            tp.bevel_size      = 0.f;
            l.fake_extruded_text("t", tp);
        });
        return s.build();
    });
}

// ─── 02 Extrusion side depth ─────────────────────────────────────────────────
static Composition S3D_02_ExtrusionSideDepth() {
    return composition({.name="S3D_02_ExtrusionSideDepth", .width=kSZ, .height=kSZ, .duration=1},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera_2_5d(cam_elevated());
        add_frame(s, "02_extrusion_side_depth",
                  "Check: side extrusion depth is visible and follows the glyph shapes cleanly.");
        s.layer("text", [](LayerBuilder& l) {
            l.enable_3d().position({0, 30, 0}).rotate({0, 15, 0});
            FakeExtrudedTextParams tp;
            tp.text            = "DEPTH";
            tp.font_size       = 148.f;
            tp.depth           = 38;
            tp.extrude_z_step  = 4.5f;
            tp.extrude_dir     = {0.3f, 0.f};
            tp.front_color     = kFront;
            tp.side_color      = kSide;
            tp.bevel_size      = 2.f;
            tp.highlight_opacity = 0.10f;
            l.fake_extruded_text("t", tp);
        });
        return s.build();
    });
}

// ─── 03 Glyph holes ──────────────────────────────────────────────────────────
static Composition S3D_03_ExtrusionGlyphHoles() {
    return composition({.name="S3D_03_ExtrusionGlyphHoles", .width=kSZ, .height=kSZ, .duration=1},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera_2_5d(cam_elevated());
        add_frame(s, "03_extrusion_glyph_holes",
                  "Check: glyph holes remain open and are not triangulated or filled incorrectly.");
        s.layer("text", [](LayerBuilder& l) {
            l.enable_3d().position({0, 30, 0}).rotate({0, 12, 0});
            FakeExtrudedTextParams tp;
            tp.text            = "BOARD";
            tp.font_size       = 145.f;
            tp.depth           = 30;
            tp.extrude_z_step  = 3.5f;
            tp.extrude_dir     = {0.3f, 0.f};
            tp.front_color     = kFront;
            tp.side_color      = kSide;
            tp.bevel_size      = 2.f;
            tp.highlight_opacity = 0.10f;
            l.fake_extruded_text("t", tp);
        });
        return s.build();
    });
}

// ─── 04 Bevel edges ──────────────────────────────────────────────────────────
static Composition S3D_04_BevelEdges() {
    return composition({.name="S3D_04_BevelEdges", .width=kSZ, .height=kSZ, .duration=1},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera_2_5d(cam_elevated());
        add_frame(s, "04_bevel_edges",
                  "Check: bevel edges follow contours cleanly with no broken geometry.");
        s.layer("text", [](LayerBuilder& l) {
            l.enable_3d().position({0, 30, 0}).rotate({0, 14, 0});
            FakeExtrudedTextParams tp;
            tp.text            = "BEVEL";
            tp.font_size       = 148.f;
            tp.depth           = 32;
            tp.extrude_z_step  = 4.f;
            tp.extrude_dir     = {0.3f, 0.f};
            tp.front_color     = kFront;
            tp.side_color      = kSide;
            tp.bevel_size      = 9.f;
            tp.highlight_opacity = 0.15f;
            l.fake_extruded_text("t", tp);
        });
        return s.build();
    });
}

// ─── 05 Accented glyphs ──────────────────────────────────────────────────────
static Composition S3D_05_AccentsExtruded() {
    return composition({.name="S3D_05_AccentsExtruded", .width=kSZ, .height=kSZ, .duration=1},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera_2_5d(cam_elevated());
        add_frame(s, "05_accents_extruded",
                  "Check: accented glyphs and marks are positioned correctly and extrude cleanly.");
        s.layer("text", [](LayerBuilder& l) {
            l.enable_3d().position({0, 20, 0}).rotate({0, 10, 0});
            FakeExtrudedTextParams tp;
            tp.text            = "\xC3\x88LITE \xC3\x80REA";  // UTF-8: ÈLITE ÀREA
            tp.font_size       = 88.f;
            tp.depth           = 26;
            tp.extrude_z_step  = 3.f;
            tp.extrude_dir     = {0.3f, 0.f};
            tp.front_color     = kFront;
            tp.side_color      = kSide;
            tp.bevel_size      = 2.5f;
            tp.highlight_opacity = 0.10f;
            l.fake_extruded_text("t", tp);
        });
        return s.build();
    });
}

// ─── 06 Depth sorting ────────────────────────────────────────────────────────
static Composition S3D_06_DepthSortingText() {
    return composition({.name="S3D_06_DepthSortingText", .width=kSZ, .height=kSZ, .duration=1},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera_2_5d(cam_elevated(740.f));
        add_frame(s, "06_depth_sorting_text",
                  "Check: front text correctly occludes back text according to depth sorting.");
        // BACK — same XY as FRONT, further from camera (z=150 → depth≈850 → rendered first)
        s.layer("back", [](LayerBuilder& l) {
            l.enable_3d().position({0, 0, 150});
            FakeExtrudedTextParams tp;
            tp.text            = "BACK";
            tp.font_size       = 135.f;
            tp.depth           = 30;
            tp.extrude_z_step  = 3.f;
            tp.extrude_dir     = {0.3f, 0.f};
            tp.front_color     = Color{0.78f, 0.76f, 0.72f, 1.f};
            tp.side_color      = Color{0.42f, 0.38f, 0.33f, 0.85f};
            tp.bevel_size      = 2.f;
            l.fake_extruded_text("t", tp);
        });
        // FRONT — same XY as BACK, closer to camera (z=-80 → depth≈620 → rendered last / on top)
        s.layer("front", [](LayerBuilder& l) {
            l.enable_3d().position({0, 0, -80});
            FakeExtrudedTextParams tp;
            tp.text            = "FRONT";
            tp.font_size       = 135.f;
            tp.depth           = 30;
            tp.extrude_z_step  = 3.f;
            tp.extrude_dir     = {0.3f, 0.f};
            tp.front_color     = kFront;
            tp.side_color      = kSide;
            tp.bevel_size      = 2.f;
            tp.highlight_opacity = 0.12f;
            l.fake_extruded_text("t", tp);
        });
        return s.build();
    });
}

// ─── 07 Camera tilt ──────────────────────────────────────────────────────────
static Composition S3D_07_CameraTiltText() {
    return composition({.name="S3D_07_CameraTiltText", .width=kSZ, .height=kSZ, .duration=1},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera_2_5d(cam_elevated(730.f));
        add_frame(s, "07_camera_tilt_text",
                  "Check: 3D tilt and perspective are visible while readability remains stable.");
        s.layer("text", [](LayerBuilder& l) {
            // X: top tilts away (top face visible). Y negative: right side faces camera (depth on right).
            // Z: gentle lean simulates italic slant. Large font fills the frame.
            l.enable_3d().position({0, 0, 0}).rotate({14, -22, -10});
            FakeExtrudedTextParams tp;
            tp.text            = "TILT";
            tp.font_size       = 240.f;
            tp.depth           = 14;          // depth_z = 14 * 3.5 = 49 ≈ 20% of font height
            tp.extrude_z_step  = 3.5f;
            tp.extrude_dir     = {0.3f, 0.f};
            tp.front_color     = kFront;
            tp.side_color      = kSide;
            tp.bevel_size      = 0.f;
            tp.highlight_opacity = 0.08f;
            l.fake_extruded_text("t", tp);
        });
        return s.build();
    });
}

// ─── 08 Near clipping ────────────────────────────────────────────────────────
static Composition S3D_08_NearClippingText() {
    return composition({.name="S3D_08_NearClippingText", .width=kSZ, .height=kSZ, .duration=1},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        // Camera at z=-700; text at z=-500 → depth=200 → ps=3.5 → clips at viewport edges
        s.camera_2_5d(cam_front(700.f));
        add_frame(s, "08_near_clipping_text",
                  "Check: near-camera text clips cleanly without unstable geometry.");
        s.layer("text", [](LayerBuilder& l) {
            l.enable_3d().position({0, 0, -500});
            FakeExtrudedTextParams tp;
            tp.text            = "CLOSING";
            tp.font_size       = 90.f;
            tp.depth           = 22;
            tp.extrude_z_step  = 3.f;
            tp.extrude_dir     = {0.3f, 0.f};
            tp.front_color     = kFront;
            tp.side_color      = kSide;
            tp.bevel_size      = 2.f;
            tp.highlight_opacity = 0.10f;
            l.fake_extruded_text("t", tp);
        });
        return s.build();
    });
}

// ─── 09 Long word spacing ─────────────────────────────────────────────────────
static Composition S3D_09_LongWordSpacing() {
    return composition({.name="S3D_09_LongWordSpacing", .width=kSZ, .height=kSZ, .duration=1},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera_2_5d(cam_front());
        add_frame(s, "09_long_word_spacing",
                  "Check: spacing and extrusion stay consistent across a long word.");
        s.layer("text", [](LayerBuilder& l) {
            l.enable_3d().position({0, 20, 0});
            FakeExtrudedTextParams tp;
            tp.text            = "CHRONONMOTIONENGINE";
            tp.font_size       = 58.f;
            tp.depth           = 0;
            tp.extrude_z_step  = 0.f;
            tp.front_color     = kFront;
            tp.side_color      = kSide;
            tp.bevel_size      = 0.f;
            tp.highlight_opacity = 0.08f;
            l.fake_extruded_text("t", tp);
        });
        return s.build();
    });
}

// ─── 10 Thin vs bold ─────────────────────────────────────────────────────────
static Composition S3D_10_ThinVsBold() {
    return composition({.name="S3D_10_ThinVsBold", .width=kSZ, .height=kSZ, .duration=1},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera_2_5d(cam_elevated(720.f));
        add_frame(s, "10_thin_vs_bold",
                  "Check: thin and bold glyph weights both extrude cleanly and stay readable.");
        s.layer("thin", [](LayerBuilder& l) {
            l.enable_3d().position({0, -95, 0}).rotate({5, 12, 0});
            FakeExtrudedTextParams tp;
            tp.text            = "THIN";
            tp.font_path       = "assets/fonts/Inter-Regular.ttf";
            tp.font_size       = 138.f;
            tp.depth           = 20;
            tp.extrude_z_step  = 3.f;
            tp.extrude_dir     = {0.3f, 0.f};
            tp.front_color     = Color{0.82f, 0.80f, 0.76f, 1.f};
            tp.side_color      = Color{0.42f, 0.38f, 0.32f, 0.82f};
            tp.bevel_size      = 1.5f;
            tp.highlight_opacity = 0.08f;
            l.fake_extruded_text("t", tp);
        });
        s.layer("bold", [](LayerBuilder& l) {
            l.enable_3d().position({0, 95, 0}).rotate({5, 12, 0});
            FakeExtrudedTextParams tp;
            tp.text            = "BOLD";
            tp.font_size       = 138.f;
            tp.depth           = 28;
            tp.extrude_z_step  = 3.5f;
            tp.extrude_dir     = {0.3f, 0.f};
            tp.front_color     = kFront;
            tp.side_color      = kSide;
            tp.bevel_size      = 3.f;
            tp.highlight_opacity = 0.13f;
            l.fake_extruded_text("t", tp);
        });
        return s.build();
    });
}

// ─── Tilt motion demo — dolly Z + light sweep ────────────────────────────────
static Composition TiltMotionDemo() {
    constexpr int kDur = 90;  // 3 s @ 30fps
    return composition({.name="TiltMotionDemo", .width=kSZ, .height=kSZ, .duration=kDur},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const float t = static_cast<float>(ctx.frame) / float(kDur - 1);  // 0→1 linear

        // Cubic ease-in-out: slow start, fast middle, slow end
        const float et = t < 0.5f
            ? 4.f * t * t * t
            : 1.f - (-2.f * t + 2.f) * (-2.f * t + 2.f) * (-2.f * t + 2.f) * 0.5f;

        // Dolly: camera moves from z=-1100 (far) to z=-600 (close) — no rotation
        const float cam_z   = -1100.f + 500.f * et;
        const float cam_zoom = std::abs(cam_z);  // keep apparent scale stable at mid-point
        Camera2_5D cam;
        cam.enabled           = true;
        cam.position          = {0.f, 160.f, cam_z};
        cam.point_of_interest = {0.f, 0.f, 0.f};
        cam.zoom              = cam_zoom;
        s.camera_2_5d(cam);

        // Background
        s.layer("_bg", [](LayerBuilder& l) {
            l.rect("r", {.size={99999, 99999}, .color=Color{0.038f, 0.052f, 0.108f}});
        });

        // Grid
        s.layer("_grid", [](LayerBuilder& l) {
            l.enable_3d().position({0, 0, 600});
            GridPlaneParams gp;
            gp.axis    = PlaneAxis::XY;
            gp.extent  = 4000;
            gp.spacing = 80;
            gp.color   = Color{0.13f, 0.17f, 0.27f, 0.20f};
            l.grid_plane("g", gp);
        });

        // Light sweep right → left (glint visible as specular peak moves across faces)
        const float lx = 0.7f - 1.4f * et;  // +0.7 → -0.7
        const float ly = 0.5f;
        const float lz = -0.5f;
        const float ll = std::sqrt(lx*lx + ly*ly + lz*lz);
        const Vec3 anim_light{lx/ll, ly/ll, lz/ll};

        // Text: X tilt shows top, Y shows right-side depth, Z lean adds italic feel
        s.layer("text", [anim_light](LayerBuilder& l) {
            l.enable_3d().position({0, 0, 0}).rotate({12, -18, -8});
            FakeExtrudedTextParams tp;
            tp.text              = "CHRONON 3D";
            tp.font_size         = 128.0f;
            tp.depth             = 14;
            tp.extrude_z_step    = 3.5f;
            tp.extrude_dir       = {0.3f, 0.0f};
            tp.front_color       = Color{0.96f, 0.94f, 0.90f, 1.f};
            tp.side_color        = Color{0.58f, 0.54f, 0.48f, 0.92f};  // slightly brighter for metallic
            tp.bevel_size        = 3.0f;
            tp.highlight_opacity = 0.08f;
            tp.light_dir         = anim_light;
            l.fake_extruded_text("t", tp);
        });

        return s.build();
    });
}

// ─── LilDirk-style demo ──────────────────────────────────────────────────────
// Matches reference video: dark grid bg, red box, white pulsing glow, XYZ tilt.
static Composition LilDirkDemo() {
    constexpr int kW = 1280, kH = 720;
    constexpr int kDur = 210;  // 7 s @ 30 fps

    return composition({.name="LilDirkDemo", .width=kW, .height=kH, .duration=kDur},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        templates::dark_grid_background(s, ctx, {});

        templates::neon_badge(s, ctx, {
            .text       = "LIL DIRK",
            .box_color  = {0.86f, 0.08f, 0.12f, 1.f},
            .glow_color = Color::white(),
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("LilDirkDemo",               LilDirkDemo)
CHRONON_REGISTER_COMPOSITION("TiltMotionDemo",            TiltMotionDemo)
CHRONON_REGISTER_COMPOSITION("S3D_01_FrontTextClean",     S3D_01_FrontTextClean)
CHRONON_REGISTER_COMPOSITION("S3D_02_ExtrusionSideDepth", S3D_02_ExtrusionSideDepth)
CHRONON_REGISTER_COMPOSITION("S3D_03_ExtrusionGlyphHoles",S3D_03_ExtrusionGlyphHoles)
CHRONON_REGISTER_COMPOSITION("S3D_04_BevelEdges",         S3D_04_BevelEdges)
CHRONON_REGISTER_COMPOSITION("S3D_05_AccentsExtruded",    S3D_05_AccentsExtruded)
CHRONON_REGISTER_COMPOSITION("S3D_06_DepthSortingText",   S3D_06_DepthSortingText)
CHRONON_REGISTER_COMPOSITION("S3D_07_CameraTiltText",     S3D_07_CameraTiltText)
CHRONON_REGISTER_COMPOSITION("S3D_08_NearClippingText",   S3D_08_NearClippingText)
CHRONON_REGISTER_COMPOSITION("S3D_09_LongWordSpacing",    S3D_09_LongWordSpacing)
CHRONON_REGISTER_COMPOSITION("S3D_10_ThinVsBold",         S3D_10_ThinVsBold)
