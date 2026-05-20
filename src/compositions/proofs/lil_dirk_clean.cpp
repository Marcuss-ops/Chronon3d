#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>

using namespace chronon3d;

namespace {

void build_rect_test(SceneBuilder& s, const FrameContext& ctx) {
    const float w = static_cast<float>(ctx.width);
    const float h = static_cast<float>(ctx.height);

    s.layer("rect_test", [&](LayerBuilder& l) {
        l.screen_dimensions(w, h);
        l.rect("rect", RectParams{
            .size = {w * 0.75f, h * 0.35f},
            .color = Color{0.25f, 0.85f, 1.0f, 1.0f},
            .pos = {w * 0.5f, h * 0.5f, 0.0f},
        });
    });
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
        build_rect_test(s, ctx);
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
        build_rect_test(s, ctx);
        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("LilDirkClean", lil_dirk_clean)
CHRONON_REGISTER_COMPOSITION("LilDirkCleanFast", lil_dirk_clean_fast)
