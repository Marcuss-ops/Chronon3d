#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/text/text_definition.hpp>

#include "content/common/animation_helpers.hpp"
#include "important_words_theme.hpp"

namespace chronon3d::content::important_words {

using namespace chronon3d::content::animation_helpers;
using namespace chronon3d::content;

// Helper for the word compositions.  Builds a single layer with the
// palette-tinted rounded-rect backdrop + WHITE text word on top + layer
// drop-shadow (SOFT black) for the "diffuse / lifted-off" look +
// per-glyph text shadow for readability. No accent line.
//
// `visible_in_out` is (fade_in_frame, hold_until_frame, fade_out_frame).
// All three must be in [0, duration].
// `layer_suffix` disambiguates layer names in multi-word compositions (Trio).
static void build_important_word(SceneBuilder& s,
                                const WordPreset& word,
                                const WordPalette& palette,
                                Frame fade_in, Frame hold_until, Frame fade_out,
                                std::string_view layer_suffix = {}) {
    const std::string lname = layer_suffix.empty()
        ? std::string("word")
        : std::string("word_") + std::string(layer_suffix);
    s.layer(lname, [word, palette, fade_in, hold_until, fade_out](LayerBuilder& l) {
        l.pin_to(Anchor::Center);
        // ── Opacity: hold at 0 until 4f before fade_in, then quick transition ──
        // This prevents layers from slowly fading in from frame 0 in the Trio.
        const Frame kPreFade = std::max(Frame{0}, fade_in - Frame{4});
        l.opacity_anim()
            .key(Frame{0},           0.0f,  EasingCurve{Easing::Linear})
            .key(kPreFade,           0.0f,  EasingCurve{Easing::Linear})
            .key(Frame{fade_in},     1.0f,  EasingCurve{Easing::OutCubic})
            .key(Frame{hold_until},  1.0f,  EasingCurve{Easing::Linear})
            .key(Frame{fade_out},    0.0f,  EasingCurve{Easing::InCubic});
        l.position_anim()
            .key(Frame{0},           Vec3{0.0f, WORD_LOWER_Y + 30.0f, 0.0f}, EasingCurve{Easing::Linear})
            .key(kPreFade,           Vec3{0.0f, WORD_LOWER_Y + 30.0f, 0.0f}, EasingCurve{Easing::Linear})
            .key(Frame{fade_in},     Vec3{0.0f, WORD_LOWER_Y,        0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{fade_out},    Vec3{0.0f, WORD_LOWER_Y + 12.0f, 0.0f}, EasingCurve{Easing::InCubic});
        // ── Gradient backdrop — modern material pill shape ──────────
        // Subtle vertical gradient: lighter at top, darker at bottom.
        // Thin white stroke (0.12 alpha) for a clean modern edge.
        {
            Color top_c = palette.backdrop;
            top_c.r = std::min(1.0f, top_c.r * 1.12f);
            top_c.g = std::min(1.0f, top_c.g * 1.12f);
            top_c.b = std::min(1.0f, top_c.b * 1.12f);
            Color bot_c = palette.backdrop;
            bot_c.r *= 0.88f;
            bot_c.g *= 0.88f;
            bot_c.b *= 0.88f;
            l.rounded_rect("bg", {
                .size   = word.rect_outer_size,
                .radius = word.corner_radius,
                .color  = palette.backdrop,
                .pos    = {0.0f, 0.0f, 0.0f},
                .fill   = graphics::FillStyle::linear(
                    {0.0f, -word.rect_outer_size.y * 0.5f},
                    {0.0f,  word.rect_outer_size.y * 0.5f},
                    {{0.0f, top_c}, {1.0f, bot_c}}),
                .stroke = {true, {1.0f, 1.0f, 1.0f, 0.12f}, 1.5f},
            });
        }
        // ── WHITE word on top — DMSans-Bold (modern geometric sans) ──
        // Per-letter text shadow for readability against the red backdrop.
        {
            auto def = from_text_spec(TextSpec{.content    = {.value = word.label},.font       = {.font_path   = WORD_FONT_PATH,
                               .font_family = WORD_FONT_FAMILY,
                               .font_weight = 700,
                               .font_size   = word.font_size},.layout     = {.tracking = word.tracking},.appearance = {.color = palette.text},});
            def.style.shadows.push_back(TextShadow{
                .enabled = true,
                .offset  = {0.0f, 4.0f},
                .blur    = 6.0f,
                .opacity = 0.50f,
                .color   = {0.0f, 0.0f, 0.0f, 1.0f},
            });
            l.text("name", def);
        }
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
        build_important_word(s, WORD_DIRECTOR, PALETTE_LIGHT, Frame{8},  Frame{32}, Frame{40},  "director");
        // Actor window:    44-76 (4f gap after Director fades out)
        build_important_word(s, WORD_ACTOR,    PALETTE_WARM,  Frame{48}, Frame{68}, Frame{76},  "actor");
        // Writer window:   80-112 (4f gap after Actor fades out)
        build_important_word(s, WORD_WRITER,   PALETTE_COOL,  Frame{84}, Frame{104}, Frame{112}, "writer");
        return s.build();
    });
}

// ── Per-domain registration ──────────────────────────────────────────────────
void register_important_word_compositions(CompositionRegistry& registry) {
    registry.add("ImportantWordDirectorLight", [](const CompositionProps&) { return important_word_director_light(); });
    registry.add("ImportantWordActorWarm", [](const CompositionProps&) { return important_word_actor_warm(); });
    registry.add("ImportantWordWriterCool", [](const CompositionProps&) { return important_word_writer_cool(); });
    registry.add("ImportantWordTrio", [](const CompositionProps&) { return important_word_trio(); });
}

} // namespace chronon3d::content::important_words
