#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/math/color.hpp>

#include <algorithm>

namespace chronon3d::content::images {

namespace {

constexpr f32 W = 1920.0f;
constexpr f32 H = 1080.0f;

} // namespace

Composition image_proofs() {
    return composition({.name = "ImageProofs", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const std::string img_path = "assets/images/camera_reference.jpg";
        const std::string png_path = "assets/images/grid_tile.png";

        // Background
        s.layer("bg", [](auto& l) { l.fill({0.02f, 0.02f, 0.04f, 1.0f}); });

        // Grid layout helpers (4 columns x 5 rows)
        constexpr int COLS = 4;
        constexpr int ROWS = 5;
        constexpr f32 TOP_MARGIN = 64.0f;
        constexpr f32 CELL_W = W / static_cast<f32>(COLS);
        constexpr f32 CELL_H = (H - TOP_MARGIN) / static_cast<f32>(ROWS);
        constexpr f32 PAD = 16.0f;

        auto get_cell_pos = [=](int col, int row) -> Vec3 {
            f32 x = -W * 0.5f + CELL_W * (static_cast<f32>(col) + 0.5f);
            f32 y = -H * 0.5f + TOP_MARGIN + CELL_H * (static_cast<f32>(row) + 0.5f);
            return {x, y, 0.0f};
        };

        auto get_cell_box = [=](int col, int row) -> Vec2 {
            return {CELL_W - PAD * 2.0f, CELL_H - PAD * 2.0f - 24.0f};
        };

        auto get_label_pos = [=](int col, int row) -> Vec3 {
            return get_cell_pos(col, row) + Vec3{0.0f, -CELL_H * 0.5f + 14.0f, 0.0f};
        };

        auto draw_cell_label = [=](LayerBuilder& l, const std::string& text, int col, int row) {
            l.text("lbl", TextSpec{.content = {.value = text}, .font = {.font_size = 12.0f}, .layout = {.box = {get_cell_box(col, row).x, 18.0f}, .align = TextAlign::Center}, .appearance = {.color = {0.6f, 0.7f, 0.9f, 0.8f}}});
        };

        // ── ROW 0: Fitting Modes ──
        // 0,0: Cover Standard
        s.layer("r0c0_lbl", [&](auto& l) { draw_cell_label(l, "COVER STANDARD", 0, 0); });
        s.layer("r0c0", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(0, 0));
            l.image("img", { .path = img_path, .size = get_cell_box(0, 0), .fit = FitMode::Cover });
        });

        // 0,1: Contain Vertical Box (shows letterbox clearly)
        s.layer("r0c1_lbl", [&](auto& l) { draw_cell_label(l, "CONTAIN VERTICAL BOX", 1, 0); });
        s.layer("r0c1", [&](auto& l) {
            Vec2 box = get_cell_box(1, 0);
            Vec2 vert_box = { box.x * 0.5f, box.y }; // vertical box aspect ratio
            l.pin_to(Anchor::Center).position(get_cell_pos(1, 0));
            l.image("img", { .path = img_path, .size = vert_box, .fit = FitMode::Contain });
        });

        // 0,2: Contain Horizontal Box (shows letterbox clearly)
        s.layer("r0c2_lbl", [&](auto& l) { draw_cell_label(l, "CONTAIN HORIZON BOX", 2, 0); });
        s.layer("r0c2", [&](auto& l) {
            Vec2 box = get_cell_box(2, 0);
            Vec2 horiz_box = { box.x, box.y * 0.5f }; // horizontal box aspect ratio
            l.pin_to(Anchor::Center).position(get_cell_pos(2, 0));
            l.image("img", { .path = img_path, .size = horiz_box, .fit = FitMode::Contain });
        });

        // 0,3: Highly Distorted Stretch
        s.layer("r0c3_lbl", [&](auto& l) { draw_cell_label(l, "DISTORTED STRETCH", 3, 0); });
        s.layer("r0c3", [&](auto& l) {
            Vec2 box = get_cell_box(3, 0);
            Vec2 distorted_box = { box.x * 0.4f, box.y * 1.2f }; // highly distorted vertical
            l.pin_to(Anchor::Center).position(get_cell_pos(3, 0));
            l.image("img", { .path = img_path, .size = distorted_box, .fit = FitMode::Stretch });
        });

        // ── ROW 1: Focal Points ──
        // 1,0: Focal Top
        s.layer("r1c0_lbl", [&](auto& l) { draw_cell_label(l, "FOCAL TOP (0.5, 0.0)", 0, 1); });
        s.layer("r1c0", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(0, 1));
            l.image("img", { .path = img_path, .size = get_cell_box(0, 1), .fit = FitMode::Cover, .focal_point = {0.5f, 0.0f} });
        });

        // 1,1: Focal Bottom
        s.layer("r1c1_lbl", [&](auto& l) { draw_cell_label(l, "FOCAL BOTTOM (0.5, 1.0)", 1, 1); });
        s.layer("r1c1", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(1, 1));
            l.image("img", { .path = img_path, .size = get_cell_box(1, 1), .fit = FitMode::Cover, .focal_point = {0.5f, 1.0f} });
        });

        // 1,2: Focal Left
        s.layer("r1c2_lbl", [&](auto& l) { draw_cell_label(l, "FOCAL LEFT (0.0, 0.5)", 2, 1); });
        s.layer("r1c2", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(2, 1));
            l.image("img", { .path = img_path, .size = get_cell_box(2, 1), .fit = FitMode::Cover, .focal_point = {0.0f, 0.5f} });
        });

        // 1,3: Focal Right
        s.layer("r1c3_lbl", [&](auto& l) { draw_cell_label(l, "FOCAL RIGHT (1.0, 0.5)", 3, 1); });
        s.layer("r1c3", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(3, 1));
            l.image("img", { .path = img_path, .size = get_cell_box(3, 1), .fit = FitMode::Cover, .focal_point = {1.0f, 0.5f} });
        });

        // ── ROW 2: Styles & Filters ──
        // 2,0: Opacity 50%
        s.layer("r2c0_lbl", [&](auto& l) { draw_cell_label(l, "OPACITY 50%", 0, 2); });
        s.layer("r2c0", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(0, 2));
            l.image("img", { .path = img_path, .size = get_cell_box(0, 2), .fit = FitMode::Cover, .opacity = 0.5f });
        });

        // 2,1: Color Tint Cyan
        s.layer("r2c1_lbl", [&](auto& l) { draw_cell_label(l, "TINT CYAN 60%", 1, 2); });
        s.layer("r2c1", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(1, 2))
             .tint(Color{0.0f, 0.8f, 1.0f, 1.0f}, 0.6f);
            l.image("img", { .path = img_path, .size = get_cell_box(1, 2), .fit = FitMode::Cover });
        });

        // 2,2: Image inside card with text
        s.layer("r2c2_lbl", [&](auto& l) { draw_cell_label(l, "CARD WITH TEXT", 2, 2); });
        s.layer("r2c2_card", [&](auto& l) {
            Vec2 box = get_cell_box(2, 2);
            l.pin_to(Anchor::Center).position(get_cell_pos(2, 2));
            // Draw background rectangle
            l.rounded_rect("card_bg", { .size = box, .radius = 16.0f, .color = {0.1f, 0.11f, 0.16f, 1.0f} });
            // Draw thumbnail image
            l.image("card_thumb", {
                .path = img_path,
                .size = {box.x - 16.0f, box.y * 0.6f},
                .pos = {0.0f, -box.y * 0.15f, 0.0f},
                .fit = FitMode::Cover,
                .radius = 8.0f
            });
            // Draw card title text
            l.text("card_title", TextSpec{.content = {.value = "LENS SPECS"}, .placement = {TextPlacementKind::Absolute, {0.0f, box.y * 0.25f}}, .font = {.font_size = 12.0f}, .layout = {.box = {box.x - 24.0f, 18.0f}, .align = TextAlign::Center}, .appearance = {.color = Color::white()}});
        });

        // 2,3: Image with mask rounded rect
        s.layer("r2c3_lbl", [&](auto& l) { draw_cell_label(l, "MASK ROUNDED RECT", 3, 2); });
        s.layer("r2c3", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(3, 2))
             .mask_rounded_rect({ .size = get_cell_box(3, 2), .radius = 24.0f });
            l.image("img", { .path = img_path, .size = get_cell_box(3, 2), .fit = FitMode::Cover });
        });

        // ── ROW 3: Glow & Shadow ──
        // 3,0: Soft Glow
        s.layer("r3c0_lbl", [&](auto& l) { draw_cell_label(l, "SOFT GLOW", 0, 3); });
        s.layer("r3c0", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(0, 3))
             .glow(GlowParams{.radius = 8.0f, .intensity = 0.6f, .color = Color{0.0f, 1.0f, 0.8f, 0.8f}});
            l.image("img", { .path = img_path, .size = get_cell_box(0, 3), .fit = FitMode::Cover, .radius = 16.0f });
        });

        // 3,1: Strong Glow
        s.layer("r3c1_lbl", [&](auto& l) { draw_cell_label(l, "STRONG GLOW", 1, 3); });
        s.layer("r3c1", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(1, 3))
             .glow(GlowParams{.radius = 24.0f, .intensity = 1.5f, .color = Color{0.0f, 1.0f, 0.8f, 0.8f}});
            l.image("img", { .path = img_path, .size = get_cell_box(1, 3), .fit = FitMode::Cover, .radius = 16.0f });
        });

        // 3,2: Shadow Card
        s.layer("r3c2_lbl", [&](auto& l) { draw_cell_label(l, "SHADOW CARD", 2, 3); });
        s.layer("r3c2", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(2, 3))
             .drop_shadow({6.0f, 6.0f}, {0.0f, 0.0f, 0.0f, 0.6f}, 12.0f);
            l.image("img", { .path = img_path, .size = get_cell_box(2, 3), .fit = FitMode::Cover, .radius = 16.0f });
        });

        // 3,3: Blurred Background
        s.layer("r3c3_lbl", [&](auto& l) { draw_cell_label(l, "BLURRED BACKGROUND", 3, 3); });
        s.layer("r3c3_bg", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(3, 3) + Vec3{0.0f, 0.0f, -10.0f})
             .opacity(0.4f)
             .blur(16.0f);
            l.image("img_bg", { .path = img_path, .size = get_cell_box(3, 3), .fit = FitMode::Cover });
        });
        s.layer("r3c3_fg", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(3, 3));
            l.image("img_fg", { .path = img_path, .size = get_cell_box(3, 3) * 0.7f, .fit = FitMode::Contain });
        });

        // ── ROW 4: Masks & Fallback ──
        // 4,0: Circle Mask Small
        s.layer("r4c0_lbl", [&](auto& l) { draw_cell_label(l, "CIRCLE MASK SMALL", 0, 4); });
        s.layer("r4c0", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(0, 4))
             .mask_circle({ .radius = 30.0f });
            l.image("img", { .path = img_path, .size = get_cell_box(0, 4), .fit = FitMode::Cover });
        });

        // 4,1: Circle Mask Large (Avatar Mask)
        s.layer("r4c1_lbl", [&](auto& l) { draw_cell_label(l, "AVATAR MASK LARGE", 1, 4); });
        s.layer("r4c1", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(1, 4))
             .mask_circle({ .radius = 70.0f });
            l.image("img", { .path = img_path, .size = get_cell_box(1, 4), .fit = FitMode::Cover });
        });

        // 4,2: Missing Image Fallback (Red Cross)
        s.layer("r4c2_lbl", [&](auto& l) { draw_cell_label(l, "MISSING IMAGE FALLBACK", 2, 4); });
        s.layer("r4c2", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(2, 4));
            l.image("img", { .path = "assets/images/does_not_exist.jpg", .size = get_cell_box(2, 4) });
        });

        // 4,3: Transparent PNG
        s.layer("r4c3_lbl", [&](auto& l) { draw_cell_label(l, "TRANSPARENT PNG", 3, 4); });
        s.layer("r4c3", [&](auto& l) {
            l.pin_to(Anchor::Center).position(get_cell_pos(3, 4));
            l.image("img", { .path = png_path, .size = get_cell_box(3, 4), .fit = FitMode::Contain });
        });

        return s.build();
    });
}

// Forward-declare factories from companion files
Composition img_gradient();
Composition img_checker();
Composition img_grid_test();
Composition img_test_pattern();
Composition img_shake_zoom();
Composition img_reference_shake_reveal();
Composition img_corner_smoothing();

// ── Per-domain registration ──────────────────────────────────────────────────
void register_image_compositions(CompositionRegistry& registry) {
    registry.add("ImgGradient", [](const CompositionProps&) { return img_gradient(); });
    registry.add("ImgChecker", [](const CompositionProps&) { return img_checker(); });
    registry.add("ImgGridTest", [](const CompositionProps&) { return img_grid_test(); });
    registry.add("ImgTestPattern", [](const CompositionProps&) { return img_test_pattern(); });
    registry.add("ImgShakeZoom", [](const CompositionProps&) { return img_shake_zoom(); });
    registry.add("ImgReferenceShakeReveal", [](const CompositionProps&) { return img_reference_shake_reveal(); });
    registry.add("ImgCornerSmoothing", [](const CompositionProps&) { return img_corner_smoothing(); });
    registry.add("ImageProofs", [](const CompositionProps&) { return image_proofs(); });
}

} // namespace chronon3d::content::images
