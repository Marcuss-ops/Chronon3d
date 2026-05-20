#include <chronon3d/chronon3d.hpp>
#include <chronon3d/compositions/proofs/lil_dirk_clean.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>

#include <algorithm>
#include <cmath>

using namespace chronon3d;

namespace chronon3d::compositions {

void build_centered_grid(LayerBuilder& l, int width, int height) {
    const float half_w = static_cast<float>(width) * 0.5f;
    const float half_h = static_cast<float>(height) * 0.5f;

    const Color minor{0.34f, 0.34f, 0.36f, 0.18f};
    const Color major{0.52f, 0.52f, 0.55f, 0.30f};
    const int spacing = 120;
    const int major_step = spacing * 4;

    for (int x = -static_cast<int>(half_w); x <= static_cast<int>(half_w); x += spacing) {
        const bool is_major = (std::abs(x) % major_step) == 0;
        l.line("v" + std::to_string(x), {
            .from = {static_cast<float>(x), -half_h, 0.0f},
            .to = {static_cast<float>(x), half_h, 0.0f},
            .thickness = is_major ? 3.0f : 1.0f,
            .color = is_major ? major : minor
        });
    }

    for (int y = -static_cast<int>(half_h); y <= static_cast<int>(half_h); y += spacing) {
        const bool is_major = (std::abs(y) % major_step) == 0;
        l.line("h" + std::to_string(y), {
            .from = {-half_w, static_cast<float>(y), 0.0f},
            .to = {half_w, static_cast<float>(y), 0.0f},
            .thickness = is_major ? 3.0f : 1.0f,
            .color = is_major ? major : minor
        });
    }
}

Scene build_lil_dirk_clean_scene(const FrameContext& ctx) {
    SceneBuilder s(ctx);
    const int width = ctx.width;
    const int height = ctx.height;
    const float t = (ctx.duration > 1)
        ? static_cast<float>(ctx.frame) / static_cast<float>(ctx.duration - 1)
        : 0.0f;

    s.camera().set(camera_motion::parallax_sweep(t, 18.0f, -1000.0f, 1000.0f));
    s.rect("bg", {
        .size = {static_cast<f32>(width), static_cast<f32>(height)},
        .color = Color{0.02f, 0.02f, 0.025f, 1.0f},
        .pos = {0.0f, 0.0f, 0.0f}
    });

    s.layer("grid", [width, height](LayerBuilder& l) {
        build_centered_grid(l, width, height);
    });

    s.layer("title", [](LayerBuilder& l) {
        l.enable_3d();
        l.text("title", TextParams{
            .text = "LIL DIRK CLEAN",
            .size = {820.0f, 160.0f},
            .pos = {0.0f, 0.0f, 0.0f},
            .font_path = "assets/fonts/Inter-Bold.ttf",
            .font_family = "Inter",
            .font_weight = 800,
            .font_style = "normal",
            .font_size = 96.0f,
            .color = Color{0.98f, 0.98f, 0.99f, 1.0f},
            .align = TextAlign::Center,
            .line_height = 1.0f,
        .tracking = 1.0f
        });
        l.with_glow(Glow{
            .enabled = true,
            .radius = 42.0f,
            .intensity = 0.34f,
            .color = Color{0.96f, 0.96f, 1.0f, 0.62f}
        });
    });
    return s.build();
}

Composition LilDirkClean() {
    return composition({
        .name     = "LilDirkClean",
        .width    = 1920,
        .height   = 1080,
        .duration = 90
    }, [](const FrameContext& ctx) {
        return build_lil_dirk_clean_scene(ctx);
    });
}

Composition LilDirkCleanFast() {
    return composition({
        .name     = "LilDirkCleanFast",
        .width    = 1920,
        .height   = 1080,
        .duration = 90
    }, [](const FrameContext& ctx) {
        return build_lil_dirk_clean_scene(ctx);
    });
}

} // namespace chronon3d::compositions

CHRONON_REGISTER_COMPOSITION("LilDirkClean", chronon3d::compositions::LilDirkClean)
CHRONON_REGISTER_COMPOSITION("LilDirkCleanFast", chronon3d::compositions::LilDirkCleanFast)
