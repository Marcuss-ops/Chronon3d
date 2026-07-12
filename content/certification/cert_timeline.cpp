// ==============================================================================
// content/certification/cert_timeline.cpp
//
// TICKET-TIMELINE-CERT — Timeline & Sequence functional certification
// compositions (P1).
//
// 5 minimal-surface compositions for the canonical verify_timeline_functional_linux.sh
// gate. Each uses SequenceBuilder exclusively (zero legacy path):
//
//   CertTimelineBoundary  — sequence from=50, dur=30. Boundary check:
//                            f49=empty, f50=local0, f79=local29, f80=empty.
//   CertTimelineLocalFrame — sequence from=30, dur=60 encodes local_frame
//                             as grayscale color (R=G=B=local/60).
//   CertTimelineNested     — chapter seq (0-119) → title seq (20-50).
//                             Inner visible at global f120-f149.
//   CertTimelineOverlap    — 3 overlapping sequences with distinct colors
//                             (red 0-49, green 25-74, blue 50-99).
//   CertTimelineAnimation  — fade-in driven by local_frame within a sequence.
//
// 1920×1080 canvas. Determinism guaranteed by fixed duration + FrameRate{30,1}.
// ==============================================================================

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_props.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/sequence_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::content::certification {

using namespace chronon3d;

static constexpr int   kWidth   = 1920;
static constexpr int   kHeight  = 1080;

// ── CertTimelineBoundary ──────────────────────────────────────────────────

Composition cert_timeline_boundary() {
    return composition(
        {.name = "CertTimelineBoundary",
         .width = kWidth,
         .height = kHeight,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{100}},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            // Background for frames with no active sequence (empty = dark)
            s.layer("empty_bg", [](LayerBuilder& l) {
                l.fullscreen_rect("bg", Color{0.02f, 0.02f, 0.04f, 1.0f});
            });
            // Sequence: active from frame 50 to frame 79 (duration=30).
            // At global frame 50 → local 0, at global frame 79 → local 29.
            // At global frame 49 → not active (dark background only).
            // At global frame 80 → not active (dark background only).
            s.sequence("cert_seq",
                {.from = Frame{50}, .duration = Frame{30}},
                [](SequenceBuilder& seq) {
                    // Solid green rect — visible only when sequence is active
                    seq.rect("seq_bg", RectParams{
                        .size = {static_cast<float>(kWidth), static_cast<float>(kHeight)},
                        .color = {0.0f, 0.8f, 0.2f, 1.0f},
                        .pos = {0.0f, 0.0f, 0.0f},
                        .fill = FillStyle::solid(Color{0.0f, 0.8f, 0.2f, 1.0f}),
                        .stroke = {},
                    });
                    // Local frame indicator: text showing the local frame number
                    int local_val = static_cast<int>(seq.local_frame().value);
                    seq.rect("indicator", RectParams{
                        .size = {200.0f, 60.0f},
                        .color = {
                            static_cast<float>(local_val) / 30.0f,
                            1.0f - static_cast<float>(local_val) / 30.0f,
                            0.0f, 1.0f},
                        .pos = {static_cast<float>(kWidth) / 2.0f - 100.0f,
                                static_cast<float>(kHeight) / 2.0f - 30.0f,
                                0.0f},
                        .fill = FillStyle::solid(Color{
                            static_cast<float>(local_val) / 30.0f,
                            1.0f - static_cast<float>(local_val) / 30.0f,
                            0.0f, 1.0f}),
                        .stroke = {},
                    });
                });

            return s.build();
        });
}

// ── CertTimelineLocalFrame ────────────────────────────────────────────────

Composition cert_timeline_local_frame() {
    return composition(
        {.name = "CertTimelineLocalFrame",
         .width = kWidth,
         .height = kHeight,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{100}},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fullscreen_rect("bg", Color{0.02f, 0.02f, 0.04f, 1.0f});
            });
            // Sequence from 30 to 89 (duration=60).
            // Encodes local_frame as grayscale: R=G=B=local/60.
            s.sequence("local_frame_seq",
                {.from = Frame{30}, .duration = Frame{60}},
                [](SequenceBuilder& seq) {
                    float t = static_cast<float>(seq.local_frame().value) / 60.0f;
                    float clamped = std::clamp(t, 0.0f, 1.0f);
                    seq.rect("enc", RectParams{
                        .size = {400.0f, 300.0f},
                        .color = {clamped, clamped, clamped, 1.0f},
                        .pos = {static_cast<float>(kWidth) / 2.0f - 200.0f,
                                static_cast<float>(kHeight) / 2.0f - 150.0f,
                                0.0f},
                        .fill = FillStyle::solid(Color{clamped, clamped, clamped, 1.0f}),
                        .stroke = {},
                    });
                });

            return s.build();
        });
}

// ── CertTimelineNested ────────────────────────────────────────────────────

