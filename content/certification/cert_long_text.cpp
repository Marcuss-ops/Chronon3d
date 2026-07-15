// ==============================================================================
// content/certification/cert_long_text.cpp
//
// FASE 3.4 — CertLongText composition
// ==============================================================================

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_props.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/core/types/frame_context.hpp>

#include "content/certification/certification_descriptor.hpp"
#include "content/common/background_helpers.hpp"

namespace chronon3d::content::certification {

using namespace chronon3d;

Composition cert_long_text() {
    constexpr int   kWidth  = 1920;
    constexpr int   kHeight = 1080;
    constexpr float kMargin = 120.0f;

    return composition(
        {.name = "CertLongText",
         .width = kWidth,
         .height = kHeight,
         .frame_rate = FrameRate{30, 1},
         .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            backgrounds::add_common_background(
                s, backgrounds::BackgroundStyles::Minimalist());

            const float kBoxW = static_cast<float>(kWidth) - kMargin * 2.0f;
            const float kBoxH = static_cast<float>(kHeight) - kMargin * 2.0f;

            s.layer("long_text", [kBoxW, kBoxH](LayerBuilder& l) {
                l.text("body", TextDefinition{
                    .content = {.value = "This is a very long sentence that must wrap "
                                         "correctly across multiple lines, demonstrating "
                                         "proper word wrapping and line spacing in the "
                                         "Chronon3D text engine for production-ready "
                                         "subtitle and body text rendering. Every word "
                                         "should remain intact without being cut or "
                                         "hyphenated mid-character."},
                    .style = {
                        .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                                 .font_family = "Inter",
                                 .font_weight = 400,
                                 .font_size = 36.0f},
                        .color = Color{0.92f, 0.92f, 0.95f, 1.0f}
                    },
                    .frame = {
                        .size = {kBoxW, kBoxH},
                        .placement = TextPlacement{
                            TextPlacementKind::Absolute,
                            {static_cast<float>(kWidth) * 0.5f,
                             kMargin + kBoxH * 0.5f}},
                        .anchor = TextAnchor::Center,
                        .align = TextAlign::Center,
                        .vertical_align = VerticalAlign::Middle,
                        .wrap = TextWrap::Word,
                        .overflow = TextOverflow::Clip,
                        .centering_mode = TextCenteringMode::PixelInk,
                        .line_height = 1.5f,
                        .tracking = 0.5f,
                        .max_lines = 0
                    }
                });
            });

            return s.build();
        });
}

void register_cert_long_text_compositions(CompositionRegistry& registry) {
    registry.add(certification_descriptor(
        "CertLongText", 1920, 1080, Frame{1},
        [](const CompositionProps&) { return cert_long_text(); }));
}

} // namespace chronon3d::content::certification
