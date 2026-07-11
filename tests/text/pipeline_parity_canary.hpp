#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// tests/text/pipeline_parity_canary.hpp
//
// Canary composition used by the real pipeline parity matrix. It is
// registered as a CLI built-in composition ("PipelineParityCanary") so
// both the in-process SDK path and the external `chronon3d_cli` path can
// render exactly the same scene.
//
// AGENTS.md v0.1 Cat-2 freeze compliant: zero new public SDK API.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_props.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/core/types/frame_context.hpp>

namespace chronon3d::test {

/// The canonical text used by all pipeline parity variants.
inline constexpr const char* kPipelineParityText = "PIPELINE PARITY";

/// Build the pipeline-parity canary composition.
///
/// The scene is intentionally simple (one centered text layer) so it is
/// cheap to render through every pipeline variant while still exercising
/// the text rasterisation / compositing path.
inline Composition make_pipeline_parity_canary(const CompositionProps& /*props*/) {
    return Composition(
        CompositionSpec{
            .name = "PipelineParityCanary",
            .width = 1920,
            .height = 1080,
            .duration = Frame{1},
        },
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("canary_layer", [](LayerBuilder& l) {
                l.screen_dimensions(1920.0f, 1080.0f);
                // TICKET-TEXT-LEGACY-POSITION-ROT (sub-area test-fixture site):
                // legacy .position Vec3 removed upstream; migrate to .placement.
                // Z=0 explicit in legacy code, safe-drop per M1.8 §5A.
                l.text("canary_text", TextSpec{.content = {.value = std::string(kPipelineParityText)},.placement = chronon3d::TextPlacement{chronon3d::TextPlacementKind::Absolute, {960.0f, 540.0f}},.font = {
                        .font_path = "assets/fonts/Inter-Bold.ttf",
                        .font_family = "Inter",
                        .font_size = 96.0f,
                    },.layout = {
                        .box = Vec2{900.0f, 200.0f},
                        .anchor = TextAnchor::Center,
                        .align = TextAlign::Center,
                        .vertical_align = VerticalAlign::Middle,
                    },});
            });
            return s.build();
        });
}

} // namespace chronon3d::test
