#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/vector/path_factories.hpp>
#include <cmath>
#include <algorithm>

namespace chronon3d::content::shapes {

// NOTE: no anonymous-namespace canvas-size constants here.  Earlier
// revisions of this file declared `constexpr f32 W = 1920; H = 1080;`
// but they collided with the same identifiers in
// content/images/compositions/*.cpp under unity aggregation.  This
// file only uses CELL_W / CELL_H / GAP_X / GAP_Y below, so no W / H
// are needed.

Composition shape_motion_proofs() {
    return composition({.name = "ShapeMotionProofs", .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // Dark premium background
        s.layer("bg", [](auto& l) {
            l.fill({0.02f, 0.02f, 0.03f, 1.0f});
        });

        // Grid parameters: 4 columns x 2 rows
        constexpr f32 CELL_W = 420.0f;
        constexpr f32 CELL_H = 240.0f;
        constexpr f32 GAP_X = 20.0f;
        constexpr f32 GAP_Y = 20.0f;

        auto get_cell_pos = [=](int col, int row) -> Vec3 {
            // Center the 4x2 grid of 420x240 cells. Total width = 4 * 420 + 3 * 20 = 1740.
            f32 x = -1740.0f * 0.5f + CELL_W * 0.5f + col * (CELL_W + GAP_X);
            f32 y = 500.0f * 0.5f - CELL_H * 0.5f - row * (CELL_H + GAP_Y);
            return {x, y - 20.0f, 0.0f}; // shift down slightly for title room
        };

        auto draw_cell_card = [=](LayerBuilder& l, const std::string& text) {
            // Draw background card (centered at 0,0)
            PathParams cp;
            cp.commands = make_rounded_rect_commands({0.0f, 0.0f}, {CELL_W, CELL_H}, 16.0f);
            cp.fill = Fill::solid_color({0.08f, 0.08f, 0.12f, 0.9f});
            cp.stroke = PathStroke{.enabled = true, .color = {0.15f, 0.15f, 0.22f, 0.5f}, .width = 1.5f};
            l.path("card_bg", cp);

            // Draw label text (in bottom)
            l.text("lbl", TextSpec{.content = {.value = text},.placement = TextPlacement{TextPlacementKind::Absolute, {0.0f, -95.0f}},.font = {.font_size = 14.0f},.layout = {.box = {CELL_W - 30.0f, 24.0f}, .align = TextAlign::Center},.appearance = {.color = {0.6f, 0.7f, 0.9f, 0.9f}},});
        };

        // Title
        s.layer("title", [](auto& l) {
            l.pin_to(Anchor::TopCenter, 20.0f);
            l.text("t", TextSpec{.content = {.value = "SHAPE MOTION GRAPHICS PROOFS"},.font = {.font_size = 28.0f},.layout = {.box = {800.0f, 40.0f}, .align = TextAlign::Center},.appearance = {.color = {0.9f, 0.95f, 1.0f, 1.0f}},});
        });

        const f32 p = ctx.progress();

        // ── ROW 0 ──
        // 0,0: Arrow Draw
        s.layer("m0", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(0, 0));
            draw_cell_card(l, "ARROW DRAW");

            PathParams ap;
            // Large diagonal arrow centered around {0, 15}
            ap.commands.push_back(PathCommand::move_to({-120.0f, -15.0f}));
            ap.commands.push_back(PathCommand::line_to({120.0f, 45.0f}));
            ap.commands.push_back(PathCommand::move_to({120.0f, 45.0f}));
            ap.commands.push_back(PathCommand::line_to({80.0f, 45.0f}));
            ap.commands.push_back(PathCommand::move_to({120.0f, 45.0f}));
            ap.commands.push_back(PathCommand::line_to({120.0f, 5.0f}));
            ap.fill.enabled = false;
            ap.stroke = PathStroke{
                .enabled = true,
                .color = {0.95f, 0.15f, 0.2f, 1.0f},
                .width = 10.0f,
                .cap = LineCap::Round,
                .join = LineJoin::Round,
                .trim_end = std::clamp(p * 1.5f, 0.0f, 1.0f)
            };
            l.path("arrow", ap);
        });

        // 0,1: Circle Reveal
        s.layer("m1", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(1, 0));
            draw_cell_card(l, "CIRCLE REVEAL");

            PathParams cp;
            cp.commands = make_rounded_rect_commands({0.0f, 15.0f}, {120.0f, 120.0f}, 60.0f);
            cp.fill.enabled = false;
            cp.stroke = PathStroke{
                .enabled = true,
                .color = {0.1f, 0.85f, 0.45f, 1.0f},
                .width = 10.0f,
                .trim_start = 0.0f,
                .trim_end = std::clamp(p * 1.4f, 0.0f, 1.0f)
            };
            cp.closed = true;
            l.path("circle", cp);
        });

        // 0,2: Progress Fill
        s.layer("m2", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(2, 0));
            draw_cell_card(l, "PROGRESS FILL");

            ProgressBarParams pbp;
            pbp.pos = {0.0f, 15.0f, 0.0f};
            pbp.size = {280.0f, 30.0f};
            pbp.progress = std::clamp(p * 1.3f, 0.0f, 1.0f);
            pbp.corner_radius = 15.0f;
            pbp.background_style.fill = FillStyle::solid({0.15f, 0.15f, 0.2f, 1.0f});
            pbp.fill_style.fill = FillStyle::solid({0.95f, 0.65f, 0.1f, 1.0f});
            pbp.color = {0.95f, 0.65f, 0.1f, 1.0f};
            l.progress_bar("pb", pbp);
        });

        // 0,3: Timeline Grow
        s.layer("m3", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(3, 0));
            draw_cell_card(l, "TIMELINE GROW");

            TimelineBarParams tbp;
            tbp.pos = {0.0f, 15.0f, 0.0f};
            tbp.size = {280.0f, 30.0f};
            tbp.start = 0.15f;
            tbp.end = 0.15f + 0.7f * std::clamp(p * 1.2f, 0.0f, 1.0f);
            tbp.corner_radius = 15.0f;
            tbp.background_style.fill = FillStyle::solid({0.15f, 0.15f, 0.2f, 1.0f});
            tbp.fill_style.fill = FillStyle::solid({0.1f, 0.75f, 0.95f, 1.0f});
            tbp.color = {0.1f, 0.75f, 0.95f, 1.0f};
            l.timeline_bar("tb", tbp);
        });

        // ── ROW 1 ──
        // 1,0: Badge Pop
        s.layer("m4", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(0, 1));
            draw_cell_card(l, "BADGE POP");

            // Pop scaling animation: bounce elastic scale
            f32 scale = 0.0f;
            if (p < 0.6f) {
                scale = (p / 0.6f) * 1.15f;
            } else {
                scale = 1.15f - ((p - 0.6f) / 0.4f) * 0.15f;
            }
            l.scale({scale, scale, 1.0f});

            BadgeParams bp;
            bp.center = {0.0f, 15.0f};
            bp.radius = 60.0f;
            bp.scallops = 18;
            bp.scallop_depth = 6.0f;
            bp.style.fill = FillStyle::solid({0.95f, 0.2f, 0.5f, 1.0f});
            bp.style.stroke = StrokeStyle{.enabled = true, .color = {1, 1, 1, 1}, .width = 3.0f};
            l.badge("badge", bp);
        });

        // 1,1: Callout Slide
        s.layer("m5", [&](auto& l) {
            // Slide in from left
            f32 slide_x = -180.0f * (1.0f - std::clamp(p * 2.0f, 0.0f, 1.0f));
            l.pin_to(Anchor::Center).position(get_cell_pos(1, 1) + Vec3{slide_x, 0.0f, 0.0f});
            draw_cell_card(l, "CALLOUT SLIDE");

            CalloutParams cp;
            cp.rect_center = {-30.0f, 20.0f};
            cp.rect_size = {180.0f, 60.0f};
            cp.corner_radius = 8.0f;
            cp.target_point = {100.0f, -30.0f};
            cp.pointer_width = 16.0f;
            cp.style.fill = FillStyle::solid({0.0f, 0.0f, 0.0f, 0.85f});
            cp.style.stroke = StrokeStyle{.enabled = true, .color = {0.2f, 0.85f, 0.95f, 1.0f}, .width = 2.0f};
            l.callout("callout", cp);
        });

        // 1,2: Dashed Line Move
        s.layer("m6", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(2, 1));
            draw_cell_card(l, "DASHED LINE MOVE");

            PathParams lp;
            lp.commands.push_back(PathCommand::move_to({-140.0f, 15.0f}));
            lp.commands.push_back(PathCommand::line_to({140.0f, 15.0f}));
            lp.fill.enabled = false;
            lp.stroke = PathStroke{
                .enabled = true,
                .color = {0.1f, 0.95f, 0.3f, 1.0f},
                .width = 8.0f,
                .cap = LineCap::Round,
                .dash_array = {30.0f, 20.0f},
                .dash_offset = p * -150.0f
            };
            l.path("line", lp);
        });

        // 1,3: Glow Pulse
        s.layer("m7", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(3, 1));
            draw_cell_card(l, "GLOW PULSE");

            f32 pulse = std::sin(p * 3.14159265f * 4.0f) * 0.5f + 0.5f;

            PathParams cp;
            cp.commands = make_rounded_rect_commands({0.0f, 15.0f}, {220.0f, 80.0f}, 16.0f);
            cp.fill = Fill::solid_color({0.12f, 0.12f, 0.16f, 1.0f});
            cp.stroke = PathStroke{.enabled = true, .color = {0.7f, 0.15f, 0.95f, 1.0f}, .width = 3.0f};
            l.path("rect", cp).with_glow({
                .enabled = true,
                .radius = 12.0f + pulse * 28.0f,
                .intensity = 1.0f + pulse * 4.0f,
                .color = {0.7f, 0.15f, 0.95f, 1.0f}
            });
        });

        return s.build();
    });
}

} // namespace chronon3d::content::shapes
