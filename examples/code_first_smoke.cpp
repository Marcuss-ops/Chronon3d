#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>

using namespace chronon3d;

static Composition CodeFirstSmoke() {
    CompositionSpec spec;
    spec.name = "CodeFirstSmoke";
    spec.width = 512;
    spec.height = 512;
    spec.frame_rate = {30, 1};
    spec.duration = 60;

    return Composition{
        spec,
        [](const FrameContext& ctx) {
            SceneBuilder builder(ctx.resource);

            auto x = interpolate(ctx.frame, 0, 60, 100.0f, 400.0f);

            builder.rect(
                "moving-box",
                {x, 256.0f, 0.0f},
                Color::white()
            );

            return builder.build();
        }
    };
}

CHRONON_REGISTER_COMPOSITION("CodeFirstSmoke", CodeFirstSmoke)
