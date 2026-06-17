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

// ── ImgShakeZoom ─────────────────────────────────────────────────────────
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

// ── ImgReferenceShakeReveal ──────────────────────────────────────────────
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

// ── ImgCornerSmoothing ───────────────────────────────────────────────────
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

} // namespace chronon3d::content::images

// Compositions are registered via ImagesModule in images_module.cpp.
// Do NOT also CHRONON_REGISTER_COMPOSITION here.
