#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>

using namespace chronon3d;
Composition AxisDebug() {
    CompositionSpec spec{
        .name = "AxisDebug",
        .width = 512,
        .height = 512,
        .duration = 1
    };

    return Composition{
        spec,
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            s.rect("bg", {256, 256, -1}, Color::black(), {512.0f, 512.0f});

            // Main axes
            s.line("x-axis", {0, 256, 0}, {512, 256, 0}, Color::red());
            s.line("y-axis", {256, 0, 0}, {256, 512, 0}, Color::green());

            // X ticks
            for (f32 x = 0; x <= 512; x += 64) {
                s.line("x-tick", {x, 250, 0}, {x, 262, 0}, Color::red());
            }

            // Y ticks
            for (f32 y = 0; y <= 512; y += 64) {
                s.line("y-tick", {250, y, 0}, {262, y, 0}, Color::green());
            }

            s.line("diagonal", {0, 0, 0}, {512, 512, 0}, Color::white());

            return s.build();
        }
    };
}

CHRONON_REGISTER_COMPOSITION("AxisDebug", AxisDebug)
