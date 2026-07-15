// content/showcases/sequence-v2/sequence_v2_compositions.cpp
//
// Sequence V2 showcases using the canonical descriptor and prepared-asset
// pipeline. Render examples:
//   chronon render SeqV2IntroOutro -o output/seq_v2_intro_outro.mp4
//   chronon render SeqV2IntroOutro --frame 10 -o output/seq_v2_f10.png

#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/motion/motion.hpp>
#include <chronon3d/assets/asset_manifest.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/sequence_builder.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_props.hpp>

#include <filesystem>
#include <string>
#include <utility>

namespace chronon3d::content::sequence_v2 {

namespace {

constexpr i32 kWidth = 1920;
constexpr i32 kHeight = 1080;
constexpr FrameRate kFps{30, 1};

const assets::InternalAssetRef kTitleFont{
    assets::AssetKind::Font,
    "assets/fonts/Poppins-Bold.ttf",
    "seq-v2/title-font",
    true
};

const assets::InternalAssetRef kPlaceholderImage{
    assets::AssetKind::Image,
    "assets/images/placeholder.png",
    "seq-v2/placeholder",
    false
};

constexpr Color kTextColor{0.94f, 0.95f, 0.98f, 1.0f};

TextDefinition title_text(const char* text, f32 size = 80.0f) {
    return TextDefinition{
        .content = {.value = text},
        .style = {
            .font = {.font_path = kTitleFont.path, .font_size = size},
            .color = kTextColor
        },
        .frame = {
            .size = {1400.0f, 200.0f},
            .align = TextAlign::Center,
            .vertical_align = VerticalAlign::Middle,
            .line_height = 1.0f,
            .tracking = 4.0f
        }
    };
}

TextDefinition body_text(const char* text, f32 size = 48.0f) {
    return TextDefinition{
        .content = {.value = text},
        .style = {
            .font = {.font_path = kTitleFont.path, .font_size = size},
            .color = Color{0.75f, 0.78f, 0.85f, 1.0f}
        },
        .frame = {
            .size = {1200.0f, 400.0f},
            .align = TextAlign::Center,
            .vertical_align = VerticalAlign::Middle,
            .line_height = 1.1f,
            .tracking = 2.0f
        }
    };
}

void apply_fade_in(LayerBuilder& layer, Frame done_at = Frame{12}) {
    chronon3d::timeline(0.0f)
        .to(done_at, 1.0f, Easing::OutCubic)
        .apply_to(layer.opacity_anim());
}

void apply_slide_fade_in(LayerBuilder& layer,
                         f32 y_offset = 40.0f,
                         Frame done_at = Frame{12}) {
    layer.position_anim()
        .key(Frame{0}, Vec3{0.0f, y_offset, 0.0f},
             EasingCurve{Easing::OutCubic})
        .key(done_at, Vec3{0.0f, 0.0f, 0.0f},
             EasingCurve{Easing::OutCubic});
    apply_fade_in(layer, done_at);
}

void apply_fade_out(LayerBuilder& layer, Frame start, Frame end) {
    layer.opacity_anim()
        .key(Frame{0}, 1.0f, EasingCurve{Easing::Hold})
        .key(start, 1.0f, EasingCurve{Easing::Linear})
        .key(end, 0.0f, EasingCurve{Easing::InCubic});
}

CompositionDescriptor sequence_descriptor(
    std::string id,
    Frame duration,
    CompositionRegistry::Factory factory,
    bool include_image = false) {
    CompositionDescriptor descriptor;
    descriptor.id = std::move(id);
    descriptor.category = "Sequence V2";
    descriptor.width = kWidth;
    descriptor.height = kHeight;
    descriptor.fps = kFps;
    descriptor.duration = duration;
    descriptor.factory = factory;

    descriptor.prepare_props = [
        factory = std::move(factory),
        duration,
        include_image
    ](const CompositionProps& props) -> PreparedCompositionResult {
        if (!props.values.empty()) {
            return PropsError{
                "",
                PropsErrorReason::InvalidFormat,
                "Sequence V2 showcases do not declare external props"
            };
        }

        assets::AssetManifest manifest;
        manifest.add(kTitleFont);
        if (include_image) manifest.add(kPlaceholderImage);

        PreparedComposition prepared;
        prepared.metadata = CompositionMetadata{
            kWidth, kHeight, kFps, duration
        };
        prepared.asset_manifest = std::move(manifest);
        prepared.assets_root = props.project_root.empty()
            ? std::filesystem::path{"."}
            : props.project_root;
        prepared.construct = [factory, props]() -> Composition {
            return factory(props);
        };
        return prepared;
    };
    return descriptor;
}

} // namespace

Composition seq_v2_intro_outro() {
    return composition(
        {.name = "SeqV2IntroOutro", .width = kWidth, .height = kHeight,
         .frame_rate = kFps, .duration = Frame{90}},
        [](const FrameContext& ctx) {
            SceneBuilder scene(ctx);
            scene.sequence("intro", {.from = Frame{0}, .duration = Frame{45}},
                [](SequenceBuilder& sequence) {
                    sequence.layer("_bg", [](LayerBuilder& layer) {
                        layer.fullscreen_rect(
                            "bg", Color{0.02f, 0.02f, 0.06f, 1.0f});
                    });
                    sequence.layer("title", [](LayerBuilder& layer) {
                        layer.pin_to(Anchor::Center);
                        apply_slide_fade_in(layer, 30.0f, Frame{15});
                        layer.text("label", title_text("SEQUENCE V2"));
                    });
                });
            scene.sequence("outro", {.from = Frame{45}, .duration = Frame{45}},
                [](SequenceBuilder& sequence) {
                    sequence.layer("_bg_outro", [](LayerBuilder& layer) {
                        layer.fullscreen_rect(
                            "bg", Color{0.04f, 0.02f, 0.08f, 1.0f});
                    });
                    sequence.layer("outro_text", [](LayerBuilder& layer) {
                        layer.pin_to(Anchor::Center);
                        apply_slide_fade_in(layer, -20.0f, Frame{15});
                        layer.text("label", title_text("LOCAL FRAME = 0"));
                    });
                });
            return scene.build();
        });
}

Composition seq_v2_deep_nesting() {
    return composition(
        {.name = "SeqV2DeepNesting", .width = kWidth, .height = kHeight,
         .frame_rate = kFps, .duration = Frame{120}},
        [](const FrameContext& ctx) {
            SceneBuilder scene(ctx);
            scene.sequence("act", {.from = Frame{0}, .duration = Frame{120}},
                [](SequenceBuilder& act) {
                    const auto chapter = [](SequenceBuilder& parent,
                                            const char* id,
                                            Frame from,
                                            const char* title,
                                            const char* body,
                                            Color background) {
                        parent.sequence(id, {.from = from, .duration = Frame{60}},
                            [title, body, background](SequenceBuilder& chapter_seq) {
                                chapter_seq.sequence(
                                    "title", {.from = Frame{0}, .duration = Frame{20}},
                                    [title, background](SequenceBuilder& sequence) {
                                        sequence.layer("_bg", [background](LayerBuilder& layer) {
                                            layer.fullscreen_rect("bg", background);
                                        });
                                        sequence.layer("chapter_title", [title](LayerBuilder& layer) {
                                            layer.pin_to(Anchor::Center);
                                            apply_fade_in(layer, Frame{10});
                                            layer.text("label", title_text(title, 64.0f));
                                        });
                                    });
                                chapter_seq.sequence(
                                    "body", {.from = Frame{20}, .duration = Frame{40}},
                                    [body](SequenceBuilder& sequence) {
                                        sequence.layer("chapter_body", [body](LayerBuilder& layer) {
                                            layer.pin_to(Anchor::Center);
                                            apply_slide_fade_in(layer, 30.0f, Frame{12});
                                            layer.text("label", body_text(body));
                                        });
                                    });
                            });
                    };

                    chapter(act, "ch1", Frame{0}, "CHAPTER ONE",
                            "Nested sequences\ncarry local frame context.",
                            Color{0.01f, 0.03f, 0.08f, 1.0f});
                    chapter(act, "ch2", Frame{60}, "CHAPTER TWO",
                            "Local frame restarts\nfor each sequence.",
                            Color{0.04f, 0.01f, 0.06f, 1.0f});
                });
            return scene.build();
        });
}

Composition seq_v2_staggered_timeline() {
    return composition(
        {.name = "SeqV2StaggeredTimeline", .width = kWidth,
         .height = kHeight, .frame_rate = kFps, .duration = Frame{100}},
        [](const FrameContext& ctx) {
            SceneBuilder scene(ctx);
            scene.layer("_bg", [](LayerBuilder& layer) {
                layer.fullscreen_rect("bg", Color{0.02f, 0.02f, 0.06f, 1.0f});
            });

            const auto word = [&scene](const char* sequence_id,
                                       const char* text,
                                       Frame from,
                                       f32 y,
                                       f32 size) {
                scene.sequence(sequence_id,
                    {.from = from, .duration = Frame{50}},
                    [text, y, size](SequenceBuilder& sequence) {
                        sequence.layer("word", [text, y, size](LayerBuilder& layer) {
                            layer.pin_to(Anchor::Center);
                            layer.position({0.0f, y, 0.0f});
                            apply_fade_in(layer, Frame{10});
                            apply_fade_out(layer, Frame{38}, Frame{48});
                            layer.text("label", title_text(text, size));
                        });
                    });
            };

            word("word1", "SEQUENCE", Frame{0}, -80.0f, 72.0f);
            word("word2", "V2", Frame{25}, 0.0f, 96.0f);
            word("word3", "SHOWCASE", Frame{50}, 80.0f, 72.0f);
            return scene.build();
        });
}

Composition seq_v2_trim_offset() {
    return composition(
        {.name = "SeqV2TrimOffset", .width = kWidth, .height = kHeight,
         .frame_rate = kFps, .duration = Frame{90}},
        [](const FrameContext& ctx) {
            SceneBuilder scene(ctx);
            scene.layer("_bg", [](LayerBuilder& layer) {
                layer.fullscreen_rect("bg", Color{0.03f, 0.01f, 0.05f, 1.0f});
            });

            const auto animated_text = [](SequenceBuilder& sequence,
                                          const char* id,
                                          f32 y,
                                          const char* text,
                                          bool body) {
                sequence.layer(id, [y, text, body](LayerBuilder& layer) {
                    layer.pin_to(Anchor::Center);
                    layer.position({0.0f, y, 0.0f});
                    layer.position_anim()
                        .key(Frame{0}, Vec3{-120.0f, 0.0f, 0.0f},
                             EasingCurve{Easing::OutCubic})
                        .key(Frame{30}, Vec3{0.0f, 0.0f, 0.0f},
                             EasingCurve{Easing::OutCubic});
                    layer.opacity_anim()
                        .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
                        .key(Frame{10}, 1.0f, EasingCurve{Easing::Linear});
                    layer.text("label", body ? body_text(text) : title_text(text));
                });
            };

            scene.sequence("main", {.from = Frame{0}, .duration = Frame{90}},
                [animated_text](SequenceBuilder& sequence) {
                    animated_text(sequence, "text", 0.0f, "TRIM BEFORE", false);
                });
            scene.sequence("trimmed",
                {.from = Frame{0}, .duration = Frame{90},
                 .trim_before = Frame{20}},
                [animated_text](SequenceBuilder& sequence) {
                    animated_text(sequence, "trimmed_text", 120.0f,
                                  "← offset by trim_before=20", true);
                });
            return scene.build();
        });
}

Composition seq_v2_mixed_media() {
    return composition(
        {.name = "SeqV2MixedMedia", .width = kWidth, .height = kHeight,
         .frame_rate = kFps, .duration = Frame{90}},
        [](const FrameContext& ctx) {
            SceneBuilder scene(ctx);
            scene.layer("_bg", [](LayerBuilder& layer) {
                layer.fullscreen_rect(
                    "bg", Color{0.015f, 0.015f, 0.04f, 1.0f});
            });
            scene.sequence("title_seq",
                {.from = Frame{0}, .duration = Frame{60}},
                [](SequenceBuilder& sequence) {
                    sequence.layer("title", [](LayerBuilder& layer) {
                        layer.pin_to(Anchor::Center);
                        layer.position({0.0f, -100.0f, 0.0f});
                        apply_fade_in(layer, Frame{12});
                        apply_fade_out(layer, Frame{46}, Frame{58});
                        layer.text("label", title_text("MIXED MEDIA", 64.0f));
                    });
                });
            scene.sequence("image_seq",
                {.from = Frame{30}, .duration = Frame{60}},
                [](SequenceBuilder& sequence) {
                    sequence.layer("image", [](LayerBuilder& layer) {
                        layer.pin_to(Anchor::Center);
                        layer.position({0.0f, 80.0f, 0.0f});
                        apply_fade_in(layer, Frame{15});
                        apply_fade_out(layer, Frame{45}, Frame{58});
                        layer.image("hero", ImageParams{
                            .path = kPlaceholderImage.path,
                            .size = {600.0f, 400.0f}
                        });
                    });
                });
            return scene.build();
        });
}

void register_sequence_v2_compositions(CompositionRegistry& registry) {
    registry.add(sequence_descriptor(
        "SeqV2IntroOutro", Frame{90},
        [](const CompositionProps&) { return seq_v2_intro_outro(); }));
    registry.add(sequence_descriptor(
        "SeqV2DeepNesting", Frame{120},
        [](const CompositionProps&) { return seq_v2_deep_nesting(); }));
    registry.add(sequence_descriptor(
        "SeqV2StaggeredTimeline", Frame{100},
        [](const CompositionProps&) { return seq_v2_staggered_timeline(); }));
    registry.add(sequence_descriptor(
        "SeqV2TrimOffset", Frame{90},
        [](const CompositionProps&) { return seq_v2_trim_offset(); }));
    registry.add(sequence_descriptor(
        "SeqV2MixedMedia", Frame{90},
        [](const CompositionProps&) { return seq_v2_mixed_media(); }, true));
}

} // namespace chronon3d::content::sequence_v2
