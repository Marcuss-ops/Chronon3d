// content/examples/light/light_text_animations.cpp
//
// 8 Light 2D Text Entrance Animations — 5 second (150 frame) compositions.
//
// Design:
//   • text::centered_text() — Poppins-Bold 90pt, modern, bright (0.95,0.96,0.99)
//   • MinimalistGrid background + ae_cinematic_white glow
//   • 150 frames = 5 seconds at 30 fps (as requested)
//   • Opacity 0.30 → 1.0 fade-in (text faintly visible from frame 0)
//   • Scale ranges ≥ 0.85 (avoids TICKET-104 zero-bbox crash)
//
// Render:
//   chronon3d_cli render LightPulse -o output/LightPulse.png
//   chronon3d_cli video LightPulse --start 0 --end 150 --fps 30 -o output/LightPulse.mp4

#include "light_text_animations.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/motion/timeline.hpp>
#include <chronon3d/text/text_glow_spec.hpp>

#include "content/common/background_helpers.hpp"
#include "content/text/text_helpers.hpp"

namespace chronon3d::content::light_text {

using namespace chronon3d::content::backgrounds;

namespace {

using AnimSetup = std::function<void(LayerBuilder&)>;

Composition make_light_comp(const char* name, const std::string& text,
                             AnimSetup setup, Frame duration = Frame{150}) {
    return composition(
        {.name = name, .width = 1920, .height = 1080, .duration = duration},
        [text, setup = std::move(setup)]
        (const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_common_background(s, BackgroundStyles::Minimalist());
            s.layer("text", [&](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                setup(l);
                // l.glow(TextGlowPresets::ae_cinematic_white().to_glow_params());
                l.text("label", text::centered_text({
                    .text      = text,
                    .box       = {1200.0f, 240.0f},
                    .pos       = {960.0f, 540.0f, 0.0f},
                    .font_size = 90.0f,
                    .tracking  = 6.0f,
                    .color     = {0.95f, 0.96f, 0.99f, 1.0f},
                }));
            });
            return s.build();
        });
}

} // anonymous namespace

// ── 1. LightPulse — gentle scale heartbeat (30f)
Composition light_pulse() {
    return make_light_comp("LightPulse", "PULSE", [](LayerBuilder& l) {
        l.scale_anim()
            .key(Frame{0},  Vec3{0.96f, 0.96f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{14}, Vec3{1.03f, 1.03f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, Vec3{0.99f, 0.99f, 1.0f}, EasingCurve{Easing::InOutCubic})
            .key(Frame{30}, Vec3{1.0f,  1.0f,  1.0f}, EasingCurve{Easing::OutCubic});
        l.opacity_anim()
            .key(Frame{0},  0.30f, EasingCurve{Easing::OutCubic})
            .key(Frame{30}, 1.0f,  EasingCurve{Easing::Linear});
    });
}

// ── 2. LightWobble — rotation wobble (32f)
Composition light_wobble() {
    return make_light_comp("LightWobble", "WOBBLE", [](LayerBuilder& l) {
        l.rotate_anim()
            .key(Frame{0},  Vec3{0.0f, 0.0f,  8.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{10}, Vec3{0.0f, 0.0f, -3.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{18}, Vec3{0.0f, 0.0f,  2.0f}, EasingCurve{Easing::InOutCubic})
            .key(Frame{26}, Vec3{0.0f, 0.0f, -1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{32}, Vec3{0.0f, 0.0f,  0.0f}, EasingCurve{Easing::OutCubic});
        l.opacity_anim()
            .key(Frame{0},  0.30f, EasingCurve{Easing::OutCubic})
            .key(Frame{16}, 1.0f,  EasingCurve{Easing::Linear});
    });
}

// ── 3. LightDropSpring — spring bounce drop (30f)
Composition light_drop_spring() {
    return make_light_comp("LightDropSpring", "DROP SPRING", [](LayerBuilder& l) {
        l.position_anim()
            .key(Frame{0},  Vec3{0.0f, -70.0f, 0.0f}, EasingCurve{Easing::OutBounce})
            .key(Frame{14}, Vec3{0.0f,   8.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, Vec3{0.0f,  -3.0f, 0.0f}, EasingCurve{Easing::InOutCubic})
            .key(Frame{30}, Vec3{0.0f,   0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
        l.scale_anim()
            .key(Frame{0},  Vec3{0.90f, 0.90f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{30}, Vec3{1.0f,  1.0f,  1.0f}, EasingCurve{Easing::OutCubic});
        l.opacity_anim()
            .key(Frame{0},  0.30f, EasingCurve{Easing::OutCubic})
            .key(Frame{14}, 1.0f,  EasingCurve{Easing::Linear});
    });
}

// ── 4. LightGlideBlur — slide + defocus (28f)
Composition light_glide_blur() {
    return make_light_comp("LightGlideBlur", "GLIDE BLUR", [](LayerBuilder& l) {
        l.position_anim()
            .key(Frame{0},  Vec3{-60.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{28}, Vec3{0.0f,   0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
        l.blur_anim()
            .key(Frame{0},  4.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{28}, 0.0f, EasingCurve{Easing::OutCubic});
        l.opacity_anim()
            .key(Frame{0},  0.30f, EasingCurve{Easing::OutCubic})
            .key(Frame{28}, 1.0f,  EasingCurve{Easing::Linear});
    });
}

// ── 5. LightRevealX — horizontal curtain (24f)
Composition light_reveal_x() {
    return make_light_comp("LightRevealX", "REVEAL X", [](LayerBuilder& l) {
        l.scale_anim()
            .key(Frame{0},  Vec3{0.75f, 1.0f, 1.0f}, EasingCurve{Easing::OutExpo})
            .key(Frame{24}, Vec3{1.0f,  1.0f, 1.0f}, EasingCurve{Easing::OutExpo});
        l.opacity_anim()
            .key(Frame{0},  0.30f, EasingCurve{Easing::OutCubic})
            .key(Frame{24}, 1.0f,  EasingCurve{Easing::Linear});
    });
}

// ── 6. LightFloatUp — float up + scale (30f)
Composition light_float_up() {
    return make_light_comp("LightFloatUp", "FLOAT UP", [](LayerBuilder& l) {
        l.position_anim()
            .key(Frame{0},  Vec3{0.0f, 40.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{30}, Vec3{0.0f, 0.0f,  0.0f}, EasingCurve{Easing::OutCubic});
        l.scale_anim()
            .key(Frame{0},  Vec3{0.94f, 0.94f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{30}, Vec3{1.0f,  1.0f,  1.0f}, EasingCurve{Easing::OutCubic});
        l.opacity_anim()
            .key(Frame{0},  0.30f, EasingCurve{Easing::OutCubic})
            .key(Frame{30}, 1.0f,  EasingCurve{Easing::Linear});
    });
}

// ── 7. LightSpin — rotational entrance (24f)
Composition light_spin() {
    return make_light_comp("LightSpin", "SPIN", [](LayerBuilder& l) {
        l.rotate_anim()
            .key(Frame{0},  Vec3{0.0f, 0.0f, -15.0f}, EasingCurve{Easing::OutBack})
            .key(Frame{24}, Vec3{0.0f, 0.0f,   0.0f}, EasingCurve{Easing::OutBack});
        l.opacity_anim()
            .key(Frame{0},  0.30f, EasingCurve{Easing::OutCubic})
            .key(Frame{24}, 1.0f,  EasingCurve{Easing::Linear});
    });
}

// ── 8. LightGlowPulse — blur glow pulse (30f)
Composition light_glow_pulse() {
    return make_light_comp("LightGlowPulse", "GLOW PULSE", [](LayerBuilder& l) {
        l.blur_anim()
            .key(Frame{0},  6.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{14}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, 2.0f, EasingCurve{Easing::InOutCubic})
            .key(Frame{30}, 0.0f, EasingCurve{Easing::OutCubic});
        l.scale_anim()
            .key(Frame{0},  Vec3{1.02f, 1.02f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{30}, Vec3{1.0f,  1.0f,  1.0f}, EasingCurve{Easing::OutCubic});
        l.opacity_anim()
            .key(Frame{0},  0.30f, EasingCurve{Easing::OutCubic})
            .key(Frame{20}, 1.0f,  EasingCurve{Easing::Linear});
    });
}

// ── Registration ──────────────────────────────────────────────────────────────
void register_light_text_compositions(CompositionRegistry& registry) {
    registry.add("LightPulse",      [](const CompositionProps&) { return light_pulse(); });
    registry.add("LightWobble",     [](const CompositionProps&) { return light_wobble(); });
    registry.add("LightDropSpring", [](const CompositionProps&) { return light_drop_spring(); });
    registry.add("LightGlideBlur",  [](const CompositionProps&) { return light_glide_blur(); });
    registry.add("LightRevealX",    [](const CompositionProps&) { return light_reveal_x(); });
    registry.add("LightFloatUp",    [](const CompositionProps&) { return light_float_up(); });
    registry.add("LightSpin",       [](const CompositionProps&) { return light_spin(); });
    registry.add("LightGlowPulse",  [](const CompositionProps&) { return light_glow_pulse(); });
}

} // namespace chronon3d::content::light_text
