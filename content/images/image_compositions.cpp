#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/math/color.hpp>

#include <cmath>

namespace chronon3d::content::images {
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

// ── ImgGradient ──────────────────────────────────────────────────────────
Composition img_gradient() {
    return composition({
        .name = "ImgGradient",
        .width = 1920, .height = 1080,
        .duration = 1,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer("base", [](LayerBuilder& l) {
            l.fullscreen_rect("base_rect", Color{0.02f, 0.02f, 0.06f, 1.0f});
        });

        static constexpr i32 kSteps = 32;
        for (i32 i = 0; i < kSteps; ++i) {
            f32 t = static_cast<f32>(i) / static_cast<f32>(kSteps - 1);
            f32 y = (t - 0.5f) * H;
            f32 band_h = H / static_cast<f32>(kSteps) + 1.0f;
            Color c{
                0.05f + t * 0.2f,
                0.02f + t * 0.5f,
                0.04f + t * 0.8f,
                (1.0f / static_cast<f32>(kSteps)) * 2.0f
            };

            s.layer("band_" + std::to_string(i), [&, y, band_h, c](LayerBuilder& l) {
                l.position({0, y, 0});
                l.rect("band_rect", {
                    .size = {W, band_h},
                    .color = c,
                });
            });
        }

        s.layer("glow", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.circle("glow_c", {
                .radius = 300.0f,
                .color = {0.25f, 0.52f, 1.0f, 0.06f},
            });
        });

        return s.build();
    });
}

// ── ImgChecker ──────────────────────────────────────────────────────────
Composition img_checker() {
    return composition({
        .name = "ImgChecker",
        .width = 1920, .height = 1080,
        .duration = 1,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 square = 80.0f;
        i32 cols = static_cast<i32>(W / square) + 1;
        i32 rows = static_cast<i32>(H / square) + 1;

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.15f, 0.15f, 0.17f, 1.0f});
        });

        for (i32 r = 0; r < rows && r < 20; ++r) {
            for (i32 c = 0; c < cols && c < 30; ++c) {
                if ((r + c) % 2 == 0) continue;
                f32 x = (static_cast<f32>(c) - static_cast<f32>(cols) * 0.5f) * square;
                f32 y = (static_cast<f32>(r) - static_cast<f32>(rows) * 0.5f) * square;

                s.layer("check_" + std::to_string(r) + "_" + std::to_string(c), [&, x, y](LayerBuilder& l) {
                    l.position({x, y, 0});
                    l.rect("sq", {
                        .size = {square, square},
                        .color = {0.25f, 0.25f, 0.28f, 1.0f},
                    });
                });
            }
        }

        return s.build();
    });
}

// ── ImgGrid ──────────────────────────────────────────────────────────────
Composition img_grid() {
    return composition({
        .name = "ImgGrid",
        .width = 1920, .height = 1080,
        .duration = 1,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.008f, 0.010f, 0.022f, 1.0f});
        });

        s.layer("grid_under", [](LayerBuilder& l) {
            l.grid_background("grid", {
                .size = {W, H},
                .bg_color = {0, 0, 0, 0},
                .grid_color = {0.25f, 0.52f, 1.0f, 0.08f},
                .spacing = 80.0f,
                .minor_thickness = 1.0f,
                .major_thickness = 2.5f,
                .major_every = 4,
                .centered = true,
            });
        });

        return s.build();
    });
}

// ── ImgSolidColors ───────────────────────────────────────────────────────
Composition img_solid_colors() {
    return composition({
        .name = "ImgSolidColors",
        .width = 1920, .height = 1080,
        .duration = 1,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        static const Color swatches[] = {
            {0.008f, 0.010f, 0.022f, 1},
            {0.25f,  0.52f,  1.0f,   1},
            {0.9f,   0.2f,   0.2f,   1},
            {0.2f,   0.9f,   0.4f,   1},
            {1.0f,   0.8f,   0.1f,   1},
            {1.0f,   1.0f,   1.0f,   1},
        };
        static constexpr i32 n = 6;
        f32 sw = W / static_cast<f32>(n);

        for (i32 i = 0; i < n; ++i) {
            s.layer("swatch_" + std::to_string(i), [i, sw](LayerBuilder& l) {
                f32 x = (static_cast<f32>(i) - static_cast<f32>(n - 1) * 0.5f) * sw;
                l.position({x, 0, 0});
                l.rect("sw", {
                    .size = {sw - 4.0f, H},
                    .color = swatches[i],
                });
            });

            s.layer("label_" + std::to_string(i), [i, sw](LayerBuilder& l) {
                f32 x = (static_cast<f32>(i) - static_cast<f32>(n - 1) * 0.5f) * sw;
                l.pin_to(Anchor::BottomCenter, 40.0f).position({x, 0, 0});
                make_text(l, "label", "C" + std::to_string(i + 1), 24.0f,
                          {1, 1, 1, 0.6f}, TextAlign::Center, 2.0f,
                          {sw - 8.0f, 30.0f});
            });
        }

        return s.build();
    });
}

} // anonymous namespace

void register_all() {}

} // namespace chronon3d::content::images

CHRONON_REGISTER_COMPOSITION("ImgGradient",    chronon3d::content::images::img_gradient)
CHRONON_REGISTER_COMPOSITION("ImgChecker",     chronon3d::content::images::img_checker)
CHRONON_REGISTER_COMPOSITION("ImgGrid",        chronon3d::content::images::img_grid)
CHRONON_REGISTER_COMPOSITION("ImgSolidColors", chronon3d::content::images::img_solid_colors)
