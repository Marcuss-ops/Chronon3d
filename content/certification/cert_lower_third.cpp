// ==============================================================================
// content/certification/cert_lower_third.cpp
//
// FASE 3.3 — CertLowerThird composition
//
// Lower third con box semi-trasparente e safe margins.
// 1920×1080 canvas, "BREAKING NEWS" + subtitle.
// ==============================================================================

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_props.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/core/types/frame_context.hpp>

#include "content/common/background_helpers.hpp"
#include "content/text/text_helpers.hpp"

namespace chronon3d::content::certification {

Composition cert_lower_third() {
    constexpr int   kWidth   = 1920;
    constexpr int   kHeight  = 1080;
    constexpr float kMargin  = 80.0f;   // safe margin from edges
    constexpr float kBoxHeight = 140.0f; // height of semi-transparent box
    return composition(
        {.name = "CertLowerThird",
         .width = kWidth,
         .height = kHeight,
         .frame_rate = FrameRate{30, 1},
         .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);

            // Background
            backgrounds::add_common_background(
                s, backgrounds::BackgroundStyles::Minimalist());

            // ── Semi-transparent lower third box ─────────────────────
            // pin_to handles Y positioning; rect at origin so layer
            // transform + pin produce the correct screen placement.
            s.layer("lower_third_box", [](LayerBuilder& l) {
                l.pin_to(Anchor::BottomCenter, kMargin);
                l.rect("box", RectParams{
                    .size = {static_cast<float>(kWidth) - kMargin * 2.0f, kBoxHeight},
                    .color = {1.0f, 1.0f, 1.0f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},   // pin_to handles Y, center X
                    .fill = FillStyle::solid(Color{0.0f, 0.0f, 0.0f, 0.55f}),
                    .stroke = {},
                });
            });

            // ── Main title line ─────────────────────────────────────
            s.layer("title_line", [](LayerBuilder& l) {
                l.pin_to(Anchor::BottomCenter, kMargin);
                l.text("title", text::centered_text({
                    .text       = "BREAKING NEWS",
                    .box        = {static_cast<float>(kWidth) - kMargin * 2.0f, 60.0f},
                    .pos        = {static_cast<float>(kWidth) * 0.5f,
                                   kBoxHeight * 0.5f + 14.0f, 0.0f},
                    .font_path  = "assets/fonts/Inter-Bold.ttf",
                    .font_family = "Inter",
                    .font_weight = 700,
                    .font_size  = 42.0f,
                    .color      = Color::white(),
                }));
            });

            // ── Subtitle line ───────────────────────────────────────
            s.layer("subtitle_line", [](LayerBuilder& l) {
                l.pin_to(Anchor::BottomCenter, kMargin);
                l.text("subtitle", text::centered_text({
                    .text       = "Chronon3D Text Engine — Production Ready",
                    .box        = {static_cast<float>(kWidth) - kMargin * 2.0f, 40.0f},
                    .pos        = {static_cast<float>(kWidth) * 0.5f,
                                   kBoxHeight * 0.5f - 38.0f, 0.0f},
                    .font_path  = "assets/fonts/Inter-Regular.ttf",
                    .font_family = "Inter",
                    .font_weight = 400,
                    .font_size  = 24.0f,
                    .color      = Color{0.85f, 0.85f, 0.9f, 1.0f},
                }));
            });

            return s.build();
        });
}

// ── Registration ──────────────────────────────────────────────────────
void register_cert_lower_third_compositions(CompositionRegistry& registry) {
    registry.add("CertLowerThird", [](const CompositionProps&) {
        return cert_lower_third();
    });
}

} // namespace chronon3d::content::certification
