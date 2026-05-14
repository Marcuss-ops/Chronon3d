#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/templates/fake3d_templates.hpp>

using namespace chronon3d;
using namespace chronon3d::templates;

// ─── Fake3DTitleStudio — standard orange theme ────────────────────────────────
Composition TmplStudioOrange() {
    return composition({.name="TmplStudioOrange", .width=1280, .height=720, .duration=120},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        fake3d_title_studio(s, ctx, {.title = "CHRONON"});
        return s.build();
    });
}

// ─── Blue theme with custom title ─────────────────────────────────────────────
Composition TmplStudioBlue() {
    return composition({.name="TmplStudioBlue", .width=1280, .height=720, .duration=120},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        fake3d_title_studio(s, ctx, {
            .title = "TILT",
            .theme = Theme::blue(),
            .font_size = 110.0f,
            .orbit_start_deg = 0.0f
        });
        return s.build();
    });
}

// ─── Lower third standalone ───────────────────────────────────────────────────
Composition TmplLowerThird() {
    return composition({.name="TmplLowerThird", .width=1280, .height=720},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        // Dark bg
        s.layer("bg", [](LayerBuilder& l) {
            l.rect("fill", {.size={4000,4000}, .color={0.08f,0.08f,0.10f,1.0f}});
        });
        lower_third(s, {
            .title    = "CHRONON ENGINE",
            .subtitle = "Headless CPU Motion Graphics",
        });
        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("TmplStudioOrange", TmplStudioOrange)
CHRONON_REGISTER_COMPOSITION("TmplStudioBlue",   TmplStudioBlue)
CHRONON_REGISTER_COMPOSITION("TmplLowerThird",   TmplLowerThird)
