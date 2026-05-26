#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/math/color.hpp>

#include <cmath>
#include <string>
#include <vector>

namespace chronon3d::content::images {

namespace {

constexpr f32 W = 1920.0f;
constexpr f32 H = 1080.0f;

struct TextSpec {
    std::string text;
    f32 size{32.0f};
    f32 tracking{0.0f};
    Color color{1, 1, 1, 1};
    TextAlign align{TextAlign::Center};

    TextSpec(std::string t) : text(std::move(t)) {}
    TextSpec& set_font(f32 s, f32 t = 0.0f) { size = s; tracking = t; return *this; }
    TextSpec& set_color(Color c) { color = c; return *this; }
    TextSpec& set_align(TextAlign a) { align = a; return *this; }

    void draw(LayerBuilder& l, const std::string& id, Vec2 box = {W * 0.7f, 160.0f}, Vec3 pos = {0, 0, 0}) const {
        l.text(id, {
            .text = text, .size = box, .pos = pos,
            .font_family = "Inter", .font_weight = 800,
            .font_size = size, .color = color, .align = align,
            .line_height = 1.2f, .tracking = tracking
        });
    }
};

} // namespace

Composition img_gradient() {
    return composition({.name = "ImgGradient", .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("base", [](auto& l) { l.fullscreen_rect("base", {0.02f, 0.02f, 0.06f, 1}); });

        const i32 steps = 32;
        for (i32 i = 0; i < steps; ++i) {
            f32 t = static_cast<f32>(i) / (steps - 1);
            s.layer("band_" + std::to_string(i), [&](auto& l) {
                l.position({0, (t - 0.5f) * H, 0});
                l.rect("rect", {.size = {W, H / steps + 1}, .color = {0.05f + t * 0.2f, 0.02f + t * 0.5f, 0.04f + t * 0.8f, (2.0f / steps)}});
            });
        }

        s.layer("glow", [](auto& l) {
            l.pin_to(Anchor::Center).circle("glow", {.radius = 300, .color = {0.25f, 0.52f, 1, 0.06f}});
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

        s.layer("bg", [](auto& l) { l.fill({0.15f, 0.15f, 0.17f, 1}); });

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
        s.layer("bg", [](auto& l) { l.fill({0.01f, 0.012f, 0.02f, 1}); });
        s.layer("grid", [](auto& l) {
            l.grid_background("grid", {.size = {W, H}, .grid_color = {0.18f, 0.52f, 1, 0.15f}, .spacing = 80});
        });
        s.layer("label", [](auto& l) {
            l.pin_to(Anchor::BottomCenter, 40);
            TextSpec("GRID RESOLUTION TEST").set_font(24, 4).set_color({0.6f, 0.7f, 0.9f, 0.6f}).draw(l, "txt", {400, 40});
        });
        return s.build();
    });
}

Composition img_test_pattern() {
    return composition({.name = "ImgTestPattern", .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](auto& l) { l.fill({0.1f, 0.1f, 0.1f, 1}); });
        
        for (int i = 0; i < 8; ++i) {
            s.layer("bar_" + std::to_string(i), [&](auto& l) {
                l.position({(i - 3.5f) * (W / 8), 0, 0});
                l.rect("bar", {.size = {W / 8, H}, .color = { (i & 4) ? 1.0f : 0.0f, (i & 2) ? 1.0f : 0.0f, (i & 1) ? 1.0f : 0.0f, 1.0f }});
            });
        }
        return s.build();
    });
}

} // namespace chronon3d::content::images

CHRONON_REGISTER_COMPOSITION("ImgGradient",    chronon3d::content::images::img_gradient)
CHRONON_REGISTER_COMPOSITION("ImgChecker",     chronon3d::content::images::img_checker)
CHRONON_REGISTER_COMPOSITION("ImgGridTest",    chronon3d::content::images::img_grid_test)
CHRONON_REGISTER_COMPOSITION("ImgTestPattern", chronon3d::content::images::img_test_pattern)
