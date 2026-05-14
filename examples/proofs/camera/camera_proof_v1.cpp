#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;

namespace {

const Color kBg        {0.03f, 0.03f, 0.06f, 1.0f};
const Color kColorNear {0.90f, 0.20f, 0.15f, 1.0f};
const Color kColorMid  {0.18f, 0.22f, 0.42f, 1.0f};
const Color kColorFar  {0.15f, 0.30f, 0.85f, 1.0f};

const TextStyle kLabel {
    .font_path = "assets/fonts/Inter-Bold.ttf",
    .size = 26, .color = Color{1,1,1,1}, .align = TextAlign::Center
};
const TextStyle kLabelSm {
    .font_path = "assets/fonts/Inter-Bold.ttf",
    .size = 18, .color = Color{1,1,1,0.75f}, .align = TextAlign::Center
};
const TextStyle kTitle {
    .font_path = "assets/fonts/Inter-Bold.ttf",
    .size = 36, .color = Color{1,1,1,1}, .align = TextAlign::Center
};
const TextStyle kSub {
    .font_path = "assets/fonts/Inter-Regular.ttf",
    .size = 19, .color = Color{1,1,1,0.45f}, .align = TextAlign::Center
};
const TextStyle kAnno {
    .font_path = "assets/fonts/Inter-Bold.ttf",
    .size = 17, .color = Color{1,1,1,0.55f}, .align = TextAlign::Right
};

constexpr Vec2 kCard {280, 170};
constexpr f32  kR    {18};

} // namespace

