#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/text/text_definition.hpp>

#include <array>
#include <string_view>

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
            l.rect("bg", {
                .size   = word.rect_outer_size,
                .color  = palette.backdrop,
                .pos    = {0.0f, 0.0f, 0.0f},
                .fill   = graphics::FillStyle::solid(palette.backdrop),
                .stroke = {false, {}, 0.0f},
            });
        }
        // ── WHITE word on top — DMSans-Bold (modern geometric sans) ──
        // Per-letter text shadow for readability against the red backdrop.
        {
            auto def = TextDefinition{
    .content = {.value = word.label},
    .style = {
        .font = {.font_path   = WORD_FONT_PATH,
                               .font_family = WORD_FONT_FAMILY,
                               .font_weight = 700,
                               .font_size   = word.font_size},
        .color = palette.text
    },
    .frame = {
        .tracking = word.tracking
    }
};
            l.text("name", def);
        }
    });
}

static void build_important_phrase(SceneBuilder& s,
                                   std::string_view layer_name,
                                   std::string_view phrase,
                                   f32 y_offset,
                                   Frame start_frame) {
    const std::string node_name = std::string(layer_name) + "_text";
    s.layer(std::string(layer_name), [phrase, y_offset, start_frame, node_name](LayerBuilder& l) {
        l.pin_to(Anchor::Center);
        l.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::Linear})
            .key(start_frame, 1.0f, EasingCurve{Easing::OutCubic});
        // Keep every row fixed while it fades in. This avoids transient
        // overlap between neighboring phrases during the staggered reveal.
        l.position(Vec3{0.0f, y_offset, 0.0f});

        l.text(node_name, TextDefinition{
            .content = {.value = std::string(phrase)},
            .style = {
                .font = {
                    .font_path = "assets/fonts/Poppins-Bold.ttf",
                    .font_family = "Poppins",
                    .font_weight = 700,
                    .font_size = 64.0f,
                },
                .color = {1.0f, 1.0f, 1.0f, 1.0f},
            },
            .frame = {
                .size = {1600.0f, 100.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 3.0f,
            },
        });
    });
}

