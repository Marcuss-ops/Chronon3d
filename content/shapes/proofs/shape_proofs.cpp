#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/vector/path_factories.hpp>
#include <chronon3d/core/composition/composition_registration.hpp>
#include <cmath>
#include <algorithm>

namespace chronon3d::content::shapes {

namespace {

constexpr f32 W = 1920.0f;
constexpr f32 H = 1080.0f;

inline std::vector<PathCommand> make_ellipse_commands(Vec2 center, Vec2 radii) {
    constexpr f32 kappa = 0.55228474983f;
    f32 rx = radii.x;
    f32 ry = radii.y;
    f32 ox = rx * kappa;
    f32 oy = ry * kappa;
    std::vector<PathCommand> cmds;
    cmds.push_back(PathCommand::move_to({center.x - rx, center.y}));
    cmds.push_back(PathCommand::cubic_to({center.x - rx, center.y + oy}, {center.x - ox, center.y + ry}, {center.x, center.y + ry}));
    cmds.push_back(PathCommand::cubic_to({center.x + ox, center.y + ry}, {center.x + rx, center.y + oy}, {center.x + rx, center.y}));
    cmds.push_back(PathCommand::cubic_to({center.x + rx, center.y - oy}, {center.x + ox, center.y - ry}, {center.x, center.y - ry}));
    cmds.push_back(PathCommand::cubic_to({center.x - ox, center.y - ry}, {center.x - rx, center.y - oy}, {center.x - rx, center.y}));
    cmds.push_back(PathCommand::close());
    return cmds;
}

inline std::vector<PathCommand> make_curved_arrow_commands(Vec2 from, Vec2 to, Vec2 control) {
    std::vector<PathCommand> cmds;
    cmds.push_back(PathCommand::move_to(from));
    cmds.push_back(PathCommand::quadratic_to(control, to));
    
    Vec2 t = glm::normalize(to - control);
    Vec2 n{-t.y, t.x};
    Vec2 h0 = to;
    Vec2 h1 = to - t * 24.0f + n * 12.0f;
    Vec2 h2 = to - t * 24.0f - n * 12.0f;
    cmds.push_back(PathCommand::move_to(h0));
    cmds.push_back(PathCommand::line_to(h1));
    cmds.push_back(PathCommand::line_to(h2));
    cmds.push_back(PathCommand::close());
    return cmds;
}

} // namespace

Composition shape_proofs() {
    return composition({.name = "ShapeProofs", .width = 1920, .height = 1080, .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // Dark premium background
        s.layer("bg", [](auto& l) {
            l.fill({0.02f, 0.02f, 0.03f, 1.0f});
        });

        constexpr int COLS = 5;
        constexpr int ROWS = 8;
        constexpr f32 card_w = 460.0f;
        constexpr f32 card_h = 145.0f;
        constexpr f32 gap_x = 24.0f;
        constexpr f32 gap_y = 22.0f;

        auto get_cell_pos = [=](int col, int row) -> Vec3 {
            constexpr f32 grid_w = COLS * card_w + (COLS - 1) * gap_x;
            constexpr f32 grid_x = (W - grid_w) * 0.5f;
            constexpr f32 grid_y = 110.0f;
            f32 x = -W * 0.5f + grid_x + static_cast<f32>(col) * (card_w + gap_x) + card_w * 0.5f;
            f32 y = H * 0.5f - grid_y - static_cast<f32>(row) * (card_h + gap_y) - card_h * 0.5f;
            return {x, y, 0.0f};
        };

        auto get_label_pos = [=](int col, int row) -> Vec3 {
            return get_cell_pos(col, row) + Vec3{0.0f, -card_h * 0.5f + 22.0f, 0.0f};
        };

        auto draw_cell_card = [=](LayerBuilder& l, const std::string& text, int col, int row) {
            // Draw background card
            PathParams cp;
            cp.commands = make_rounded_rect_commands({0.0f, 0.0f}, {card_w - 18.0f, card_h - 18.0f}, 12.0f);
            cp.fill = Fill::solid_color({0.08f, 0.08f, 0.12f, 0.9f});
            cp.stroke = PathStroke{.enabled = true, .color = {0.15f, 0.15f, 0.22f, 0.5f}, .width = 1.5f};
            l.path("card_bg", cp);

            // Draw label text
            l.text("lbl", TextParams{
                .text = text,
                .size = {card_w - 20.0f, 20.0f},
                .pos = get_label_pos(col, row) - get_cell_pos(col, row),
                .font_size = 11.0f,
                .color = {0.55f, 0.65f, 0.8f, 0.85f},
                .align = TextAlign::Center,
            });
        };

        // Title
        s.layer("title", [](auto& l) {
            l.pin_to(Anchor::TopCenter, 48.0f);
            l.text("t", TextParams{
                .text = "SHAPE / VECTOR SYSTEM PROOFS",
                .size = {800.0f, 40.0f},
                .font_size = 28.0f,
                .color = {0.9f, 0.95f, 1.0f, 1.0f},
                .align = TextAlign::Center,
            });
        });

        // ── RIGA 1: Basic Shapes ──
        // 0,0: Solid Rect
        s.layer("r0c0", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(0, 0));
            draw_cell_card(l, "SOLID RECT", 0, 0);
            PathParams p;
            p.commands = make_rounded_rect_commands({0.0f, -18.0f}, {160.0f, 84.0f}, 0.0f);
            p.fill = Fill::solid_color({0.15f, 0.45f, 0.85f, 1.0f});
            l.path("shape", p);
        });

        // 0,1: Rounded Rect
        s.layer("r0c1", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(1, 0));
            draw_cell_card(l, "ROUNDED RECT", 1, 0);
            PathParams p;
            p.commands = make_rounded_rect_commands({0.0f, -18.0f}, {160.0f, 84.0f}, 15.0f);
            p.fill = Fill::solid_color({0.12f, 0.72f, 0.42f, 1.0f});
            l.path("shape", p);
        });

        // 0,2: Circle
        s.layer("r0c2", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(2, 0));
            draw_cell_card(l, "CIRCLE", 2, 0);
            PathParams p;
            p.commands = make_rounded_rect_commands({0.0f, -18.0f}, {90.0f, 90.0f}, 45.0f);
            p.fill = Fill::solid_color({0.95f, 0.35f, 0.25f, 1.0f});
            l.path("shape", p);
        });

        // 0,3: Ellipse
        s.layer("r0c3", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(3, 0));
            draw_cell_card(l, "ELLIPSE", 3, 0);
            PathParams p;
            p.commands = make_ellipse_commands({0.0f, -18.0f}, {80.0f, 45.0f});
            p.fill = Fill::solid_color({0.6f, 0.3f, 0.9f, 1.0f});
            l.path("shape", p);
        });

        // 0,4: Line
        s.layer("r0c4", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(4, 0));
            draw_cell_card(l, "LINE", 4, 0);
            PathParams p;
            p.commands.push_back(PathCommand::move_to({-60.0f, -36.0f}));
            p.commands.push_back(PathCommand::line_to({80.0f, 24.0f}));
            p.fill.enabled = false;
            p.stroke = PathStroke{.enabled = true, .color = {1, 1, 1, 1}, .width = 6.0f, .cap = LineCap::Round};
            l.path("shape", p);
        });

        // ── RIGA 2: Fill / Gradient ──
        // 1,0: Solid Fill
        s.layer("r1c0", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(0, 1));
            draw_cell_card(l, "SOLID FILL", 0, 1);
            PathParams p;
            p.commands = make_rounded_rect_commands({0.0f, -18.0f}, {160.0f, 84.0f}, 8.0f);
            p.fill = Fill::solid_color({0.95f, 0.2f, 0.45f, 1.0f});
            l.path("shape", p);
        });

        // 1,1: Linear Gradient
        s.layer("r1c1", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(1, 1));
            draw_cell_card(l, "LINEAR GRADIENT", 1, 1);
            PathParams p;
            p.commands = make_rounded_rect_commands({0.0f, -18.0f}, {160.0f, 84.0f}, 8.0f);
            p.fill = Fill::linear({0.0f, 0.0f}, {1.0f, 1.0f}, {
                GradientStop{0.0f, Color{0.0f, 0.95f, 0.95f, 1.0f}},
                GradientStop{1.0f, Color{0.15f, 0.2f, 0.95f, 1.0f}}
            });
            l.path("shape", p);
        });

        // 1,2: Radial Gradient
        s.layer("r1c2", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(2, 1));
            draw_cell_card(l, "RADIAL GRADIENT", 2, 1);
            PathParams p;
            p.commands = make_rounded_rect_commands({0.0f, -18.0f}, {160.0f, 84.0f}, 8.0f);
            p.fill = Fill::radial({0.5f, 0.5f}, 0.5f, {
                GradientStop{0.0f, Color{1.0f, 0.8f, 0.0f, 1.0f}},
                GradientStop{1.0f, Color{0.8f, 0.1f, 0.0f, 1.0f}}
            });
            l.path("shape", p);
        });

        // 1,3: Gradient Badge
        s.layer("r1c3", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(3, 1));
            draw_cell_card(l, "GRADIENT BADGE", 3, 1);
            BadgeParams bp;
            bp.center = {0.0f, -18.0f};
            bp.radius = 35.0f;
            bp.scallops = 16;
            bp.scallop_depth = 4.0f;
            bp.style.fill = Fill::linear({0.0f, 0.0f}, {0.0f, 1.0f}, {
                GradientStop{0.0f, Color{0.9f, 0.5f, 0.1f, 1.0f}},
                GradientStop{1.0f, Color{0.95f, 0.15f, 0.5f, 1.0f}}
            });
            l.badge("shape", bp);
        });

        // 1,4: Transparent Fill
        s.layer("r1c4", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(4, 1));
            draw_cell_card(l, "TRANSPARENT FILL", 4, 1);
            PathParams p;
            p.commands = make_rounded_rect_commands({0.0f, -18.0f}, {160.0f, 84.0f}, 8.0f);
            p.fill.enabled = false;
            p.stroke = PathStroke{.enabled = true, .color = {0.0f, 0.85f, 1.0f, 1.0f}, .width = 3.0f};
            l.path("shape", p);
        });

        // ── RIGA 3: Stroke System ──
        // 2,0: Stroke Thin
        s.layer("r2c0", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(0, 2));
            draw_cell_card(l, "STROKE THIN", 0, 2);
            PathParams p;
            p.commands = make_rounded_rect_commands({0.0f, -18.0f}, {160.0f, 84.0f}, 12.0f);
            p.fill.enabled = false;
            p.stroke = PathStroke{.enabled = true, .color = {1, 1, 1, 1}, .width = 1.5f};
            l.path("shape", p);
        });

        // 2,1: Stroke Thick
        s.layer("r2c1", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(1, 2));
            draw_cell_card(l, "STROKE THICK", 1, 2);
            PathParams p;
            p.commands = make_rounded_rect_commands({0.0f, -18.0f}, {110.0f, 50.0f}, 12.0f);
            p.fill.enabled = false;
            p.stroke = PathStroke{.enabled = true, .color = {1.0f, 0.7f, 0.0f, 1.0f}, .width = 12.0f};
            l.path("shape", p);
        });

        // 2,2: Round Caps
        s.layer("r2c2", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(2, 2));
            draw_cell_card(l, "ROUND CAPS", 2, 2);
            PathParams p;
            p.commands.push_back(PathCommand::move_to({-50.0f, -18.0f}));
            p.commands.push_back(PathCommand::line_to({50.0f, -18.0f}));
            p.fill.enabled = false;
            p.stroke = PathStroke{.enabled = true, .color = {0.8f, 0.2f, 0.9f, 1.0f}, .width = 10.0f, .cap = LineCap::Round};
            l.path("shape", p);
        });

        // 2,3: Bevel Join
        s.layer("r2c3", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(3, 2));
            draw_cell_card(l, "BEVEL JOIN", 3, 2);
            PathParams p;
            p.commands.push_back(PathCommand::move_to({-40.0f, -25.0f}));
            p.commands.push_back(PathCommand::line_to({0.0f, 15.0f}));
            p.commands.push_back(PathCommand::line_to({40.0f, -25.0f}));
            p.fill.enabled = false;
            p.stroke = PathStroke{.enabled = true, .color = {0.2f, 0.8f, 0.8f, 1.0f}, .width = 12.0f, .join = LineJoin::Bevel};
            l.path("shape", p);
        });

        // 2,4: Dashed Line
        s.layer("r2c4", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(4, 2));
            draw_cell_card(l, "DASHED LINE", 4, 2);
            PathParams p;
            p.commands.push_back(PathCommand::move_to({-80.0f, -18.0f}));
            p.commands.push_back(PathCommand::line_to({60.0f, -18.0f}));
            p.fill.enabled = false;
            p.stroke = PathStroke{
                .enabled = true,
                .color = {0.1f, 0.95f, 0.3f, 1.0f},
                .width = 5.0f,
                .cap = LineCap::Round,
                .dash_array = {12.0f, 8.0f}
            };
            l.path("shape", p);
        });

        // ── RIGA 4: Arrows / Callouts ──
        // 3,0: Arrow Right
        s.layer("r3c0", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(0, 3));
            draw_cell_card(l, "ARROW RIGHT", 0, 3);
            ArrowParams ap;
            ap.from = {-55.0f, -18.0f};
            ap.to = {55.0f, -18.0f};
            ap.thickness = 12.0f;
            ap.head_width = 30.0f;
            ap.head_length = 26.0f;
            ap.style.fill = Fill::solid_color({0.95f, 0.15f, 0.15f, 1.0f});
            l.arrow("shape", ap);
        });

        // 3,1: Arrow Curved
        s.layer("r3c1", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(1, 3));
            draw_cell_card(l, "ARROW CURVED", 1, 3);
            PathParams p;
            p.commands = make_curved_arrow_commands({-50.0f, -25.0f}, {45.0f, 15.0f}, {15.0f, -30.0f});
            p.fill = Fill::solid_color({0.95f, 0.5f, 0.1f, 1.0f});
            p.stroke = PathStroke{.enabled = true, .color = {1, 1, 1, 1}, .width = 1.5f};
            l.path("shape", p);
        });

        // 3,2: Callout Box
        s.layer("r3c2", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(2, 3));
            draw_cell_card(l, "CALLOUT BOX", 2, 3);
            CalloutParams cp;
            cp.rect_center = {-10.0f, 6.0f};
            cp.rect_size = {100.0f, 40.0f};
            cp.corner_radius = 6.0f;
            cp.target_point = {45.0f, -26.0f};
            cp.pointer_width = 12.0f;
            cp.style.fill = Fill::solid_color({0.0f, 0.0f, 0.0f, 0.8f});
            cp.style.stroke = PathStroke{.enabled = true, .color = {0.0f, 0.95f, 0.95f, 1.0f}, .width = 2.0f};
            l.callout("shape", cp);
        });

        // 3,3: Speech Bubble
        s.layer("r3c3", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(3, 3));
            draw_cell_card(l, "SPEECH BUBBLE", 3, 3);
            SpeechBubbleParams sbp;
            sbp.center = {0.0f, 8.0f};
            sbp.size = {110.0f, 42.0f};
            sbp.corner_radius = 10.0f;
            sbp.pointer_tip = {-18.0f, -24.0f};
            sbp.pointer_width = 14.0f;
            sbp.pointer_center = -18.0f;
            sbp.style.fill = Fill::solid_color({0.15f, 0.18f, 0.24f, 1.0f});
            sbp.style.stroke = PathStroke{.enabled = true, .color = {0.6f, 0.7f, 0.8f, 1.0f}, .width = 2.0f};
            l.speech_bubble("shape", sbp);
        });

        // 3,4: Pointer Label
        s.layer("r3c4", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(4, 3));
            draw_cell_card(l, "POINTER LABEL", 4, 3);
            CalloutParams cp;
            cp.rect_center = {12.0f, -18.0f};
            cp.rect_size = {80.0f, 32.0f};
            cp.corner_radius = 4.0f;
            cp.target_point = {-45.0f, -18.0f};
            cp.pointer_width = 8.0f;
            cp.style.fill = Fill::solid_color({0.85f, 0.15f, 0.45f, 1.0f});
            cp.style.stroke = PathStroke{.enabled = true, .color = {1, 1, 1, 1}, .width = 1.5f};
            l.callout("shape", cp);
        });

        // ── RIGA 5: Badges / YouTube Graphics ──
        // 4,0: News Badge
        s.layer("r4c0", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(0, 4));
            draw_cell_card(l, "NEWS BADGE", 0, 4);
            PathParams p;
            p.commands = make_rounded_rect_commands({0.0f, -18.0f}, {120.0f, 44.0f}, 0.0f);
            p.fill = Fill::solid_color({0.8f, 0.05f, 0.1f, 1.0f});
            p.stroke = PathStroke{.enabled = true, .color = {1, 1, 1, 1}, .width = 2.0f};
            l.path("shape", p);
            
            l.text("t", {.text = "NEWS", .size = {120.0f, 20.0f}, .pos = {0.0f, -14.0f, 0.0f}, .font_size = 13.0f, .color = {1,1,1,1}, .align = TextAlign::Center});
        });

        // 4,1: Warning Badge
        s.layer("r4c1", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(1, 4));
            draw_cell_card(l, "WARNING BADGE", 1, 4);
            PathParams p;
            p.commands.push_back(PathCommand::move_to({0.0f, 24.0f -18.0f}));
            p.commands.push_back(PathCommand::line_to({40.0f, -24.0f -18.0f}));
            p.commands.push_back(PathCommand::line_to({-40.0f, -24.0f -18.0f}));
            p.commands.push_back(PathCommand::close());
            p.fill = Fill::solid_color({0.95f, 0.7f, 0.05f, 1.0f});
            p.stroke = PathStroke{.enabled = true, .color = {0.1f, 0.1f, 0.1f, 1.0f}, .width = 3.5f, .join = LineJoin::Round};
            l.path("shape", p);

            l.text("t", {.text = "!", .size = {30.0f, 30.0f}, .pos = {0.0f, -22.0f, 0.0f}, .font_size = 18.0f, .color = {0,0,0,1}, .align = TextAlign::Center});
        });

        // 4,2: Number Badge
        s.layer("r4c2", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(2, 4));
            draw_cell_card(l, "NUMBER BADGE", 2, 4);
            PathParams p;
            p.commands = make_rounded_rect_commands({0.0f, -18.0f}, {48.0f, 48.0f}, 24.0f);
            p.fill = Fill::solid_color({0.1f, 0.1f, 0.12f, 1.0f});
            p.stroke = PathStroke{.enabled = true, .color = {0.95f, 0.35f, 0.1f, 1.0f}, .width = 3.0f};
            l.path("shape", p);

            l.text("t", {.text = "10", .size = {48.0f, 24.0f}, .pos = {0.0f, -14.0f, 0.0f}, .font_size = 14.0f, .color = {1,1,1,1}, .align = TextAlign::Center});
        });

        // 4,3: Star Badge
        s.layer("r4c3", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(3, 4));
            draw_cell_card(l, "STAR BADGE", 3, 4);
            StarParams sp;
            sp.center = {0.0f, -18.0f};
            sp.points = 5;
            sp.outer_radius = 32.0f;
            sp.inner_radius = 15.0f;
            sp.style.fill = Fill::solid_color({0.95f, 0.8f, 0.1f, 1.0f});
            sp.style.stroke = PathStroke{.enabled = true, .color = {1, 0.9f, 0.5f, 1.0f}, .width = 2.0f};
            l.star("shape", sp);
        });

        // 4,4: Pill Label
        s.layer("r4c4", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(4, 4));
            draw_cell_card(l, "PILL LABEL", 4, 4);
            PathParams p;
            p.commands = make_rounded_rect_commands({0.0f, -18.0f}, {110.0f, 32.0f}, 16.0f);
            p.fill = Fill::solid_color({0.15f, 0.6f, 0.9f, 1.0f});
            p.stroke = PathStroke{.enabled = true, .color = {1, 1, 1, 1}, .width = 2.0f};
            l.path("shape", p);

            l.text("t", {.text = "SUBSCRIBE", .size = {110.0f, 20.0f}, .pos = {0.0f, -13.0f, 0.0f}, .font_size = 10.0f, .color = {1,1,1,1}, .align = TextAlign::Center});
        });

        // ── RIGA 6: Progress / Timeline ──
        // 5,0: Progress 25%
        s.layer("r5c0", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(0, 5));
            draw_cell_card(l, "PROGRESS 25%", 0, 5);
            ProgressBarParams pbp;
            pbp.pos = {0.0f, -18.0f, 0.0f};
            pbp.size = {120.0f, 14.0f};
            pbp.progress = 0.25f;
            pbp.corner_radius = 7.0f;
            pbp.background_style.fill = Fill::solid_color({0.12f, 0.12f, 0.16f, 1.0f});
            pbp.fill_style.fill = Fill::solid_color({0.95f, 0.25f, 0.65f, 1.0f});
            pbp.color = {0.95f, 0.25f, 0.65f, 1.0f};
            l.progress_bar("shape", pbp);
        });

        // 5,1: Progress 75%
        s.layer("r5c1", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(1, 5));
            draw_cell_card(l, "PROGRESS 75%", 1, 5);
            ProgressBarParams pbp;
            pbp.pos = {0.0f, -18.0f, 0.0f};
            pbp.size = {120.0f, 14.0f};
            pbp.progress = 0.75f;
            pbp.corner_radius = 7.0f;
            pbp.background_style.fill = Fill::solid_color({0.12f, 0.12f, 0.16f, 1.0f});
            pbp.fill_style.fill = Fill::solid_color({0.1f, 0.85f, 0.5f, 1.0f});
            pbp.color = {0.1f, 0.85f, 0.5f, 1.0f};
            l.progress_bar("shape", pbp);
        });

        // 5,2: Timeline Bar
        s.layer("r5c2", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(2, 5));
            draw_cell_card(l, "TIMELINE BAR", 2, 5);
            TimelineBarParams tbp;
            tbp.pos = {0.0f, -18.0f, 0.0f};
            tbp.size = {120.0f, 12.0f};
            tbp.start = 0.25f;
            tbp.end = 0.75f;
            tbp.corner_radius = 6.0f;
            tbp.background_style.fill = Fill::solid_color({0.12f, 0.12f, 0.16f, 1.0f});
            tbp.fill_style.fill = Fill::solid_color({0.0f, 0.75f, 0.95f, 1.0f});
            tbp.color = {0.0f, 0.75f, 0.95f, 1.0f};
            l.timeline_bar("shape", tbp);
        });

        // 5,3: Segmented Bar
        s.layer("r5c3", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(3, 5));
            draw_cell_card(l, "SEGMENTED BAR", 3, 5);
            for (int i = 0; i < 5; ++i) {
                f32 seg_x = -50.0f + i * 25.0f;
                PathParams p;
                p.commands = make_rounded_rect_commands({seg_x, -18.0f}, {20.0f, 12.0f}, 4.0f);
                if (i < 3) {
                    p.fill = Fill::solid_color({1.0f, 0.6f, 0.1f, 1.0f});
                } else {
                    p.fill = Fill::solid_color({0.15f, 0.15f, 0.2f, 1.0f});
                }
                l.path("seg_" + std::to_string(i), p);
            }
        });

        // 5,4: Circular Progress
        s.layer("r5c4", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(4, 5));
            draw_cell_card(l, "CIRCULAR PROGRESS", 4, 5);
            PathParams p;
            p.commands = make_rounded_rect_commands({0.0f, -18.0f}, {100.0f, 100.0f}, 50.0f);
            p.fill.enabled = false;
            p.stroke = PathStroke{
                .enabled = true,
                .color = {0.1f, 0.8f, 0.95f, 1.0f},
                .width = 6.0f,
                .trim_start = 0.0f,
                .trim_end = 0.75f
            };
            p.closed = true;
            l.path("shape", p);
        });

        // ── RIGA 7: Effects ──
        // 6,0: Shadow Shape
        s.layer("r6c0", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(0, 6));
            draw_cell_card(l, "SHADOW SHAPE", 0, 6);
            PathParams p;
            p.commands = make_rounded_rect_commands({0.0f, -18.0f}, {90.0f, 48.0f}, 10.0f);
            p.fill = Fill::solid_color({0.15f, 0.15f, 0.2f, 1.0f});
            p.stroke = PathStroke{.enabled = true, .color = {1, 1, 1, 1}, .width = 2.0f};
            l.path("shape", p).with_shadow({
                .enabled = true,
                .offset = {0.0f, -8.0f},
                .color = {0.0f, 0.0f, 0.0f, 0.8f},
                .radius = 8.0f
            });
        });

        // 6,1: Soft Glow
        s.layer("r6c1", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(1, 6));
            draw_cell_card(l, "SOFT GLOW", 1, 6);
            PathParams p;
            p.commands = make_rounded_rect_commands({0.0f, -18.0f}, {90.0f, 48.0f}, 10.0f);
            p.fill = Fill::solid_color({0.1f, 0.1f, 0.12f, 1.0f});
            p.stroke = PathStroke{.enabled = true, .color = {0.0f, 0.95f, 0.95f, 1.0f}, .width = 2.0f};
            l.path("shape", p).with_glow({
                .enabled = true,
                .radius = 12.0f,
                .intensity = 1.0f,
                .color = {0.0f, 0.95f, 0.95f, 1.0f}
            });
        });

        // 6,2: Strong Glow
        s.layer("r6c2", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(2, 6));
            draw_cell_card(l, "STRONG GLOW", 2, 6);
            PathParams p;
            p.commands = make_rounded_rect_commands({0.0f, -18.0f}, {90.0f, 48.0f}, 10.0f);
            p.fill = Fill::solid_color({0.1f, 0.1f, 0.12f, 1.0f});
            p.stroke = PathStroke{.enabled = true, .color = {0.95f, 0.1f, 0.5f, 1.0f}, .width = 3.0f};
            l.path("shape", p).with_glow({
                .enabled = true,
                .radius = 24.0f,
                .intensity = 3.0f,
                .color = {0.95f, 0.1f, 0.5f, 1.0f}
            });
        });

        // 6,3: Blur Shape
        s.layer("r6c3", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(3, 6));
            draw_cell_card(l, "BLUR SHAPE", 3, 6);
            PathParams p;
            p.commands = make_rounded_rect_commands({0.0f, -18.0f}, {80.0f, 44.0f}, 0.0f);
            p.fill = Fill::solid_color({0.2f, 0.8f, 0.3f, 1.0f});
            l.path("shape", p);
            l.blur(10.0f);
        });

        // 6,4: Shape + Blend
        s.layer("r6c4", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(4, 6));
            draw_cell_card(l, "SHAPE + BLEND", 4, 6);
            l.blend(BlendMode::Add);
            
            PathParams p1;
            p1.commands = make_rounded_rect_commands({-15.0f, -18.0f}, {50.0f, 50.0f}, 25.0f);
            p1.fill = Fill::solid_color({1.0f, 0.1f, 0.1f, 0.7f});
            l.path("c1", p1);

            PathParams p2;
            p2.commands = make_rounded_rect_commands({15.0f, -18.0f}, {50.0f, 50.0f}, 25.0f);
            p2.fill = Fill::solid_color({0.1f, 0.1f, 1.0f, 0.7f});
            l.path("c2", p2);
        });

        // ── RIGA 8: Mask-like Shapes ──
        // 7,0: Circle Mask Shape
        s.layer("r7c0", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(0, 7));
            draw_cell_card(l, "CIRCLE MASK SHAPE", 0, 7);
            PathParams p;
            p.commands = make_rounded_rect_commands({0.0f, -18.0f}, {60.0f, 60.0f}, 30.0f);
            p.fill = Fill::solid_color({1, 1, 1, 1});
            l.path("shape", p);
        });

        // 7,1: Rounded Mask Shape
        s.layer("r7c1", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(1, 7));
            draw_cell_card(l, "ROUNDED MASK SHAPE", 1, 7);
            PathParams p;
            p.commands = make_rounded_rect_commands({0.0f, -18.0f}, {100.0f, 50.0f}, 12.0f);
            p.fill = Fill::solid_color({1, 1, 1, 1});
            l.path("shape", p);
        });

        // 7,2: Diagonal Reveal Shape
        s.layer("r7c2", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(2, 7));
            draw_cell_card(l, "DIAGONAL REVEAL SHAPE", 2, 7);
            PathParams p;
            p.commands.push_back(PathCommand::move_to({-50.0f, 20.0f -18.0f}));
            p.commands.push_back(PathCommand::line_to({20.0f, 20.0f -18.0f}));
            p.commands.push_back(PathCommand::line_to({50.0f, -20.0f -18.0f}));
            p.commands.push_back(PathCommand::line_to({-20.0f, -20.0f -18.0f}));
            p.commands.push_back(PathCommand::close());
            p.fill = Fill::solid_color({1, 1, 1, 1});
            l.path("shape", p);
        });

        // 7,3: Split Screen Shape
        s.layer("r7c3", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(3, 7));
            draw_cell_card(l, "SPLIT SCREEN SHAPE", 3, 7);
            PathParams p;
            p.commands = make_rounded_rect_commands({-25.0f, -18.0f}, {45.0f, 54.0f}, 0.0f);
            p.fill = Fill::solid_color({1, 1, 1, 1});
            l.path("shape", p);
        });

        // 7,4: Frame Border
        s.layer("r7c4", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(4, 7));
            draw_cell_card(l, "FRAME BORDER", 4, 7);
            PathParams p;
            p.commands = make_rounded_rect_commands({0.0f, -18.0f}, {110.0f, 54.0f}, 0.0f);
            p.fill.enabled = false;
            p.stroke = PathStroke{.enabled = true, .color = {1, 1, 1, 1}, .width = 4.0f};
            l.path("shape", p);
        });

        return s.build();
    });
}

} // namespace chronon3d::content::shapes
