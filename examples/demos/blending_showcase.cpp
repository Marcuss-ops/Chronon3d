#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>

using namespace chronon3d;
Composition BlendingShowcase() {
    CompositionSpec spec{
        .name = "BlendingShowcase",
        .width = 512,
        .height = 512,
        .duration = 1
    };

    return Composition{
        spec,
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);

            // Background
            s.rect("bg", {256, 256, -1}, Color::black(), {512.0f, 512.0f});

            // Three overlapping circles with transparency (RGB)
            // Red circle
            s.circle("red-circle", {200, 200, 0}, 100.0f, Color(1, 0, 0, 0.5f));
            
            // Green circle
            s.circle("green-circle", {312, 200, 0}, 100.0f, Color(0, 1, 0, 0.5f));
            
            // Blue circle
            s.circle("blue-circle", {256, 312, 0}, 100.0f, Color(0, 0, 1, 0.5f));

            // A white rectangle behind them (to see transparency)
            s.rect("ref-box", {256, 256, -0.5f}, Color::white(), {50, 50});

            return s.build();
        }
    };
}

CHRONON_REGISTER_COMPOSITION("BlendingShowcase", BlendingShowcase)
