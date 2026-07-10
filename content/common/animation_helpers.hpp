#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/animation/motion/timeline.hpp>
#include <functional>
#include <string>

#include "content/common/background_helpers.hpp"
#include "content/text/text_helpers_centered.hpp"

namespace chronon3d::content::animation_helpers {

// ── Shared constants ───────────────────────────────────────────────────────
inline constexpr Color SHADOW_COLOR = {0.0f, 0.0f, 0.0f, 0.15f};
inline constexpr Color TEXT_COLOR   = {0.92f, 0.93f, 0.97f, 1.0f};
inline constexpr const char* FONT_REGULAR = "assets/fonts/Poppins-Regular.ttf";
inline constexpr f32   BOX_W = 1200.0f;
inline constexpr f32   BOX_H = 240.0f;

// ── add_black_background ───────────────────────────────────────────────────
// Full-screen black background layer used by all animation compositions.
inline void add_black_background(SceneBuilder& s) {
    s.layer("_bg", [](LayerBuilder& l) {
        l.fullscreen_rect("bg", Color{0.0f, 0.0f, 0.0f, 1.0f});
    });
}

// ── make_text ──────────────────────────────────────────────────────────────
// Standard centered text: 1200×240 box, Poppins-Bold, consistent tracking &
// drop-shadow-ready color.  Designed for AnimFadeIn, AnimSlide, AnimScale.
//
// Delegates to text::centered_text() which produces a fully-populated
// TextSpec with all layout fields (anchor, centering_mode, wrap, overflow,
// max_lines, etc.) that the M1.5 text compiler pipeline requires.
inline TextSpec make_text(const std::string& text, f32 font_size = 64.0f) {
    auto spec = chronon3d::content::text::centered_text({
        .text      = text,
        .box       = {BOX_W, BOX_H},
        .font_size = font_size,
        .tracking  = 3.0f,
        .color     = TEXT_COLOR,
    });
    return spec;
}

// ═══════════════════════════════════════════════════════════════════════════
// make_text_anim — unified helper for text-on-bg animation compositions.
//
// Replaces the previously-duplicated make_easy_anim (text_animations.cpp)
// and make_basic_anim (animation_compositions.cpp), which differed only in
// background style and font-size.  The caller supplies:
//   - the TextSpec (so make_text() vs txt_center() variants stay available)
//   - the background style (TextAnimBg)
//   - a setup lambda that wires position_anim / scale_anim / opacity_anim
//     using motion::Timeline<T> for declarative easing.
//
// The helper handles the shared skeleton (bg layer + centered text layer
// + drop shadow + text spec) so composition authors write only anim logic.
// ── TextAnimBg — which background family to use ──────────────────────────
enum class TextAnimBg : u8 {
    Black,            // ── black-only (basic anims: FadeIn / Slide / Scale)
    MinimalistGrid,   // ── subtle dark gray bg + grid (easy anims: SlideUp / ScalePop / etc.)
};

// ── MakeTextAnimFn — the per-composition animation setup ──────────────────
using MakeTextAnimFn = std::function<void(LayerBuilder& l)>;

// ── make_text_anim ────────────────────────────────────────────────────────
// Builds a Composition with the shared skeleton + per-composition animation.
inline Composition make_text_anim(
    const char* name,
    const TextSpec& text_spec,
    Frame duration,
    TextAnimBg bg_kind,
    MakeTextAnimFn setup)
{
    return composition(
        {.name = name, .width = 1920, .height = 1080,
         .duration = duration},
        [text_spec, bg_kind, setup = std::move(setup)](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            switch (bg_kind) {
                case TextAnimBg::Black:
                    add_black_background(s);
                    break;
                case TextAnimBg::MinimalistGrid:
                    chronon3d::content::backgrounds::add_common_background(
                        s, chronon3d::content::backgrounds::BackgroundStyles::Minimalist());
                    break;
            }
            s.layer("text", [&](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                setup(l);
                l.drop_shadow(Vec2{0.0f, 4.0f}, SHADOW_COLOR, 8.0f)
                 .text("label", text_spec);
            });
            return s.build();
        });
}

// ═══════════════════════════════════════════════════════════════════════════
//  Convenience: motion::Timeline<Vec3> / <f32> shorthands for the common
//  "in / out" animation shape used by all 5 easy + 3 basic anims.
//
//  Animations finish at Frame{6} on duration=60 compositions, matching the
//  original `p = min(progress*3, 1)` + `interpolate(p, 0, 0.30, ...)` shape:
//
//      progress(60-frame comp) saturates at frame 20; the 0.30-buffer inside
//      the interpolate() function maps that to "fully animated" at frame 6,
//      then holds for the remaining frames.  Using Frame{6} explicitly yields
//      the same visual behaviour.

inline motion::Timeline<f32> text_anim_opacity(f32 peak = 1.0f, Frame done_at = Frame{6}) {
    return motion::timeline(0.0f).to(done_at, peak, Easing::OutCubic);
}

inline motion::Timeline<f32> text_anim_opacity_outback(Frame done_at = Frame{6}) {
    return motion::timeline(0.0f).to(done_at, 1.0f, Easing::OutBack);
}

} // namespace chronon3d::content::animation_helpers
