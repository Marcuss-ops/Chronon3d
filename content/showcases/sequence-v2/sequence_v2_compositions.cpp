// content/showcases/sequence-v2/sequence_v2_compositions.cpp
//
// Sequence V2 — Migration Step 3 showcase compositions.
//
// 5 new compositions that use ONLY the SequenceBuilder API (no legacy
// SceneBuilder-only patterns).  Demonstrates:
//   1. SeqV2IntroOutro         — basic sequential sequences (local_frame restart)
//   2. SeqV2DeepNesting        — 3-level nested sequences
//   3. SeqV2StaggeredTimeline  — overlapping sequences with animation
//   4. SeqV2TrimOffset         — trim_before for delayed animation start
//   5. SeqV2MixedMedia         — text + image in separate sequences (manifest)
//
// All compositions use s.sequence(name, spec, [](SequenceBuilder& seq) {...})
// exclusively — zero legacy path.  AssetManifest is collected automatically
// by LayerBuilder (text_run → font ref, image → image ref).
//
// Render:
//   chronon3d_cli video SeqV2IntroOutro -o output/seq_v2_intro_outro.mp4
//   chronon3d_cli still SeqV2IntroOutro --frame 10 -o output/seq_v2_f10.png

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/sequence_builder.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/motion/timeline.hpp>
#include <chronon3d/assets/asset_manifest.hpp>

#include "content/common/background_helpers.hpp"
#include <chronon3d/text/text_definition.hpp>  // F2.D — canonical DTO

