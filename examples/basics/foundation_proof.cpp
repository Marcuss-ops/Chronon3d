#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;

Composition FoundationProof() {
    CompositionSpec spec{
        .name = "FoundationProof",
        .width = 800,
        .height = 450,
        .duration = 60
    };

    return Composition{
        spec,
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);

            // 1. Background (Dark Gray)
            s.rect("bg", {400, 225, 0}, Color(0.1f, 0.1f, 0.1f, 1.0f), {800, 450});

            // 2. Grid for Coordinate System Proof (100px steps)
            for (int x = 0; x <= 800; x += 100) {
                s.line("grid_v", {static_cast<f32>(x), 0, 0}, {static_cast<f32>(x), 450, 0}, Color(0.2f, 0.2f, 0.2f, 1.0f));
            }
            for (int y = 0; y <= 450; y += 100) {
                s.line("grid_h", {0, static_cast<f32>(y), 0}, {800, static_cast<f32>(y), 0}, Color(0.2f, 0.2f, 0.2f, 1.0f));
            }

            // 3. Alpha Blending Proof (Overlap test)
            s.circle("c_red", {350, 225, 0}, 80.0f, Color(1, 0, 0, 0.5f));
            s.circle("c_green", {450, 225, 0}, 80.0f, Color(0, 1, 0, 0.5f));
            s.circle("c_blue", {400, 300, 0}, 80.0f, Color(0, 0, 1, 0.5f));

            // 4. Draw Order Proof
            s.rect("top_rect", {400, 225, 0}, Color(1, 1, 1, 0.8f), {40, 40});

            // 5. Moving Shape Proof (using spring!)
            // We use ctx.frame as time
            f32 move_x = spring(ctx.frame, ctx.frame_rate, 100.0f, 700.0f);
            s.rect("moving_indicator", {move_x, 100, 0}, Color::white(), {30, 30});

            // 6. Clipping Proof
            s.circle("clip_edge", {800, 450, 0}, 50.0f, Color::red());

            return s.build();
        }
    };
}

CHRONON_REGISTER_COMPOSITION("FoundationProof", FoundationProof)
