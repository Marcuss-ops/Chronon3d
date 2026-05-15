#ifdef CHRONON_BUILD_TESTS
#include <doctest/doctest.h>
#endif

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>

namespace chronon3d {

/**
 * BackgroundGrid - Una composizione di test che renderizza una griglia.
 * Definita in tests/ per validazione, ma registrata globalmente per essere renderizzata dalla CLI.
 */
static Composition BackgroundGrid() {
    return composition(
        { .name = "BackgroundGrid", .width = 1280, .height = 720, .duration = 1 },
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            
            const Vec3 center{static_cast<f32>(ctx.width) * 0.5f, static_cast<f32>(ctx.height) * 0.5f, 0.0f};
            const Vec2 size{static_cast<f32>(ctx.width), static_cast<f32>(ctx.height)};
            const Color background = Color::from_hex("#111111");
            const Color grid = Color{1.0f, 1.0f, 1.0f, 0.15f};

            s.rect("background", {.size = size, .color = background, .pos = center});

            constexpr i32 step = 40;

            for (i32 x = 0; x <= ctx.width; x += step) {
                s.line("grid-v-" + std::to_string(x),
                       { .from = {static_cast<f32>(x), 0.0f, 0.0f},
                         .to = {static_cast<f32>(x), static_cast<f32>(ctx.height), 0.0f},
                         .color = grid });
            }

            for (i32 y = 0; y <= ctx.height; y += step) {
                s.line("grid-h-" + std::to_string(y),
                       { .from = {0.0f, static_cast<f32>(y), 0.0f},
                         .to = {static_cast<f32>(ctx.width), static_cast<f32>(y), 0.0f},
                         .color = grid });
            }

            return s.build();
        }
    );
}

CHRONON_REGISTER_COMPOSITION("BackgroundGrid", BackgroundGrid)

#ifdef CHRONON_BUILD_TESTS
TEST_CASE("BackgroundGrid is registered and valid") {
    CompositionRegistry registry;
    CHECK(registry.contains("BackgroundGrid"));
    
    auto comp = registry.create("BackgroundGrid");
    CHECK(comp.width() == 1280);
    CHECK(comp.height() == 720);
}
#endif

} // namespace chronon3d
