#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/math/color.hpp>

#include <string>
#include <cmath>

#include "helpers/text_helpers.hpp"

namespace chronon3d::content::text {

namespace {

// Grid layout: 4 columns x 4 rows
constexpr int COLS = 4;
constexpr int ROWS = 4;
constexpr f32 TOP_MARGIN = 64.0f;
constexpr f32 CELL_W = W / static_cast<f32>(COLS);
constexpr f32 CELL_H = (H - TOP_MARGIN) / static_cast<f32>(ROWS);
constexpr f32 PAD = 16.0f;

Vec3 cell_pos(int col, int row) {
    f32 x = -W * 0.5f + CELL_W * (static_cast<f32>(col) + 0.5f);
    f32 y = -H * 0.5f + TOP_MARGIN + CELL_H * (static_cast<f32>(row) + 0.5f);
    return {x, y, 0.0f};
}

Vec2 cell_box(int col, int row) {
    return {CELL_W - PAD * 2.0f, CELL_H - PAD * 2.0f - 24.0f};
}

Vec3 label_pos(int col, int row) {
    return cell_pos(col, row) + Vec3{0.0f, -CELL_H * 0.5f + 14.0f, 0.0f};
}

void cell_label(LayerBuilder& l, const std::string& name, const std::string& text,
                int col, int row, Color color = {0.4f, 0.4f, 0.5f, 1.0f}) {
    l.text(name, TextParams{
        .text = text,
        .size = {cell_box(col, row).x, 18.0f},
        .pos = label_pos(col, row),
        .font_size = 12.0f,
        .color = color,
        .align = TextAlign::Center,
    });
}

} // namespace

Composition text_proofs() {
    return composition({.name = "TextProofs", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 op = std::min(1.0f, ctx.progress() * 3.0f);
        f32 t = static_cast<f32>(ctx.seconds());

        // Background
        s.layer("bg", [](auto& l) { l.fill({0.02f, 0.02f, 0.04f, 1.0f}); });

        // ── Row 0: Presets ──────────────────────────────────────

        // 0,0: Headline center wrap
        s.layer("r0c0_label", [&](auto& l) {
            l.opacity(op);
            cell_label(l, "lbl", "HEADLINE CENTER", 0, 0);
        });
        s.layer("r0c0", [&](auto& l) {
            l.opacity(op).pin_to(Anchor::Center).position(cell_pos(0, 0));
            apply_text(l, "headline", TextDef{
                .text = "BIG BOLD HEADLINE TEXT THAT WRAPS",
                .size = 40.0f, .color = {1, 1, 1, 1}, .align = TextAlign::Center
            }, cell_box(0, 0));
        });

        // 0,1: Subtitle with box
        s.layer("r0c1_label", [&](auto& l) {
            l.opacity(op);
            cell_label(l, "lbl", "SUBTITLE BOX", 1, 0);
        });
        s.layer("r0c1", [&](auto& l) {
            l.opacity(op).pin_to(Anchor::Center).position(cell_pos(1, 0));
            l.text("sub", TextParams{
                .text = "Subtitle with background box",
                .size = cell_box(1, 0),
                .font_size = 28.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .box_style = {.enabled = true, .padding = {12.0f, 8.0f}, .radius = 6.0f,
                              .background = {0.15f, 0.15f, 0.25f, 0.9f}},
            });
        });

        // 0,2: Lower third
        s.layer("r0c2_label", [&](auto& l) {
            l.opacity(op);
            cell_label(l, "lbl", "LOWER THIRD", 2, 0);
        });
        s.layer("r0c2", [&](auto& l) {
            l.opacity(op).pin_to(Anchor::Center).position(cell_pos(2, 0) + Vec3{0.0f, 20.0f, 0.0f});
            apply_text(l, "lt_name", TextDef{
                .text = "JOHN DOE", .size = 26.0f,
                .color = {1, 1, 1, 1}, .align = TextAlign::Left, .font_weight = 700
            }, {cell_box(2, 0).x, 30.0f}, {0, -15, 0});
            apply_text(l, "lt_role", TextDef{
                .text = "Creative Director", .size = 18.0f,
                .color = {0.6f, 0.6f, 0.7f, 1}, .align = TextAlign::Left, .font_weight = 400
            }, {cell_box(2, 0).x, 24.0f}, {0, 15, 0});
        });

        // 0,3: Breaking news
        s.layer("r0c3_label", [&](auto& l) {
            l.opacity(op);
            cell_label(l, "lbl", "BREAKING NEWS", 3, 0);
        });
        s.layer("r0c3", [&](auto& l) {
            l.opacity(op).pin_to(Anchor::Center).position(cell_pos(3, 0));
            l.text("breaking", TextParams{
                .text = "BREAKING NEWS",
                .size = cell_box(3, 0),
                .font_size = 32.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .box_style = {.enabled = true, .padding = {10.0f, 6.0f}, .radius = 4.0f,
                              .background = {0.8f, 0.0f, 0.0f, 1.0f}},
                .paint = {.fill = {1, 1, 1, 1}, .stroke_enabled = true,
                          .stroke_color = {0.0f, 0.0f, 0.0f, 1.0f}, .stroke_width = 1.5f},
                .shadows = {{.enabled = true, .offset = {2.0f, 2.0f},
                             .blur = 4.0f, .opacity = 0.8f, .color = {0.0f, 0.0f, 0.0f, 1.0f}}},
            });
        });

        // ── Row 1: Layout edge cases ────────────────────────────

        // 1,0: Auto fit single line
        s.layer("r1c0_label", [&](auto& l) {
            l.opacity(op);
            cell_label(l, "lbl", "AUTO FIT SINGLE LINE", 0, 1);
        });
        s.layer("r1c0", [&](auto& l) {
            l.opacity(op).pin_to(Anchor::Center).position(cell_pos(0, 1));
            l.text("autofit", TextParams{
                .text = "AUTO FIT SINGLE LINE TEXT",
                .size = cell_box(0, 1),
                .font_size = 72.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .auto_fit = true,
                .max_lines = 1,
                .min_font_size = 10.0f,
                .max_font_size = 72.0f,
                .wrap = TextWrap::None,
            });
        });

        // 1,1: Ellipsis 1 line
        s.layer("r1c1_label", [&](auto& l) {
            l.opacity(op);
            cell_label(l, "lbl", "ELLIPSIS 1 LINE", 1, 1);
        });
        s.layer("r1c1", [&](auto& l) {
            l.opacity(op).pin_to(Anchor::Center).position(cell_pos(1, 1));
            l.text("ellipsis1", TextParams{
                .text = "This is a very long text that should be truncated with ellipsis on a single line",
                .size = cell_box(1, 1),
                .font_size = 22.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Left,
                .max_lines = 1,
                .ellipsis = true,
                .wrap = TextWrap::Word,
            });
        });

        // 1,2: Ellipsis 2 lines
        s.layer("r1c2_label", [&](auto& l) {
            l.opacity(op);
            cell_label(l, "lbl", "ELLIPSIS 2 LINES", 2, 1);
        });
        s.layer("r1c2", [&](auto& l) {
            l.opacity(op).pin_to(Anchor::Center).position(cell_pos(2, 1));
            l.text("ellipsis2", TextParams{
                .text = "This text should wrap to two lines and then show ellipsis at the end of the second line when it runs out of space",
                .size = cell_box(2, 1),
                .font_size = 20.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Left,
                .max_lines = 2,
                .ellipsis = true,
                .wrap = TextWrap::Word,
            });
        });

        // 1,3: Character wrap (with narrow box & border to show wrap boundary)
        s.layer("r1c3_label", [&](auto& l) {
            l.opacity(op);
            cell_label(l, "lbl", "CHARACTER WRAP", 3, 1);
        });
        s.layer("r1c3", [&](auto& l) {
            l.opacity(op).pin_to(Anchor::Center).position(cell_pos(3, 1));
            Vec2 box_sz = cell_box(3, 1);
            box_sz.x *= 0.5f; // make it narrow to force wrapping
            l.text("charwrap", TextParams{
                .text = "CharacterWrapTestNoSpaces",
                .size = box_sz,
                .font_size = 22.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Left,
                .box_style = {.enabled = true, .padding = {6.0f, 4.0f}, .radius = 2.0f,
                              .background = {0.0f, 0.0f, 0.0f, 0.0f},
                              .border_enabled = true, .border_color = {0.3f, 0.3f, 0.4f, 0.6f}, .border_width = 1.0f},
                .wrap = TextWrap::Character,
            });
        });

        // ── Row 2: Styling edge cases ───────────────────────────

        // 2,0: Auto fit multiline
        s.layer("r2c0_label", [&](auto& l) {
            l.opacity(op);
            cell_label(l, "lbl", "AUTO FIT MULTILINE", 0, 2);
        });
        s.layer("r2c0", [&](auto& l) {
            l.opacity(op).pin_to(Anchor::Center).position(cell_pos(0, 2));
            l.text("longword", TextParams{
                .text = "Auto fit multiline text that has to shrink to fit within the box boundaries across multiple lines cleanly",
                .size = cell_box(0, 2),
                .font_size = 48.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .auto_fit = true,
                .max_lines = 3,
                .min_font_size = 10.0f,
                .max_font_size = 48.0f,
                .wrap = TextWrap::Word,
            });
        });

        // 2,1: Stroke + shadow (thick stroke, heavy shadow, lighter box background)
        s.layer("r2c1_label", [&](auto& l) {
            l.opacity(op);
            cell_label(l, "lbl", "STROKE + SHADOW", 1, 2);
        });
        s.layer("r2c1", [&](auto& l) {
            l.opacity(op).pin_to(Anchor::Center).position(cell_pos(1, 2));
            l.text("stroke", TextParams{
                .text = "STROKE",
                .size = cell_box(1, 2),
                .font_size = 44.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .box_style = {.enabled = true, .padding = {12.0f, 8.0f}, .radius = 6.0f,
                              .background = {0.4f, 0.4f, 0.5f, 0.8f}},
                .paint = {.fill = {1, 1, 1, 1}, .stroke_enabled = true,
                          .stroke_color = {0.0f, 0.0f, 0.0f, 1.0f}, .stroke_width = 5.0f},
                .shadows = {{.enabled = true, .offset = {8.0f, 8.0f},
                             .blur = 16.0f, .opacity = 0.9f, .color = {0.0f, 0.0f, 0.0f, 1.0f}}},
            });
        });

        // 2,2: Only spaces
        s.layer("r2c2_label", [&](auto& l) {
            l.opacity(op);
            cell_label(l, "lbl", "ONLY SPACES", 2, 2);
        });
        s.layer("r2c2", [&](auto& l) {
            l.opacity(op).pin_to(Anchor::Center).position(cell_pos(2, 2));
            l.text("spaces", TextParams{
                .text = "     ",
                .size = cell_box(2, 2),
                .font_size = 24.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .box_style = {.enabled = true, .padding = {8.0f, 4.0f}, .radius = 4.0f,
                              .background = {0.15f, 0.0f, 0.0f, 0.5f}},
            });
            // Secondary help label for only spaces test
            l.text("spaces_help", TextParams{
                .text = "empty text should not crash",
                .size = {cell_box(2, 2).x, 14.0f},
                .pos = {0.0f, cell_box(2, 2).y * 0.35f, 0.0f},
                .font_size = 10.0f,
                .color = {0.4f, 0.4f, 0.5f, 0.8f},
                .align = TextAlign::Center,
            });
        });

        // 2,3: Manual newline
        s.layer("r2c3_label", [&](auto& l) {
            l.opacity(op);
            cell_label(l, "lbl", "MANUAL NEWLINE", 3, 2);
        });
        s.layer("r2c3", [&](auto& l) {
            l.opacity(op).pin_to(Anchor::Center).position(cell_pos(3, 2));
            l.text("newline", TextParams{
                .text = "Line One\nLine Two\nLine Three",
                .size = cell_box(3, 2),
                .font_size = 22.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .line_height = 1.4f,
            });
        });

        // ── Row 3: Alignment + special ──────────────────────────

        // 3,0: Vertical middle
        s.layer("r3c0_label", [&](auto& l) {
            l.opacity(op);
            cell_label(l, "lbl", "VERT MIDDLE", 0, 3);
        });
        s.layer("r3c0", [&](auto& l) {
            l.opacity(op).pin_to(Anchor::Center).position(cell_pos(0, 3));
            l.text("vmid", TextParams{
                .text = "Vertically centered",
                .size = cell_box(0, 3),
                .font_size = 28.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .box_style = {.enabled = true, .padding = {8.0f, 4.0f}, .radius = 4.0f,
                              .background = {0.0f, 0.1f, 0.2f, 0.5f}},
            });
        });

        // 3,1: Right align
        s.layer("r3c1_label", [&](auto& l) {
            l.opacity(op);
            cell_label(l, "lbl", "RIGHT ALIGN", 1, 3);
        });
        s.layer("r3c1", [&](auto& l) {
            l.opacity(op).pin_to(Anchor::Center).position(cell_pos(1, 3));
            l.text("right", TextParams{
                .text = "Right aligned\ntext here",
                .size = cell_box(1, 3),
                .font_size = 24.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Right,
                .vertical_align = VerticalAlign::Middle,
                .line_height = 1.4f,
                .box_style = {.enabled = true, .padding = {12.0f, 8.0f}, .radius = 4.0f,
                              .background = {0.1f, 0.1f, 0.15f, 0.9f}},
            });
        });

        // 3,2: Neon preset (with text-level glow shadows)
        s.layer("r3c2_label", [&](auto& l) {
            l.opacity(op);
            cell_label(l, "lbl", "NEON GLOW", 2, 3);
        });
        s.layer("r3c2", [&](auto& l) {
            l.opacity(op).pin_to(Anchor::Center).position(cell_pos(2, 3));
            l.text("neon", TextParams{
                .text = "NEON",
                .size = cell_box(2, 3),
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 800,
                .font_size = 48.0f,
                .color = {0.0f, 1.0f, 0.8f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 6.0f,
                .shadows = {
                    {.enabled = true, .offset = {0,0}, .blur = 8.0f, .opacity = 1.0f,
                     .color = {0.0f, 1.0f, 0.8f, 0.8f}},
                    {.enabled = true, .offset = {0,0}, .blur = 20.0f, .opacity = 1.0f,
                     .color = {0.0f, 1.0f, 0.8f, 0.5f}},
                    {.enabled = true, .offset = {0,0}, .blur = 40.0f, .opacity = 1.0f,
                     .color = {0.0f, 1.0f, 0.8f, 0.3f}},
                },
            });
        });

        // 3,3: Animated pulse (opacity changes over time)
        s.layer("r3c3_label", [&](auto& l) {
            l.opacity(op);
            cell_label(l, "lbl", "PULSE ANIM", 3, 3);
        });
        s.layer("r3c3", [&](auto& l) {
            f32 pulse = 0.5f + 0.5f * std::sin(t * 3.0f);
            l.opacity(op * pulse).pin_to(Anchor::Center).position(cell_pos(3, 3));
            l.text("pulse", TextParams{
                .text = "PULSE",
                .size = cell_box(3, 3),
                .font_size = 40.0f,
                .color = {0.3f, 0.8f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
            });
        });

        // Title bar
        s.layer("title", [&](auto& l) {
            l.opacity(op).pin_to(Anchor::TopCenter, 20.0f);
            apply_text(l, "title_txt", TextDef{
                .text = "TEXT ENGINE PROOF SHEET  |  ALL CASES",
                .size = 18.0f, .color = {0.5f, 0.5f, 0.6f, 1}, .align = TextAlign::Center,
                .font_weight = 400
            }, {W * 0.8f, 24.0f});
        });

        return s.build();
    });
}

} // namespace chronon3d::content::text
