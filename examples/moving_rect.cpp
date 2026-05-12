#include <chronon3d/chronon3d.hpp>
#include <chronon3d/io/image_writer.hpp>
#include <iostream>
#include <filesystem>
#include <fmt/format.h>

using namespace chronon3d;

int main() {
    CompositionSpec spec{
        .name = "MovingRect",
        .width = 512,
        .height = 512,
        .duration = 61
    };

    Composition comp{
        spec,
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            s.rect("bg", {256, 256, -1}, Color::black());

            auto x = interpolate(ctx.frame, 0, 60, 100.0f, 400.0f);
            s.rect("moving-box", {x, 256.0f, 0.0f}, Color::white());

            return s.build();
        }
    };

    std::filesystem::create_directories("output");
    SoftwareRenderer renderer;

    for (Frame f : {0, 30, 60}) {
        auto fb = renderer.render_frame(comp, f);
        std::string path = fmt::format("output/moving_rect_{:04d}.png", f);
        if (save_png(*fb, path)) {
            std::cout << "Generated " << path << std::endl;
        }
    }

    return 0;
}
