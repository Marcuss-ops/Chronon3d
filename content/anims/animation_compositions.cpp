#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/math/color.hpp>

#include <cmath>

namespace chronon3d::content::anims {
namespace {

constexpr f32 W = 1920.0f;
constexpr f32 H = 1080.0f;

static void make_text(LayerBuilder& l, const std::string& name, const std::string& text,
                       f32 font_size, Color color, TextAlign align, f32 tracking,
                       Vec2 sz = {W * 0.7f, 160.0f}, Vec3 pos = {0.0f, 0.0f, 0.0f}) {
    l.text(name, TextParams{
        .text = text,
        .size = sz,
        .pos = pos,
        .font_family = "Inter",
        .font_weight = 800,
        .font_size = font_size,
        .color = color,
        .align = align,
        .line_height = 1.2f,
        .tracking = tracking,
    });
}

// ── AnimLogoReveal ───────────────────────────────────────────────────────
Composition anim_logo_reveal() {
    return composition({
        .name = "AnimLogoReveal",
        .width = 1920, .height = 1080,
        .duration = 90,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 scale = 0.5f + 0.5f * std::min(1.0f, p * 3.0f);
        f32 op = std::min(1.0f, p * 4.0f);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.005f, 0.008f, 0.022f, 1.0f});
        });

        s.layer("logo_ring", [&](LayerBuilder& l) {
            l.opacity(op).pin_to(Anchor::Center).scale({scale, scale, 1});
            l.circle("ring", {
                .radius = 160.0f,
                .color = {0.25f, 0.52f, 1.0f, 0.15f},
            });
        });

        s.layer("logo_core", [&](LayerBuilder& l) {
            l.opacity(op).pin_to(Anchor::Center).scale({scale, scale, 1});
            l.circle("core", {
                .radius = 60.0f,
                .color = {0.25f, 0.52f, 1.0f, 0.6f},
            });
        });

        s.layer("logo_text", [&](LayerBuilder& l) {
            l.opacity(op).pin_to(Anchor::Center)
             .position({0, -60, 0}).scale({scale, scale, 1});
            make_text(l, "label", "CHRONON", 48.0f, {1, 1, 1, 1}, TextAlign::Center, 12.0f);
        });

        // Corner decorations
        for (int i = 0; i < 4; ++i) {
            f32 angle = static_cast<f32>(i) * 1.5708f;
            f32 ex = std::cos(angle) * 300.0f;
            f32 ey = std::sin(angle) * 300.0f;

            s.layer("ray_" + std::to_string(i), [&, ex, ey, p](LayerBuilder& l) {
                f32 ray_op = std::min(1.0f, (p - 0.1f) * 5.0f) * 0.2f *
                    (0.7f + 0.3f * std::sin(p * 6.2832f * 1.5f + static_cast<f32>(i)));
                l.opacity(ray_op).position({ex, ey, 0});
                l.rect("ray_shp", {
                    .size = {120.0f, 2.0f},
                    .color = {0.25f, 0.52f, 1.0f, 1},
                });
            });
        }

        return s.build();
    });
}

// ── AnimSlidePan ─────────────────────────────────────────────────────────
Composition anim_slide_pan() {
    return composition({
        .name = "AnimSlidePan",
        .width = 1920, .height = 1080,
        .duration = 150,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 pan_x = -p * 600.0f;

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.01f, 0.015f, 0.03f, 1.0f});
        });

        for (int i = 0; i < 6; ++i) {
            f32 ex = static_cast<f32>(i) * 320.0f - 800.0f;
            Color c{
                0.2f + 0.3f * (static_cast<f32>(i) / 6.0f),
                0.4f + 0.2f * (1.0f - static_cast<f32>(i) / 6.0f),
                0.8f, 1.0f
            };

            s.layer("elem_" + std::to_string(i), [&, ex, c, p](LayerBuilder& l) {
                f32 elem_op = std::min(1.0f, p * 2.0f) *
                    (1.0f - 0.3f * std::abs(static_cast<f32>(i) - 3.0f) / 6.0f);
                l.opacity(elem_op).position({ex + pan_x, 0, 0});
                l.rounded_rect("box", {
                    .size = {200.0f, 160.0f},
                    .radius = 16.0f,
                    .color = c,
                    .pos = {0, -80 - static_cast<f32>(i % 3) * 120.0f, 0},
                });
            });
        }

        return s.build();
    });
}

// ── AnimZoomIn ───────────────────────────────────────────────────────────
Composition anim_zoom_in() {
    return composition({
        .name = "AnimZoomIn",
        .width = 1920, .height = 1080,
        .duration = 120,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 zoom_z = 800.0f - p * 700.0f;

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.02f, 0.02f, 0.04f, 1.0f});
        });

        for (int i = 0; i < 3; ++i) {
            s.layer("ring_" + std::to_string(i), [&, i](LayerBuilder& l) {
                f32 ring_r = 50.0f + static_cast<f32>(i) * 80.0f + p * 100.0f;
                f32 ring_op = std::min(1.0f, p * 3.0f) * 0.2f;
                l.enable_3d().opacity(ring_op).position({0, 0, zoom_z});
                l.circle("ring_c", {
                    .radius = ring_r,
                    .color = {0.25f, 0.52f, 1.0f, 1},
                });
            });
        }

        s.layer("zoom_label", [&](LayerBuilder& l) {
            l.opacity(std::min(1.0f, p * 3.0f))
             .enable_3d().position({0, 0, zoom_z});
            make_text(l, "label", "ZOOM IN", 64.0f, {1, 1, 1, 1}, TextAlign::Center, 8.0f);
        });

        return s.build();
    });
}

