#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/sequence_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/text/text_definition.hpp>

#include <array>
#include <string>
#include <string_view>

#include "content/common/animation_helpers.hpp"
#include "content/showcases/minimalist/minimalist_theme.hpp"
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
enum class WordMotion { FadeSlide, ScalePop, BlurFocus, SlideLeft, SlideRight };

static void build_important_word(SceneBuilder& s,
                                const WordPreset& word,
                                const WordPalette& palette,
                                Frame fade_in, Frame hold_until, Frame fade_out,
                                std::string_view layer_suffix = {},
                                WordMotion motion = WordMotion::FadeSlide) {
    const std::string lname = layer_suffix.empty()
        ? std::string("word")
        : std::string("word_") + std::string(layer_suffix);
    s.layer(lname, [word, palette, fade_in, hold_until, fade_out, motion](LayerBuilder& l) {
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
        const f32 centered_x = -word.rect_outer_size.x * 0.5f;
        const f32 x_in = centered_x + (motion == WordMotion::SlideLeft ? -120.0f
                         : motion == WordMotion::SlideRight ? 120.0f : 0.0f);
        l.position_anim()
            .key(Frame{0},           Vec3{x_in, WORD_LOWER_Y + 30.0f, 0.0f}, EasingCurve{Easing::Linear})
            .key(kPreFade,           Vec3{x_in, WORD_LOWER_Y + 30.0f, 0.0f}, EasingCurve{Easing::Linear})
            .key(Frame{fade_in},     Vec3{centered_x, WORD_LOWER_Y,        0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{fade_out},    Vec3{centered_x, WORD_LOWER_Y + 12.0f, 0.0f}, EasingCurve{Easing::InCubic});
        if (motion == WordMotion::ScalePop) {
            l.scale_anim()
                .key(Frame{0}, Vec3{0.82f, 0.82f, 1.0f}, EasingCurve{Easing::OutBack})
                .key(Frame{fade_in}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutBack});
        } else if (motion == WordMotion::BlurFocus) {
            l.blur_anim()
                .key(Frame{0}, 14.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{fade_in}, 0.0f, EasingCurve{Easing::OutCubic});
        }
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
        .size = word.rect_outer_size,
        .anchor = TextAnchor::Center,
        .align = TextAlign::Center,
        .vertical_align = VerticalAlign::Middle,
        .tracking = word.tracking
    }
};
            l.text("name", def);
        }
    });
}

static Composition make_lower_word_animation(const char* name,
                                             const WordPreset& word,
                                             const WordPalette& palette,
                                             WordMotion motion) {
    return composition({.name = name, .width = 1920, .height = 1080, .duration = 90},
        [word, palette, motion](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_black_background(s);
            build_important_word(s, word, palette, Frame{10}, Frame{70}, Frame{84}, {}, motion);
            return s.build();
        });
}

struct PhraseLine {
    std::string_view id;
    std::string_view text;
    f32 y_offset;
};

inline constexpr std::array<PhraseLine, 5> kImportantPhrases{{
    {"phrase_01", "KEEP MOVING",           -240.0f},
    {"phrase_02", "FOCUS ON WHAT MATTERS", -120.0f},
    {"phrase_03", "MAKE IT SIMPLE",           0.0f},
    {"phrase_04", "SHOW YOUR STORY",        120.0f},
    {"phrase_05", "CREATE WITH PURPOSE",    240.0f},
}};

using PhraseStartFrames = std::array<Frame, kImportantPhrases.size()>;

struct PhraseStackPreset {
    std::string_view name;
    Frame duration;
    PhraseStartFrames starts;
};

inline constexpr std::array<PhraseStackPreset, 4> kPhraseStackPresets{{
    {"ImportantPhrasesStack", Frame{90},
     {Frame{4}, Frame{8}, Frame{12}, Frame{16}, Frame{20}}},
    {"ImportantPhrasesStackFast", Frame{60},
     {Frame{0}, Frame{3}, Frame{6}, Frame{9}, Frame{12}}},
    {"ImportantPhrasesStackSlow", Frame{120},
     {Frame{8}, Frame{28}, Frame{48}, Frame{68}, Frame{88}}},
    {"ImportantPhrasesStackReverse", Frame{90},
     {Frame{20}, Frame{16}, Frame{12}, Frame{8}, Frame{4}}},
}};

