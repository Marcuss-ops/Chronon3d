#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>
#include <chronon3d/math/color.hpp>

#include <algorithm>
#include <cmath>
namespace chronon3d::content::images {

namespace {

constexpr f32 W = 1920.0f;
constexpr f32 H = 1080.0f;

} // namespace

Composition img_gradient() {
    return composition({.name = "ImgGradient", .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("base", [](auto& l) { l.cache_static().fullscreen_rect("base", {0.02f, 0.02f, 0.06f, 1}); });

        const i32 steps = 32;
        for (i32 i = 0; i < steps; ++i) {
            f32 t = static_cast<f32>(i) / (steps - 1);
            s.layer("band_" + std::to_string(i), [&](auto& l) {
                l.position({0, (t - 0.5f) * H, 0});
                l.rect("rect", {.size = {W, H / steps + 1}, .color = {0.05f + t * 0.2f, 0.02f + t * 0.5f, 0.04f + t * 0.8f, (2.0f / steps)}});
            });
        }

        s.layer("glow", [](auto& l) {
            l.cache_static().pin_to(Anchor::Center).circle("glow", {.radius = 300, .color = {0.25f, 0.52f, 1, 0.06f}});
        });
        return s.build();
    });
}

Composition img_checker() {
    return composition({.name = "ImgChecker", .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 sq = 80.0f;
        const i32 cols = static_cast<i32>(W / sq) + 1;
        const i32 rows = static_cast<i32>(H / sq) + 1;

        s.layer("bg", [](auto& l) { l.cache_static().fill({0.15f, 0.15f, 0.17f, 1}); });

        for (i32 r = 0; r < rows && r < 20; ++r) {
            for (i32 c = 0; c < cols && c < 30; ++c) {
                if ((r + c) % 2 == 0) continue;
                s.layer("check_" + std::to_string(r) + "_" + std::to_string(c), [&](auto& l) {
                    l.position({(c - cols * 0.5f) * sq, (r - rows * 0.5f) * sq, 0});
                    l.rect("sq", {.size = {sq, sq}, .color = {0.18f, 0.18f, 0.20f, 1}});
                });
            }
        }
        return s.build();
    });
}

Composition img_grid_test() {
    return composition({.name = "ImgGridTest", .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](auto& l) { l.cache_static().fill({0.01f, 0.012f, 0.02f, 1}); });
        s.layer("grid", [](auto& l) {
            l.cache_static().grid_background("grid", {.size = {W, H}, .grid_color = {0.18f, 0.52f, 1, 0.15f}, .spacing = 80});
        });
        s.layer("label", [](auto& l) {
            l.pin_to(Anchor::BottomCenter, 40);
            l.text("txt", {
                .text = "GRID RESOLUTION TEST",
                .size = {400, 40},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_family = "Inter",
                .font_weight = 800,
                .font_size = 24.0f,
                .color = Color{0.6f, 0.7f, 0.9f, 0.6f},
                .align = TextAlign::Center,
                .line_height = 1.2f,
                .tracking = 4.0f
            });
        });
        return s.build();
    });
}

Composition img_test_pattern() {
    return composition({.name = "ImgTestPattern", .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](auto& l) { l.cache_static().fill({0.1f, 0.1f, 0.1f, 1}); });
        
        for (int i = 0; i < 8; ++i) {
            s.layer("bar_" + std::to_string(i), [&](auto& l) {
                l.position({(i - 3.5f) * (W / 8), 0, 0});
                l.rect("bar", {.size = {W / 8, H}, .color = { (i & 4) ? 1.0f : 0.0f, (i & 2) ? 1.0f : 0.0f, (i & 1) ? 1.0f : 0.0f, 1.0f }});
            });
        }
        return s.build();
    });
}

Composition img_shake_zoom() {
    return composition({.name = "ImgShakeZoom", .duration = 3}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer("grid", [](auto& l) {
            l.grid_background("grid", {.size = {W, H}, .grid_color = {0.18f, 0.52f, 1, 0.10f}, .spacing = 80});
        });

        f32 t = static_cast<f32>(ctx.frame) / 60.0f;
        f32 shake_x = std::sin(t * 22.0f) * 8.0f * std::exp(-t * 3.0f);
        f32 shake_y = std::cos(t * 18.0f) * 5.0f * std::exp(-t * 2.8f);
        f32 scale = 1.0f + t * 0.03f;

        s.layer("image", [shake_x, shake_y, scale](auto& l) {
            l.pin_to(Anchor::Center);
            l.position({shake_x, shake_y, 0});
            l.scale({scale, scale, 1.0f});
            l.image("hero", {
                .path = "tools/telemetry_dashboard/frontend/src/assets/hero.png",
                .size = {800, 480},
                .radius = 24.0f
            });
        });
        return s.build();
    });
}

