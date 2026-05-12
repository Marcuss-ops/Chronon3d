#include <chronon3d/chronon3d.hpp>
#include <chronon3d/io/image_writer.hpp>
#include <iostream>
#include <filesystem>

using namespace chronon3d;

int main() {
    CompositionSpec spec{
        .name = "BasicShapes",
        .width = 512,
        .height = 512,
        .duration = 1
    };

    Composition comp{
        spec,
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);

            // Background
            s.rect("bg", {256, 256, -1}, Color::black());

            // Shapes
            s.rect("center", {256, 256, 0}, Color::white());
            s.rect("left",   {128, 256, 0}, Color::red());
            s.rect("right",  {384, 256, 0}, Color::green());
            s.rect("bottom", {256, 384, 0}, Color::blue());

            return s.build();
        }
    };

    std::filesystem::create_directories("output");
    SoftwareRenderer renderer;
    auto fb = renderer.render_frame(comp, 0);
    
    if (save_png(*fb, "output/basic_shapes.png")) {
        std::cout << "Generated output/basic_shapes.png" << std::endl;
    } else {
        std::cerr << "Failed to save PNG!" << std::endl;
        return 1;
    }

    return 0;
}
