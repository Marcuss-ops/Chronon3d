// ==============================================================================
// content/certification/cert_lower_third.cpp
//
// FASE 3.3 — CertLowerThird composition
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

Composition cert_lower_third() {
    constexpr int   kWidth   = 1920;
    constexpr int   kHeight  = 1080;
    constexpr float kMargin  = 80.0f;
    constexpr float kBoxHeight = 140.0f;
    return composition(
        {.name = "CertLowerThird",
         .width = kWidth,
         .height = kHeight,
         .frame_rate = FrameRate{30, 1},
         .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            backgrounds::add_common_background(
                s, backgrounds::BackgroundStyles::Minimalist());

            s.layer("lower_third_box", [](LayerBuilder& l) {
                l.pin_to(Anchor::BottomCenter, kMargin);
                l.rect("box", RectParams{
                    .size = {static_cast<float>(kWidth) - kMargin * 2.0f, kBoxHeight},
                    .color = {1.0f, 1.0f, 1.0f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{0.0f, 0.0f, 0.0f, 0.55f}),
                    .stroke = {},
                });
            });

            s.layer("title_line", [](LayerBuilder& l) {
                l.pin_to(Anchor::BottomCenter, kMargin);
                l.text("title", TextDefinition{
                    .content = {.value = "BREAKING NEWS"},
                    .style = {
                        .font = {.font_path = "assets/fonts/Inter-Bold.ttf",
                                 .font_family = "Inter",
                                 .font_weight = 700,
                                 .font_size = 42.0f},
                        .color = Color::white()
                    },
                    .frame = {
                        .size = {static_cast<float>(kWidth) - kMargin * 2.0f, 60.0f},
                        .placement = TextPlacement{TextPlacementKind::Absolute, {0.0f, -20.0f}},
                        .anchor = TextAnchor::Center,
                        .align = TextAlign::Center,
                        .vertical_align = VerticalAlign::Middle,
                        .wrap = TextWrap::Word,
                        .overflow = TextOverflow::Clip,
                        .centering_mode = TextCenteringMode::PixelInk,
                        .line_height = 0.95f,
                        .max_lines = 1
                    }
                });
            });

            s.layer("subtitle_line", [](LayerBuilder& l) {
                l.pin_to(Anchor::BottomCenter, kMargin);
                l.text("subtitle", TextDefinition{
                    .content = {.value = "Chronon3D Text Engine — Production Ready"},
                    .style = {
                        .font = {.font_path = "assets/fonts/Inter-Regular.ttf",
                                 .font_family = "Inter",
                                 .font_weight = 400,
                                 .font_size = 24.0f},
                        .color = Color{0.85f, 0.85f, 0.9f, 1.0f}
                    },
                    .frame = {
                        .size = {static_cast<float>(kWidth) - kMargin * 2.0f, 40.0f},
                        .anchor = TextAnchor::Center,
                        .align = TextAlign::Center,
                        .vertical_align = VerticalAlign::Middle,
                        .wrap = TextWrap::Word,
                        .overflow = TextOverflow::Clip,
                        .centering_mode = TextCenteringMode::PixelInk,
                        .line_height = 0.95f,
                        .max_lines = 1
                    }
                });
            });

            return s.build();
        });
}

void register_cert_lower_third_compositions(CompositionRegistry& registry) {
    registry.add(certification_descriptor(
        "CertLowerThird", 1920, 1080, Frame{1},
        [](const CompositionProps&) { return cert_lower_third(); }));
}

} // namespace chronon3d::content::certification