Composition img_reference_shake_reveal() {
    return composition({.name = "ImgReferenceShakeReveal", .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        scene::utils::dark_grid_background(s, ctx, {
            .bg_color = {0.008f, 0.010f, 0.022f, 1.0f},
            .grid_color = {0.18f, 0.52f, 1.0f, 0.08f},
            .spacing = 88.0f,
            .centered = true
        });

        const f32 p = std::clamp(static_cast<f32>(ctx.progress()), 0.0f, 1.0f);
        const f32 t = static_cast<f32>(ctx.frame) / ctx.fps();

        const f32 fade = std::clamp(p / 0.22f, 0.0f, 1.0f);
        const f32 fade_eased = fade * fade * (3.0f - 2.0f * fade);
        const Vec2 image_size{640.0f, 360.0f};
        const f32 zoom = 0.94f + p * 0.08f;
        const f32 shake_x = std::sin(t * 16.0f) * 1.4f * std::exp(-p * 2.2f);
        const f32 shake_y = std::cos(t * 13.0f) * 0.9f * std::exp(-p * 2.0f);

        s.layer("reference_image", [fade_eased, shake_x, shake_y, image_size, zoom](auto& l) {
            l.opacity(fade_eased)
             .pin_to(Anchor::Center)
             .position({shake_x, shake_y, 0.0f})
             .scale({zoom, zoom, 1.0f})
             .image("camera_reference", {
                 .path = "assets/images/camera_reference.jpg",
                 .size = image_size,
                 .radius = 0.0f
             });
        });

        return s.build();
    });
}

Composition img_corner_smoothing() {
    return composition({.name = "ImgCornerSmoothing", .duration = 3}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](auto& l) { l.fill({0.01f, 0.012f, 0.02f, 1}); });
        s.layer("grid", [](auto& l) {
            l.grid_background("grid", {.size = {W, H}, .grid_color = {0.18f, 0.52f, 1, 0.12f}, .spacing = 80});
        });

        f32 t = static_cast<f32>(ctx.frame) / 60.0f;
        f32 radius = (std::sin(t * glm::pi<float>()) * 0.5f + 0.5f) * 60.0f;

        // 1. Animated Rounded Rectangle
        s.layer("rect_layer", [radius](auto& l) {
            l.position({-450, 0, 0});
            l.rounded_rect("r1", {
                .size = {320, 320},
                .radius = radius,
                .color = Color{0.15f, 0.42f, 0.95f, 1.0f}
            });
        });

        // 2. Animated Rounded Image
        s.layer("image_layer", [radius](auto& l) {
            l.position({0, 0, 0});
            l.image("img1", {
                .path = "tools/telemetry_dashboard/frontend/src/assets/hero.png",
                .size = {320, 320},
                .radius = radius
            });
        });

        // 3. Dynamic Text block with rounded box styling
        s.layer("text_layer", [radius](auto& l) {
            l.position({450, 0, 0});
            l.text("txt", {
                .text = "SMOOTH\nCORNERS",
                .size = {320, 320},
                .font_family = "Inter",
                .font_weight = 800,
                .font_size = 32.0f,
                .color = Color{1, 1, 1, 1},
                .align = TextAlign::Center,
                .line_height = 1.3f,
                .box_style = {
                    .enabled = true,
                    .padding = {20, 110},
                    .radius = radius,
                    .background = Color{0.08f, 0.08f, 0.1f, 0.9f},
                    .border_enabled = true,
                    .border_color = Color{0.3f, 0.6f, 1.0f, 0.4f},
                    .border_width = 2.0f
                }
            });
        });

        return s.build();
    });
}

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
            l.text("lbl", TextParams{
                .text = text,
                .size = {get_cell_box(col, row).x, 18.0f},
                .pos = get_label_pos(col, row),
                .font_size = 12.0f,
                .color = {0.6f, 0.7f, 0.9f, 0.8f},
                .align = TextAlign::Center,
            });
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
            l.text("card_title", TextParams{
                .text = "LENS SPECS",
                .size = {box.x - 24.0f, 18.0f},
                .pos = {0.0f, box.y * 0.25f, 0.0f},
                .font_size = 12.0f,
                .color = Color::white(),
                .align = TextAlign::Center
            });
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

} // namespace chronon3d::content::images

// Compositions are now registered via ImagesModule in images_module.cpp
// (ExtensionRegistry pattern).  Do NOT also CHRONON_REGISTER_COMPOSITION
// here or we get duplicate-registration runtime errors from
// `ExtensionRegistry::register_composition` ("Duplicate composition: ...").

