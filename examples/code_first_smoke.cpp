#include <chronon3d/chronon3d.hpp>
#include <iostream>

using namespace chronon3d;

int main() {
    CompositionSpec spec;
    spec.name = "CodeFirstSmoke";
    spec.width = 512;
    spec.height = 512;
    spec.frame_rate = {30, 1};
    spec.duration = 60;

    Composition comp{
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

    SoftwareRenderer renderer;
    auto fb = renderer.render_frame(comp, 30);
    fb->save_ppm("code_first_smoke.ppm");

    std::cout << "Rendered smoke frame 30 to code_first_smoke.ppm" << std::endl;

    return 0;
}
