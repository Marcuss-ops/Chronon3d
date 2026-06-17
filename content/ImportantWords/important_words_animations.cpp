#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/animation/easing/easing.hpp>

#include "content/common/animation_helpers.hpp"
#include "content/text/text_helpers.hpp"
#include "important_words_theme.hpp"

namespace chronon3d::content::important_words {

using namespace chronon3d::content::animation_helpers;
using namespace chronon3d::content;

// Helper for the word compositions.  Builds a single layer with the
// palette-tinted rounded-rect backdrop + WHITE text word on top + layer
// drop-shadow (SOFT black) for the "diffuse / lifted-off" look. No
// accent line, share-or-fail design.
//
// `visible_in_out` is (fade_in_frame, hold_until_frame, fade_out_frame).
// All three must be in [0, duration].
static void build_important_word(SceneBuilder& s,
                                const WordPreset& word,
                                const WordPalette& palette,
                                Frame fade_in, Frame hold_until, Frame fade_out) {
    s.layer("word", [word, palette, fade_in, hold_until, fade_out](LayerBuilder& l) {
        l.pin_to(Anchor::Center);
        l.opacity_anim()
            .key(Frame{0},           0.0f,  EasingCurve{Easing::OutCubic})
            .key(Frame{fade_in},     1.0f,  EasingCurve{Easing::Linear})
            .key(Frame{hold_until},  1.0f,  EasingCurve{Easing::Linear})
            .key(Frame{fade_out},    0.0f,  EasingCurve{Easing::InCubic});
        l.position_anim()
            .key(Frame{0},           Vec3{0.0f, WORD_LOWER_Y + 30.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{fade_in},     Vec3{0.0f, WORD_LOWER_Y,        0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{fade_out},    Vec3{0.0f, WORD_LOWER_Y + 12.0f, 0.0f}, EasingCurve{Easing::InCubic});
        // ── RED backdrop rect (opaque, behind text in painter's order) ──
        // corner_radius dropped 10→4 so the backdrop reads as a proper
        // RECTANGLE rather than a soft chip/squircle.
        l.rounded_rect("bg", {
            .size   = word.rect_outer_size,
            .radius = word.corner_radius,
            .color  = palette.backdrop,
            .pos    = {0.0f, 0.0f, 0.0f},
        });
        // ── WHITE word on top — Inter-Bold (modern geometric sans) ──────
        // text::centered_text() carries the font_path + font_family designators
        // so the rendered text uses Inter-Bold instead of the previous Poppins.
        //
        // ⚠ C++20 [dcl.init.aggr] designated-initializer-order footgun:
        // designators MUST be in struct-declaration order, otherwise the
        // compiler rejects the call as malformed.  Field order in
        // CenterTextOptions is: text, box, pos, font_path, font_family,
        // font_weight, font_style, font_size, tracking, color, …
        l.text("name", text::centered_text({
            .text        = word.label,
            .font_path   = WORD_FONT_PATH,
            .font_family = WORD_FONT_FAMILY,
            .font_size   = word.font_size,
            .tracking    = word.tracking,
            .color       = palette.text,
        }));
        // ── SOFT black drop shadow (radius 16, 0.55 alpha) ───────────────
        // The wider blur + low alpha gives a diffuse contact-shadow look
        // rather than a hard drop shadow. Geometry shared via theme.hpp.
        l.drop_shadow(WORD_SHADOW_OFFSET, palette.shadow, WORD_SHADOW_RADIUS);
    });
}

// Duration tuning (per user request: "stay on screen a bit longer"):
//   single-word comps: 60 → 90 frame (+50% on-screen presence)
//   trio:               90 → 120 frame (+33%, gives 40 frame per word)
// Fade / hold / fade proportions preserved across the new durations.

// 1. ImportantWordDirectorLight — DIRECTOR label, coral palette.
//    90 frame total: fade_in 10, hold_until 70, fade_out 85.
Composition important_word_director_light() {
    return composition({.name="ImportantWordDirectorLight", .width=1920, .height=1080, .duration=90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        build_important_word(s, WORD_DIRECTOR, PALETTE_LIGHT, Frame{10}, Frame{70}, Frame{85});
        return s.build();
    });
}

// 2. ImportantWordActorWarm — ACTOR label, vermillion palette.
//    90 frame total: same fade/hold pattern as #1.
Composition important_word_actor_warm() {
    return composition({.name="ImportantWordActorWarm", .width=1920, .height=1080, .duration=90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        build_important_word(s, WORD_ACTOR, PALETTE_WARM, Frame{10}, Frame{70}, Frame{85});
        return s.build();
    });
}

// 3. ImportantWordWriterCool — WRITER label, magenta-red palette.
//    90 frame total: same fade/hold pattern as #1.
Composition important_word_writer_cool() {
    return composition({.name="ImportantWordWriterCool", .width=1920, .height=1080, .duration=90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        build_important_word(s, WORD_WRITER, PALETTE_COOL, Frame{10}, Frame{70}, Frame{85});
        return s.build();
    });
}

// 4. ImportantWordTrio — cycles DIRECTOR (coral), ACTOR (vermillion),
//    WRITER (magenta-red) sequentially across 120 frame.  Each word gets
//    40 frame of on-screen presence (8 fade-in + 24 hold + 8 fade-out),
//    so the trio sits comfortably without rushed transitions.
Composition important_word_trio() {
    return composition({.name="ImportantWordTrio", .width=1920, .height=1080, .duration=120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        // Director window: 0-40
        build_important_word(s, WORD_DIRECTOR, PALETTE_LIGHT, Frame{8},  Frame{32}, Frame{40});
        // Actor window:    40-80
        build_important_word(s, WORD_ACTOR,    PALETTE_WARM,  Frame{48}, Frame{72}, Frame{80});
        // Writer window:   80-120
        build_important_word(s, WORD_WRITER,   PALETTE_COOL,  Frame{88}, Frame{112}, Frame{120});
        return s.build();
    });
}

} // namespace chronon3d::content::important_words
