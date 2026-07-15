// ==============================================================================
// content/certification/cert_determinism.cpp
//
// TICKET-DETERMINISM-CERT — Determinism & cache certification composition.
// ==============================================================================

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_props.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/core/types/frame_context.hpp>

#include "content/certification/certification_descriptor.hpp"

namespace chronon3d::content::certification {

using namespace chronon3d;

static constexpr int   kDetW     = 1920;
static constexpr int   kDetH     = 1080;
static constexpr float kDetCX    = static_cast<float>(kDetW) * 0.5f;
static constexpr float kDetCY    = static_cast<float>(kDetH) * 0.5f;
static constexpr float kDetRectW = 400.0f;
static constexpr float kDetRectH = 300.0f;

Composition cert_determinism() {
    return composition(
        {.name = "CertDeterminism",
         .width = kDetW,
         .height = kDetH,
         .frame_rate = FrameRate{30, 1},
         .duration = 1},
        [](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.layer("bg", [](LayerBuilder& l) {
                l.fullscreen_rect("bg", Color{0.05f, 0.05f, 0.08f, 1.0f});
            });
            s.layer("rect", [](LayerBuilder& l) {
                l.position({kDetCX - kDetRectW * 0.5f,
                            kDetCY - kDetRectH * 0.5f,
                            0.0f});
                l.rect("r", RectParams{
                    .size = {kDetRectW, kDetRectH},
                    .color = {1.0f, 1.0f, 1.0f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{1.0f, 1.0f, 1.0f, 1.0f}),
                    .stroke = {},
                });
            });
            return s.build();
        });
}

void register_cert_determinism_compositions(CompositionRegistry& registry) {
    registry.add(certification_descriptor(
        "CertDeterminism", kDetW, kDetH, Frame{1},
        [](const CompositionProps&) { return cert_determinism(); }));
}

} // namespace chronon3d::content::certification