static void build_important_phrase(SequenceBuilder& sequence,
                                   const PhraseLine& phrase) {
    const std::string layer_name{phrase.id};
    const std::string node_name = layer_name + "_text";
    sequence.layer(layer_name, [phrase, node_name](LayerBuilder& l) {
        l.pin_to(Anchor::Center);
        l.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::Linear})
            .key(Frame{8}, 1.0f, EasingCurve{Easing::OutCubic});
        l.position(Vec3{-960.0f, phrase.y_offset - 400.0f, 0.0f});

        l.text(node_name, TextDefinition{
            .content = {.value = std::string(phrase.text)},
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
                .anchor = TextAnchor::Center,
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

static Composition make_phrase_stack(PhraseStackPreset preset) {
    return composition({.name=std::string(preset.name), .width=1920, .height=1080, .duration=preset.duration}, [preset](const FrameContext& ctx) {
        SceneBuilder scene(ctx);
        add_black_background(scene);
        for (std::size_t i = 0; i < kImportantPhrases.size(); ++i) {
            const Frame from = preset.starts[i];
            scene.sequence(
                std::string(kImportantPhrases[i].id),
                {.from = from, .duration = preset.duration - from},
                [phrase = kImportantPhrases[i]](SequenceBuilder& sequence) {
                    build_important_phrase(sequence, phrase);
                });
        }
        return scene.build();
    });
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

Composition important_word_focus() {
    return make_lower_word_animation("ImportantWordFocus", {"FOCUS", {500.0f, 140.0f}, 72.0f, 8.0f, 32.0f, 28.0f, 10.0f}, PALETTE_WARM, WordMotion::FadeSlide);
}

Composition important_word_action() {
    return make_lower_word_animation("ImportantWordAction", {"ACTION", {560.0f, 140.0f}, 72.0f, 8.0f, 32.0f, 28.0f, 10.0f}, PALETTE_LIGHT, WordMotion::ScalePop);
}

Composition important_word_create() {
    return make_lower_word_animation("ImportantWordCreate", {"CREATE", {560.0f, 140.0f}, 72.0f, 8.0f, 32.0f, 28.0f, 10.0f}, PALETTE_COOL, WordMotion::BlurFocus);
}

Composition important_word_story() {
    return make_lower_word_animation("ImportantWordStory", {"STORY", {500.0f, 140.0f}, 72.0f, 8.0f, 32.0f, 28.0f, 10.0f}, PALETTE_LIGHT, WordMotion::SlideLeft);
}

Composition important_word_impact() {
    return make_lower_word_animation("ImportantWordImpact", {"IMPACT", {560.0f, 140.0f}, 72.0f, 8.0f, 32.0f, 28.0f, 10.0f}, PALETTE_COOL, WordMotion::SlideRight);
}

// ── Image + words showcase ────────────────────────────────────────────────
// A compact end-to-end example using an asset already shipped by the repo.
// The image does the visual anchoring; five short phrases, a name line and a
// subtitle provide the readable story layer.  Motion is deliberately limited
// to opacity, position and scale so the composition stays cheap for the SDK.
Composition important_story_image() {
    return composition({.name="ImportantStoryImage", .width=1920, .height=1080, .duration=150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        chronon3d::content::minimalist::add_minimalist_background(s);

        s.layer("story_image", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.position_anim()
                .key(Frame{0}, Vec3{-500.0f, 36.0f, 0.0f}, EasingCurve{Easing::OutCubic})
                .key(Frame{42}, Vec3{-500.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic})
                .key(Frame{120}, Vec3{-500.0f, 0.0f, 0.0f}, EasingCurve{Easing::Linear})
                .key(Frame{150}, Vec3{-530.0f, 0.0f, 0.0f}, EasingCurve{Easing::InCubic});
            l.scale_anim()
                .key(Frame{0}, Vec3{1.04f, 1.04f, 1.0f}, EasingCurve{Easing::OutCubic})
                .key(Frame{42}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic});
            l.opacity_anim()
                .key(Frame{0}, 0.0f, EasingCurve{Easing::Linear})
                .key(Frame{24}, 1.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{120}, 1.0f, EasingCurve{Easing::Linear})
                .key(Frame{150}, 0.0f, EasingCurve{Easing::InCubic});
            chronon3d::content::minimalist::add_image_border(l);
            l.image("landscape", {
                .asset_path = chronon3d::content::minimalist::IMAGE_PATH,
                .size = chronon3d::content::minimalist::IMAGE_SIZE,
                .radius = chronon3d::content::minimalist::IMAGE_RADIUS
            });
        });

        const std::array<std::pair<const char*, Color>, 5> lines = {{
            {"KEEP MOVING",       {1.00f, 0.45f, 0.30f, 1.0f}},
            {"FOCUS ON WHAT MATTERS", {1.00f, 0.76f, 0.22f, 1.0f}},
            {"MAKE IT SIMPLE",     {0.45f, 0.86f, 1.00f, 1.0f}},
            {"SHOW YOUR STORY",    {0.75f, 0.55f, 1.00f, 1.0f}},
            {"CREATE WITH PURPOSE",{1.00f, 0.38f, 0.62f, 1.0f}},
        }};
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const auto [text, color] = lines[i];
            const Frame start{static_cast<Frame>(30 + i * 9)};
            const f32 y = -190.0f + static_cast<f32>(i) * 82.0f;
            s.layer("story_phrase_" + std::to_string(i), [text, color, start, y](LayerBuilder& l) {
                l.pin_to(Anchor::Center);
                l.position(Vec3{100.0f, -y, 0.0f});
                l.opacity_anim()
                    .key(Frame{0}, 0.0f, EasingCurve{Easing::Hold})
                    .key(start, 0.0f, EasingCurve{Easing::Hold})
                    .key(start + Frame{8}, 1.0f, EasingCurve{Easing::OutCubic})
                    .key(Frame{132}, 1.0f, EasingCurve{Easing::Linear})
                    .key(Frame{150}, 0.0f, EasingCurve{Easing::InCubic});
                l.text("phrase", TextDefinition{
                    .content = {.value = text},
                    .style = {.font = {.font_path = "assets/fonts/Poppins-Bold.ttf",
                                       .font_family = "Poppins", .font_weight = 700,
                                       .font_size = 42.0f},
                              .color = color},
                    .frame = {.size = {760.0f, 72.0f}, .align = TextAlign::Left,
                              .vertical_align = VerticalAlign::Middle, .tracking = 1.5f}
                });
            });
        }

        s.layer("story_heading", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center).position({100.0f, 310.0f, 0.0f});
            l.opacity_anim().key(Frame{0}, 0.0f).key(Frame{45}, 1.0f, EasingCurve{Easing::OutCubic});
            l.text("heading", TextDefinition{
                .content = {.value = "MAKE IT MATTER"},
                .style = {.font = {.font_path = "assets/fonts/Poppins-Bold.ttf",
                                   .font_family = "Poppins", .font_weight = 700,
                                   .font_size = 68.0f}, .color = {1.0f, 1.0f, 1.0f, 1.0f}},
                .frame = {.size = {760.0f, 90.0f}, .align = TextAlign::Left,
                          .vertical_align = VerticalAlign::Middle, .tracking = 2.0f}
            });
        });

        s.layer("story_subtitle", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center).position({100.0f, -300.0f, 0.0f});
            l.opacity_anim().key(Frame{45}, 0.0f).key(Frame{65}, 1.0f, EasingCurve{Easing::OutCubic});
            l.text("subtitle", TextDefinition{
                .content = {.value = "DIRECTOR  ·  ACTOR  ·  WRITER"},
                .style = {.font = {.font_path = "assets/fonts/Poppins-Bold.ttf",
                                   .font_family = "Poppins", .font_weight = 600,
                                   .font_size = 28.0f}, .color = {1.0f, 0.82f, 0.24f, 1.0f}},
                .frame = {.size = {760.0f, 60.0f}, .align = TextAlign::Left,
                          .vertical_align = VerticalAlign::Middle, .tracking = 2.0f}
            });
        });
        return s.build();
    });
}

