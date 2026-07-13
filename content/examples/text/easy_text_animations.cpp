// ============================================================================
// content/examples/text/easy_text_animations.cpp
//
// TICKET-REFACTOR-CONTENT-EXAMPLES-17 — 5 easy text animation factories
// (single text layer, opacity/position/scale/blur motion).
//
// Extracted from the >360-LoC monolithic `content/examples/text/text_animations.cpp`
// (now deleted; replaced by this cpp + typewriter_animations.cpp +
// text_animation_registration.cpp).
//
// All 5 factories use the shared `make_text_anim` skeleton from
// content/common/animation_helpers.hpp (grid bg, centered layer, drop shadow,
// 64-80pt text) and supply only their animation-specific setup via a lambda.
// Cat-3 minimal-surface: no new SDK surface; no new helpers (add_bg + txt_center
// are local to this file).
// ============================================================================

#include "easy_text_animations.hpp"

#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/motion/timeline.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>

#include "content/common/animation_helpers.hpp"
#include "content/common/background_helpers.hpp"
#include <chronon3d/text/text_definition.hpp>

namespace chronon3d::content::anims {

using chronon3d::content::animation_helpers::TEXT_COLOR;
using chronon3d::content::animation_helpers::FONT_REGULAR;
using chronon3d::content::animation_helpers::TextAnimBg;
using chronon3d::content::animation_helpers::make_text_anim;
using chronon3d::content::animation_helpers::text_anim_opacity;

namespace {

constexpr f32 BOX_W = 1400.0f;
constexpr f32 BOX_H = 130.0f;
constexpr f32 BASE_Y = -50.0f;

// MinimalistGrid background for the 5 easy anims.
void add_bg(SceneBuilder& s) {
    chronon3d::content::backgrounds::add_common_background(
        s, chronon3d::content::backgrounds::BackgroundStyles::Minimalist());
}

// Centered text params for the 5 easy anims.
TextDefinition txt_center(std::string text, f32 font_size = 72.0f) {
    return TextDefinition{
    .content = {.value = std::move(text)},
    .style = {
        .font = {.font_path = FONT_REGULAR, .font_size = font_size},
        .color = TEXT_COLOR
    },
    .frame = {
        .placement = {TextPlacementKind::Absolute, {0.0f, 0.0f}},
        .size = {BOX_W, BOX_H},
        .align = TextAlign::Center,
        .vertical_align = VerticalAlign::Middle,
        .line_height = 1.22f,
        .tracking = 4.0f
    }
};
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
//  5 EASY ANIMATIONS — each delegates the common skeleton to make_text_anim.
// ═══════════════════════════════════════════════════════════════════════════

// 1. AnimSlideUp — slides up from below with fade
Composition anim_slide_up() {
    // Original used p=min(progress*3,1)+interpolate(p,0,0.30,...): completes at frame 6.
    return make_text_anim("AnimSlideUp", txt_center("Slide Up", 80.0f),
                          Frame{60}, TextAnimBg::MinimalistGrid,
        [](LayerBuilder& l) {
            motion::timeline(Vec3{0.0f, BASE_Y + 80.0f, 0.0f})
                .to(Frame{6}, Vec3{0.0f, BASE_Y, 0.0f}, Easing::OutCubic)
                .apply_to(l.position_anim());
            text_anim_opacity().apply_to(l.opacity_anim());
        });
}

// 2. AnimScalePop — scales from small with overshoot
Composition anim_scale_pop() {
    return make_text_anim("AnimScalePop", txt_center("Scale Pop", 80.0f),
                          Frame{60}, TextAnimBg::MinimalistGrid,
        [](LayerBuilder& l) {
            motion::timeline(Vec3{0.6f, 0.6f, 1.0f})
                .to(Frame{6}, Vec3{1.0f, 1.0f, 1.0f}, Easing::OutBack)
                .apply_to(l.scale_anim());
            text_anim_opacity().apply_to(l.opacity_anim());
        });
}

// 3. AnimBlurFocus — blurry → sharp via focus_in
Composition anim_blur_focus() {
    // Declarative blur Timeline (24px → 0px) replaces the focus_in preset.
    return make_text_anim("AnimBlurFocus", txt_center("Blur Focus", 80.0f),
                          Frame{60}, TextAnimBg::MinimalistGrid,
        [](LayerBuilder& l) {
            l.opacity(1.0f);
            motion::timeline(24.0f)
                .to(Frame{20}, 0.0f, Easing::OutCubic)
                .apply_to(l.blur_anim());
        });
}

// 4. AnimSlideLeft — slides in from the left
Composition anim_slide_left() {
    return make_text_anim("AnimSlideLeft", txt_center("Slide Left", 80.0f),
                          Frame{60}, TextAnimBg::MinimalistGrid,
        [](LayerBuilder& l) {
            motion::timeline(Vec3{-120.0f, BASE_Y, 0.0f})
                .to(Frame{6}, Vec3{0.0f, BASE_Y, 0.0f}, Easing::OutCubic)
                .apply_to(l.position_anim());
            text_anim_opacity().apply_to(l.opacity_anim());
        });
}

// 5. AnimBounceDrop — drops from above with bounce
Composition anim_bounce_drop() {
    // K=2.5: 0.30/K * 80 ≈ frame 9.6 (rounded to 10 for OutBounce),
    // opacity finishes at 0.5/K * 80 = frame 16.
    return make_text_anim("AnimBounceDrop", txt_center("Bounce Drop", 80.0f),
                          Frame{80}, TextAnimBg::MinimalistGrid,
        [](LayerBuilder& l) {
            motion::timeline(Vec3{0.0f, -230.0f, 0.0f})
                .to(Frame{10}, Vec3{0.0f, BASE_Y, 0.0f}, Easing::OutBounce)
                .apply_to(l.position_anim());
            motion::timeline(0.0f)
                .to(Frame{16}, 1.0f, Easing::OutCubic)
                .apply_to(l.opacity_anim());
        });
}

} // namespace chronon3d::content::anims
