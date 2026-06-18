#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/animation/easing/easing.hpp>

#include "content/common/animation_helpers.hpp"
#include "content/text/text_helpers.hpp"
#include "special_names_theme.hpp"

namespace chronon3d::content::special_names {

using namespace chronon3d::content::animation_helpers;
using namespace chronon3d::content;

constexpr Frame kIntro = 22;

namespace {

// ── make_special_name_comp ──────────────────────────────────────────────────
// Shared helper for the 6 standard SpecialName compositions.  Wraps the common
// skeleton (black bg, centered layer, text) and lets each composition supply
// only its unique animation keyframes via a setup lambda.
//
// Previously each composition duplicated ~18 lines of identical boilerplate;
// now they are 8–12 lines of pure animation data.
using AnimSetup = std::function<void(LayerBuilder&)>;

Composition make_special_name_comp(const char* name, AnimSetup setup) {
    return composition({.name = name, .width = 1920, .height = 1080, .duration = 60},
        [setup = std::move(setup)](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_black_background(s);
            s.layer("name", [&](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                setup(l);
                // ── Subtle white-blue glow — soft halo around the name ──
                // ── Subtle white-blue glow — soft halo around the name ──
                l.glow({
                    .enabled = true,
                    .radius = 18.0f,
                    .intensity = 0.25f,
                    .color = {0.80f, 0.85f, 1.0f, 1.0f},
                    .softness = 1.0f,
                    .falloff = 0.85f,
                });
                l.text("name", text::centered_text({.text = DEMO_NAME, .font_size = 110.0f, .tracking = 14.0f}));
            });
            return s.build();
        });
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// 1. FadeUp — name rises into place.
Composition special_name_fade_up() {
    return make_special_name_comp("SpecialNameFadeUp", [](LayerBuilder& l) {
        l.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{kIntro}, 1.0f, EasingCurve{Easing::Linear});
        l.position_anim()
            .key(Frame{0}, Vec3{0.0f, 30.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{kIntro}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
    });
}

// 2. SlideLeft — name enters from the left.
Composition special_name_slide_left() {
    return make_special_name_comp("SpecialNameSlideLeft", [](LayerBuilder& l) {
        l.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{kIntro}, 1.0f, EasingCurve{Easing::Linear});
        l.position_anim()
            .key(Frame{0}, Vec3{-60.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{kIntro}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
    });
}

// 3. SlideRight — name enters from the right.
Composition special_name_slide_right() {
    return make_special_name_comp("SpecialNameSlideRight", [](LayerBuilder& l) {
        l.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{kIntro}, 1.0f, EasingCurve{Easing::Linear});
        l.position_anim()
            .key(Frame{0}, Vec3{60.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{kIntro}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
    });
}

// 4. ScaleIn — name scales from 0.85 to 1.0.
Composition special_name_scale_in() {
    return make_special_name_comp("SpecialNameScaleIn", [](LayerBuilder& l) {
        l.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{kIntro}, 1.0f, EasingCurve{Easing::Linear});
        l.scale_anim()
            .key(Frame{0}, Vec3{0.85f, 0.85f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{kIntro}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic});
    });
}

// 5. Stamp — OutBack overshoot, settles at 1.0 by frame 22.
Composition special_name_stamp() {
    return make_special_name_comp("SpecialNameStamp", [](LayerBuilder& l) {
        l.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutExpo})
            .key(Frame{12}, 1.0f, EasingCurve{Easing::Linear});
        l.scale_anim()
            .key(Frame{0}, Vec3{0.70f, 0.70f, 1.0f}, EasingCurve{Easing::OutBack})
            .key(Frame{kIntro}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutBack});
    });
}

// 6. BlurIn — name fades in while a blur clears.
Composition special_name_blur_in() {
    return make_special_name_comp("SpecialNameBlurIn", [](LayerBuilder& l) {
        l.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{kIntro}, 1.0f, EasingCurve{Easing::Linear});
        l.blur_anim()
            .key(Frame{0}, 3.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{kIntro}, 0.0f, EasingCurve{Easing::OutCubic});
    });
}

// ═════════════════════════════════════════════════════════════════════════════
// 7. Typewriter — character-by-character reveal (manually coded — uses ctx).
// ═════════════════════════════════════════════════════════════════════════════
Composition special_name_typewriter() {
    return composition({.name="SpecialNameTypewriter", .width=1920, .height=1080, .duration=60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        s.layer("tw", [&ctx](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.glow({
                .enabled = true,
                .radius = 18.0f,
                .intensity = 0.25f,
                .color = {0.80f, 0.85f, 1.0f, 1.0f},
                .softness = 1.0f,
                .falloff = 0.85f,
            });
            chronon3d::content::text::CenterTextOptions opts;
            opts.text      = DEMO_NAME;
            opts.font_size = 110.0f;
            opts.tracking  = 14.0f;
            opts.color     = NAME_TEXT_GOLD;
            l.text("name", chronon3d::content::text::typewriter_text(
                opts, ctx.frame, 0.8f,
                {.easing = EasingCurve{Easing::OutCubic},
                 .start_delay = Frame{4},
                 .fade_chars = 0.5f}));
        });
        return s.build();
    });
}

// ═════════════════════════════════════════════════════════════════════════════
// (Note: SpecialNameSplit removed — replaced by glow on all compositions.)
// (Note: the previous SpecialNameRole* / RolePreset family has been moved
// to a dedicated content/ImportantWords/ category — palettes + presets live
// in important_words_theme.hpp there.)
// ═════════════════════════════════════════════════════════════════════════════

// ── Per-domain registration ──────────────────────────────────────────────────
void register_special_name_compositions() {
    static bool done = false;
    if (done) return;
    done = true;
    detail::add_builtin_composition("SpecialNameFadeUp",    special_name_fade_up);
    detail::add_builtin_composition("SpecialNameSlideLeft", special_name_slide_left);
    detail::add_builtin_composition("SpecialNameSlideRight",special_name_slide_right);
    detail::add_builtin_composition("SpecialNameScaleIn",   special_name_scale_in);
    detail::add_builtin_composition("SpecialNameStamp",     special_name_stamp);
    detail::add_builtin_composition("SpecialNameBlurIn",    special_name_blur_in);
    detail::add_builtin_composition("SpecialNameTypewriter", special_name_typewriter);
    detail::add_builtin_composition("SpecialNameSplit",     special_name_split);
}

} // namespace chronon3d::content::special_names