Composition cert_timeline_nested() {
    return composition(
        {.name = "CertTimelineNested",
         .width = kWidth,
         .height = kHeight,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{200}},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fullscreen_rect("bg", Color{0.01f, 0.01f, 0.03f, 1.0f});
            });
            // Outer sequence: "chapter" from global 100, duration 100
            s.sequence("chapter",
                {.from = Frame{100}, .duration = Frame{100}},
                [](SequenceBuilder& chapter) {
                    // Inner sequence: "title" from chapter-local 20, duration 30
                    // At global 120 → chapter.local=20, title.local=0
                    // At global 149 → chapter.local=49, title.local=29
                    // At global 150 → title not active (duration expired)
                    chapter.sequence("title",
                        {.from = Frame{20}, .duration = Frame{30}},
                        [](SequenceBuilder& title) {
                            title.rect("nested_bg", RectParams{
                                .size = {static_cast<float>(kWidth), static_cast<float>(kHeight)},
                                .color = {0.9f, 0.75f, 0.1f, 1.0f},
                                .pos = {0.0f, 0.0f, 0.0f},
                                .fill = FillStyle::solid(Color{0.9f, 0.75f, 0.1f, 1.0f}),
                                .stroke = {},
                            });
                        });
                });

            return s.build();
        });
}

// ── CertTimelineOverlap ───────────────────────────────────────────────────

Composition cert_timeline_overlap() {
    return composition(
        {.name = "CertTimelineOverlap",
         .width = kWidth,
         .height = kHeight,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{100}},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fullscreen_rect("bg", Color{0.01f, 0.01f, 0.03f, 1.0f});
            });
            // Red sequence: global 0–49
            s.sequence("red_seq",
                {.from = Frame{0}, .duration = Frame{50}},
                [](SequenceBuilder& seq) {
                    seq.rect("red", RectParams{
                        .size = {600.0f, 350.0f},
                        .color = {0.9f, 0.1f, 0.1f, 1.0f},
                        .pos = {100.0f, static_cast<float>(kHeight) / 2.0f - 175.0f, 0.0f},
                        .fill = FillStyle::solid(Color{0.9f, 0.1f, 0.1f, 1.0f}),
                        .stroke = {},
                    });
                });
            // Green sequence: global 25–74
            s.sequence("green_seq",
                {.from = Frame{25}, .duration = Frame{50}},
                [](SequenceBuilder& seq) {
                    seq.rect("green", RectParams{
                        .size = {600.0f, 350.0f},
                        .color = {0.1f, 0.9f, 0.1f, 1.0f},
                        .pos = {static_cast<float>(kWidth) / 2.0f - 300.0f,
                                static_cast<float>(kHeight) / 2.0f - 175.0f, 0.0f},
                        .fill = FillStyle::solid(Color{0.1f, 0.9f, 0.1f, 1.0f}),
                        .stroke = {},
                    });
                });
            // Blue sequence: global 50–99
            s.sequence("blue_seq",
                {.from = Frame{50}, .duration = Frame{50}},
                [](SequenceBuilder& seq) {
                    seq.rect("blue", RectParams{
                        .size = {600.0f, 350.0f},
                        .color = {0.1f, 0.1f, 0.9f, 1.0f},
                        .pos = {static_cast<float>(kWidth) - 700.0f,
                                static_cast<float>(kHeight) / 2.0f - 175.0f, 0.0f},
                        .fill = FillStyle::solid(Color{0.1f, 0.1f, 0.9f, 1.0f}),
                        .stroke = {},
                    });
                });

            return s.build();
        });
}

// ── CertTimelineAnimation ─────────────────────────────────────────────────

Composition cert_timeline_animation() {
    return composition(
        {.name = "CertTimelineAnimation",
         .width = kWidth,
         .height = kHeight,
         .frame_rate = FrameRate{30, 1},
         .duration = Frame{100}},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fullscreen_rect("bg", Color{0.02f, 0.02f, 0.04f, 1.0f});
            });
            // Sequence from global 40, duration 50. Fade-in over 25 local frames.
            s.sequence("fade_seq",
                {.from = Frame{40}, .duration = Frame{50}},
                [](SequenceBuilder& seq) {
                    float local = static_cast<float>(seq.local_frame().value);
                    float opacity = std::clamp(local / 25.0f, 0.0f, 1.0f);
                    seq.rect("fade", RectParams{
                        .size = {400.0f, 200.0f},
                        .color = {1.0f, 1.0f, 1.0f, opacity},
                        .pos = {static_cast<float>(kWidth) / 2.0f - 200.0f,
                                static_cast<float>(kHeight) / 2.0f - 100.0f,
                                0.0f},
                        .fill = FillStyle::solid(Color{1.0f, 1.0f, 1.0f, opacity}),
                        .stroke = {},
                    });
                });

            return s.build();
        });
}

// ── Registration ──────────────────────────────────────────────────────────

void register_cert_timeline_compositions(CompositionRegistry& registry) {
    registry.add("CertTimelineBoundary", [](const CompositionProps&) {
        return cert_timeline_boundary();
    });
    registry.add("CertTimelineLocalFrame", [](const CompositionProps&) {
        return cert_timeline_local_frame();
    });
    registry.add("CertTimelineNested", [](const CompositionProps&) {
        return cert_timeline_nested();
    });
    registry.add("CertTimelineOverlap", [](const CompositionProps&) {
        return cert_timeline_overlap();
    });
    registry.add("CertTimelineAnimation", [](const CompositionProps&) {
        return cert_timeline_animation();
    });
}

} // namespace chronon3d::content::certification
