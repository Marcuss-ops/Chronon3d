// ==============================================================================
// content/certification/cert_timeline.cpp
//
// TICKET-TIMELINE-CERT — Timeline & Sequence functional certification.
// ==============================================================================

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_props.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/sequence_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/core/types/frame_context.hpp>

#include "content/certification/certification_descriptor.hpp"

#include <algorithm>

namespace chronon3d::content::certification {

using namespace chronon3d;

static constexpr int kTLW = 1920;
static constexpr int kTLH = 1080;

Composition cert_timeline_boundary() {
    return composition(
        {.name = "CertTimelineBoundary", .width = kTLW, .height = kTLH,
         .frame_rate = FrameRate{30, 1}, .duration = Frame{100}},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.layer("empty_bg", [](LayerBuilder& l) {
                l.fullscreen_rect("bg", Color{0.02f, 0.02f, 0.04f, 1.0f});
            });
            s.sequence("cert_seq",
                {.from = Frame{50}, .duration = Frame{30}},
                [](SequenceBuilder& seq) {
                    seq.rect("seq_bg", RectParams{
                        .size = {static_cast<float>(kTLW), static_cast<float>(kTLH)},
                        .color = {0.0f, 0.8f, 0.2f, 1.0f},
                        .pos = {0.0f, 0.0f, 0.0f},
                        .fill = FillStyle::solid(Color{0.0f, 0.8f, 0.2f, 1.0f}),
                        .stroke = {},
                    });
                    const int local = static_cast<int>(seq.local_frame().value);
                    const float t = static_cast<float>(local) / 30.0f;
                    seq.rect("indicator", RectParams{
                        .size = {200.0f, 60.0f},
                        .color = {t, 1.0f - t, 0.0f, 1.0f},
                        .pos = {static_cast<float>(kTLW) / 2.0f - 100.0f,
                                static_cast<float>(kTLH) / 2.0f - 30.0f,
                                0.0f},
                        .fill = FillStyle::solid(Color{t, 1.0f - t, 0.0f, 1.0f}),
                        .stroke = {},
                    });
                });
            return s.build();
        });
}

Composition cert_timeline_local_frame() {
    return composition(
        {.name = "CertTimelineLocalFrame", .width = kTLW, .height = kTLH,
         .frame_rate = FrameRate{30, 1}, .duration = Frame{100}},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fullscreen_rect("bg", Color{0.02f, 0.02f, 0.04f, 1.0f});
            });
            s.sequence("local_frame_seq",
                {.from = Frame{30}, .duration = Frame{60}},
                [](SequenceBuilder& seq) {
                    const float t = static_cast<float>(seq.local_frame().value) / 60.0f;
                    const float value = std::clamp(t, 0.0f, 1.0f);
                    seq.rect("enc", RectParams{
                        .size = {400.0f, 300.0f},
                        .color = {value, value, value, 1.0f},
                        .pos = {static_cast<float>(kTLW) / 2.0f - 200.0f,
                                static_cast<float>(kTLH) / 2.0f - 150.0f,
                                0.0f},
                        .fill = FillStyle::solid(Color{value, value, value, 1.0f}),
                        .stroke = {},
                    });
                });
            return s.build();
        });
}

Composition cert_timeline_nested() {
    return composition(
        {.name = "CertTimelineNested", .width = kTLW, .height = kTLH,
         .frame_rate = FrameRate{30, 1}, .duration = Frame{200}},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fullscreen_rect("bg", Color{0.01f, 0.01f, 0.03f, 1.0f});
            });
            s.sequence("chapter",
                {.from = Frame{100}, .duration = Frame{100}},
                [](SequenceBuilder& chapter) {
                    chapter.sequence("title",
                        {.from = Frame{20}, .duration = Frame{30}},
                        [](SequenceBuilder& title) {
                            title.rect("nested_bg", RectParams{
                                .size = {static_cast<float>(kTLW),
                                         static_cast<float>(kTLH)},
                                .color = {0.9f, 0.75f, 0.1f, 1.0f},
                                .pos = {0.0f, 0.0f, 0.0f},
                                .fill = FillStyle::solid(
                                    Color{0.9f, 0.75f, 0.1f, 1.0f}),
                                .stroke = {},
                            });
                        });
                });
            return s.build();
        });
}