// ── AnimBounce ───────────────────────────────────────────────────────────
Composition anim_bounce() {
    return composition({
        .name = "AnimBounce",
        .width = 1920, .height = 1080,
        .duration = 60,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 t = p * 4.0f;
        f32 raw_y = (t - std::floor(t));
        f32 bounce_y = raw_y * (1.0f - raw_y) * 4.0f * 300.0f;
        f32 y_pos = 300.0f - bounce_y;
        f32 squash = 1.0f + 0.3f * std::sin(t * 6.2832f) * (1.0f - raw_y * 2.0f);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.01f, 0.015f, 0.03f, 1.0f});
        });

        s.layer("floor", [](LayerBuilder& l) {
            l.position({0, 300, 0});
            l.rect("floor_shp", {
                .size = {W * 0.8f, 2.0f},
                .color = {1, 1, 1, 0.2f},
            });
        });

        s.layer("ball", [&](LayerBuilder& l) {
            l.opacity(0.95f).position({0, y_pos, 0})
             .scale({squash, 2.0f - squash, 1});
            l.circle("ball_c", {
                .radius = 40.0f,
                .color = Color{0.25f, 0.52f, 1.0f, 0.95f},
            });
        });

        s.layer("shadow", [&](LayerBuilder& l) {
            f32 shadow_scale = 1.0f - raw_y * 0.6f;
            l.opacity(0.2f * shadow_scale).position({20, 305, 0})
             .scale({shadow_scale, 0.3f, 1});
            l.circle("shadow_c", {
                .radius = 40.0f,
                .color = {0, 0, 0, 1},
            });
        });

        return s.build();
    });
}

// ── AnimFadeChain ────────────────────────────────────────────────────────
Composition anim_fade_chain() {
    return composition({
        .name = "AnimFadeChain",
        .width = 1920, .height = 1080,
        .duration = 150,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 frame = static_cast<f32>(ctx.frame);

        struct FadeItem { std::string text; f32 start; };
        const FadeItem items[] = {
            {"Build",   0.0f},
            {"Render",  25.0f},
            {"Review",  50.0f},
            {"Ship",    75.0f},
        };

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.008f, 0.012f, 0.025f, 1.0f});
        });

        for (size_t i = 0; i < 4; ++i) {
            f32 local = frame - items[i].start;
            f32 fade = 15.0f;
            f32 op = local < 0 ? 0.0f : std::min(1.0f, local / fade);
            f32 offset_x = (1.0f - op) * 60.0f;

            if (op > 0.0f) {
                s.layer("fade_" + std::to_string(i), [&, i, op, offset_x](LayerBuilder& l) {
                    f32 y = -160.0f + static_cast<f32>(i) * 120.0f;
                    l.opacity(op).pin_to(Anchor::Center).position({offset_x, y, 0});

                    l.rect("bar", {
                        .size = {std::min(op * 60.0f, 60.0f), 4.0f},
                        .color = {0.25f, 0.52f, 1.0f, 1},
                        .pos = {-300, 0, 0},
                    });

                    make_text(l, "label", items[i].text, 52.0f, {1, 1, 1, 1},
                              TextAlign::Left, 4.0f, {400.0f, 60.0f}, {-220.0f, 0, 0});
                });
            }
        }

        return s.build();
    });
}

// ── AnimRotate ───────────────────────────────────────────────────────────
Composition anim_rotate() {
    return composition({
        .name = "AnimRotate",
        .width = 1920, .height = 1080,
        .duration = 120,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 angle = p * 360.0f * 2.0f;
        f32 op = std::min(1.0f, p * 3.0f);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.008f, 0.012f, 0.025f, 1.0f});
        });

        s.layer("rot_ring", [&](LayerBuilder& l) {
            l.opacity(op * 0.2f).pin_to(Anchor::Center).rotate({0, 0, angle});
            l.circle("ring", {
                .radius = 200.0f,
                .color = {0.25f, 0.52f, 1.0f, 1},
            });
        });

        s.layer("rot_square", [&](LayerBuilder& l) {
            l.opacity(op * 0.15f).pin_to(Anchor::Center).rotate({0, 0, -angle * 1.5f});
            l.rounded_rect("square", {
                .size = {280.0f, 280.0f},
                .radius = 20.0f,
                .color = {0.25f, 0.52f, 1.0f, 0.5f},
            });
        });

        s.layer("rot_label", [&](LayerBuilder& l) {
            l.opacity(op).pin_to(Anchor::Center);
            make_text(l, "label", "ROTATE", 48.0f, {1, 1, 1, 1}, TextAlign::Center, 8.0f);
        });

        return s.build();
    });
}

} // anonymous namespace

void register_all() {}

} // namespace chronon3d::content::anims

CHRONON_REGISTER_COMPOSITION("AnimLogoReveal",  chronon3d::content::anims::anim_logo_reveal)
CHRONON_REGISTER_COMPOSITION("AnimSlidePan",    chronon3d::content::anims::anim_slide_pan)
CHRONON_REGISTER_COMPOSITION("AnimZoomIn",      chronon3d::content::anims::anim_zoom_in)
CHRONON_REGISTER_COMPOSITION("AnimBounce",      chronon3d::content::anims::anim_bounce)
CHRONON_REGISTER_COMPOSITION("AnimFadeChain",   chronon3d::content::anims::anim_fade_chain)
CHRONON_REGISTER_COMPOSITION("AnimRotate",      chronon3d::content::anims::anim_rotate)