// ── Per-domain registration ──────────────────────────────────────────────────
void register_important_word_compositions(CompositionRegistry& registry) {
    const auto add_word = [&registry](const char* id, std::function<Composition(const CompositionProps&)> factory) {
        registry.add(make_composition_descriptor(
            CompositionDescriptor{.id = id, .category = std::string{content_category::ImportantWord}},
            std::move(factory)));
    };
    const auto add_phrase = [&registry](std::string id, std::function<Composition(const CompositionProps&)> factory) {
        registry.add(make_composition_descriptor(
            CompositionDescriptor{.id = std::move(id), .category = std::string{content_category::Phrase}},
            std::move(factory)));
    };
    add_word("ImportantWordDirectorLight", [](const CompositionProps&) { return important_word_director_light(); });
    add_word("ImportantWordActorWarm", [](const CompositionProps&) { return important_word_actor_warm(); });
    add_word("ImportantWordWriterCool", [](const CompositionProps&) { return important_word_writer_cool(); });
    add_word("ImportantWordTrio", [](const CompositionProps&) { return important_word_trio(); });
    for (const PhraseStackPreset preset : kPhraseStackPresets) {
        add_phrase(std::string(preset.name),
                   [preset](const CompositionProps&) { return make_phrase_stack(preset); });
    }
    add_word("SubtitleYellowFade", [](const CompositionProps&) { return subtitle_yellow_fade(); });
    add_word("ImportantWordsRedLower", [](const CompositionProps&) { return important_words_red_lower(); });
    add_word("ImportantStoryImage", [](const CompositionProps&) { return important_story_image(); });
    add_word("ImportantWordFocus", [](const CompositionProps&) { return important_word_focus(); });
    add_word("ImportantWordAction", [](const CompositionProps&) { return important_word_action(); });
    add_word("ImportantWordCreate", [](const CompositionProps&) { return important_word_create(); });
    add_word("ImportantWordStory", [](const CompositionProps&) { return important_word_story(); });
    add_word("ImportantWordImpact", [](const CompositionProps&) { return important_word_impact(); });
}

} // namespace chronon3d::content::important_words