// ---------------------------------------------------------------------------
// CameraBasicTwoNodeProof
// Minimal scene: camera pos + POI, three objects at near/mid/far depths.
// Verifies: perspective scale, z-order, basic camera setup.
// ---------------------------------------------------------------------------
Composition CameraBasicTwoNodeProof() {
    return composition({
        .name = "CameraBasicTwoNodeProof",
        .width = 1280, .height = 720, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // Two-node camera: position + point_of_interest
        s.camera_2_5d({
            .enabled = true,
            .position = {0, 0, -1000},
            .point_of_interest = {0, 0, 0},
            .zoom = 1000.0f
        });

        s.rect("bg", {.size={1280,720}, .color=kBg, .pos={640,360,0}});

        // Checker at far Z gives spatial depth reference
        s.layer("grid", [](LayerBuilder& l) {
            l.enable_3d(true).position({640, 360, 900});
            l.image("checker", {
                .path="assets/images/checker.png",
                .size={2600,1500}, .pos={0,0,0}, .opacity=0.14f
            });
        });

        // FAR (blue) — smaller due to distance
        s.layer("far", [](LayerBuilder& l) {
            l.enable_3d(true).position({280, 360, 500});
            l.rounded_rect("c", {.size=kCard,.radius=kR,.color=kColorFar,.pos={0,0,0}});
            l.text("l", {.content="FAR  z +500", .style=kLabel, .pos={0,-12,0}});
        });

        // PANEL (mid) — reference, z=0
        s.layer("mid", [](LayerBuilder& l) {
            l.enable_3d(true).position({640, 360, 0});
            l.rounded_rect("c", {.size=kCard,.radius=kR,.color=kColorMid,.pos={0,0,0}});
            l.text("l", {.content="PANEL  z 0", .style=kLabel, .pos={0,-12,0}});
        });

        // NEAR (red) — larger, partially off-screen shows depth
        s.layer("near", [](LayerBuilder& l) {
            l.enable_3d(true).position({1000, 360, -400});
            l.rounded_rect("c", {.size=kCard,.radius=kR,.color=kColorNear,.pos={0,0,0}});
            l.text("l", {.content="NEAR  z -400", .style=kLabel, .pos={0,-12,0}});
        });

        // 2D overlay — unaffected by camera
        s.layer("title", [](LayerBuilder& l) {
            l.position({640, 52, 0});
            l.text("t", {.content="CAMERA BASIC - TWO NODE", .style=kTitle, .pos={0,0,0}});
        });
        s.layer("sub", [](LayerBuilder& l) {
            l.position({640, 88, 0});
            l.text("s", {
                .content="pos (0,0,-1000)  POI (0,0,0)  zoom 1000",
                .style=kSub, .pos={0,0,0}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("CameraBasicTwoNodeProof", CameraBasicTwoNodeProof)

// ---------------------------------------------------------------------------
// CameraFallbackProof
// No camera_2_5d. Three cards at different Z depths all appear the same size.
// Verifies: fallback 2D flat mode, no crash, all objects visible.
// ---------------------------------------------------------------------------
Composition CameraFallbackProof() {
    return composition({
        .name = "CameraFallbackProof",
        .width = 1280, .height = 720, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.rect("bg", {.size={1280,720}, .color=kBg, .pos={640,360,0}});

        s.layer("far", [](LayerBuilder& l) {
            l.enable_3d(true).position({230, 360, 500});
            l.rounded_rect("c", {.size=kCard,.radius=kR,.color=kColorFar,.pos={0,0,0}});
            l.text("l",  {.content="z +500",             .style=kLabel,   .pos={0,-20,0}});
            l.text("n",  {.content="same size as others", .style=kLabelSm, .pos={0, 14,0}});
        });
        s.layer("mid", [](LayerBuilder& l) {
            l.enable_3d(true).position({640, 360, 0});
            l.rounded_rect("c", {.size=kCard,.radius=kR,.color=kColorMid,.pos={0,0,0}});
            l.text("l",  {.content="z 0",                .style=kLabel,   .pos={0,-20,0}});
            l.text("n",  {.content="same size as others", .style=kLabelSm, .pos={0, 14,0}});
        });
        s.layer("near", [](LayerBuilder& l) {
            l.enable_3d(true).position({1050, 360, -400});
            l.rounded_rect("c", {.size=kCard,.radius=kR,.color=kColorNear,.pos={0,0,0}});
            l.text("l",  {.content="z -400",             .style=kLabel,   .pos={0,-20,0}});
            l.text("n",  {.content="same size as others", .style=kLabelSm, .pos={0, 14,0}});
        });

        s.layer("title", [](LayerBuilder& l) {
            l.position({640, 52, 0});
            l.text("t", {.content="CAMERA FALLBACK - 2D FLAT", .style=kTitle, .pos={0,0,0}});
        });
        s.layer("sub", [](LayerBuilder& l) {
            l.position({640, 88, 0});
            l.text("s", {
                .content="no camera_2_5d -- z depth does not affect size or position",
                .style=kSub, .pos={0,0,0}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("CameraFallbackProof", CameraFallbackProof)

// ---------------------------------------------------------------------------
// CameraZoomComparisonProof
// One composition, three frames showing zoom 300 / 877 / 1500.
// Camera FIXED at z=-600. Only zoom changes between frames.
// Frame 0: zoom 300  -- scene small, wide field of view
// Frame 1: zoom 877  -- near AE default, normal
// Frame 2: zoom 1500 -- scene large, telephoto, not cropped
// Verifies: zoom changes apparent size; 2D overlay stays fixed.
// ---------------------------------------------------------------------------
Composition CameraZoomComparisonProof() {
    return composition({
        .name = "CameraZoomComparisonProof",
        .width = 1280, .height = 720, .duration = 3
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // Zoom per frame
        constexpr float kZooms[3] = {300.0f, 877.0f, 1500.0f};
        const char* kTitles[3]    = {"ZOOM 300 - WIDE",   "ZOOM 877 - NORMAL",  "ZOOM 1500 - TELE"};
        const char* kNotes[3]     = {"wide FOV, scene small", "AE default FOV", "telephoto, scene large"};

        const int fi     = static_cast<int>(ctx.frame < 3 ? ctx.frame : 2);
        const float zoom = kZooms[fi];

        s.camera_2_5d({
            .enabled = true,
            .position = {0, 0, -600},
            .point_of_interest = {0, 0, 0},
            .zoom = zoom
        });

        s.rect("bg", {.size={1280,720}, .color=kBg, .pos={640,360,0}});

        // Far checker — reference for scale change
        s.layer("checker", [](LayerBuilder& l) {
            l.enable_3d(true).position({640, 360, 700});
            l.image("img", {
                .path="assets/images/checker.png",
                .size={2600,1500}, .pos={0,0,0}, .opacity=0.18f
            });
        });

        // Central panel at z=0 — always scale 1.0 * (zoom/600)
        s.layer("panel", [](LayerBuilder& l) {
            l.enable_3d(true).position({640, 360, 0});
            l.rounded_rect("c", {
                .size={380,230}, .radius=24,
                .color=kColorMid, .pos={0,0,0}
            });
            l.text("l", {.content="PANEL  z 0", .style=kLabel, .pos={0,-14,0}});
            l.text("n", {.content="scale = zoom / 600", .style=kLabelSm, .pos={0,16,0}});
        });

        // 2D overlay — constant regardless of zoom
        s.layer("title", [&](LayerBuilder& l) {
            l.position({640, 52, 0});
            l.text("t", {.content=kTitles[fi], .style=kTitle, .pos={0,0,0}});
        });
        s.layer("sub", [&](LayerBuilder& l) {
            l.position({640, 88, 0});
            l.text("s", {.content=kNotes[fi], .style=kSub, .pos={0,0,0}});
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("CameraZoomComparisonProof", CameraZoomComparisonProof)

// ---------------------------------------------------------------------------
// CameraParallaxProof
// Camera pans X over 90 frames. Three layers at different depths.
// Near layer shifts MORE, far layer shifts LESS, 2D overlay stays FIXED.
// Verifies: parallax depth cue, per-role depth, 2D/3D mixed layer.
// ---------------------------------------------------------------------------
Composition CameraParallaxProof() {
    return composition({
        .name = "CameraParallaxProof",
        .width = 1280, .height = 720, .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const float cam_x = interpolate(ctx.frame, 0, 89, -280.0f, 280.0f);

        s.camera_2_5d({
            .enabled = true,
            .position = {cam_x, 0, -1000},
            .point_of_interest = {0, 0, 0},
            .zoom = 1000.0f
        });

        s.rect("bg", {.size={1280,720}, .color=kBg, .pos={640,360,0}});

        // FAR background — shifts least
        s.layer("far-bg", [](LayerBuilder& l) {
            l.enable_3d(true).position({640, 360, 800});
            l.image("checker", {
                .path="assets/images/checker.png",
                .size={2800,1600}, .pos={0,0,0}, .opacity=0.20f
            });
        });

        s.layer("far-card", [](LayerBuilder& l) {
            l.enable_3d(true).position({640, 360, 600});
            l.rounded_rect("c", {
                .size={500,160}, .radius=14,
                .color=Color{0.10f,0.15f,0.28f,0.85f}, .pos={0,0,0}
            });
            l.text("l", {
                .content="FAR  z +600  (slow parallax)",
                .style=kLabelSm, .pos={0,-8,0}
            });
        });

        // MID subject — shifts moderately
        s.layer("subject", [](LayerBuilder& l) {
            l.enable_3d(true).position({640, 360, 0});
            l.rounded_rect("c", {
                .size={420,220}, .radius=22,
                .color=kColorMid, .pos={0,0,0}
            });
            l.text("l", {.content="SUBJECT  z 0", .style=kLabel, .pos={0,-12,0}});
        });

        // NEAR foreground — shifts most
        s.layer("near-title", [](LayerBuilder& l) {
            l.enable_3d(true).position({640, 220, -350});
            l.rounded_rect("c", {
                .size={560,70}, .radius=10,
                .color=Color{kColorNear.r,kColorNear.g,kColorNear.b,0.80f}, .pos={0,0,0}
            });
            l.text("l", {
                .content="NEAR  z -350  (fast parallax)",
                .style=kLabel, .pos={0,-12,0}
            });
        });

        // 2D fixed overlay — never moves with camera
        s.layer("overlay", [](LayerBuilder& l) {
            l.position({640, 48, 0});
            l.text("t", {
                .content="CAMERA PARALLAX",
                .style=kTitle, .pos={0,0,0}
            });
        });
        s.layer("sub", [&](LayerBuilder& l) {
            l.position({640, 84, 0});
            l.text("s", {
                .content="2D HUD fixed  |  cam pans X from -280 to +280  |  near > far shift",
                .style=kSub, .pos={0,0,0}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("CameraParallaxProof", CameraParallaxProof)

// ---------------------------------------------------------------------------
// CameraAxisDebugProof
// 2D schematic showing axis conventions and Z-depth sign.
// No camera_2_5d -- pure diagram.
// X = red (horizontal), Y = green (vertical), Z = depth (blue/label)
// Z- = near (closer to viewer), Z+ = far (deeper into scene)
// ---------------------------------------------------------------------------
Composition CameraAxisDebugProof() {
    return composition({
        .name = "CameraAxisDebugProof",
        .width = 1280, .height = 720, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.rect("bg", {.size={1280,720}, .color=kBg, .pos={640,360,0}});

        // Origin cross
        const float ox = 500.0f, oy = 380.0f;
        const float arm = 160.0f;

        // X axis (red, horizontal)
        s.line("x-pos", {
            .from={ox, oy, 0}, .to={ox + arm, oy, 0},
            .thickness=3.0f, .color=Color{0.95f,0.25f,0.20f,1}
        });
        s.line("x-neg", {
            .from={ox, oy, 0}, .to={ox - arm*0.4f, oy, 0},
            .thickness=1.5f, .color=Color{0.95f,0.25f,0.20f,0.35f}
        });

        // Y axis (green, vertical)
        s.line("y-pos", {
            .from={ox, oy, 0}, .to={ox, oy - arm, 0},
            .thickness=3.0f, .color=Color{0.20f,0.85f,0.30f,1}
        });
        s.line("y-neg", {
            .from={ox, oy, 0}, .to={ox, oy + arm*0.4f, 0},
            .thickness=1.5f, .color=Color{0.20f,0.85f,0.30f,0.35f}
        });

        // Z axis (blue/cyan, diagonal perspective hint going down-right)
        s.line("z-pos", {
            .from={ox, oy, 0}, .to={ox + arm*0.7f, oy + arm*0.5f, 0},
            .thickness=3.0f, .color=Color{0.20f,0.55f,1.0f,1}
        });
        s.line("z-neg", {
            .from={ox, oy, 0}, .to={ox - arm*0.28f, oy - arm*0.20f, 0},
            .thickness=1.5f, .color=Color{0.20f,0.55f,1.0f,0.35f}
        });

        // Axis arrow heads (small squares at tips)
        s.rect("xh", {.size={10,10},.color=Color{0.95f,0.25f,0.20f,1},.pos={ox+arm,oy,0}});
        s.rect("yh", {.size={10,10},.color=Color{0.20f,0.85f,0.30f,1},.pos={ox,oy-arm,0}});
        s.rect("zh", {.size={10,10},.color=Color{0.20f,0.55f,1.0f,1},.pos={ox+arm*0.7f,oy+arm*0.5f,0}});

        // Axis labels
        s.layer("x-lbl", [&](LayerBuilder& l) {
            l.position({ox+arm+28, oy, 0});
            l.rounded_rect("bg", {
                .size={70,36}, .radius=6,
                .color=Color{0.95f,0.25f,0.20f,0.85f}, .pos={0,0,0}
            });
            l.text("t", {
                .content="X+",
                .style=TextStyle{.font_path="assets/fonts/Inter-Bold.ttf",
                                 .size=22,.color=Color{1,1,1,1},.align=TextAlign::Center},
                .pos={0,-10,0}
            });
        });
        s.layer("y-lbl", [&](LayerBuilder& l) {
            l.position({ox, oy-arm-28, 0});
            l.rounded_rect("bg", {
                .size={70,36}, .radius=6,
                .color=Color{0.20f,0.85f,0.30f,0.85f}, .pos={0,0,0}
            });
            l.text("t", {
                .content="Y+",
                .style=TextStyle{.font_path="assets/fonts/Inter-Bold.ttf",
                                 .size=22,.color=Color{1,1,1,1},.align=TextAlign::Center},
                .pos={0,-10,0}
            });
        });
        s.layer("z-lbl", [&](LayerBuilder& l) {
            l.position({ox+arm*0.7f+28, oy+arm*0.5f+20, 0});
            l.rounded_rect("bg", {
                .size={70,36}, .radius=6,
                .color=Color{0.20f,0.55f,1.0f,0.85f}, .pos={0,0,0}
            });
            l.text("t", {
                .content="Z+",
                .style=TextStyle{.font_path="assets/fonts/Inter-Bold.ttf",
                                 .size=22,.color=Color{1,1,1,1},.align=TextAlign::Center},
                .pos={0,-10,0}
            });
        });

        // Depth convention explanation (right panel)
        const float px = 860.0f;

        s.layer("conv-title", [&](LayerBuilder& l) {
            l.position({px, 200, 0});
            l.text("t", {
                .content="Z DEPTH CONVENTION",
                .style=TextStyle{.font_path="assets/fonts/Inter-Bold.ttf",
                                 .size=22,.color=Color{1,1,1,0.7f},.align=TextAlign::Left},
                .pos={0,0,0}
            });
        });

        // Z- = near
        s.layer("z-near", [&](LayerBuilder& l) {
            l.position({px, 268, 0});
            l.rounded_rect("bg", {
                .size={320,52}, .radius=8,
                .color=Color{kColorNear.r,kColorNear.g,kColorNear.b,0.80f}, .pos={0,0,0}
            });
            l.text("t", {
                .content="Z- = NEAR  (closer to viewer)",
                .style=TextStyle{.font_path="assets/fonts/Inter-Bold.ttf",
                                 .size=18,.color=Color{1,1,1,1},.align=TextAlign::Left},
                .pos={-144,0,0}
            });
        });

        // Z=0 = screen
        s.layer("z-screen", [&](LayerBuilder& l) {
            l.position({px, 338, 0});
            l.rounded_rect("bg", {
                .size={320,52}, .radius=8,
                .color=Color{0.30f,0.75f,0.40f,0.70f}, .pos={0,0,0}
            });
            l.text("t", {
                .content="Z 0 = SCREEN PLANE  (ref)",
                .style=TextStyle{.font_path="assets/fonts/Inter-Bold.ttf",
                                 .size=18,.color=Color{1,1,1,1},.align=TextAlign::Left},
                .pos={-144,0,0}
            });
        });

        // Z+ = far
        s.layer("z-far", [&](LayerBuilder& l) {
            l.position({px, 408, 0});
            l.rounded_rect("bg", {
                .size={320,52}, .radius=8,
                .color=Color{kColorFar.r,kColorFar.g,kColorFar.b,0.80f}, .pos={0,0,0}
            });
            l.text("t", {
                .content="Z+ = FAR  (deeper into scene)",
                .style=TextStyle{.font_path="assets/fonts/Inter-Bold.ttf",
                                 .size=18,.color=Color{1,1,1,1},.align=TextAlign::Left},
                .pos={-144,0,0}
            });
        });

        s.layer("cam-note", [&](LayerBuilder& l) {
            l.position({px, 490, 0});
            l.text("t", {
                .content="Camera typically at Z-  (neg Z, behind screen)",
                .style=TextStyle{.font_path="assets/fonts/Inter-Regular.ttf",
                                 .size=16,.color=Color{1,1,1,0.40f},.align=TextAlign::Left},
                .pos={-144,0,0}
            });
        });

        // Title
        s.layer("title", [](LayerBuilder& l) {
            l.position({640, 48, 0});
            l.text("t", {.content="CAMERA AXIS DEBUG - Z CONVENTION", .style=kTitle, .pos={0,0,0}});
        });
        s.layer("sub", [](LayerBuilder& l) {
            l.position({640, 84, 0});
            l.text("s", {
                .content="no camera_2_5d  |  X red  Y green  Z blue  |  Z- = near  Z+ = far",
                .style=kSub, .pos={0,0,0}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("CameraAxisDebugProof", CameraAxisDebugProof)

// ---------------------------------------------------------------------------
// TopViewDebugProof  (bonus)
// 2D schematic of the Z-depth layout. No camera_2_5d.
// Shows camera, depth rows, scale bars for zoom 1000 reference.
// ---------------------------------------------------------------------------
Composition TopViewDebugProof() {
    return composition({
        .name = "TopViewDebugProof",
        .width = 1280, .height = 720, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.rect("bg", {.size={1280,720}, .color=kBg, .pos={640,360,0}});

        const float cx    = 540.0f;
        const float y_cam = 128.0f;
        const float y_nr  = 265.0f;
        const float y_mid = 393.0f;
        const float y_far = 543.0f;
        const float x_ann = 345.0f;
        const float x_bar = 760.0f;
        const float bar1  = 180.0f;

        s.line("z-axis", {
            .from={cx, y_cam+26,0}, .to={cx, y_far+26,0},
            .thickness=1.5f, .color=Color{0.40f,0.40f,0.40f,0.40f}
        });
        s.line("frust-l", {
            .from={cx-34,y_cam+26,0}, .to={cx-230,y_far+26,0},
            .thickness=1.0f, .color=Color{0.9f,0.85f,0.25f,0.18f}
        });
        s.line("frust-r", {
            .from={cx+34,y_cam+26,0}, .to={cx+230,y_far+26,0},
            .thickness=1.0f, .color=Color{0.9f,0.85f,0.25f,0.18f}
        });
        s.line("screen-pl", {
            .from={cx-240,y_mid,0}, .to={cx+240,y_mid,0},
            .thickness=1.5f, .color=Color{0.30f,0.75f,0.40f,0.40f}
        });

        auto row = [&](float y, const char* lbl, const char* ann, Color col, float scale) {
            s.layer(lbl, [&](LayerBuilder& l) {
                l.position({cx, y, 0});
                l.rounded_rect("b", {
                    .size={210,44},.radius=7,
                    .color=Color{col.r,col.g,col.b,0.88f},.pos={0,0,0}
                });
                l.text("t", {
                    .content=lbl,
                    .style=TextStyle{.font_path="assets/fonts/Inter-Bold.ttf",
                                     .size=18,.color=Color{1,1,1,1},.align=TextAlign::Center},
                    .pos={0,-10,0}
                });
            });
            s.layer(std::string(ann) + "-ann", [&](LayerBuilder& l) {
                l.position({x_ann, y, 0});
                l.text("z", {.content=ann, .style=kAnno, .pos={0,-8,0}});
            });
            const float w = bar1 * scale;
            s.layer(std::string(lbl) + "-bar", [&](LayerBuilder& l) {
                l.position({x_bar + w*0.5f, y, 0});
                l.rect("r", {.size={w,18},.color=Color{col.r,col.g,col.b,0.65f},.pos={0,0,0}});
            });
        };

        s.layer("cam-box", [&](LayerBuilder& l) {
            l.position({cx, y_cam, 0});
            l.rounded_rect("b", {.size={110,42},.radius=7,.color=Color{0.85f,0.78f,0.15f,1},.pos={0,0,0}});
            l.text("t", {
                .content="CAMERA",
                .style=TextStyle{.font_path="assets/fonts/Inter-Bold.ttf",
                                 .size=15,.color=Color{0,0,0,1},.align=TextAlign::Center},
                .pos={0,-9,0}
            });
        });
        s.layer("cam-ann", [&](LayerBuilder& l) {
            l.position({x_ann, y_cam, 0});
            l.text("z", {.content="cam  z -1000", .style=kAnno, .pos={0,-8,0}});
        });

        row(y_nr,  "NEAR  z -400",  "z -400",     kColorNear, 1000.0f/600.0f);
        row(y_mid, "PANEL z 0",     "z 0 (screen)",kColorMid, 1.0f);
        row(y_far, "FAR  z +500",   "z +500",      kColorFar, 1000.0f/1500.0f);

        s.layer("bar-hdr", [&](LayerBuilder& l) {
            l.position({x_bar+90, y_nr-42, 0});
            l.text("h", {
                .content="visual scale (zoom 1000)",
                .style=TextStyle{.font_path="assets/fonts/Inter-Bold.ttf",
                                 .size=14,.color=Color{1,1,1,0.30f},.align=TextAlign::Center},
                .pos={0,0,0}
            });
        });

        s.layer("title", [](LayerBuilder& l) {
            l.position({640, 48, 0});
            l.text("t", {.content="TOP VIEW DEBUG - Z DEPTH LAYOUT", .style=kTitle, .pos={0,0,0}});
        });
        s.layer("sub", [](LayerBuilder& l) {
            l.position({640, 82, 0});
            l.text("s", {
                .content="schematic (no camera)  |  vertical axis = Z depth  |  bars = apparent size at zoom 1000",
                .style=kSub, .pos={0,0,0}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("TopViewDebugProof", TopViewDebugProof)
