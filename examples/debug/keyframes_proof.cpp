#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;

// Shows keyframes() with multi-stop motion and easing.
// Three shapes each follow a different easing on the same keyframe stops.
static Composition KeyframesProof() {
    return composition({
        .name = "KeyframesProof", .width = 1280, .height = 720, .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.rect("bg", {
            .size = {1280, 720}, .color = Color{0.03f, 0.03f, 0.05f, 1}, .pos = {640, 360, 0}
        });

        // All segments InOutCubic
        const f32 x_smooth = keyframes(ctx.frame, {
            KF{  0,   80.0f, Easing::InOutCubic },
            KF{ 30,  640.0f, Easing::InOutCubic },
            KF{ 80,  640.0f, Easing::InOutCubic },
            KF{110, 1200.0f }
        });

        // Linear — no easing
        const f32 x_linear = keyframes(ctx.frame, {
            KF{  0,   80.0f },
            KF{ 30,  640.0f },
            KF{ 80,  640.0f },
            KF{110, 1200.0f }
        });

        // Per-segment: fast out then slow in
        const f32 x_mixed = keyframes(ctx.frame, {
            {  0,   80.0f, Easing::OutExpo  },
            { 30,  640.0f, Easing::Linear   },
            { 80,  640.0f, Easing::InExpo   },
            {110, 1200.0f }
        });

        const TextStyle label{
            .font_path = "assets/fonts/Inter-Bold.ttf",
            .size = 22, .color = Color{1, 1, 1, 0.8f}
        };

        // Smooth (InOutCubic)
        s.layer("smooth", [&](LayerBuilder& l) {
            l.position({x_smooth, 200, 0});
            l.rounded_rect("r", { .size = {120, 60}, .radius = 12,
                .color = Color{0.2f, 0.6f, 1, 1}, .pos = {0, 0, 0} });
            l.text("lbl", { .content = "InOutCubic", .style = label, .pos = {-45, 45, 0} });
        });

        // Linear
        s.layer("linear", [&](LayerBuilder& l) {
            l.position({x_linear, 360, 0});
            l.rounded_rect("r", { .size = {120, 60}, .radius = 12,
                .color = Color{0.9f, 0.5f, 0.1f, 1}, .pos = {0, 0, 0} });
            l.text("lbl", { .content = "Linear", .style = label, .pos = {-28, 45, 0} });
        });

        // Mixed
        s.layer("mixed", [&](LayerBuilder& l) {
            l.position({x_mixed, 520, 0});
            l.rounded_rect("r", { .size = {120, 60}, .radius = 12,
                .color = Color{0.3f, 0.85f, 0.4f, 1}, .pos = {0, 0, 0} });
            l.text("lbl", { .content = "Out/In Expo", .style = label, .pos = {-46, 45, 0} });
        });

        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("KeyframesProof", KeyframesProof)
