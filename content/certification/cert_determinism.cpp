// ==============================================================================
// content/certification/cert_determinism.cpp
//
// TICKET-DETERMINISM-CERT — Determinism & cache certification composition.
//
// A single minimal-surface composition for the canonical
// verify_determinism_linux.sh gate:
//
//   CertDeterminism — solid white rect (400×300, centered) on dark background.
//                      Every render at the same frame MUST produce pixel-identical
//                      output (same sha256 hash). No random, no time dependency,
//                      no external assets.
//
// 1920×1080 canvas. FrameRate{30,1}, duration=1, frame=0 only.
// ==============================================================================

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_props.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/core/types/frame_context.hpp>

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
            // Dark background
            s.layer("bg", [](LayerBuilder& l) {
                l.fullscreen_rect("bg", Color{0.05f, 0.05f, 0.08f, 1.0f});
            });
            // Solid white rect — deterministic, no random, no time dep
            s.layer("rect", [](LayerBuilder& l) {
                l.position({kDetCX - kDetRectW * 0.5f, kDetCY - kDetRectH * 0.5f, 0.0f});
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
    registry.add("CertDeterminism", [](const CompositionProps&) {
        return cert_determinism();
    });
}

} // namespace chronon3d::content::certification
