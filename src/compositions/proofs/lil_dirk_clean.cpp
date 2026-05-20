#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>

using namespace chronon3d;

namespace {

void build_lil_dirk_card(SceneBuilder& s, const FrameContext& ctx, bool fast) {
    scene::utils::dark_grid_background(s, ctx, {
        .bg_color = Color{0.0f, 0.0f, 0.0f, 1.0f},
        .grid_color = Color{1.0f, 1.0f, 1.0f, 0.05f},
        .spacing = 84.0f,
        .extent = 4000.0f,
        .centered = true
    });

    std::vector<presets::motion::MotionObject> objects = {
        presets::motion::MotionObject::text("title", "LIL DIRK")
            .at({0.0f, 0.0f, -110.0f})
            .time(0, 120)
            .font_path("assets/fonts/Inter-Bold.ttf")
            .font_family("Inter")
            .font_weight(800)
            .font_size(128.0f)
            .tracking(1.0f)
            .align(presets::motion::TextAlign::Center)
            .color(Color{0.98f, 0.98f, 0.96f, 1.0f})
            .glow(!fast)
            .enable_3d(),
    };

    presets::motion::draw_motion_objects(s, ctx, objects);
}

} // namespace

static Composition lil_dirk_clean() {
    return composition({
        .name     = "LilDirkClean",
        .width    = 1920,
        .height   = 1080,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        build_lil_dirk_card(s, ctx, false);
        return s.build();
    });
}

static Composition lil_dirk_clean_fast() {
    return composition({
        .name     = "LilDirkCleanFast",
        .width    = 1920,
        .height   = 1080,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        build_lil_dirk_card(s, ctx, true);
        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("LilDirkClean", lil_dirk_clean)
CHRONON_REGISTER_COMPOSITION("LilDirkCleanFast", lil_dirk_clean_fast)
