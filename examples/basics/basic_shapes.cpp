#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;
Composition BasicShapes() {
    CompositionSpec spec{
        .name = "BasicShapes",
        .width = 512,
        .height = 512,
        .duration = 1
    };

    return Composition{
        spec,
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            s.rect("bg",     {256, 256, -1}, Color::black());
            s.rect("center", {256, 256,  0}, Color::white());
            s.rect("left",   {128, 256,  0}, Color::red());
            s.rect("right",  {384, 256,  0}, Color::green());
            s.rect("bottom", {256, 384,  0}, Color::blue());
            return s.build();
        }
    };
}

CHRONON_REGISTER_COMPOSITION("BasicShapes", BasicShapes)
