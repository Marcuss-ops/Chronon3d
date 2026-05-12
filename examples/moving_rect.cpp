#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;

static Composition MovingRect() {
    CompositionSpec spec{
        .name = "MovingRect",
        .width = 512,
        .height = 512,
        .duration = 61
    };

    return Composition{
        spec,
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            s.rect("bg", {256, 256, -1}, Color::black());
            auto x = interpolate(ctx.frame, 0, 60, 100.0f, 400.0f);
            s.rect("moving-box", {x, 256.0f, 0.0f}, Color::white());
            return s.build();
        }
    };
}

CHRONON_REGISTER_COMPOSITION("MovingRect", MovingRect)