namespace chronon3d::content::sequence_v2 {

using namespace chronon3d::content::backgrounds;

namespace {

// ── Shared asset declarations ───────────────────────────────────────────────
// Explicit AssetRef declarations for all assets used by these compositions.
// This makes asset dependencies visible and typed — the canonical pattern
// for Sequence V2 content (vs raw string paths).
const chronon3d::assets::InternalAssetRef kTitleFont{
    assets::AssetKind::Font,
    "assets/fonts/Poppins-Bold.ttf",
    "seq-v2/title-font",
    true
};
const chronon3d::assets::InternalAssetRef kPlaceholderImage{
    assets::AssetKind::Image,
    "assets/images/placeholder.png",
    "seq-v2/placeholder",
    false  // soft dep — composition renders without it
};

constexpr Color kTextColor{0.94f, 0.95f, 0.98f, 1.0f};

/// F2.D — returns canonical TextDefinition instead of TextSpec.
TextDefinition title_text(const char* text, f32 size = 80.0f) {
    return TextDefinition{
        .content = {.value = text},
        .style = {.font = {.font_path = kTitleFont.path, .font_size = size},
                  .color = kTextColor},
        .frame = {.size = {1400.0f, 200.0f}, .align = TextAlign::Center,
                  .vertical_align = VerticalAlign::Middle,
                  .line_height = 1.0f, .tracking = 4.0f},
    };
}

/// F2.D — returns canonical TextDefinition instead of TextSpec.
TextDefinition body_text(const char* text, f32 size = 48.0f) {
    return TextDefinition{
        .content = {.value = text},
        .style = {.font = {.font_path = kTitleFont.path, .font_size = size},
                  .color = Color{0.75f, 0.78f, 0.85f, 1.0f}},
        .frame = {.size = {1200.0f, 400.0f}, .align = TextAlign::Center,
                  .vertical_align = VerticalAlign::Middle,
                  .line_height = 1.1f, .tracking = 2.0f},
    };
}

void add_seq_bg(SceneBuilder& s) {
    add_common_background(s, BackgroundStyles::Minimalist());
}

// ── Fade-in animation applied inside a sequence ─────────────────────────────
void apply_fade_in(LayerBuilder& l, Frame done_at = Frame{12}) {
    motion::timeline(0.0f)
        .to(done_at, 1.0f, Easing::OutCubic)
        .apply_to(l.opacity_anim());
}

// ── Slide-up + fade-in ──────────────────────────────────────────────────────
void apply_slide_fade_in(LayerBuilder& l, f32 y_offset = 40.0f,
                         Frame done_at = Frame{12}) {
    l.position_anim()
        .key(Frame{0}, Vec3{0.0f, y_offset, 0.0f}, EasingCurve{Easing::OutCubic})
        .key(done_at, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
    apply_fade_in(l, done_at);
}

// ── Fade-out animation ──────────────────────────────────────────────────────
void apply_fade_out(LayerBuilder& l, Frame start, Frame end) {
    l.opacity_anim()
        .key(Frame{0}, 1.0f, EasingCurve{Easing::Hold})
        .key(start, 1.0f, EasingCurve{Easing::Linear})
        .key(end, 0.0f, EasingCurve{Easing::InCubic});
}

} // anonymous namespace

// ═════════════════════════════════════════════════════════════════════════════
// 1. SeqV2IntroOutro — basic sequential sequences
// ═════════════════════════════════════════════════════════════════════════════
//
// Two non-overlapping sequences:
//   intro (0–44): title fades in, holds
//   outro (45–89): outro text fades in, title fades out
//
// Demonstrates: local_frame restarts at 0 for each sequence.
Composition seq_v2_intro_outro() {
    return composition({.name = "SeqV2IntroOutro", .width = 1920, .height = 1080,
                        .duration = 90},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);

            s.sequence("intro", {.from = Frame{0}, .duration = Frame{45}},
                [](SequenceBuilder& seq) {
                    seq.layer("_bg", [](LayerBuilder& l) {
                        l.fullscreen_rect("bg", Color{0.02f, 0.02f, 0.06f, 1.0f});
                    });
                    seq.layer("title", [](LayerBuilder& l) {
                        l.pin_to(Anchor::Center);
                        apply_slide_fade_in(l, 30.0f, Frame{15});
                        l.text("label", title_text("SEQUENCE V2"));
                    });
                });

            s.sequence("outro", {.from = Frame{45}, .duration = Frame{45}},
                [](SequenceBuilder& seq) {
                    seq.layer("_bg_outro", [](LayerBuilder& l) {
                        l.fullscreen_rect("bg", Color{0.04f, 0.02f, 0.08f, 1.0f});
                    });
                    seq.layer("outro_text", [](LayerBuilder& l) {
                        l.pin_to(Anchor::Center);
                        apply_slide_fade_in(l, -20.0f, Frame{15});
                        l.text("label", title_text("LOCAL FRAME = 0"));
                    });
                });

            return s.build();
        });
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. SeqV2DeepNesting — 3-level nested sequences
// ═════════════════════════════════════════════════════════════════════════════
//
//   act (0–119)
//     chapter1 (0–59)
//       title (0–19): "CHAPTER ONE" fades in
//       body (20–59): body text slides in at local frame 0
//     chapter2 (60–119)
//       title (0–19): "CHAPTER TWO" fades in (local frame restarts!)
//       body (20–59): body text slides in
//
// Demonstrates: 3-level nesting, nested local_frame propagation.
Composition seq_v2_deep_nesting() {
    return composition({.name = "SeqV2DeepNesting", .width = 1920, .height = 1080,
                        .duration = 120},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);

            s.sequence("act", {.from = Frame{0}, .duration = Frame{120}},
                [](SequenceBuilder& act) {
                    // Chapter 1
                    act.sequence("ch1", {.from = Frame{0}, .duration = Frame{60}},
                        [](SequenceBuilder& ch) {
                            ch.sequence("title", {.from = Frame{0}, .duration = Frame{20}},
                                [](SequenceBuilder& seq) {
                                    seq.layer("_bg", [](LayerBuilder& l) {
                                        l.fullscreen_rect("bg", Color{0.01f, 0.03f, 0.08f, 1.0f});
                                    });
                                    seq.layer("ch1_title", [](LayerBuilder& l) {
                                        l.pin_to(Anchor::Center);
                                        apply_fade_in(l, Frame{10});
                                        l.text("label", title_text("CHAPTER ONE", 64.0f));
                                    });
                                });

                            ch.sequence("body", {.from = Frame{20}, .duration = Frame{40}},
                                [](SequenceBuilder& seq) {
                                    seq.layer("ch1_body", [](LayerBuilder& l) {
                                        l.pin_to(Anchor::Center);
                                        apply_slide_fade_in(l, 30.0f, Frame{12});
                                        l.text("label",
                                            body_text("Nested sequences\ncarry local frame context."));
                                    });
                                });
                        });

                    // Chapter 2
                    act.sequence("ch2", {.from = Frame{60}, .duration = Frame{60}},
                        [](SequenceBuilder& ch) {
                            ch.sequence("title", {.from = Frame{0}, .duration = Frame{20}},
                                [](SequenceBuilder& seq) {
                                    seq.layer("_bg2", [](LayerBuilder& l) {
                                        l.fullscreen_rect("bg", Color{0.04f, 0.01f, 0.06f, 1.0f});
                                    });
                                    seq.layer("ch2_title", [](LayerBuilder& l) {
                                        l.pin_to(Anchor::Center);
                                        apply_fade_in(l, Frame{10});
                                        l.text("label", title_text("CHAPTER TWO", 64.0f));
                                    });
                                });

                            ch.sequence("body", {.from = Frame{20}, .duration = Frame{40}},
                                [](SequenceBuilder& seq) {
                                    seq.layer("ch2_body", [](LayerBuilder& l) {
                                        l.pin_to(Anchor::Center);
                                        apply_slide_fade_in(l, 30.0f, Frame{12});
                                        l.text("label",
                                            body_text("Local frame restarts\nfor each sequence."));
                                    });
                                });
                        });
                });

            return s.build();
        });
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. SeqV2StaggeredTimeline — overlapping sequences with animation
// ═════════════════════════════════════════════════════════════════════════════
//
// Three overlapping text sequences:
//   "SEQUENCE" (0–49), "V2" (25–74), "SHOWCASE" (50–99)
//
// Each word fades in at its sequence start and fades out at its end.
// Demonstrates: overlapping active windows, per-sequence animation timing.
Composition seq_v2_staggered_timeline() {
    return composition({.name = "SeqV2StaggeredTimeline", .width = 1920,
                        .height = 1080, .duration = 100},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);

            // Top-level background covering the full composition duration
            s.layer("_bg", [](LayerBuilder& l) {
                l.fullscreen_rect("bg", Color{0.02f, 0.02f, 0.06f, 1.0f});
            });

            s.sequence("word1", {.from = Frame{0}, .duration = Frame{50}},
                [](SequenceBuilder& seq) {
                    seq.layer("word1", [](LayerBuilder& l) {
                        l.pin_to(Anchor::Center);
                        l.position({0.0f, -80.0f, 0.0f});
                        apply_fade_in(l, Frame{10});
                        apply_fade_out(l, Frame{38}, Frame{48});
                        l.text("label", title_text("SEQUENCE", 72.0f));
                    });
                });

            s.sequence("word2", {.from = Frame{25}, .duration = Frame{50}},
                [](SequenceBuilder& seq) {
                    seq.layer("word2", [](LayerBuilder& l) {
                        l.pin_to(Anchor::Center);
                        apply_fade_in(l, Frame{10});
                        apply_fade_out(l, Frame{38}, Frame{48});
                        l.text("label", title_text("V2", 96.0f));
                    });
                });

            s.sequence("word3", {.from = Frame{50}, .duration = Frame{50}},
                [](SequenceBuilder& seq) {
                    seq.layer("word3", [](LayerBuilder& l) {
                        l.pin_to(Anchor::Center);
                        l.position({0.0f, 80.0f, 0.0f});
                        apply_fade_in(l, Frame{10});
                        apply_fade_out(l, Frame{38}, Frame{48});
                        l.text("label", title_text("SHOWCASE", 72.0f));
                    });
                });

            return s.build();
        });
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. SeqV2TrimOffset — trim_before for delayed animation start
// ═════════════════════════════════════════════════════════════════════════════
//
// A 90-frame composition with trim_before=20:
//   - At global frame 0: local_frame = 0 - 0 + 20 = 20
//   - The animation keyframes (which start at frame 0) are already 20 frames
//     into their progression, so the text appears mid-animation.
//
// Demonstrates: trim_before remaps the local frame origin.
Composition seq_v2_trim_offset() {
    return composition({.name = "SeqV2TrimOffset", .width = 1920, .height = 1080,
                        .duration = 90},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);

            s.layer("_bg", [](LayerBuilder& l) {
                l.fullscreen_rect("bg", Color{0.03f, 0.01f, 0.05f, 1.0f});
            });

            s.sequence("main", {.from = Frame{0}, .duration = Frame{90}},
                [](SequenceBuilder& seq) {
                    seq.layer("text", [](LayerBuilder& l) {
                        l.pin_to(Anchor::Center);
                        // Animation keyframes: position drifts left → center over 30 frames.
                        // With trim_before=20, at global frame 0 the local frame is already 20,
                        // so the text starts nearly centered (80% through the drift).
                        l.position_anim()
                            .key(Frame{0},  Vec3{-120.0f, 0.0f, 0.0f},
                                 EasingCurve{Easing::OutCubic})
                            .key(Frame{30}, Vec3{0.0f, 0.0f, 0.0f},
                                 EasingCurve{Easing::OutCubic});
                        l.opacity_anim()
                            .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
                            .key(Frame{10}, 1.0f, EasingCurve{Easing::Linear});
                        l.text("label", title_text("TRIM BEFORE"));
                    });
                });

            // Trim-before variant: same animation but starts 20 frames into its content
            s.sequence("trimmed", {.from = Frame{0}, .duration = Frame{90},
                                   .trim_before = Frame{20}},
                [](SequenceBuilder& seq) {
                    seq.layer("trimmed_text", [](LayerBuilder& l) {
                        l.pin_to(Anchor::Center);
                        l.position({0.0f, 120.0f, 0.0f});
                        l.position_anim()
                            .key(Frame{0},  Vec3{-120.0f, 0.0f, 0.0f},
                                 EasingCurve{Easing::OutCubic})
                            .key(Frame{30}, Vec3{0.0f, 0.0f, 0.0f},
                                 EasingCurve{Easing::OutCubic});
                        l.opacity_anim()
                            .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
                            .key(Frame{10}, 1.0f, EasingCurve{Easing::Linear});
                        l.text("label", body_text("← offset by trim_before=20"));
                    });
                });

            return s.build();
        });
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. SeqV2MixedMedia — text + image in separate sequences (manifest demo)
// ═════════════════════════════════════════════════════════════════════════════
//
// Two overlapping sequences:
//   title_seq (0–59): text layer with font asset
//   image_seq (30–89): image layer with image asset
//
// The AssetManifest automatically collects both font and image refs.
// Demonstrates: multi-asset manifest aggregation across sequence boundaries.
Composition seq_v2_mixed_media() {
    return composition({.name = "SeqV2MixedMedia", .width = 1920, .height = 1080,
                        .duration = 90},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);

            s.layer("_bg", [](LayerBuilder& l) {
                l.fullscreen_rect("bg", Color{0.015f, 0.015f, 0.04f, 1.0f});
            });

            s.sequence("title_seq", {.from = Frame{0}, .duration = Frame{60}},
                [](SequenceBuilder& seq) {
                    seq.layer("title", [](LayerBuilder& l) {
                        l.pin_to(Anchor::Center);
                        l.position({0.0f, -100.0f, 0.0f});
                        apply_fade_in(l, Frame{12});
                        apply_fade_out(l, Frame{46}, Frame{58});
                        l.text("label", title_text("MIXED MEDIA", 64.0f));
                    });
                });

            s.sequence("image_seq", {.from = Frame{30}, .duration = Frame{60}},
                [](SequenceBuilder& seq) {
                    seq.layer("image", [](LayerBuilder& l) {
                        l.pin_to(Anchor::Center);
                        l.position({0.0f, 80.0f, 0.0f});
                        apply_fade_in(l, Frame{15});
                        apply_fade_out(l, Frame{45}, Frame{58});
                        l.image("hero", ImageParams{
                            .path = kPlaceholderImage.path,
                            .size = {600.0f, 400.0f},
                        });
                    });
                });

            return s.build();
        });
}

// ── Per-domain registration ──────────────────────────────────────────────────
void register_sequence_v2_compositions(CompositionRegistry& registry) {
    registry.add("SeqV2IntroOutro",
        [](const CompositionProps&) { return seq_v2_intro_outro(); });
    registry.add("SeqV2DeepNesting",
        [](const CompositionProps&) { return seq_v2_deep_nesting(); });
    registry.add("SeqV2StaggeredTimeline",
        [](const CompositionProps&) { return seq_v2_staggered_timeline(); });
    registry.add("SeqV2TrimOffset",
        [](const CompositionProps&) { return seq_v2_trim_offset(); });
    registry.add("SeqV2MixedMedia",
        [](const CompositionProps&) { return seq_v2_mixed_media(); });
}

} // namespace chronon3d::content::sequence_v2