static void build_yellow_subtitle(SceneBuilder& s,
                                  std::string_view layer_name,
                                  std::string_view subtitle,
                                  Frame start_frame,
                                  Frame end_frame) {
    const std::string node_name = std::string(layer_name) + "_text";
    s.layer(std::string(layer_name), [subtitle, start_frame, end_frame, node_name](LayerBuilder& l) {
        l.pin_to(Anchor::Center);
        l.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::Linear})
            .key(start_frame, 1.0f, EasingCurve{Easing::OutCubic})
            .key(end_frame, 1.0f, EasingCurve{Easing::Linear})
            .key(end_frame + Frame{5}, 0.0f, EasingCurve{Easing::InCubic});
        l.position(Vec3{0.0f, 390.0f, 0.0f});
        l.text(node_name, TextDefinition{
            .content = {.value = std::string(subtitle)},
            .style = {
                .font = {
                    .font_path = "assets/fonts/Poppins-Bold.ttf",
                    .font_family = "Poppins",
                    .font_weight = 600,
                    .font_size = 48.0f,
                },
                .color = {1.0f, 0.88f, 0.05f, 1.0f},
                .paint = {.fill = {1.0f, 0.88f, 0.05f, 1.0f}},
            },
            .frame = {
                .size = {1600.0f, 84.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 1.0f,
            },
        });
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

static Composition make_phrase_stack(std::string_view composition_name,
                                     Frame duration,
                                     std::array<Frame, 5> starts) {
    return composition({.name=std::string(composition_name), .width=1920, .height=1080, .duration=duration}, [starts](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        build_important_phrase(s, "phrase_01", "KEEP MOVING",          -240.0f, starts[0]);
        build_important_phrase(s, "phrase_02", "FOCUS ON WHAT MATTERS", -120.0f, starts[1]);
        build_important_phrase(s, "phrase_03", "MAKE IT SIMPLE",           0.0f, starts[2]);
        build_important_phrase(s, "phrase_04", "SHOW YOUR STORY",         120.0f, starts[3]);
        build_important_phrase(s, "phrase_05", "CREATE WITH PURPOSE",     240.0f, starts[4]);
        return s.build();
    });
}

Composition important_phrases_stack() {
    return make_phrase_stack("ImportantPhrasesStack", Frame{90},
                             {Frame{4}, Frame{8}, Frame{12}, Frame{16}, Frame{20}});
}

Composition important_phrases_stack_fast() {
    return make_phrase_stack("ImportantPhrasesStackFast", Frame{60},
                             {Frame{0}, Frame{3}, Frame{6}, Frame{9}, Frame{12}});
}

Composition important_phrases_stack_slow() {
    return make_phrase_stack("ImportantPhrasesStackSlow", Frame{120},
                             {Frame{8}, Frame{28}, Frame{48}, Frame{68}, Frame{88}});
}

Composition important_phrases_stack_reverse() {
    return make_phrase_stack("ImportantPhrasesStackReverse", Frame{90},
                             {Frame{20}, Frame{16}, Frame{12}, Frame{8}, Frame{4}});
}

Composition subtitle_yellow_fade() {
    return composition({.name="SubtitleYellowFade", .width=1920, .height=1080, .duration=60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        build_yellow_subtitle(s, "subtitle_01", "THIS IS YOUR STORY", Frame{6}, Frame{55});
        return s.build();
    });
}

Composition important_words_red_lower() {
    return composition({.name="ImportantWordsRedLower", .width=1920, .height=1080, .duration=105}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        build_important_word(s, WordPreset{
            .label = "FOCUS",
            .rect_outer_size = {500.0f, 140.0f},
            .font_size = 72.0f,
            .tracking = 8.0f,
            .pad_x = 32.0f,
            .pad_y = 28.0f,
            .corner_radius = 10.0f,
        }, PALETTE_WARM, Frame{8}, Frame{34}, Frame{44}, "focus");
        build_important_word(s, WordPreset{
            .label = "ACTION",
            .rect_outer_size = {560.0f, 140.0f},
            .font_size = 72.0f,
            .tracking = 8.0f,
            .pad_x = 32.0f,
            .pad_y = 28.0f,
            .corner_radius = 10.0f,
        }, PALETTE_WARM, Frame{52}, Frame{78}, Frame{88}, "action");
        return s.build();
    });
}

// ── Per-domain registration ──────────────────────────────────────────────────
void register_important_word_compositions(CompositionRegistry& registry) {
    registry.add(make_composition_descriptor("ImportantWordDirectorLight", [](const CompositionProps&) { return important_word_director_light(); }));
    registry.add(make_composition_descriptor("ImportantWordActorWarm", [](const CompositionProps&) { return important_word_actor_warm(); }));
    registry.add(make_composition_descriptor("ImportantWordWriterCool", [](const CompositionProps&) { return important_word_writer_cool(); }));
    registry.add(make_composition_descriptor("ImportantWordTrio", [](const CompositionProps&) { return important_word_trio(); }));
    registry.add(make_composition_descriptor("ImportantPhrasesStack", [](const CompositionProps&) { return important_phrases_stack(); }));
    registry.add(make_composition_descriptor("ImportantPhrasesStackFast", [](const CompositionProps&) { return important_phrases_stack_fast(); }));
    registry.add(make_composition_descriptor("ImportantPhrasesStackSlow", [](const CompositionProps&) { return important_phrases_stack_slow(); }));
    registry.add(make_composition_descriptor("ImportantPhrasesStackReverse", [](const CompositionProps&) { return important_phrases_stack_reverse(); }));
    registry.add(make_composition_descriptor("SubtitleYellowFade", [](const CompositionProps&) { return subtitle_yellow_fade(); }));
    registry.add(make_composition_descriptor("ImportantWordsRedLower", [](const CompositionProps&) { return important_words_red_lower(); }));
}

} // namespace chronon3d::content::important_words
