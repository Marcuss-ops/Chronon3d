#include <chronon3d/chronon3d.hpp>
#include <iostream>

using namespace chronon3d;

/**
 * Advanced Example: Showing how to use Sequences and complex interpolation.
 * This looks and feels like a Remotion component but in high-performance C++.
 */
Composition AdvancedVideo() {
    return composition(
        CompositionSpec{
            .name = "AdvancedShowcase",
            .width = 1280,
            .height = 720,
            .frame_rate = {60, 1},
            .duration = 300
        },
        [](const FrameContext& ctx) {
            SceneBuilder builder(ctx.resource);
            
            // 1. Global background
            builder.rect("bg", {ctx.width / 2.0f, ctx.height / 2.0f, -1}, Color::black(), {(f32)ctx.width, (f32)ctx.height, 1.0f});

            // 2. A sequence that only renders between frame 0 and 120
            if (in_sequence(ctx, 0, 120)) {
                auto sctx = sequence_context(ctx, 0); // local time starting at 0
                
                auto scale = interpolate(sctx.frame, 0, 60, 0.0f, 1.0f, Easing::OutBack);
                auto rot = interpolate(sctx.frame, 0, 120, 0.0f, 360.0f);

                builder.rect("intro_box", 
                    {ctx.width / 2.0f, ctx.height / 2.0f, 0}, 
                    Color::red(),
                    {200.0f * scale, 200.0f * scale, 1.0f},
                    {0, 0, rot} // Rotation Euler
                );
            }

            // 3. A second sequence that fades in as the first one ends
            if (in_sequence(ctx, 100, 250)) {
                auto sctx = sequence_context(ctx, 100);
                
                auto opacity = interpolate(sctx.frame, 0, 30, 0.0f, 1.0f);
                auto slide = interpolate(sctx.frame, 0, 60, -500.0f, 0.0f, Easing::OutExpo);

                builder.rect("main_content", 
                    {ctx.width / 2.0f + slide, ctx.height / 2.0f, 0}, 
                    Color::white().with_alpha(opacity),
                    {400.0f, 200.0f, 1.0f}
                );
            }

            return builder.build();
        }
    );
}

// How to register and use it:
void setup_showcase(CompositionRegistry& registry) {
    registry.add("AdvancedShowcase", []() {
        return AdvancedVideo();
    });
}

int main() {
    CompositionRegistry registry;
    setup_showcase(registry);
    
    auto comp = registry.create("AdvancedShowcase");

    std::cout << "Rendering frame 60 of " << comp.name() << "..." << std::endl;
    
    Scene scene = comp.evaluate(60);
    std::cout << "Scene generated with " << scene.nodes().size() << " nodes." << std::endl;

    return 0;
}
