#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/text/text_glow_spec.hpp>

#include "content/common/background_helpers.hpp"
#include "content/text/text_helpers.hpp"

namespace chronon3d::content::minimalist {

using namespace chronon3d::content::backgrounds;

namespace {

// ── make_minimalist_comp ────────────────────────────────────────────────────
// Shared helper for all 20 MinimalistText compositions.  Encapsulates the
// common skeleton (grid background, centered layer, glow, text) and lets each
// composition supply only its unique animation keyframes via a setup lambda.
//
// Previously each composition duplicated ~30 lines of identical boilerplate
// (add_black_background, layer, pin_to, glow, text); now they are 8–12 lines
// of pure animation data.
using AnimSetup = std::function<void(LayerBuilder&)>;

Composition make_minimalist_comp(const char* name, const char* text,
                                  AnimSetup setup,
                                  f32 font_size = 100.0f, f32 tracking = 6.0f) {
    return composition({.name = name, .width = 1920, .height = 1080, .duration = 150},
        [text, setup = std::move(setup), font_size, tracking](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_common_background(s, BackgroundStyles::Minimalist());
            s.layer("phrase", [&](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                setup(l);
                l.glow(TextGlowPresets::ae_cinematic_white().to_glow_params());
                l.text("phrase", text::centered_text({
                    .text      = text,
                    .font_size = font_size,
                    .tracking  = tracking,
                }));
            });
            return s.build();
        });
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// INTRO ANIMATIONS — 15 text entrance presets
// ═════════════════════════════════════════════════════════════════════════════

// 1. FadeUp — testo sale mentre appare (opacity 0→1, pos_y +30→0, 18 fr)
Composition minimalist_text_fade_up() {
    return make_minimalist_comp("MinimalistTextFadeUp", "FADE UP", [](LayerBuilder& l) {
        l.position_anim()
            .key(Frame{0},  Vec3{0.0f, 30.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{18}, Vec3{0.0f, 0.0f,  0.0f}, EasingCurve{Easing::OutCubic});
        l.opacity_anim()
            .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{18}, 1.0f, EasingCurve{Easing::Linear});
    });
}

// 2. TrackingReveal — lettere distanziate che si avvicinano (scale_x 1.18→1.0)
Composition minimalist_text_tracking_reveal() {
    return make_minimalist_comp("MinimalistTextTrackingReveal", "TRACKING", [](LayerBuilder& l) {
        l.scale_anim()
            .key(Frame{0},  Vec3{1.18f, 1.0f, 1.0f}, EasingCurve{Easing::OutExpo})
            .key(Frame{24}, Vec3{1.0f,  1.0f, 1.0f}, EasingCurve{Easing::OutExpo});
        l.opacity_anim()
            .key(Frame{0},  0.0f, EasingCurve{Easing::OutExpo})
            .key(Frame{24}, 1.0f, EasingCurve{Easing::Linear});
    }, 100.0f, 4.0f);
}

// 3. ClipReveal — sale dal basso con fade-in (pos_y +120→0, 18 fr)
Composition minimalist_text_clip_reveal() {
    return make_minimalist_comp("MinimalistTextClipReveal", "CLIP REVEAL", [](LayerBuilder& l) {
        l.position_anim()
            .key(Frame{0},  Vec3{0.0f, 120.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{18}, Vec3{0.0f, 0.0f,   0.0f}, EasingCurve{Easing::OutCubic});
        l.opacity_anim()
            .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{18}, 1.0f, EasingCurve{Easing::Linear});
    });
}

// 4. FadeDown — scende dall'alto mentre appare (pos_y -30→0, 18 fr)
Composition minimalist_text_fade_down() {
    return make_minimalist_comp("MinimalistTextFadeDown", "FADE DOWN", [](LayerBuilder& l) {
        l.position_anim()
            .key(Frame{0},  Vec3{0.0f, -30.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{18}, Vec3{0.0f, 0.0f,   0.0f}, EasingCurve{Easing::OutCubic});
        l.opacity_anim()
            .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{18}, 1.0f, EasingCurve{Easing::Linear});
    });
}

// 5. SoftScale — zoom elegante (scale 0.94→1.0, 16 fr)
Composition minimalist_text_soft_scale() {
    return make_minimalist_comp("MinimalistTextSoftScale", "SOFT SCALE", [](LayerBuilder& l) {
        l.scale_anim()
            .key(Frame{0},  Vec3{0.94f, 0.94f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{16}, Vec3{1.0f,  1.0f,  1.0f}, EasingCurve{Easing::OutCubic});
        l.opacity_anim()
            .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{16}, 1.0f, EasingCurve{Easing::Linear});
    }, 100.0f, 5.0f);
}

// 6. BlurFocus — sfoca→nitido (blur 8→0, scale 1.02→1.0, 18 fr)
Composition minimalist_text_blur_focus() {
    return make_minimalist_comp("MinimalistTextBlurFocus", "BLUR FOCUS", [](LayerBuilder& l) {
        l.blur_anim()
            .key(Frame{0},  8.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{18}, 0.0f, EasingCurve{Easing::OutCubic});
        l.scale_anim()
            .key(Frame{0},  Vec3{1.02f, 1.02f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{18}, Vec3{1.0f,  1.0f,  1.0f}, EasingCurve{Easing::OutCubic});
        l.opacity_anim()
            .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{18}, 1.0f, EasingCurve{Easing::Linear});
    }, 102.0f, 5.0f);
}

// 7. SlideLeft — entra da sinistra (pos_x -60→0, 20 fr)
Composition minimalist_text_slide_left() {
    return make_minimalist_comp("MinimalistTextSlideLeft", "SLIDE LEFT", [](LayerBuilder& l) {
        l.position_anim()
            .key(Frame{0},  Vec3{-60.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{20}, Vec3{0.0f,   0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
        l.opacity_anim()
            .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{20}, 1.0f, EasingCurve{Easing::Linear});
    });
}

// 8. SlideRight — entra da destra (pos_x +60→0, 20 fr)
Composition minimalist_text_slide_right() {
    return make_minimalist_comp("MinimalistTextSlideRight", "SLIDE RIGHT", [](LayerBuilder& l) {
        l.position_anim()
            .key(Frame{0},  Vec3{60.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{20}, Vec3{0.0f,  0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
        l.opacity_anim()
            .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{20}, 1.0f, EasingCurve{Easing::Linear});
    });
}

// 9. ScalePop — overshoot morbido (scale 0.85→1.06→0.98→1.0, 24 fr)
Composition minimalist_text_scale_pop() {
    return make_minimalist_comp("MinimalistTextScalePop", "SCALE POP", [](LayerBuilder& l) {
        l.scale_anim()
            .key(Frame{0},  Vec3{0.85f, 0.85f, 1.0f}, EasingCurve{Easing::OutBack})
            .key(Frame{16}, Vec3{1.06f, 1.06f, 1.0f}, EasingCurve{Easing::OutBack})
            .key(Frame{20}, Vec3{0.98f, 0.98f, 1.0f}, EasingCurve{Easing::InOutCubic})
            .key(Frame{24}, Vec3{1.0f,  1.0f,  1.0f}, EasingCurve{Easing::OutCubic});
        l.opacity_anim()
            .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{16}, 1.0f, EasingCurve{Easing::Linear});
    });
}

// 10. FloatIn — fluttua dal basso (pos_y +80→0, scale 0.95→1.0, 22 fr)
Composition minimalist_text_float_in() {
    return make_minimalist_comp("MinimalistTextFloatIn", "FLOAT IN", [](LayerBuilder& l) {
        l.position_anim()
            .key(Frame{0},  Vec3{0.0f, 80.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, Vec3{0.0f, 0.0f,  0.0f}, EasingCurve{Easing::OutCubic});
        l.scale_anim()
            .key(Frame{0},  Vec3{0.95f, 0.95f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, Vec3{1.0f,  1.0f,  1.0f}, EasingCurve{Easing::OutCubic});
        l.opacity_anim()
            .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, 1.0f, EasingCurve{Easing::Linear});
    });
}

// 11. LetterRise — scale_y verticale + sollevamento (scale_y 0.85→1.0, pos_y -10→0)
Composition minimalist_text_letter_rise() {
    return make_minimalist_comp("MinimalistTextLetterRise", "LETTER RISE", [](LayerBuilder& l) {
        l.scale_anim()
            .key(Frame{0},  Vec3{1.0f, 0.85f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, Vec3{1.0f, 1.0f,  1.0f}, EasingCurve{Easing::OutCubic});
        l.position_anim()
            .key(Frame{0},  Vec3{0.0f, -10.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, Vec3{0.0f, 0.0f,   0.0f}, EasingCurve{Easing::OutCubic});
        l.opacity_anim()
            .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, 1.0f, EasingCurve{Easing::Linear});
    });
}

// 12. DriftIn — diagonale basso-destra (pos +30,+30→0, 22 fr)
Composition minimalist_text_drift_in() {
    return make_minimalist_comp("MinimalistTextDriftIn", "DRIFT IN", [](LayerBuilder& l) {
        l.position_anim()
            .key(Frame{0},  Vec3{30.0f, 30.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, Vec3{0.0f,  0.0f,  0.0f}, EasingCurve{Easing::OutCubic});
        l.opacity_anim()
            .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, 1.0f, EasingCurve{Easing::Linear});
    });
}

// 13. TiltIn — rotazione -3°→0°, scale 0.97→1.0, 20 fr
Composition minimalist_text_tilt_in() {
    return make_minimalist_comp("MinimalistTextTiltIn", "TILT IN", [](LayerBuilder& l) {
        l.rotate_anim()
            .key(Frame{0},  Vec3{0.0f, 0.0f, -3.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{20}, Vec3{0.0f, 0.0f,  0.0f}, EasingCurve{Easing::OutCubic});
        l.scale_anim()
            .key(Frame{0},  Vec3{0.97f, 0.97f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{20}, Vec3{1.0f,  1.0f,  1.0f}, EasingCurve{Easing::OutCubic});
        l.opacity_anim()
            .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{20}, 1.0f, EasingCurve{Easing::Linear});
    });
}

// 14. MaskReveal — sipario verticale (scale_y 0.6→1.0, 18 fr)
Composition minimalist_text_mask_reveal() {
    return make_minimalist_comp("MinimalistTextMaskReveal", "MASK REVEAL", [](LayerBuilder& l) {
        l.scale_anim()
            .key(Frame{0},  Vec3{1.0f, 0.6f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{18}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic});
        l.opacity_anim()
            .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{18}, 1.0f, EasingCurve{Easing::Linear});
    });
}

// 15. SnapPop — overshoot aggressivo (scale 0.6→1.04→0.98→1.0, 22 fr)
Composition minimalist_text_snap_pop() {
    return make_minimalist_comp("MinimalistTextSnapPop", "SNAP POP", [](LayerBuilder& l) {
        l.scale_anim()
            .key(Frame{0},  Vec3{0.6f,  0.6f,  1.0f}, EasingCurve{Easing::OutBack})
            .key(Frame{14}, Vec3{1.04f, 1.04f, 1.0f}, EasingCurve{Easing::OutBack})
            .key(Frame{18}, Vec3{0.98f, 0.98f, 1.0f}, EasingCurve{Easing::InOutCubic})
            .key(Frame{22}, Vec3{1.0f,  1.0f,  1.0f}, EasingCurve{Easing::OutCubic});
        l.opacity_anim()
            .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{14}, 1.0f, EasingCurve{Easing::Linear});
    });
}

// ═════════════════════════════════════════════════════════════════════════════
// EXIT ANIMATIONS — intro 0→22 + hold + exit 115→140 (150 frame total)
// Same make_minimalist_comp helper — the setup lambda just has more keyframes.
// ═════════════════════════════════════════════════════════════════════════════

// 16. FadeAway — uscita morbida con drift verso il basso
Composition minimalist_text_fade_away() {
    return make_minimalist_comp("MinimalistTextFadeAway", "FADE AWAY", [](LayerBuilder& l) {
        l.opacity_anim()
            .key(Frame{0},   0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{22},  1.0f, EasingCurve{Easing::Linear})
            .key(Frame{115}, 1.0f, EasingCurve{Easing::Linear})
            .key(Frame{140}, 0.0f, EasingCurve{Easing::InCubic});
        l.scale_anim()
            .key(Frame{0},  Vec3{0.97f, 0.97f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, Vec3{1.0f,  1.0f,  1.0f}, EasingCurve{Easing::OutCubic});
        l.position_anim()
            .key(Frame{115}, Vec3{0.0f, 0.0f,  0.0f}, EasingCurve{Easing::Linear})
            .key(Frame{140}, Vec3{0.0f, 12.0f, 0.0f}, EasingCurve{Easing::InCubic});
    });
}

// 17. ScaleOut — rimpicciolisce e sfoca uscendo (scale 1.0→0.86, blur 0→6)
Composition minimalist_text_scale_out() {
    return make_minimalist_comp("MinimalistTextScaleOut", "SCALE OUT", [](LayerBuilder& l) {
        l.opacity_anim()
            .key(Frame{0},   0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{22},  1.0f, EasingCurve{Easing::Linear})
            .key(Frame{115}, 1.0f, EasingCurve{Easing::Linear})
            .key(Frame{140}, 0.0f, EasingCurve{Easing::InCubic});
        l.scale_anim()
            .key(Frame{0},   Vec3{0.95f, 0.95f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{22},  Vec3{1.0f,  1.0f,  1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{115}, Vec3{1.0f,  1.0f,  1.0f}, EasingCurve{Easing::Linear})
            .key(Frame{140}, Vec3{0.86f, 0.86f, 1.0f}, EasingCurve{Easing::InCubic});
        l.blur_anim()
            .key(Frame{115}, 0.0f, EasingCurve{Easing::Linear})
            .key(Frame{140}, 6.0f, EasingCurve{Easing::InCubic});
    });
}

// 18. SlideUpOut — scivola verso l'alto svanendo (pos_y 0→-60)
Composition minimalist_text_slide_up_out() {
    return make_minimalist_comp("MinimalistTextSlideUpOut", "SLIDE UP OUT", [](LayerBuilder& l) {
        l.position_anim()
            .key(Frame{0},   Vec3{0.0f, 20.0f,  0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{22},  Vec3{0.0f, 0.0f,   0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{110}, Vec3{0.0f, 0.0f,   0.0f}, EasingCurve{Easing::Linear})
            .key(Frame{140}, Vec3{0.0f, -60.0f, 0.0f}, EasingCurve{Easing::InCubic});
        l.opacity_anim()
            .key(Frame{0},   0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{22},  1.0f, EasingCurve{Easing::Linear})
            .key(Frame{110}, 1.0f, EasingCurve{Easing::Linear})
            .key(Frame{140}, 0.0f, EasingCurve{Easing::InCubic});
    });
}

// 19. BlurAway — sfoca e allarga uscendo (blur 0→8, scale 1.0→1.02)
Composition minimalist_text_blur_away() {
    return make_minimalist_comp("MinimalistTextBlurAway", "BLUR AWAY", [](LayerBuilder& l) {
        l.opacity_anim()
            .key(Frame{0},   0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{22},  1.0f, EasingCurve{Easing::Linear})
            .key(Frame{115}, 1.0f, EasingCurve{Easing::Linear})
            .key(Frame{140}, 0.0f, EasingCurve{Easing::InCubic});
        l.scale_anim()
            .key(Frame{0},   Vec3{1.02f, 1.02f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{22},  Vec3{1.0f,  1.0f,  1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{115}, Vec3{1.0f,  1.0f,  1.0f}, EasingCurve{Easing::Linear})
            .key(Frame{140}, Vec3{1.02f, 1.02f, 1.0f}, EasingCurve{Easing::InCubic});
        l.blur_anim()
            .key(Frame{115}, 0.0f, EasingCurve{Easing::Linear})
            .key(Frame{140}, 8.0f,  EasingCurve{Easing::InCubic});
    });
}

// 20. TiltOut — rotazione +3° + rimpicciolisce (rotate_z 0→+3°, scale 1.0→0.94)
Composition minimalist_text_tilt_out() {
    return make_minimalist_comp("MinimalistTextTiltOut", "TILT OUT", [](LayerBuilder& l) {
        l.opacity_anim()
            .key(Frame{0},   0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{22},  1.0f, EasingCurve{Easing::Linear})
            .key(Frame{115}, 1.0f, EasingCurve{Easing::Linear})
            .key(Frame{140}, 0.0f, EasingCurve{Easing::InCubic});
        l.scale_anim()
            .key(Frame{0},   Vec3{0.97f, 0.97f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{22},  Vec3{1.0f,  1.0f,  1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{115}, Vec3{1.0f,  1.0f,  1.0f}, EasingCurve{Easing::Linear})
            .key(Frame{140}, Vec3{0.94f, 0.94f, 1.0f}, EasingCurve{Easing::InCubic});
        l.rotate_anim()
            .key(Frame{115}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::Linear})
            .key(Frame{140}, Vec3{0.0f, 0.0f, 3.0f}, EasingCurve{Easing::InCubic});
    });
}

// ── Per-domain registration ──────────────────────────────────────────────────
// Forward-declare image presets from the companion file.
Composition minimalist_image_fade_in();
Composition minimalist_image_focus_in();
Composition minimalist_image_scale_drop();
Composition minimalist_image_fade_shift_vertical();
Composition minimalist_image_center_split();
Composition minimalist_image_reveal_from_bottom();
Composition minimalist_image_framing_bracket();
Composition minimalist_image_tracking_breathing();
Composition minimalist_image_elegant_exit();
Composition minimalist_image_elastic_slide();

void register_minimalist_compositions() {
    static bool done = false;
    if (done) return;
    done = true;
    detail::add_builtin_composition("MinimalistTextFadeUp",              minimalist_text_fade_up);
    detail::add_builtin_composition("MinimalistTextTrackingReveal",     minimalist_text_tracking_reveal);
    detail::add_builtin_composition("MinimalistTextClipReveal",         minimalist_text_clip_reveal);
    detail::add_builtin_composition("MinimalistTextFadeDown",           minimalist_text_fade_down);
    detail::add_builtin_composition("MinimalistTextSoftScale",          minimalist_text_soft_scale);
    detail::add_builtin_composition("MinimalistTextBlurFocus",          minimalist_text_blur_focus);
    detail::add_builtin_composition("MinimalistTextSlideLeft",          minimalist_text_slide_left);
    detail::add_builtin_composition("MinimalistTextSlideRight",         minimalist_text_slide_right);
    detail::add_builtin_composition("MinimalistTextScalePop",           minimalist_text_scale_pop);
    detail::add_builtin_composition("MinimalistTextFloatIn",            minimalist_text_float_in);
    detail::add_builtin_composition("MinimalistTextLetterRise",         minimalist_text_letter_rise);
    detail::add_builtin_composition("MinimalistTextDriftIn",            minimalist_text_drift_in);
    detail::add_builtin_composition("MinimalistTextTiltIn",             minimalist_text_tilt_in);
    detail::add_builtin_composition("MinimalistTextMaskReveal",         minimalist_text_mask_reveal);
    detail::add_builtin_composition("MinimalistTextSnapPop",            minimalist_text_snap_pop);
    detail::add_builtin_composition("MinimalistTextFadeAway",           minimalist_text_fade_away);
    detail::add_builtin_composition("MinimalistTextScaleOut",           minimalist_text_scale_out);
    detail::add_builtin_composition("MinimalistTextSlideUpOut",         minimalist_text_slide_up_out);
    detail::add_builtin_composition("MinimalistTextBlurAway",           minimalist_text_blur_away);
    detail::add_builtin_composition("MinimalistTextTiltOut",            minimalist_text_tilt_out);
    detail::add_builtin_composition("MinimalistImageFadeIn",            minimalist_image_fade_in);
    detail::add_builtin_composition("MinimalistImageFocusIn",           minimalist_image_focus_in);
    detail::add_builtin_composition("MinimalistImageScaleDrop",         minimalist_image_scale_drop);
    detail::add_builtin_composition("MinimalistImageFadeShiftVertical", minimalist_image_fade_shift_vertical);
    detail::add_builtin_composition("MinimalistImageCenterSplit",       minimalist_image_center_split);
    detail::add_builtin_composition("MinimalistImageRevealFromBottom",  minimalist_image_reveal_from_bottom);
    detail::add_builtin_composition("MinimalistImageFramingBracket",    minimalist_image_framing_bracket);
    detail::add_builtin_composition("MinimalistImageTrackingBreathing", minimalist_image_tracking_breathing);
    detail::add_builtin_composition("MinimalistImageElegantExit",       minimalist_image_elegant_exit);
    detail::add_builtin_composition("MinimalistImageElasticSlide",      minimalist_image_elastic_slide);
}

} // namespace chronon3d::content::minimalist