Composition cert_timeline_overlap() {
    return composition(
        {.name = "CertTimelineOverlap", .width = kTLW, .height = kTLH,
         .frame_rate = FrameRate{30, 1}, .duration = Frame{100}},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fullscreen_rect("bg", Color{0.01f, 0.01f, 0.03f, 1.0f});
            });
            s.sequence("red_seq",
                {.from = Frame{0}, .duration = Frame{50}},
                [](SequenceBuilder& seq) {
                    seq.rect("red", RectParams{
                        .size = {600.0f, 350.0f},
                        .color = {0.9f, 0.1f, 0.1f, 1.0f},
                        .pos = {100.0f,
                                static_cast<float>(kTLH) / 2.0f - 175.0f,
                                0.0f},
                        .fill = FillStyle::solid(Color{0.9f, 0.1f, 0.1f, 1.0f}),
                        .stroke = {},
                    });
                });
            s.sequence("green_seq",
                {.from = Frame{25}, .duration = Frame{50}},
                [](SequenceBuilder& seq) {
                    seq.rect("green", RectParams{
                        .size = {600.0f, 350.0f},
                        .color = {0.1f, 0.9f, 0.1f, 1.0f},
                        .pos = {static_cast<float>(kTLW) / 2.0f - 300.0f,
                                static_cast<float>(kTLH) / 2.0f - 175.0f,
                                0.0f},
                        .fill = FillStyle::solid(Color{0.1f, 0.9f, 0.1f, 1.0f}),
                        .stroke = {},
                    });
                });
            s.sequence("blue_seq",
                {.from = Frame{50}, .duration = Frame{50}},
                [](SequenceBuilder& seq) {
                    seq.rect("blue", RectParams{
                        .size = {600.0f, 350.0f},
                        .color = {0.1f, 0.1f, 0.9f, 1.0f},
                        .pos = {static_cast<float>(kTLW) - 700.0f,
                                static_cast<float>(kTLH) / 2.0f - 175.0f,
                                0.0f},
                        .fill = FillStyle::solid(Color{0.1f, 0.1f, 0.9f, 1.0f}),
                        .stroke = {},
                    });
                });
            return s.build();
        });
}

Composition cert_timeline_animation() {
    return composition(
        {.name = "CertTimelineAnimation", .width = kTLW, .height = kTLH,
         .frame_rate = FrameRate{30, 1}, .duration = Frame{100}},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fullscreen_rect("bg", Color{0.02f, 0.02f, 0.04f, 1.0f});
            });
            s.sequence("fade_seq",
                {.from = Frame{40}, .duration = Frame{50}},
                [](SequenceBuilder& seq) {
                    const float local = static_cast<float>(seq.local_frame().value);
                    const float opacity = std::clamp(local / 25.0f, 0.0f, 1.0f);
                    seq.rect("fade", RectParams{
                        .size = {400.0f, 200.0f},
                        .color = {1.0f, 1.0f, 1.0f, opacity},
                        .pos = {static_cast<float>(kTLW) / 2.0f - 200.0f,
                                static_cast<float>(kTLH) / 2.0f - 100.0f,
                                0.0f},
                        .fill = FillStyle::solid(Color{1.0f, 1.0f, 1.0f, opacity}),
                        .stroke = {},
                    });
                });
            return s.build();
        });
}

void register_cert_timeline_compositions(CompositionRegistry& registry) {
    registry.add(certification_descriptor(
        "CertTimelineBoundary", kTLW, kTLH, Frame{100},
        [](const CompositionProps&) { return cert_timeline_boundary(); }));
    registry.add(certification_descriptor(
        "CertTimelineLocalFrame", kTLW, kTLH, Frame{100},
        [](const CompositionProps&) { return cert_timeline_local_frame(); }));
    registry.add(certification_descriptor(
        "CertTimelineNested", kTLW, kTLH, Frame{200},
        [](const CompositionProps&) { return cert_timeline_nested(); }));
    registry.add(certification_descriptor(
        "CertTimelineOverlap", kTLW, kTLH, Frame{100},
        [](const CompositionProps&) { return cert_timeline_overlap(); }));
    registry.add(certification_descriptor(
        "CertTimelineAnimation", kTLW, kTLH, Frame{100},
        [](const CompositionProps&) { return cert_timeline_animation(); }));
}

} // namespace chronon3d::content::certification
