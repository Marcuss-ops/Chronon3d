// ==============================================================================
// content/certification/cert_multilingual.cpp
//
// FASE 3.5-3.6 — Multilingual text certification.
// ==============================================================================

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_props.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/core/types/frame_context.hpp>

#include "content/certification/certification_descriptor.hpp"
#include "content/common/background_helpers.hpp"

#include <array>
#include <string>

namespace chronon3d::content::certification {

using namespace chronon3d;

namespace {

constexpr int kWidth = 1920;
constexpr int kHeight = 1080;
constexpr float kMargin = 60.0f;
constexpr float kRowHeight = 120.0f;
constexpr float kGap = 20.0f;
constexpr float kStartY = 140.0f;
constexpr float kFontSize = 36.0f;

struct MultilingualRow {
    const char* label;
    const char* text;
    Color label_color;
    Color text_color;
};

const std::array<MultilingualRow, 6> kRows{{
    {"LATIN+Accents →", "Café naïve — São Paulo, João",
     Color{0.5f, 0.8f, 0.5f, 1.0f}, Color::white()},
    {"CJK →", "こんにちは世界 — 你好世界 中文测试",
     Color{0.5f, 0.8f, 0.5f, 1.0f}, Color::white()},
    {"Arabic RTL (EXPECTED FAIL) →", "مرحبا بالعالم",
     Color{0.9f, 0.6f, 0.3f, 1.0f}, Color{0.9f, 0.7f, 0.3f, 1.0f}},
    {"Hebrew RTL (EXPECTED FAIL) →", "שלום עולם",
     Color{0.9f, 0.6f, 0.3f, 1.0f}, Color{0.9f, 0.7f, 0.3f, 1.0f}},
    {"Emoji (EXPECTED FAIL) →", "🔥 🎉 🚀",
     Color{0.9f, 0.6f, 0.3f, 1.0f}, Color{0.9f, 0.7f, 0.3f, 1.0f}},
    {"Mixed →", "Café 你好 مرحبا 🔥",
     Color{0.6f, 0.7f, 0.9f, 1.0f}, Color::white()}
}};

TextDefinition row_text(const char* value,
                        float font_size,
                        Color color,
                        Vec2 frame_size,
                        Vec2 position,
                        int max_lines) {
    return TextDefinition{
        .content = {.value = value},
        .style = {
            .font = {
                .font_path = font_size < 30.0f
                    ? "assets/fonts/Inter-Regular.ttf"
                    : "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = font_size < 30.0f ? 400 : 700,
                .font_size = font_size
            },
            .color = color
        },
        .frame = {
            .size = frame_size,
            .placement = TextPlacement{TextPlacementKind::Absolute, position},
            .anchor = TextAnchor::Center,
            .align = TextAlign::Center,
            .vertical_align = VerticalAlign::Middle,
            .wrap = TextWrap::Word,
            .overflow = TextOverflow::Clip,
            .centering_mode = TextCenteringMode::PixelInk,
            .line_height = 0.95f,
            .max_lines = max_lines
        }
    };
}

} // namespace

Composition cert_multilingual() {
    return composition(
        {.name = "CertMultilingual",
         .width = kWidth,
         .height = kHeight,
         .frame_rate = FrameRate{30, 1},
         .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder scene(ctx);
            backgrounds::add_common_background(
                scene, backgrounds::BackgroundStyles::Minimalist());

            scene.layer("header", [](LayerBuilder& layer) {
                layer.text("header", row_text(
                    "Multilingual Text Certification — FASE 3.5-3.6",
                    30.0f,
                    Color{0.7f, 0.7f, 0.75f, 1.0f},
                    {static_cast<float>(kWidth) - kMargin * 2.0f, 56.0f},
                    {static_cast<float>(kWidth) * 0.5f, 64.0f},
                    1));
            });

            for (std::size_t index = 0; index < kRows.size(); ++index) {
                const MultilingualRow row = kRows[index];
                const float y = kStartY +
                    static_cast<float>(index) * (kRowHeight + kGap);
                const std::string layer_id = "row" + std::to_string(index + 1);

                scene.layer(layer_id, [row, y, layer_id](LayerBuilder& layer) {
                    layer.text(layer_id + "_label", row_text(
                        row.label,
                        22.0f,
                        row.label_color,
                        {300.0f, 48.0f},
                        {210.0f, y},
                        1));
                    layer.text(layer_id + "_text", row_text(
                        row.text,
                        kFontSize,
                        row.text_color,
                        {static_cast<float>(kWidth) - 500.0f, kRowHeight},
                        {static_cast<float>(kWidth) * 0.5f + 100.0f, y},
                        2));
                });
            }

            return scene.build();
        });
}

void register_cert_multilingual_compositions(CompositionRegistry& registry) {
    registry.add(certification_descriptor(
        "CertMultilingual", kWidth, kHeight, Frame{1},
        [](const CompositionProps&) { return cert_multilingual(); }));
}

} // namespace chronon3d::content::certification
